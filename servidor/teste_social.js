// Teste do chat global e dos presentes (node teste_social.js): sobe o servidor com certificados de teste (tokens
// assinados aqui, como o Google assina os do Firebase) e confere login, mensagens, limite, filtro e presentes.
'use strict';
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const WebSocket = require('ws');

const PORTA = 10777;
const { privateKey, publicKey } = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
const certs = path.join(os.tmpdir(), 'certs_teste_social.json');
fs.writeFileSync(certs, JSON.stringify({ teste: publicKey.export({ type: 'spki', format: 'pem' }) }));
const PROJ = 'navegador-f06a8';
function token(uid, nome) {
  const b = (o) => Buffer.from(JSON.stringify(o)).toString('base64url');
  const agora = Math.floor(Date.now() / 1000);
  const corpo = b({ alg: 'RS256', kid: 'teste' }) + '.' + b({ aud: PROJ, iss: 'https://securetoken.google.com/' + PROJ, sub: uid, exp: agora + 600, name: nome });
  return corpo + '.' + crypto.createSign('RSA-SHA256').update(corpo).sign(privateKey, 'base64url');
}
let falhas = 0;
const ok = (c, msg) => { console.log((c ? '  ok  ' : '  FALHOU ') + msg); if (!c) falhas++; };
const dorme = (ms) => new Promise((r) => setTimeout(r, ms));

function cliente(uid, nome) {
  return new Promise((pronto) => {
    const ws = new WebSocket(`ws://localhost:${PORTA}/chat`);
    const c = { ws, msgs: [], erros: [], eu: null };
    ws.on('message', (d) => {
      const j = JSON.parse(d.toString());
      if (j.k === 'ok') { c.eu = j.eu; pronto(c); }
      else if (j.k === 'msg') c.msgs.push(j.m);
      else if (j.k === 'erro') c.erros.push(j.txt);
    });
    ws.on('open', () => ws.send(JSON.stringify({ k: 'auth', token: token(uid, nome), plataforma: 'ios' })));
  });
}

(async () => {
  const srv = spawn(process.execPath, [path.join(__dirname, 'server.js')], { env: { ...process.env, PORT: PORTA, LOJA_TEST_CERTS: certs, FIREBASE_DB_URL: 'http://127.0.0.1:9' }, stdio: 'inherit' });
  await dorme(800);
  try {
    // login falso: derruba
    const falso = await new Promise((r) => { const ws = new WebSocket(`ws://localhost:${PORTA}/chat`); ws.on('open', () => ws.send(JSON.stringify({ k: 'auth', token: 'a.b.c' }))); ws.on('close', (code) => r(code)); });
    ok(falso === 4001, 'token falso é recusado (' + falso + ')');
    const a = await cliente('uidAnaaaaaaaa', 'Ana'), b = await cliente('uidBetoooooooo', 'Beto');
    ok(a.eu && a.eu.nome === 'Ana', 'login no chat com o nome da conta');
    a.ws.send(JSON.stringify({ k: 'msg', txt: 'oi geral, porra' }));
    await dorme(300);
    const m = b.msgs[0];
    ok(m && m.nome === 'Ana' && m.txt === 'oi geral, *****' && m.plataforma === 'ios', 'mensagem chega para os outros, com palavrão tapado: ' + JSON.stringify(m && m.txt));
    a.ws.send(JSON.stringify({ k: 'msg', txt: 'de novo' }));
    await dorme(200);
    ok(a.erros.some((e) => /Devagar/.test(e)), 'limite de 1,5 s entre mensagens');
    await dorme(1600);
    a.ws.send(JSON.stringify({ k: 'msg', txt: 'oi geral, porra' }));
    await dorme(200);
    ok(a.erros.some((e) => /repetida/.test(e)), 'mensagem repetida não passa');
    a.ws.send(JSON.stringify({ k: 'msg', txt: 'x'.repeat(500) }));
    await dorme(1700);
    a.ws.send(JSON.stringify({ k: 'msg', txt: 'y'.repeat(500) }));
    await dorme(300);
    ok(b.msgs.length >= 2 && b.msgs.every((x) => x.txt.length <= 200), 'mensagem longa cortada em 200 letras');
    // flood: 6 em 30 s -> calado
    for (let i = 0; i < 6; i++) { b.ws.send(JSON.stringify({ k: 'msg', txt: 'flood ' + i })); await dorme(1550); }
    b.ws.send(JSON.stringify({ k: 'msg', txt: 'flood final' }));
    await dorme(300);
    ok(b.erros.some((e) => /5 min/.test(e)), 'flood: fica 5 min sem falar');
    const c = await cliente('uidCaioooooooo', 'Caio');
    ok(c.ws.readyState === 1, 'quem chega depois entra (histórico enviado no login)');
    // presentes: sem a conta de serviço o servidor não faz (e o celular não faz sozinho)
    const r = await fetch(`http://localhost:${PORTA}/api/social/presente`, { method: 'POST', headers: { Authorization: 'Bearer ' + token('uidAnaaaaaaaa', 'Ana'), 'Content-Type': 'application/json' },
      body: JSON.stringify({ para: 'uidBetoooooooo', item: 'skin_arma:ak47:cosmica', chave: 'abcdef123456' }) });
    ok(r.status === 503, 'presente sem conta de serviço: desligado (' + r.status + ')');
    const r2 = await fetch(`http://localhost:${PORTA}/api/social/presente`, { method: 'POST', body: '{}' });
    ok(r2.status === 401, 'presente sem login: recusado (' + r2.status + ')');
    for (const x of [a, b, c]) x.ws.close();
  } catch (e) { console.error(e); falhas++; }
  srv.kill();
  console.log(falhas ? `FALHOU (${falhas})` : 'OK');
  process.exit(falhas ? 1 : 0);
})();
