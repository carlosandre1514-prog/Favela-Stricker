// Servidor do Favela Stricker (Render): entrega o site do jogo (pasta web/) e faz as partidas ao vivo.
//
// Partida (protocolo 2, web/protocolo.js): cada celular/navegador abre um WebSocket em
//   /partida?sala=CODIGO&id=ID&time=0|1&v=2
//   binário [1][estado]        o meu lutador -> o servidor guarda o último de cada um e, 30 vezes por segundo,
//                              manda a cada pessoa UM pacote com os lutadores da sala que mudaram
//                              (antes repassava cada mensagem para cada um: 9 vezes mais pacotes)
//   binário [2][áudio]         voz -> só para o mesmo time, como [2][vaga de quem falou][áudio]
//   texto  {"k":"ev",...}      evento (dano, abate, granada) -> para todos da sala na hora
//   texto  {"k":"pg",...}      ping -> volta só para quem mandou
//   o servidor manda {"k":"vagas","d":{vaga: id}} quando alguém entra ou sai (quem é cada vaga no pacote)
// Quem sai: os outros recebem {"k":"ev","d":{"t":"saiu","id":ID}}.
// Proteções: celular com internet ruim não acumula pacotes (pula os velhos), limite de mensagens por segundo,
// limite de conexões e salas ("servidores cheios" em vez de cair) e versão antiga do jogo recusada.
// As salas (código, lista de jogadores, quem começa) ficam no Firebase; aqui só passa o que é ao vivo.
const http = require('http');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { WebSocketServer } = require('ws');
const loja = require('./loja-nexus');  // loja de moedas/skins do site NEXUS (Pix): /api/loja/...

const PORT = process.env.PORT || 10000;
const WEB = path.join(__dirname, '..', 'web');
const MAX_POR_SALA = 10, MAX_POR_SALA_BR = 40;  // Polícia x Bandidos 10; Ressurgência 10 times de 4
const MAX_CONEXOES = +process.env.MAX_CONEXOES || 3000;  // passou disso: "servidores cheios" (não cai)
const MAX_SALAS = +process.env.MAX_SALAS || 400;
const TICK_MS = 33;                 // pacote da sala 30 vezes por segundo
const TAM_ESTADO = 22;              // web/protocolo.js
const FILA_PULA = 64 * 1024;        // celular atrasado (internet ruim): pula pacotes até a fila andar
const FILA_CORTA = 2 * 1024 * 1024; // travado de vez: desconecta (não deixa a memória do servidor estourar)
const MSGS_POR_SEGUNDO = 200;       // estado 20/s + voz 50/s + eventos: folga grande; acima disso ignora
const MSGS_CORTA = 600;             // abuso (bug ou trapaça): desconecta

const TIPOS = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.wasm': 'application/wasm', '.data': 'application/octet-stream',
  '.png': 'image/png', '.webp': 'image/webp', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.wav': 'audio/wav',
};

