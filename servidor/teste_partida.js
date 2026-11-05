// Teste rápido das partidas ao vivo (protocolo 2) contra um servidor já rodando: node teste_partida.js [porta]
// Confere: estados chegam no pacote da sala com o id certo (vagas), voz só para o mesmo time, eventos passam,
// jogo antigo (sem v=2) é recusado.
const path = require('path');
const WebSocket = require('ws');
const PROTO = require(path.join(__dirname, '..', 'web', 'protocolo.js'));
const porta = +process.argv[2] || 10000;
const url = (q) => `ws://127.0.0.1:${porta}/partida?${q}`;
const falhas = [];
const conferir = (ok, msg) => { if (!ok) falhas.push(msg); };

function jogador(id, time) {
  const ws = new WebSocket(url(`sala=TESTE&id=${id}&time=${time}&v=2`));
  ws.binaryType = 'arraybuffer';
  const j = { ws, id, vagas: {}, estados: {}, vozes: 0, eventos: [] };
  ws.on('message', (d, bin) => {
    if (!bin) {
      const m = JSON.parse(d.toString());
      if (m.k === 'vagas') j.vagas = m.d;
      if (m.k === 'ev') j.eventos.push(m.d);
      return;
    }
    const b = new Uint8Array(d);
    if (b[0] === PROTO.T_ESTADO) Object.assign(j.estados, PROTO.decodificarEstados(b, j.vagas));
    if (b[0] === PROTO.T_VOZ) j.vozes++;
  });
  j.aberto = new Promise((ok) => ws.on('open', ok));
  return j;
}
const espera = (ms) => new Promise((ok) => setTimeout(ok, ms));

(async () => {
  const a = jogador('ana', 0), b = jogador('beto', 0), c = jogador('caio', 1);
  await Promise.all([a.aberto, b.aberto, c.aberto]);
  await espera(200);
  a.ws.send(PROTO.codificarEstado({ p: [10.5, 2, -30.25], y: 1.5, pi: -0.2, a: true, w: 'ak47', n: 42, vel: [0, 0, 3] }));
  a.ws.send(PROTO.codificarVoz(new Uint8Array(160).fill(7)));
  a.ws.send(JSON.stringify({ k: 'ev', d: { t: 'dano', alvo: 'caio', v: 30 } }));
  await espera(300);
  for (const j of [b, c]) {
    const e = j.estados.ana;
    conferir(e && Math.abs(e.p[0] - 10.5) < 0.03 && Math.abs(e.p[2] + 30.25) < 0.03 && e.w === 'ak47' && e.n === 42 && e.a,
      `${j.id} não recebeu o estado da ana certo: ${JSON.stringify(e)}`);
    conferir(j.eventos.some((v) => v.t === 'dano'), `${j.id} não recebeu o evento`);
  }
  conferir(!a.estados.ana, 'a ana recebeu o próprio estado de volta');
  conferir(b.vozes === 1, `voz para o mesmo time: ${b.vozes}`);
  conferir(c.vozes === 0, `voz vazou para o outro time: ${c.vozes}`);
  const velho = new WebSocket(url('sala=TESTE&id=velho&time=0'));
  const codigo = await new Promise((ok) => velho.on('close', ok));
  conferir(codigo === 4003, `jogo antigo devia ser recusado (4003), veio ${codigo}`);
  for (const j of [a, b, c]) j.ws.close();
  console.log(falhas.length ? 'FALHOU:\n  ' + falhas.join('\n  ') : 'partida ao vivo OK (estado, vagas, voz por time, eventos, versão)');
  process.exit(falhas.length ? 1 : 0);
})();
