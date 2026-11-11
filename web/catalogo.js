// Favela Strike — catálogo das skins de arma (mercado, com moedas). O mesmo arquivo no jogo (APK e site) e no
// servidor (servidor/social.js confere o preço e se o item pode ser presente). desde: versão do jogo em que a skin
// entrou (a vitrine da tela inicial mostra primeiro as novidades).
'use strict';
// Skins de arma (mercado, com moedas): só mudam o visual, não o dano. As com modelo: a arma inteira é outra (modelo
// próprio feito na Meshy, shaders/kit/armas/<arma>_<skin>.glb), com o mesmo dano e o mesmo som da arma
var SKINS_ARMA = [
  { arma: 'ak47', key: 'cosmica', nome: 'AK-47 Energia Cósmica', raridade: 'lendaria', preco: 2200, modelo: true, desde: 54 },
  { arma: 'm4a1', key: 'dragao_raio', nome: 'M4A1 Dragão Relâmpago', raridade: 'lendaria', preco: 2200, modelo: true, desde: 54 },
  { arma: 'm4a1', key: 'mitica', nome: 'M4A1 Mítica', raridade: 'lendaria', preco: 2000, modelo: true, desde: 54 },
  { arma: 'ak47', key: 'dragao_azul', nome: 'AK-47 Dragão Azul', raridade: 'lendaria', preco: 1800, desde: 40 },
  { arma: 'm4colt', key: 'carmesim', nome: 'M4 Colt Vermelho Carmesim', raridade: 'lendaria', preco: 1800, desde: 40 },
  { arma: 'ak47', key: 'dourada', nome: 'AK-47 Dourada', raridade: 'lendaria', preco: 1499, desde: 40 },
  { arma: 'm4colt', key: 'dourada', nome: 'M4 Colt Dourada', raridade: 'lendaria', preco: 1499, desde: 40 },
  { arma: 'tt33', key: 'gold_relic', nome: 'TT-33 Gold Relic', raridade: 'lendaria', preco: 1499, desde: 40 },
  { arma: 'ak47', key: 'dragao', nome: 'AK-47 Dragão', raridade: 'epica', preco: 899, desde: 40 },
  { arma: 'tt33', key: 'nebula_shadow', nome: 'TT-33 Nebula Shadow', raridade: 'epica', preco: 899, desde: 40 },
  { arma: 'm4a1', key: 'neon', nome: 'M4A1 Neon', raridade: 'rara', preco: 499, desde: 40 },
  { arma: 'ak74', key: 'favela', nome: 'AK-74 Favela', raridade: 'rara', preco: 499, desde: 40 },
  { arma: 'tt33', key: 'crimson_forge', nome: 'TT-33 Crimson Forge', raridade: 'rara', preco: 499, desde: 40 },
  { arma: 'ak74_tatica', key: 'selva', nome: 'AK-74 tática Selva', raridade: 'comum', preco: 199, desde: 40 },
  { arma: 'm4a1', key: 'urbana', nome: 'M4A1 Urbana', raridade: 'comum', preco: 199, desde: 40 },
  { arma: 'ak47', key: 'deserto', nome: 'AK-47 Deserto', raridade: 'comum', preco: 199, desde: 40 },
];
// Presentes: só skins pagas do mercado (as roupas são grátis para todos). Quem dá paga o preço da loja com as
// próprias moedas; quem recebe ganha a skin (não pode receber uma que já tem).
const presenteavel = (item) => {
  const m = /^skin_arma:([a-z0-9_]+):([a-z0-9_]+)$/.exec(String(item || ''));
  const s = m && SKINS_ARMA.find((x) => x.arma === m[1] && x.key === m[2]);
  return s && s.preco > 0 ? s : null;
};
if (typeof module !== 'undefined') module.exports = { SKINS_ARMA, presenteavel };