// ---------------- site ----------------
const servidor = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  if (await loja.tratar(req, res, url)) return;  // rotas da loja (o token do jogador é conferido lá)
  if (url.pathname === '/saude') { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end('ok'); return; }
  if (url.pathname === '/api/estado') {  // quantas pessoas jogando agora (para acompanhar a carga)
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-cache' });
    res.end(JSON.stringify({ conexoes: wss.clients.size, salas: salas.size, max_conexoes: MAX_CONEXOES }));
    return;
  }
  let arq = path.normalize(decodeURIComponent(url.pathname)).replace(/^([/\\])+/, '');
  if (!arq || arq.endsWith('/')) arq += 'index.html';
  const cheio = path.join(WEB, arq);
  if (!cheio.startsWith(WEB)) { res.writeHead(403); res.end(); return; }
  // O pacote grande vai só comprimido (favela.data.gz) e, se passar do limite do GitHub, em partes (.gz.000, .gz.001…)
  const gz = cheio + '.gz';
  const partes = [];
  for (let i = 0; fs.existsSync(gz + '.' + String(i).padStart(3, '0')); i++) partes.push(gz + '.' + String(i).padStart(3, '0'));
  const comGz = fs.existsSync(gz) ? [gz] : partes;
  fs.stat(cheio, (err, st) => {
    const existe = !err && st.isFile();
    if (!existe && !comGz.length) { res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }); res.end('não encontrado'); return; }
    const ext = path.extname(cheio);
    const cab = {
      'Content-Type': TIPOS[ext] || 'application/octet-stream',
      // o pacote grande (modelos e texturas) fica guardado no navegador; a página sempre é buscada de novo
      'Cache-Control': ext === '.html' ? 'no-cache' : 'public, max-age=3600',
    };
    const aceitaGz = /\bgzip\b/.test(req.headers['accept-encoding'] || '');
    // versão comprimida feita no build (favela.data.gz, favela.wasm.gz)
    if (comGz.length && (aceitaGz || !existe)) {
      if (!aceitaGz) {
        // navegador sem gzip (raro): descomprime aqui
        res.writeHead(200, cab);
        const un = zlib.createGunzip();
        un.pipe(res);
        enviarEmSequencia(comGz, un);
        return;
      }
      cab['Content-Encoding'] = 'gzip';
      cab['Content-Length'] = comGz.reduce((n, f) => n + fs.statSync(f).size, 0);
      res.writeHead(200, cab);
      enviarEmSequencia(comGz, res);
      return;
    }
    cab['Content-Length'] = st.size;
    if (st.size < 2_000_000 && /text|javascript|json|svg/.test(cab['Content-Type']) && aceitaGz) {
      delete cab['Content-Length'];
      cab['Content-Encoding'] = 'gzip';
      res.writeHead(200, cab);
      fs.createReadStream(cheio).pipe(zlib.createGzip()).pipe(res);
      return;
    }
    res.writeHead(200, cab);
    fs.createReadStream(cheio).pipe(res);
  });
});

// Manda vários arquivos um atrás do outro na mesma resposta
function enviarEmSequencia(arquivos, destino, i = 0) {
  if (i >= arquivos.length) { destino.end(); return; }
  const r = fs.createReadStream(arquivos[i]);
  r.on('error', () => destino.destroy());
  r.on('end', () => enviarEmSequencia(arquivos, destino, i + 1));
  r.pipe(destino, { end: false });
}

// ---------------- partidas ao vivo ----------------
const salas = new Map();  // código -> Map(id -> {ws, time, vaga, id, estado, novo})

const wss = new WebSocketServer({ server: servidor, path: '/partida', maxPayload: 32 * 1024 });

// manda sem deixar acumular: se a fila desta pessoa já está grande, pula (o próximo pacote traz o estado novo)
function mandar(p, dado, opcoes) {
  const ws = p.ws;
  if (ws.readyState !== 1) return;
  if (ws.bufferedAmount > FILA_CORTA) { ws.terminate(); return; }
  if (ws.bufferedAmount > FILA_PULA) return;
  ws.send(dado, opcoes);
}

function avisarVagas(sala) {
  const d = {};
  for (const p of sala.values()) d[p.vaga] = p.id;
  const txt = JSON.stringify({ k: 'vagas', d });
  for (const p of sala.values()) if (p.ws.readyState === 1) p.ws.send(txt);
}

