// Favela Strike — eventos para o painel de métricas. Igual no APK, no site do PC e no do iPhone.
//
// O jogo NÃO tem a chave do painel: manda os eventos para o servidor do jogo (POST /api/painel), que junta e envia
// com a chave guardada lá (servidor/painel.js). Tudo em segundo plano: se falhar, o jogo segue igual.
//   instalar (1ª vez no aparelho) · abrir · ping (60 s com o jogo na tela) · saiu (minimizou/fechou)
//   cadastro / login (com o método) · partida (abates, vitória, minutos, modo) · erro (não tratados) · feedback
//   compra_moedas (skin comprada com moedas do jogo) · nivel_subiu (passou de nível)
// Nunca manda e-mail, senha ou dados pessoais: o usuário é o uid do Firebase (antes do login, um id do aparelho).
'use strict';
const painel = (() => {
  const fila = [];
  let aparelho = guardado.get('painel_aparelho', '');
  if (!aparelho) {
    aparelho = 'ap_' + Math.random().toString(36).slice(2, 10) + Date.now().toString(36);
    guardado.set('painel_aparelho', aparelho);
  }
  let erros = 0, fora = false;
  const nome = () => { try { return conta.nome(); } catch (_) { return ''; } };
  const url = () => (typeof SERVIDOR === 'string' && /^https?:/.test(SERVIDOR) ? SERVIDOR + '/api/painel' : '');

  function ev(tipo, dados = {}) {
    if (fila.length >= 300) fila.shift();
    fila.push({ tipo, dados, em: new Date().toISOString() });
  }
  // o usuário é decidido na hora de mandar: os eventos do começo (abrir, instalar) já saem com a conta, se o login
  // salvo voltar em poucos segundos
  function lote() {
    const u = (typeof fb !== 'undefined' && fb && fb.uid) || aparelho;
    return fila.splice(0, 50).map((e) => ({ ...e, usuario: e.usuario || u, nome: nome(), plataforma: typeof PLATAFORMA === 'string' ? PLATAFORMA : '' }));
  }
  async function enviar(urgente = false) {
    const destino = url();
    if (!fila.length || !destino) return;
    const l = lote();
    const corpo = JSON.stringify({ eventos: l });
    try {
      if (urgente && navigator.sendBeacon && navigator.sendBeacon(destino, corpo)) return;  // fechando: o navegador termina de mandar
      const r = await fetch(destino, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: corpo, keepalive: true });
      if (!r.ok && r.status >= 500) throw new Error('servidor');
    } catch (_) {
      if (fila.length + l.length <= 300) fila.unshift(...l);  // sem internet / servidor acordando: tenta depois
    }
  }

  function saiu() {
    if (fora) return;
    fora = true;
    ev('saiu');
    enviar(true);
  }
  function voltou() {
    if (!fora) return;
    fora = false;
    ev('ping');  // volta a aparecer como online
  }

  // começo: instalar (só a primeira vez) e abrir
  if (!guardado.get('painel_instalado', false)) { ev('instalar'); guardado.set('painel_instalado', true); }
  // ícone publicado no site (o servidor do Render entrega a pasta web/): o painel mostra o ícone original
  const icone = typeof SERVIDOR === 'string' && /^https:/.test(SERVIDOR) ? SERVIDOR + '/img/icone.png' : '';
  ev('abrir', icone ? { icone } : {});
  setTimeout(() => enviar(), 4000);  // (dá tempo do login salvo voltar)
  setInterval(() => enviar(), 2500);
  setInterval(() => { if (!document.hidden && !fora) ev('ping'); }, 60000);
  document.addEventListener('visibilitychange', () => (document.hidden ? saiu() : voltou()));
  addEventListener('pagehide', saiu);
  // erros não tratados (no máximo 15 por vez que abre, para não inundar)
  const erro = (msg, onde) => {
    if (++erros > 15) return;
    ev('erro', { mensagem: String(msg || 'erro').slice(0, 300), onde: String(onde || '').slice(0, 120), versao: typeof VERSAO === 'string' ? VERSAO : '' });
  };
  addEventListener('error', (e) => erro(e.message, (e.filename || '').split('/').pop() + ':' + (e.lineno || 0)));
  addEventListener('unhandledrejection', (e) => erro(e.reason && (e.reason.message || e.reason), 'promessa'));

  return { ev, enviar, saiu, voltou };
})();
window.painel = painel;  // (o APK chama window.painel.saiu() ao minimizar)
