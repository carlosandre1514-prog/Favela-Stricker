// Servidor do Favela Stricker (Render): entrega o site do jogo (pasta web/) e faz as partidas ao vivo.
//
// Partida: cada celular/navegador abre um WebSocket em /partida?sala=CODIGO&id=ID&time=0|1&nome=NOME
//   texto  {"k":"st","d":{...}}  estado dos meus lutadores (20 vezes por segundo) -> vai para todos da sala
//   texto  {"k":"ev","d":{...}}  evento (dano, abate, granada)                       -> vai para todos da sala
//   binário  áudio do chat de voz (μ-law 8 kHz, 20 ms) -> vai só para o mesmo time, com 1 byte na frente
//            dizendo quem falou (número do jogador na sala), para cada voz tocar separada
// Quem sai: os outros recebem {"k":"ev","d":{"t":"saiu","id":ID}}.
// As salas (código, lista de jogadores, quem começa) ficam no Firebase; aqui só passa o que é ao vivo.
const http = require('http');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 10000;
const WEB = path.join(__dirname, '..', 'web');
const MAX_POR_SALA = 10;

const TIPOS = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
  '.json': 'application/json', '.wasm': 'application/wasm', '.data': 'application/octet-stream',
  '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml', '.ico': 'image/x-icon', '.wav': 'audio/wav',
};

// ---------------- site ----------------
const servidor = http.createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  if (url.pathname === '/saude') { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end('ok'); return; }
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
const salas = new Map();  // código -> Map(id -> {ws, time, vaga})

const wss = new WebSocketServer({ server: servidor, path: '/partida', maxPayload: 256 * 1024 });

wss.on('connection', (ws, req) => {
  const q = new URL(req.url, 'http://x').searchParams;
  const codigo = (q.get('sala') || '').toUpperCase().replace(/[^A-Z0-9]/g, '').slice(0, 8);
  const id = (q.get('id') || '').slice(0, 64);
  const time = q.get('time') === '1' ? 1 : 0;
  if (!codigo || !id) { ws.close(4000, 'sala e id obrigatórios'); return; }
  let sala = salas.get(codigo);
  if (!sala) { sala = new Map(); salas.set(codigo, sala); }
  const antigo = sala.get(id);
  if (antigo) { try { antigo.ws.close(4001, 'entrou de novo'); } catch (_) {} sala.delete(id); }
  if (sala.size >= MAX_POR_SALA) { ws.close(4002, 'sala cheia'); return; }
  // vaga: número de 0 a 254 que identifica a voz de cada um
  const usadas = new Set([...sala.values()].map((p) => p.vaga));
  let vaga = 0;
  while (usadas.has(vaga)) vaga++;
  const eu = { ws, time, vaga, id };
  sala.set(id, eu);
  ws.vivo = true;
  ws.on('pong', () => { ws.vivo = true; });

  ws.on('message', (dado, binario) => {
    if (binario) {  // voz: só para o próprio time, com o número de quem falou na frente
      if (dado.length > 2000) return;
      const pacote = Buffer.concat([Buffer.from([eu.vaga]), dado]);
      for (const p of sala.values())
        if (p !== eu && p.time === eu.time && p.ws.readyState === 1) p.ws.send(pacote, { binary: true });
      return;
    }
    const texto = dado.toString();
    if (texto.length > 64 * 1024) return;
    for (const p of sala.values()) if (p !== eu && p.ws.readyState === 1) p.ws.send(texto);
  });

  ws.on('close', () => {
    if (sala.get(id) !== eu) return;
    sala.delete(id);
    const aviso = JSON.stringify({ k: 'ev', d: { t: 'saiu', id } });
    for (const p of sala.values()) if (p.ws.readyState === 1) p.ws.send(aviso);
    if (sala.size === 0) salas.delete(codigo);
  });
});

// conexões caídas (celular sem sinal) saem depois de 30 s sem resposta
setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.vivo) { ws.terminate(); continue; }
    ws.vivo = false;
    try { ws.ping(); } catch (_) {}
  }
}, 15000);

servidor.listen(PORT, () => console.log(`Favela Stricker no ar na porta ${PORT}`));
