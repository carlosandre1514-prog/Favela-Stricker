// Favela Stricker — utilidades da interface: palco ajustado à tela, telas, avisos, janelas e ícones.
'use strict';

const $ = (s, e = document) => e.querySelector(s);
const $$ = (s, e = document) => [...e.querySelectorAll(s)];
const html = (s) => { const t = document.createElement('template'); t.innerHTML = s.trim(); return t.content.firstElementChild; };
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const fmt = (n) => Number(n || 0).toLocaleString('pt-BR');
const dorme = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------------- palco: desenhado em 844x390 e ajustado à tela ----------------
// Ultra: acabamento extra da interface (transições, vidro desfocado); nos outros níveis fica desligado
if (motor.nivel() >= 3) document.body.classList.add('ultra');

const BASE_L = 844, BASE_A = 390;
const palco = $('#palco');
let escala = 1;
function ajustarPalco() {
  escala = Math.min(innerWidth / BASE_L, innerHeight / BASE_A);
  if (!TOQUE) escala = Math.min(escala, 1.6);  // monitor grande: não deixa os botões gigantes
  palco.style.width = innerWidth / escala + 'px';
  palco.style.height = innerHeight / escala + 'px';
  palco.style.transform = `scale(${escala})`;
  for (const f of aoRedimensionar) f();
}
const aoRedimensionar = [];
const larguraPalco = () => innerWidth / escala;
const alturaPalco = () => innerHeight / escala;
addEventListener('resize', ajustarPalco);
ajustarPalco();

// ---------------- telas ----------------
const VERSAO = ANDROID ? 'v' + (() => { try { return Nativo.versao(); } catch (_) { return ''; } })() : 'site';
function mostrar(no, tag) {
  if (ANDROID) { try { Nativo.fase('tela ' + tag); } catch (_) {} }  // vai para o rastro do diagnóstico
  for (const c of [...palco.children]) if (!c.classList.contains('fixo')) c.remove();
  no.dataset.tela = tag;
  palco.prepend(no);
  return no;
}
const telaAtual = () => { const t = palco.querySelector('[data-tela]'); return t ? t.dataset.tela : ''; };

function aviso(msg, ms = 2600) {
  const velho = $('#aviso');
  if (velho) velho.remove();
  const t = html(`<div id="aviso" class="fixo">${esc(msg)}</div>`);
  palco.appendChild(t);
  setTimeout(() => t.remove(), ms);
}

// Janela com botões. botoes: [{texto, classe, valor}] → Promise com o valor do botão tocado
function janela(titulo, mensagem, botoes = [{ texto: 'OK', classe: 'btn-ouro', valor: true }], extra = '') {
  return new Promise((ok) => {
    const c = html(`<div class="camada fixo"><div class="janela"><span class="t">${esc(titulo)}</span>
      ${mensagem ? `<span class="m">${esc(mensagem)}</span>` : ''}${extra}
      <div class="linha" style="justify-content:flex-end;margin-top:4px">${botoes.map((b, i) => `<button class="btn ${b.classe || 'btn-fraco'}" data-i="${i}">${esc(b.texto)}</button>`).join('')}</div></div></div>`);
    palco.appendChild(c);
    for (const b of $$('[data-i]', c)) b.onclick = () => { c.remove(); ok(botoes[Number(b.dataset.i)].valor); };
  });
}
const confirmar = (titulo, mensagem, sim = 'Sim', nao = 'Cancelar') =>
  janela(titulo, mensagem, [{ texto: nao, classe: 'btn-fraco', valor: false }, { texto: sim, classe: 'btn-ouro', valor: true }]);

// ---------------- ícones (os mesmos traços do modelo das telas) ----------------
const ICONE = {
  voltar: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#F3F4F6" stroke-width="2.5"><path d="M15 5l-7 7 7 7"/></svg>',
  fechar: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#F3F4F6" stroke-width="2.5"><path d="M6 6l12 12M18 6L6 18"/></svg>',
  play: '<svg width="22" height="22" viewBox="0 0 24 24" fill="#111827"><path d="M7 4v16l13-8z"/></svg>',
  skins: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#F3F4F6" stroke-width="2"><path d="M3 10l9-6 9 6-9 6z"/><path d="M3 15l9 6 9-6"/></svg>',
  moedas: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#FCD34D" stroke-width="2"><circle cx="12" cy="12" r="8"/><path d="M12 8v8M9.5 10.5h4a1.5 1.5 0 0 1 0 3h-3"/></svg>',
  moeda: (t = 14) => `<svg width="${t}" height="${t}" viewBox="0 0 24 24" fill="#FCD34D"><circle cx="12" cy="12" r="9"/></svg>`,
  armas: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#F3F4F6" stroke-width="2"><path d="M3 10h14l3-2v4h-3l-2 2h-3l-1 5H8l1-5H3z"/></svg>',
  grupo: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#F3F4F6" stroke-width="2"><circle cx="9" cy="8" r="3"/><path d="M3 20c0-3.3 2.7-6 6-6s6 2.7 6 6"/><circle cx="17" cy="9" r="2.5"/><path d="M16 14c2.8 0 5 2.2 5 5"/></svg>',
  engrenagem: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#F3F4F6" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"/></svg>',
  pessoa: '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="#F3F4F6" stroke-width="2"><circle cx="12" cy="8" r="4"/><path d="M4 21c0-4.4 3.6-8 8-8s8 3.6 8 8"/></svg>',
  escudo: '<svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2"><path d="M12 2l8 3v6c0 5-3.5 9-8 11-4.5-2-8-6-8-11V5z"/><path d="M9 12l2 2 4-4"/></svg>',
  morro: '<svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="#111827" stroke-width="2"><path d="M3 20l6-9 4 5 3-4 5 8z"/><path d="M8 7h3V4"/></svg>',
  coracao: '<svg width="15" height="15" viewBox="0 0 24 24" fill="#F87171"><path d="M12 21s-8-5.3-8-11a4.5 4.5 0 0 1 8-2.8A4.5 4.5 0 0 1 20 10c0 5.7-8 11-8 11z"/></svg>',
  pausa: '<svg width="16" height="16" viewBox="0 0 24 24" fill="#F3F4F6"><rect x="6" y="5" width="4" height="14"/><rect x="14" y="5" width="4" height="14"/></svg>',
  sacola: '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2"><path d="M4 7h16l-1.5 12h-13z"/><path d="M9 7a3 3 0 0 1 6 0"/></svg>',
  trocar: '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.2"><path d="M4 8h13l-3-3M20 16H7l3 3"/></svg>',
  alvo: (t) => `<svg width="${t}" height="${t}" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2"><circle cx="12" cy="12" r="7"/><path d="M12 2v5M12 17v5M2 12h5M17 12h5"/></svg>`,
  cima: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.5"><path d="M5 15l7-7 7 7"/></svg>',
  baixo: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.5"><path d="M5 9l7 7 7-7"/></svg>',
  granada: '<svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="#FCD34D" stroke-width="2"><circle cx="11" cy="14" r="6"/><path d="M9 8V6h4v2M13 6l3-2 2 2"/></svg>',
  recarregar: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.2"><path d="M20 12a8 8 0 1 1-2.3-5.6"/><path d="M20 4v5h-5"/></svg>',
  microfone: '<svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.2"><rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0M12 18v3"/></svg>',
  altofalante: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.2"><path d="M4 9h4l5-4v14l-5-4H4z"/><path d="M16 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11"/></svg>',
  mudo: '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.2"><path d="M4 9h4l5-4v14l-5-4H4z"/><path d="M17 9l5 6M22 9l-5 6"/></svg>',
};