wss.on('connection', (ws, req) => {
  const q = new URL(req.url, 'http://x').searchParams;
  const codigo = (q.get('sala') || '').toUpperCase().replace(/[^A-Z0-9]/g, '').slice(0, 8);
  const id = (q.get('id') || '').slice(0, 64);
  const time = q.get('time') === '1' ? 1 : 0;
  if (q.get('v') !== '2') { ws.close(4003, 'atualize o jogo'); return; }
  if (!codigo || !id) { ws.close(4000, 'sala e id obrigatórios'); return; }
  if (wss.clients.size > MAX_CONEXOES) { ws.close(4004, 'servidores cheios'); return; }
  let sala = salas.get(codigo);
  if (!sala) {
    if (salas.size >= MAX_SALAS) { ws.close(4004, 'servidores cheios'); return; }
    sala = new Map();
    salas.set(codigo, sala);
  }
  const antigo = sala.get(id);
  if (antigo) { try { antigo.ws.close(4001, 'entrou de novo'); } catch (_) {} sala.delete(id); }
  if (sala.size >= (q.get('modo') === 'br' ? MAX_POR_SALA_BR : MAX_POR_SALA)) { ws.close(4002, 'sala cheia'); if (!sala.size) salas.delete(codigo); return; }
  // vaga: número de 0 a 254 que identifica cada um nos pacotes (estado e voz)
  const usadas = new Set([...sala.values()].map((p) => p.vaga));
  let vaga = 0;
  while (usadas.has(vaga)) vaga++;
  const eu = { ws, time, vaga, id, estado: null, novo: false };
  sala.set(id, eu);
  avisarVagas(sala);
  ws.vivo = true;
  ws.on('pong', () => { ws.vivo = true; });
  let contaSeg = 0, inicioSeg = Date.now();

  ws.on('message', (dado, binario) => {
    try {
      // limite de mensagens por segundo
      const agora = Date.now();
      if (agora - inicioSeg >= 1000) { inicioSeg = agora; contaSeg = 0; }
      if (++contaSeg > MSGS_CORTA) { ws.terminate(); return; }
      if (contaSeg > MSGS_POR_SEGUNDO) return;
      if (binario) {
        const tipo = dado[0];
        if (tipo === 1) {  // meu estado: guarda o último (vai no pacote da sala)
          if (dado.length !== 1 + TAM_ESTADO) return;
          eu.estado = Buffer.from(dado.subarray(1));
          eu.novo = true;
        } else if (tipo === 2) {  // voz: só para o próprio time, com a vaga de quem falou
          if (dado.length < 2 || dado.length > 2000) return;
          const pacote = Buffer.allocUnsafe(dado.length + 1);
          pacote[0] = 2; pacote[1] = eu.vaga;
          dado.copy(pacote, 2, 1);
          for (const p of sala.values()) if (p !== eu && p.time === eu.time) mandar(p, pacote, { binary: true });
        }
        return;
      }
      const texto = dado.toString();
      if (texto.startsWith('{"k":"pg"')) { if (texto.length < 200) ws.send(texto); return; }  // ping: só para quem mandou
      if (!texto.startsWith('{"k":"ev"') || texto.length > 4096) return;  // evento: para a sala na hora
      for (const p of sala.values()) if (p !== eu) mandar(p, texto);
    } catch (e) { console.error('mensagem com erro:', e && e.message); }
  });

  ws.on('close', () => {
    if (sala.get(id) !== eu) return;
    sala.delete(id);
    if (sala.size === 0) { salas.delete(codigo); return; }
    const aviso = JSON.stringify({ k: 'ev', d: { t: 'saiu', id } });
    for (const p of sala.values()) if (p.ws.readyState === 1) p.ws.send(aviso);
    avisarVagas(sala);
  });
  ws.on('error', () => {});
});

// Pacote da sala: cada pessoa recebe junto os lutadores (dos outros) que mudaram desde o último pacote
setInterval(() => {
  for (const sala of salas.values()) {
    const novos = [];
    for (const p of sala.values()) if (p.novo && p.estado) { novos.push(p); p.novo = false; }
    if (!novos.length) continue;
    const pacoteDe = (sem) => {
      const lista = sem ? novos.filter((p) => p !== sem) : novos;
      if (!lista.length) return null;
      const b = Buffer.allocUnsafe(2 + lista.length * (1 + TAM_ESTADO));
      b[0] = 1; b[1] = lista.length;
      let o = 2;
      for (const p of lista) { b[o] = p.vaga; p.estado.copy(b, o + 1); o += 1 + TAM_ESTADO; }
      return b;
    };
    const todos = pacoteDe(null);  // quem não mudou recebe o pacote inteiro; quem mudou, sem o próprio estado
    for (const p of sala.values()) {
      const b = novos.includes(p) ? pacoteDe(p) : todos;
      if (b) mandar(p, b, { binary: true });
    }
  }
}, TICK_MS);

// erro inesperado numa mensagem não derruba as partidas de todo mundo
process.on('uncaughtException', (e) => console.error('erro inesperado:', e && e.stack));

// conexões caídas (celular sem sinal) saem depois de 30 s sem resposta
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.vivo) { ws.terminate(); continue; }
    ws.vivo = false;
    try { ws.ping(); } catch (_) {}
  }
}, 15000);

servidor.listen(PORT, () => console.log(`Favela Stricker no ar na porta ${PORT}`));
