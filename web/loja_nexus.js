// Favela Strike — loja do site NEXUS: o jogador compra moedas (ou skins) por Pix no site e elas entram sozinhas no jogo.
// O celular nunca vê a chave do site: ele fala com o servidor do jogo (servidor/loja-nexus.js) mandando o token de
// login do Firebase; o servidor confere quem é e só então pede ao site as compras pagas daquele jogador.
// (guia completo: repositório do site, sdk/favela-stricker/INTEGRACAO.md)
'use strict';

const LOJA = { site: 'https://nexus-p70i.onrender.com', jogo: 'favela-stricker' };
const SKU_PACOTES = ['MOEDAS_100', 'MOEDAS_310', 'MOEDAS_520', 'MOEDAS_1060', 'MOEDAS_2180', 'MOEDAS_5600'];
let lojaPronta = false;  // o servidor do jogo respondeu (chave do site configurada no Render)

async function lojaApi(metodo, rota, corpo) {
  if (!fb || !fb.uid || fb.anonimo) return null;  // convidado não compra
  try {
    const t = await fb.tokenValido();
    const r = await fetch(SERVIDOR + '/api/loja/' + rota, {
      method: metodo, headers: { Authorization: 'Bearer ' + t, 'Content-Type': 'application/json', ...(social.id ? { 'X-Jogador-Id': social.id } : {}) },
      body: corpo ? JSON.stringify(corpo) : undefined,
    });
    return r.ok ? r.json() : null;
  } catch (_) { return null; }
}

// ao entrar na conta: avisa o painel do site (jogador e sessão) e já busca compras feitas enquanto o jogo estava fechado
async function lojaAoEntrar() {
  const r = await lojaApi('POST', 'entrar', { nick: conta.nome() });
  lojaPronta = !!(r && r.ok);
  if (lojaPronta) receberComprasDoSite();
}

// abre o pagamento do pacote no navegador, com o ID do jogador (9 dígitos) já preenchido
async function comprarNoSite(i) {
  if (!fb || fb.anonimo) return janela('Entre na sua conta', 'Para comprar moedas, entre com e-mail e senha (convidado não pode comprar).');
  if (!lojaPronta) await lojaAoEntrar();
  if (!lojaPronta) return janela('Loja em configuração', 'A compra de moedas pelo site ainda está sendo ligada. Tente de novo daqui a pouco.');
  if (!social.id) await garantirIdentidade();
  const url = `${LOJA.site}/jogo/${LOJA.jogo}?id=${encodeURIComponent(social.id || fb.uid)}&sku=${SKU_PACOTES[i]}`;
  if (window.Nativo && Nativo.abrirLink) Nativo.abrirLink(url); else window.open(url, '_blank');
  janela('Pagamento aberto no navegador', `No site, o seu ID é ${social.id ? idBonito(social.id) : '(criando…)'}. Pague o Pix e volte ao jogo: as moedas entram sozinhas em alguns segundos.`);
}

// entrega: credita e grava a conta ANTES de confirmar; o código do pedido fica na conta (nunca credita 2 vezes)
let entregando = false;
async function receberComprasDoSite() {
  if (entregando || !lojaPronta) return;
  entregando = true;
  try {
    const lista = await lojaApi('GET', 'entregas');
    for (const o of lista || []) {
      const marca = 'pedido:' + o.code;
      if (!conta.itens().has(marca)) {
        let moedas = 0;
        for (const it of o.items || []) {
          const sku = String(it.sku).toUpperCase();
          const roupa = ROUPAS.find((r) => 'SKIN_' + r.key.toUpperCase() === sku);
          const sa = SKINS_ARMA.find((s) => `SKINARMA_${s.arma}_${s.key}`.toUpperCase() === sku);
          if (roupa) conta.ganhar('skin:' + roupa.key);
          else if (sa) conta.ganhar(`skin_arma:${sa.arma}:${sa.key}`);
          else moedas += (it.coins || 0) * (it.qty || 1);
        }
        if (moedas) conta.moedas = conta.moedas + moedas;
        conta.ganhar(marca);  // registra o pedido junto do saldo
        aviso(`Compra recebida: ${moedas ? '+' + fmt(moedas) + ' moedas' : (o.items || []).map((x) => x.name).join(', ')}`);
        for (const c of $$('.chip-moedas span:not(.mais)')) c.textContent = fmt(conta.moedas);  // atualiza o saldo na tela
      }
      await lojaApi('POST', 'confirmar', { code: o.code });
    }
  } catch (_) { /* sem internet: tenta de novo depois */ }
  finally { entregando = false; }
}

// ao voltar do navegador e a cada minuto com o jogo aberto
document.addEventListener('visibilitychange', () => { if (!document.hidden) receberComprasDoSite(); });
setInterval(() => { if (!document.hidden) receberComprasDoSite(); }, 60000);
