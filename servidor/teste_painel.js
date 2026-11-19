// Teste do painel de métricas: sobe um "painel" falso, liga o servidor do jogo apontando para ele e confere
// o que chega (lotes, chave, plataforma no formato do painel, filtro de dados pessoais, servidor_parou).
// Rodar: node servidor/teste_painel.js
'use strict';
const http = require('http');
const { spawn } = require('child_process');
const path = require('path');

const recebidos = [];
let cabecalhos = null;
const falso = http.createServer((req, res) => {
  let s = '';
  req.on('data', (c) => { s += c; });
  req.on('end', () => {
    cabecalhos = req.headers;
    const j = JSON.parse(s);
    if (j.chave !== 'pnl_teste') { res.writeHead(401); res.end('{}'); return; }
    recebidos.push(...j.eventos);
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true, recebidos: j.eventos.length }));
  });
});

const espera = (ms) => new Promise((ok) => setTimeout(ok, ms));
let falhas = 0;
const conferir = (cond, msg) => { console.log((cond ? '✅ ' : '❌ ') + msg); if (!cond) falhas++; };

falso.listen(0, async () => {
  const portaPainel = falso.address().port, porta = 18000 + Math.floor(Math.random() * 1000);
  const srv = spawn(process.execPath, [path.join(__dirname, 'server.js')], {
    env: { ...process.env, PORT: String(porta), PAINEL_CHAVE: 'pnl_teste', PAINEL_URL: `http://127.0.0.1:${portaPainel}/rpc` },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let log = '';
  srv.stdout.on('data', (d) => { log += d; });
  srv.stderr.on('data', (d) => { log += d; });
  for (let i = 0; i < 50 && !log.includes('no ar'); i++) await espera(100);

  const r = await fetch(`http://127.0.0.1:${porta}/api/painel`, {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ eventos: [
      { tipo: 'abrir', usuario: 'uid1', nome: 'Zé', plataforma: 'android', dados: { icone: 'https://x/img/icone.png' } },
      { tipo: 'login', usuario: 'uid1', plataforma: 'pc', dados: { metodo: 'google', email: 'ze@x.com', senha: '123' } },
      { tipo: 'compra_moedas', usuario: 'uid1', plataforma: 'ios', dados: { item: 'AK Dragão', moedas: 1500 } },
      { tipo: 'nivel_subiu', usuario: 'uid1', dados: { nivel: 6 } },
      { tipo: 'assinatura', usuario: 'uid1', dados: { valor: 999 } },  // o celular não pode mandar pagamento
    ] }),
  });
  conferir(r.status === 204, 'o servidor aceita os eventos do app (/api/painel)');
  await espera(4500);  // lote sai a cada 2 s

  const tipos = recebidos.map((e) => e.tipo);
  conferir(cabecalhos && cabecalhos.apikey && cabecalhos['content-type'] === 'application/json', 'manda apikey e Content-Type');
  conferir(tipos.includes('servidor'), 'evento "servidor" (online + versão) sai ao ligar');
  const srvEv = recebidos.find((e) => e.tipo === 'servidor');
  conferir(srvEv && typeof srvEv.dados.online === 'number' && srvEv.dados.versao, 'servidor manda online e versao');
  const abrir = recebidos.find((e) => e.tipo === 'abrir');
  conferir(abrir && abrir.plataforma === 'Android' && abrir.dados.icone, 'abrir chega com plataforma "Android" e o ícone');
  const login = recebidos.find((e) => e.tipo === 'login');
  conferir(login && login.plataforma === 'PC' && login.dados.metodo === 'google', 'login chega com método e plataforma "PC"');
  conferir(login && !('email' in login.dados) && !('senha' in login.dados), 'e-mail e senha são removidos');
  conferir(tipos.includes('compra_moedas') && recebidos.find((e) => e.tipo === 'compra_moedas').plataforma === 'iOS', 'compra_moedas chega (plataforma "iOS")');
  conferir(tipos.includes('nivel_subiu'), 'nivel_subiu chega');
  conferir(!tipos.includes('assinatura'), 'pagamento vindo do celular é recusado (só o servidor manda)');

  srv.kill('SIGTERM');
  await new Promise((ok) => srv.on('exit', ok));
  conferir(recebidos.some((e) => e.tipo === 'servidor_parou'), '"servidor_parou" sai ao desligar');

  console.log(falhas ? `\n${falhas} falha(s)` : '\nTudo certo.');
  falso.close();
  process.exit(falhas ? 1 : 0);
});
