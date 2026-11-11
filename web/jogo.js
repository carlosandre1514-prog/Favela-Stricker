// Favela Stricker — as telas do jogo (as mesmas no site e no APK), no visual do modelo das telas.
'use strict';

const chipMoedas = (mais = true) => `<button class="chip-moedas ${mais ? '' : 'sem-mais'}" data-ir="moedas">${ICONE.moeda(16)}<span>${fmt(conta.moedas)}</span>${mais ? '<span class="mais">+</span>' : ''}</button>`;
function topo(titulo, voltar, extra = '', direita = chipMoedas(false)) {
  const h = html(`<div class="topo"><button class="voltar" aria-label="Voltar">${ICONE.voltar}</button><span class="titulo">${esc(titulo)}</span>${extra}<span class="cresce"></span>${direita}</div>`);
  h.querySelector('.voltar').onclick = voltar;
  return h;
}
function ligarAtalhos(t) {
  for (const b of $$('[data-ir]', t)) b.onclick = () => ({ moedas: telaMoedas, mercado: () => telaMercado('todas'), armas: telaArmas, config: () => telaConfig('sens'), multi: telaMultiplayer, amigos: () => telaAmigos('amigos'), chat: telaChatGlobal })[b.dataset.ir]();
}
const nomeNivel = () => NIVEIS[motor.nivel()]?.nome || 'Leve';

// Botão "voltar" do Android: cada tela sabe para onde volta. true = tratado.
window.voltar = () => {
  if ($('.camada')) { $('.camada').remove(); return true; }
  if (partida) { if (partida.loja) fecharLojaPartida(); else pausar(); return true; }
  const t = telaAtual();
  if (t === 'inicio' || t === 'login' || t === 'carregando') return false;
  if (t === 'sala') { $('.voltar')?.click(); return true; }
  const v = $('.voltar');
  if (v) { v.click(); return true; }
  telaInicio();
  return true;
};

// ---------------- login ----------------
function telaLogin() {
  motor.girarMenu(true);
  let criando = false;
  const t = mostrar(html(`<div class="tela sobre-mapa" style="flex-direction:row;align-items:center;justify-content:space-between;padding:22px 30px">
    <div class="coluna" style="gap:6px">
      <div class="logo-fs"><svg class="coroa" viewBox="0 0 40 26" aria-hidden="true"><path d="M3 23 L6 6 L14 15 L20 3 L26 15 L34 6 L37 23 Z" fill="none" stroke="#FBBF24" stroke-width="3" stroke-linejoin="round"/></svg>
        <span class="bebas favela">FAVELA</span><span class="bebas strike">STRIKE</span></div>
      <span style="font-size:12px;color:#D1D5DB;letter-spacing:2px;font-weight:600">POLÍCIA x BANDIDOS · 5 CONTRA 5 · PRIMEIRA PESSOA</span>
    </div>
    <div class="cartao coluna" style="width:360px;background:rgba(11,15,23,.85)">
      <span class="bebas" id="l-titulo" style="font-size:30px">ENTRAR</span>
      <input type="text" id="l-nome" placeholder="Seu nome no jogo" maxlength="16" style="display:none">
      <input type="email" id="l-email" placeholder="E-mail" autocomplete="email">
      <input type="password" id="l-senha" placeholder="Senha (6 ou mais)" autocomplete="current-password">
      <button class="btn btn-ouro" id="l-ir">ENTRAR</button>
      <div class="linha" style="justify-content:space-between"><button class="dica" id="l-troca" style="color:#93C5FD;font-weight:700">Não tem conta? Criar conta</button>
        <button class="dica" id="l-esqueci">Esqueci a senha</button></div>
      <button class="btn btn-fraco" id="l-convidado">Jogar como convidado</button>
      <div class="status" id="l-status"></div>
    </div><span class="versao">Favela Strike ${esc(VERSAO)}</span></div>`), 'login');
  $('#l-nome', t).value = guardado.get('nome', '');
  $('#l-email', t).value = guardado.get('email', '');
  const st = $('#l-status', t);
  const modo = (c) => {
    criando = c;
    $('#l-titulo', t).textContent = c ? 'CRIAR CONTA' : 'ENTRAR';
    $('#l-nome', t).style.display = c ? '' : 'none';
    $('#l-ir', t).textContent = c ? 'CRIAR CONTA' : 'ENTRAR';
    $('#l-troca', t).textContent = c ? 'Já tem conta? Entrar' : 'Não tem conta? Criar conta';
  };
  const rodar = async (msg, trabalho) => {
    const f = novoFirebase();
    if (!f) { st.textContent = 'Configuração do jogo faltando'; return; }
    st.textContent = msg;
    for (const b of $$('button', t)) b.disabled = true;
    const e = await trabalho(f);
    for (const b of $$('button', t)) b.disabled = false;
    if (e) { st.textContent = e; return; }
    fb = f;
    guardado.set('login_refresh', f.refresh || '');
    await carregarConta();
    garantirIdentidade().then(() => { if (typeof lojaAoEntrar === 'function') lojaAoEntrar(); });  // loja do site: com o ID de 9 dígitos
    telaInicio();
  };
  $('#l-ir', t).onclick = () => {
    const em = $('#l-email', t).value.trim(), pw = $('#l-senha', t).value;
    guardado.set('email', em);
    if (criando) {
      const nm = $('#l-nome', t).value.trim().slice(0, 16);
      if (!nm) { st.textContent = 'Escolha um nome para o jogo'; return; }
      guardado.set('nome', nm);
      rodar('Criando a conta…', (f) => f.criar(em, pw));
    } else rodar('Entrando…', (f) => f.entrar(em, pw));
  };
  $('#l-senha', t).onkeydown = (e) => { if (e.key === 'Enter') $('#l-ir', t).click(); };
  $('#l-troca', t).onclick = () => modo(!criando);
  $('#l-esqueci', t).onclick = async () => {
    const em = $('#l-email', t).value.trim();
    if (!em) { st.textContent = 'Digite o e-mail primeiro'; return; }
    const f = novoFirebase();
    if (f) st.textContent = (await f.recuperar(em)) || 'Enviamos um e-mail para trocar a senha';
  };
  $('#l-convidado', t).onclick = () => rodar('Entrando como convidado…', (f) => f.convidado());
}

async function comecarLogin() {
  const salvo = guardado.get('login_refresh', '');
  const f = novoFirebase();
  if (!salvo || !f) { telaLogin(); return; }
  mostrar(html('<div class="tela sobre-mapa" style="align-items:center;justify-content:center"><span class="bebas" style="font-size:30px">ENTRANDO…</span></div>'), 'entrando');
  if (await f.restaurar(salvo)) { fb = f; await carregarConta(); garantirIdentidade().then(() => { if (typeof lojaAoEntrar === 'function') lojaAoEntrar(); }); telaInicio(); } else telaLogin();
}

// ---------------- tela inicial ----------------
function destaqueDoMercado() {
  const naoTem = SKINS_ARMA.filter((s) => !conta.temSkinArma(s));
  return naoTem.sort((a, b) => b.preco - a.preco)[0] || SKINS_ARMA[0];
}
// Cartão da conta (tela inicial): insígnia da patente, nome, nível, patente e barra de XP
function cartaoConta() {
  const n = nivelDe(conta.xp);
  const pct = n.precisa ? Math.round(100 * n.noNivel / n.precisa) : 100;
  return `<button class="cartao-conta" id="i-conta" aria-label="Sua conta: nível ${n.nivel}, ${esc(n.patente.nome)}">
    <span class="insignia">${insignia(n.patente, 30)}</span>
    <span class="dados"><span class="linha1"><b>${esc(conta.nome())}</b><span class="nv">NV ${n.nivel}</span></span>
      <span class="pat">${esc(n.patente.nome)}${n.divisao ? ' ' + n.divisao : ''}${social.id || guardado.get('meu_id', '') ? ` · ID ${idBonito(social.id || guardado.get('meu_id', ''))}` : ''}</span>
      <span class="barra-xp"><i style="width:${pct}%"></i></span>
      <span class="xp">${n.precisa ? `${fmt(n.noNivel)} / ${fmt(n.precisa)} XP` : 'NÍVEL MÁXIMO'}</span></span>
  </button>`;
}
// Personagem da tela inicial: o da última partida (lado, roupa e arma com a skin equipada), que gira 360 graus
// arrastando o dedo. Duas camadas do mesmo tamanho (tools/recortar_lobby.py), cada uma uma tira com os ângulos
// lado a lado: o personagem na pose de segurar e a arma por cima.
const LOBBY = { quadros: 24, largura: 390, altura: 480 };
let anguloBoneco = 0;  // quadro mostrado (fica onde a pessoa deixou enquanto o app está aberto)
function bonecoInicio() {
  const u = guardado.get('ultimo', null) || { time: guardado.get('lado', 0) };
  const time = u.time === 1 ? 1 : 0;
  const roupa = ROUPAS.find((r) => r.key === u.roupa && r.time === time) || conta.roupa(time);
  // a arma usada por último na partida (com a skin equipada dela); sem partida ainda, a que tem skin
  const arma = ARMAS.some((a) => a.key === u.arma && a.key !== 'tt33') ? u.arma : armaInicial(time);
  const skin = conta.skinArma(arma);
  const comSkin = SKINS_ARMA.some((s) => s.arma === arma && s.key === skin) ? `_${skin}` : '';
  return `<div class="boneco-inicio" id="boneco" style="aspect-ratio:${LOBBY.largura} / ${LOBBY.altura};--n:${LOBBY.quadros}" role="img"
      aria-label="${esc(roupa.nome)} com ${esc(armaPorKey(arma).nome)} · arraste para girar">
    <div class="boneco-camada" style="background-image:url(lobby/p_${roupa.key}.webp)"></div>
    <div class="boneco-camada" style="background-image:url(lobby/a_${arma}${comSkin}.webp),url(lobby/a_${arma}.webp)"></div>
    <span class="dica-girar">⟲ arraste para girar</span></div>`;
}
// arrastar para os lados gira o personagem (um ângulo a cada 14 px); a dica some depois do primeiro giro
function ligarGiroBoneco(raiz) {
  const el = $('#boneco', raiz);
  if (!el) return;
  const camadas = $$('.boneco-camada', el);
  const mostrar = (i) => {
    anguloBoneco = ((i % LOBBY.quadros) + LOBBY.quadros) % LOBBY.quadros;
    const x = LOBBY.quadros > 1 ? (anguloBoneco / (LOBBY.quadros - 1)) * 100 : 0;
    for (const c of camadas) c.style.backgroundPosition = `${x}% 0`;
  };
  mostrar(anguloBoneco);
  let x0 = null, a0 = 0;
  el.addEventListener('pointerdown', (ev) => { x0 = ev.clientX; a0 = anguloBoneco; el.setPointerCapture(ev.pointerId); el.classList.add('girou'); });
  el.addEventListener('pointermove', (ev) => { if (x0 !== null) mostrar(a0 + Math.round((ev.clientX - x0) / 14)); });
  const soltar = () => { x0 = null; };
  el.addEventListener('pointerup', soltar);
  el.addEventListener('pointercancel', soltar);
}
let mapaAdiantado = false;
function telaInicio() {
  motor.girarMenu(true);
  // já vai carregando o mapa da partida por trás do menu (o celular monta a favela enquanto a pessoa escolhe)
  // (espera o motor ficar pronto: no celular ele demora a abrir)
  if (!mapaAdiantado) {
    mapaAdiantado = true;
    const tentar = () => { if (motor.pronto()) motor.prepararMapa(); else setTimeout(tentar, 1000); };
    setTimeout(tentar, 1500);
  }
  const d = destaqueDoMercado();
  const t = mostrar(html(`<div class="tela sobre-mapa" id="inicio" style="padding:0">
    <div class="esquerda">
      <div class="coluna" style="gap:4px">
        <div class="logo-fs"><svg class="coroa" viewBox="0 0 40 26" aria-hidden="true"><path d="M3 23 L6 6 L14 15 L20 3 L26 15 L34 6 L37 23 Z" fill="none" stroke="#FBBF24" stroke-width="3" stroke-linejoin="round"/></svg>
        <span class="bebas favela">FAVELA</span><span class="bebas strike">STRIKE</span></div>
        <span style="font-size:11px;color:#D1D5DB;letter-spacing:2px;font-weight:700">RESSURGÊNCIA · POLÍCIA x BANDIDOS</span>
      </div>
      <button class="btn btn-ouro btn-grande" id="i-jogar" style="width:260px">${ICONE.play} JOGAR</button>
      <button class="btn btn-ouro btn-medio" data-ir="multi" style="width:260px">${ICONE.grupo} MULTIPLAYER</button>
      <div class="grade">
        <button class="btn btn-fraco" data-ir="mercado">${ICONE.skins} Skins</button>
        <button class="btn btn-fraco" data-ir="armas">${ICONE.armas} Arsenal</button>
      </div>
    </div>
    <div class="direita-topo">
      ${chipMoedas()}
      <button class="chip amigos" data-ir="amigos" aria-label="Amigos">${ICONE.grupo}<span>Amigos</span><b class="badge-amigos" style="display:none">0</b></button>
      <button class="chip" data-ir="chat" aria-label="Chat global"><span>💬</span><span>Chat</span></button>
      <button class="chip engrenagem" data-ir="config" aria-label="Configurações">${ICONE.engrenagem}</button>
      ${cartaoConta()}
    </div>
    ${bonecoInicio()}
    <span class="versao">Favela Strike ${esc(VERSAO)}</span>
    <button class="destaque" data-ir="mercado">
      <div class="linha" style="gap:8px"><span class="etiqueta fundo-${d.raridade}">${RARIDADES[d.raridade].toUpperCase()}</span><span class="dica" style="color:#D1D5DB">Destaque do mercado</span></div>
      <img src="armas/${d.arma}_${d.key}.png" alt="${esc(d.nome)}">
      <div class="linha" style="justify-content:space-between"><span class="bebas" style="font-size:21px">${esc(d.nome.toUpperCase().replace(' ', ' · '))}</span>
        <span class="linha" style="gap:6px;font-size:15px;font-weight:800;color:#FCD34D">${ICONE.moeda(14)} ${fmt(d.preco)}</span></div>
    </button>
  </div>`), 'inicio');
  ligarAtalhos(t);
  ligarGiroBoneco(t);
  atualizarBadgeAmigos();
  $('#i-jogar', t).onclick = telaModo;
  $('#i-conta', t).onclick = () => telaConfig('conta');
}

// ---------------- escolha do lado ----------------
// ---------------- escolha do modo ----------------
function telaModo() {
  motor.girarMenu(true);
  const t = mostrar(html(`<div class="tela">
    <div class="lados">
      <button class="lado modo-br" data-modo="1">
        <div class="linha" style="gap:12px"><div class="icone" style="background:#DC2626">${ICONE.grupo}</div>
          <div class="coluna" style="gap:0"><span class="nome" style="color:#FCA5A5">RESSURGÊNCIA</span><span style="font-size:11px;font-weight:800;letter-spacing:2px;color:#FECACA">BATTLE ROYALE</span></div></div>
        <span class="texto" style="color:#FEE2E2">10 times de 4 caem do céu na favela. Caiu? Volta em 10 s perto do seu time, enquanto alguém do time estiver de pé. A zona fecha: o último time de pé vence.</span>
        <span class="cresce"></span>
        <div class="linha" style="gap:6px;flex-wrap:wrap"><span class="tag" style="background:rgba(220,38,38,.22);color:#FEE2E2">40 jogadores</span>
          <span class="tag" style="background:rgba(220,38,38,.22);color:#FEE2E2">zona em 6 fases</span><span class="tag" style="background:rgba(220,38,38,.22);color:#FEE2E2">voz do time</span></div>
        <div class="linha" style="justify-content:space-between"><span style="font-size:12px;color:#FECACA">~8 min · online, só gente de verdade</span><span class="entrar" style="background:#DC2626">JOGAR</span></div>
      </button>
      <button class="lado pol" data-modo="0">
        <div class="linha" style="gap:12px"><div class="icone" style="background:#3B82F6">${ICONE.escudo}</div>
          <div class="coluna" style="gap:0"><span class="nome" style="color:#93C5FD">POLÍCIA x BANDIDOS</span><span style="font-size:11px;font-weight:800;letter-spacing:2px;color:#BFDBFE">5 CONTRA 5</span></div></div>
        <span class="texto" style="color:#DBEAFE">Escolha o lado e tome o morro. Renasce na base, compra armas com o $ dos abates. Até 50 abates ou 20 min.</span>
        <span class="cresce"></span>
        <div class="linha" style="gap:6px;flex-wrap:wrap"><span class="tag" style="background:rgba(59,130,246,.22);color:#DBEAFE">10 jogadores</span>
          <span class="tag" style="background:rgba(59,130,246,.22);color:#DBEAFE">loja na base</span></div>
        <div class="linha" style="justify-content:space-between"><span style="font-size:12px;color:#BFDBFE">também no multiplayer</span><span class="entrar" style="background:#3B82F6">JOGAR</span></div>
      </button>
    </div></div>`), 'modo');
  t.prepend(topo('ESCOLHA O MODO', telaInicio, '', ''));
  $('[data-modo="1"]', t).onclick = () => { const u = guardado.get('ultimo', null); telaRoupa(u && u.time === 1 ? 1 : guardado.get('lado', 0), null, 1); };
  $('[data-modo="0"]', t).onclick = telaLado;
}

function telaLado() {
  motor.girarMenu(true);
  const t = mostrar(html(`<div class="tela">
    <div class="lados">
      <button class="lado pol" data-time="0">
        <div class="linha" style="gap:12px"><div class="icone" style="background:#3B82F6">${ICONE.escudo}</div><span class="nome" style="color:#93C5FD">POLÍCIA</span></div>
        <span class="texto" style="color:#DBEAFE">Nasce fora da favela, na rua dos prédios. Sobe as escadarias para tomar o morro.</span>
        <span class="cresce"></span>
        <div class="linha" style="gap:6px;flex-wrap:wrap"><span class="tag" style="background:rgba(59,130,246,.22);color:#DBEAFE">M4A1 · M4 Colt com luneta</span>
          <span class="tag" style="background:rgba(59,130,246,.22);color:#DBEAFE">5 fardas grátis: PM, Civil, Exército, Choque, BOPE</span></div>
        <div class="linha" style="justify-content:space-between"><span style="font-size:12px;color:#BFDBFE">4 robôs + você</span><span class="entrar" style="background:#3B82F6">ENTRAR</span></div>
      </button>
      <button class="lado ban" data-time="1">
        <div class="linha" style="gap:12px"><div class="icone" style="background:#F59E0B">${ICONE.morro}</div><span class="nome" style="color:#FCD34D">BANDIDOS</span></div>
        <span class="texto" style="color:#FEF3C7">Nasce na quadra, dentro da favela. Com polícia na quadra, nasce num canto do morro sem policial.</span>
        <span class="cresce"></span>
        <div class="linha" style="gap:6px;flex-wrap:wrap"><span class="tag" style="background:rgba(245,158,11,.22);color:#FEF3C7">AK-47 · AK-74 · AK-74 tática</span>
          <span class="tag" style="background:rgba(245,158,11,.22);color:#FEF3C7">5 roupas grátis: Rubro, Alvinegro, Tricolor, Canarinho, Faixa</span></div>
        <div class="linha" style="justify-content:space-between"><span style="font-size:12px;color:#FDE68A">4 robôs + você</span><span class="entrar" style="background:#F59E0B;color:#111827">ENTRAR</span></div>
      </button>
    </div></div>`), 'lado');
  t.prepend(topo('ESCOLHA SEU LADO', telaModo, '', '<span class="dica">Partida até 50 abates · 20 min</span>'));
  for (const b of $$('[data-time]', t)) b.onclick = () => telaRoupa(Number(b.dataset.time));
}

// ---------------- roupa do lado (antes de jogar). pronto != null: veio da sala do multiplayer ----------------
function telaRoupa(time, pronto, modo = 0) {
  motor.girarMenu(true);
  const br = modo === 1;
  const minhas = br ? ROUPAS : ROUPAS.filter((r) => r.time === time);
  let sel = conta.roupa(time);
  const t = mostrar(html(`<div class="tela">
    <div class="linha cresce" style="align-items:stretch;gap:14px">
      <div class="grade-itens cresce" id="r-grade" style="grid-template-columns:repeat(${br ? 6 : minhas.length},minmax(0,1fr));grid-auto-rows:1fr;align-content:stretch;overflow:hidden"></div>
    </div>
    <div class="linha" style="gap:8px;flex-wrap:wrap"><span class="dica">Arma inicial:</span><span id="r-armas" class="linha" style="gap:6px;flex-wrap:wrap"></span>
      <span class="dica cresce">${br ? '+ TT-33 · você cai com ela (e a skin dela) e pega munição de quem abater' : '+ TT-33 · qualquer arma serve nos dois lados; as outras compra na base com o $ dos abates'}</span>
      <button class="btn btn-ouro btn-grande" id="r-ir" style="height:48px;font-size:24px;width:200px">${pronto ? 'PRONTO' : br ? `${ICONE.play} CONTINUAR` : `${ICONE.play} COMEÇAR`}</button></div>
  </div>`), 'roupa');
  t.prepend(topo(br ? 'SUA ROUPA · RESSURGÊNCIA' : time ? 'SUA ROUPA · BANDIDOS' : 'SUA FARDA · POLÍCIA', () => (pronto ? telaSala() : br ? telaModo() : telaLado())));
  const desenhar = () => {
    $('#r-grade', t).innerHTML = minhas.map((r) => {
      const tem = conta.temRoupa(r);
      return `<button class="item ${r === sel ? 'sel' : ''} ${tem ? '' : 'travado'}" data-k="${r.key}" style="justify-content:flex-end">
        <img src="retratos/${r.key}.png" alt="" style="flex:1 1 0;min-height:0;height:0">
        <span class="n" style="font-size:13px">${esc(r.nome)}${br ? `<span class="dica" style="display:block;font-size:10px;font-weight:700">${r.time ? 'bandido' : 'polícia'}</span>` : ''}</span>
        <span class="p ${tem ? 'verde' : 'amarelo'}">${tem ? (r === sel ? 'equipada' : 'grátis') : `${ICONE.moeda(12)} ${fmt(r.preco)}`}</span></button>`;
    }).join('');
    for (const d of $$('.item', t)) d.onclick = async () => {
      const r = minhas.find((x) => x.key === d.dataset.k);
      if (conta.temRoupa(r)) { sel = r; desenhar(); return; }
      if (await comprarComMoedas(r.nome, r.preco, 'skin:' + r.key)) { sel = r; desenhar(); }
    };
  };
  desenhar();
  const desenharArmas = () => {
    const atual = armaInicial(time);
    $('#r-armas', t).innerHTML = opcoesArmaInicial().map((a) => `<button class="chip ${a.key === atual ? 'sel' : ''}" data-arma="${a.key}"
      style="${a.key === atual ? 'background:#FCD34D;color:#111827;font-weight:800' : ''}">${esc(a.nome)}${conta.skinArma(a.key) ? ' ★' : ''}</button>`).join('');
    for (const b of $$('[data-arma]', t)) b.onclick = () => { guardado.set('arma_inicial', b.dataset.arma); desenharArmas(); };
  };
  desenharArmas();
  $('#r-ir', t).onclick = () => {
    const lado = br ? sel.time : time;
    conta.usarRoupa(lado, sel.key);
    guardado.set('lado', lado);
    if (pronto) pronto(); else if (br) telaRessurgencia(); else iniciarPartida(lado, sel, null, null);
  };
}

async function comprarComMoedas(nome, preco, id) {
  const tem = conta.moedas;
  if (tem < preco) {
    const ir = await janela('Moedas insuficientes', `${nome} custa ${fmt(preco)} moedas e você tem ${fmt(tem)}. Faltam ${fmt(preco - tem)}.\nCompre moedas na loja de moedas.`,
      [{ texto: 'Agora não', classe: 'btn-fraco', valor: false }, { texto: 'Loja de moedas', classe: 'btn-ouro', valor: true }]);
    if (ir) telaMoedas();
    return false;
  }
  if (!(await confirmar('Comprar ' + nome + '?', `Custa ${fmt(preco)} moedas. Você tem ${fmt(tem)}.`, 'Comprar'))) return false;
  conta.moedas = tem - preco;
  conta.ganhar(id);
  aviso('Comprado: ' + nome);
  return true;
}

// Arma em 360 graus (loja): só na visualização ampliada (tocar na arma do painel). Lá ela gira devagar sozinha e
// arrastando o dedo; ao fechar, para tudo (a animação e os ouvintes saem junto). Na grade e no painel: foto parada.
function abrirArma360(nome, titulo) {
  const g = GIRO[nome];
  if (!g) return;
  const fundo = document.createElement('div');
  fundo.className = 'arma360';
  fundo.innerHTML = `<div class="arma360-caixa" role="dialog" aria-modal="true" aria-label="${esc(titulo)} em 360 graus">
      <div class="linha" style="justify-content:space-between;align-items:center"><span class="bebas" style="font-size:26px">${esc(titulo.toUpperCase())}</span>
      <button class="btn btn-fraco" id="a360-fechar" aria-label="Fechar">✕</button></div>
      <div class="giro-arma grande" style="aspect-ratio:${g.largura} / ${g.altura};--n:${g.quadros};background-image:url(giro/${nome}.webp)"></div>
      <span class="dica">Arraste para girar · 360°</span></div>`;
  document.body.appendChild(fundo);
  const el = fundo.querySelector('.giro-arma'), n = g.quadros;
  let i = 0, x0 = null, i0 = 0, raf = 0, ultimo = 0, parado = 0;
  const mostrar = (k) => { i = ((k % n) + n) % n; el.style.backgroundPosition = `${n > 1 ? (i / (n - 1)) * 100 : 0}% 0`; };
  const girar = (t) => {  // um ângulo a cada 110 ms; para enquanto o dedo arrasta e um pouco depois
    raf = requestAnimationFrame(girar);
    if (x0 !== null || t < parado) return;
    if (t - ultimo > 110) { ultimo = t; mostrar(i + 1); }
  };
  const reduzir = matchMedia('(prefers-reduced-motion: reduce)').matches;
  mostrar(0);
  if (!reduzir) raf = requestAnimationFrame(girar);
  const down = (ev) => { x0 = ev.clientX; i0 = i; el.setPointerCapture(ev.pointerId); };
  const move = (ev) => { if (x0 !== null) mostrar(i0 - Math.round((ev.clientX - x0) / 12)); };
  const up = () => { x0 = null; parado = performance.now() + 1500; };
  el.addEventListener('pointerdown', down);
  el.addEventListener('pointermove', move);
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', up);
  const fechar = () => {
    cancelAnimationFrame(raf);
    el.removeEventListener('pointerdown', down); el.removeEventListener('pointermove', move);
    el.removeEventListener('pointerup', up); el.removeEventListener('pointercancel', up);
    removeEventListener('keydown', tecla);
    fundo.remove();
  };
  const tecla = (ev) => { if (ev.key === 'Escape') fechar(); };
  addEventListener('keydown', tecla);
  fundo.querySelector('#a360-fechar').onclick = fechar;
  fundo.addEventListener('click', (ev) => { if (ev.target === fundo) fechar(); });
}

// ---------------- mercado de skins (moedas) ----------------
// categorias: todas, armas (skins de arma) e personagem (roupas da polícia e dos bandidos)
function itensMercado(aba) {
  const armas = aba === 'personagem' ? [] : SKINS_ARMA.map((s) => ({
    id: `skin_arma:${s.arma}:${s.key}`, nome: s.nome, raridade: s.raridade, preco: s.preco, img: `armas/${s.arma}_${s.key}.png`, giro: `${s.arma}_${s.key}`,
    tem: conta.temSkinArma(s), usando: conta.skinArma(s.arma) === s.key, arma: s.arma, key: s.key,
    desc: s.arma === 'tt33' ? 'Skin de pistola' : s.modelo ? 'Arma com modelo próprio · vale nos dois lados' : 'Skin de arma · vale nos dois lados',
  }));
  const roupas = aba === 'armas' ? [] : ROUPAS.map((r) => ({
    id: 'skin:' + r.key, nome: r.nome, raridade: r.raridade, preco: r.preco, img: `retratos/${r.key}.png`, tem: conta.temRoupa(r),
    usando: conta.roupa(r.time) === r, roupa: r, desc: r.time ? 'Personagem · bandidos' : 'Personagem · polícia',
  }));
  const ordem = { lendaria: 0, epica: 1, rara: 2, comum: 3 };
  return [...armas, ...roupas].sort((a, b) => ordem[a.raridade] - ordem[b.raridade] || b.preco - a.preco);
}
function telaMercado(aba = 'todas', selId = null) {
  motor.girarMenu(true);
  const itens = itensMercado(aba);
  let sel = itens.find((i) => i.id === selId) || itens[0];
  const t = mostrar(html(`<div class="tela" style="gap:10px">
    <div class="linha cresce" style="align-items:stretch;gap:14px">
      <div class="grade-itens cresce" id="m-grade"></div>
      <div class="detalhe" id="m-det" style="width:340px;border-color:rgba(252,211,77,.45)"></div>
    </div></div>`), 'mercado');
  const abas = `<div class="abas">${[['todas', 'Todas'], ['armas', 'Armas'], ['personagem', 'Personagem']].map(([k, n]) => `<button class="aba ${k === aba ? 'sel' : ''}" data-aba="${k}">${n}</button>`).join('')}</div>`;
  const tp = topo('MERCADO DE SKINS', telaInicio, abas, chipMoedas());
  t.prepend(tp);
  ligarAtalhos(tp);
  for (const b of $$('[data-aba]', t)) b.onclick = () => telaMercado(b.dataset.aba);
  const desenhar = () => {
    $('#m-grade', t).innerHTML = itens.map((it) => `<button class="item ${it === sel ? 'sel' : ''}" data-id="${it.id}">
      <img src="${it.img}" alt=""><span class="n">${esc(it.nome)}</span><span class="r raridade-${it.raridade}">${RARIDADES[it.raridade]}</span>
      <span class="p ${it.tem ? 'azul' : 'amarelo'}">${it.usando ? 'usando' : it.tem ? (it.preco ? '✓ sua' : 'grátis') : `${ICONE.moeda(12)} ${fmt(it.preco)}`}</span></button>`).join('');
    for (const d of $$('.item', t)) d.onclick = () => { sel = itens.find((i) => i.id === d.dataset.id); desenhar(); };
    const det = $('#m-det', t);
    let botao;
    if (!sel.tem) botao = `<button class="btn btn-ouro" id="m-acao" style="font-size:15px;letter-spacing:1px">COMPRAR POR ${ICONE.moeda(14).replace('#FCD34D', '#111827')} ${fmt(sel.preco)}</button>`;
    else if (sel.arma) botao = sel.usando ? '<button class="btn btn-fraco" id="m-acao">USANDO · tocar para tirar</button>' : '<button class="btn btn-azul" id="m-acao">USAR NA ' + esc(armaPorKey(sel.arma).nome) + '</button>';
    else botao = sel.usando ? '<button class="btn btn-fraco" disabled>USANDO</button>' : '<button class="btn btn-azul" id="m-acao">USAR</button>';
    det.innerHTML = `<div class="linha" style="gap:8px"><span class="etiqueta fundo-${sel.raridade}">${RARIDADES[sel.raridade].toUpperCase()}</span><span class="dica">${esc(sel.desc)}</span></div>
      <span class="bebas" style="font-size:30px">${esc(sel.nome.toUpperCase())}</span>
      <div class="previa">${sel.giro && GIRO[sel.giro] ? `<button class="ver360" id="m-360" aria-label="Ver ${esc(sel.nome)} em 360 graus"><img src="${sel.img}" alt=""><span class="selo360">360°</span></button>` : `<img src="${sel.img}" alt="${esc(sel.nome)}">`}</div>
      <span class="dica" style="font-size:11px">${sel.arma ? 'Só visual, não muda o dano · a skin é da sua arma: no online os outros veem ela na sua mão e nas suas costas' : (sel.roupa.time ? 'Usada quando você joga de bandido' : 'Usada quando você joga de polícia') + ' · quantos quiserem podem usar a mesma'}</span>${botao}`;
    const b360 = $('#m-360', det);
    if (b360) b360.onclick = () => abrirArma360(sel.giro, sel.nome);
    const acao = $('#m-acao', det);
    if (!acao) return;
    acao.onclick = async () => {
      if (!sel.tem) {
        if (!(await comprarComMoedas(sel.nome, sel.preco, sel.id))) return;
        if (sel.arma) conta.usarSkinArma(sel.arma, sel.key);
        telaMercado(aba, sel.id);
        return;
      }
      if (sel.arma) conta.usarSkinArma(sel.arma, sel.usando ? '' : sel.key);
      else { conta.usarRoupa(sel.roupa.time, sel.roupa.key); guardado.set('lado', sel.roupa.time); }
      telaMercado(aba, sel.id);
    };
  };
  desenhar();
}

// ---------------- loja de moedas ----------------
function desenhoMoedas(n) {
  const pos = [[0, 0], [-10, 4], [10, 4], [-4, -8], [6, -10], [-14, -6], [14, -4]].slice(0, n);
  return `<svg width="70" height="56" viewBox="-35 -28 70 56">${pos.map(([x, y]) => `<circle cx="${x}" cy="${y}" r="13" fill="#FBBF24" stroke="#B45309" stroke-width="3"/>`).join('')}</svg>`;
}
function telaMoedas() {
  motor.girarMenu(true);
  const t = mostrar(html(`<div class="tela">
    <div class="pacotes">${PACOTES.map((p, i) => `<div class="pacote ${p.selo === 'POPULAR' ? 'popular' : p.selo ? 'melhor' : ''}">
      ${p.selo ? `<span class="selo" style="background:${p.selo === 'POPULAR' ? '#60A5FA' : '#4ADE80'};color:#0B1220">${p.selo}</span>` : ''}
      <div class="moedas-desenho">${desenhoMoedas([1, 2, 3, 4, 5, 7][i])}</div>
      <span class="qtd">${fmt(p.moedas)}</span><span class="bonus">${p.bonus ? '+' + fmt(p.bonus) + ' de bônus' : ''}</span>
      <button class="preco" data-i="${i}">${p.preco}</button></div>`).join('')}</div>
    <span class="dica" style="font-size:11px">Pagamento por Pix no site oficial. As moedas entram sozinhas no jogo (precisa entrar com e-mail e senha).</span>
  </div>`), 'moedas');
  t.prepend(topo('LOJA DE MOEDAS', telaInicio, '<span class="dica">moedas compram skins de arma e de roupa</span>', `<span class="chip-moedas sem-mais">${ICONE.moeda(16)} ${fmt(conta.moedas)}</span>`));
  for (const b of $$('[data-i]', t)) b.onclick = () => comprarNoSite(Number(b.dataset.i));  // Pix no site (loja_nexus.js)
}

// ---------------- armas e dano ----------------
function telaArmas() {
  motor.girarMenu(true);
  const linhas = ARMAS.map((a) => `<tr><td><b>${esc(a.nome)}</b>${a.luneta ? ' <span class="dica">luneta</span>' : ''}</td><td>${a.dano}</td><td class="cab">${Math.round(a.dano * 2.5)}</td>
    <td><b>${Math.ceil(100 / a.dano)}</b></td><td>${String(a.cadencia).replace('.', ',')}</td><td>${a.pente}</td><td>${a.cai} m</td></tr>`).join('');
  const t = mostrar(html(`<div class="tela">
    <div class="linha cresce" style="align-items:stretch;gap:14px">
      <div class="cartao cresce" style="padding:6px 10px;overflow:auto"><table class="tabela">
        <tr><th>ARMA</th><th>CORPO</th><th>CABEÇA</th><th>TIROS P/<br>ABATER</th><th>TIROS/S</th><th>PENTE</th><th>CAI APÓS</th></tr>${linhas}</table></div>
      <div class="cartao coluna" style="width:250px;gap:6px">
        <span class="bebas ouro" style="font-size:28px">GRANADA</span>
        <svg width="118" height="118" viewBox="0 0 160 160" style="align-self:center">
          <circle cx="80" cy="80" r="74" fill="rgba(245,158,11,.25)" stroke="#F59E0B" stroke-width="2" stroke-dasharray="6 5"/>
          <circle cx="80" cy="80" r="37" fill="rgba(239,68,68,.5)" stroke="#EF4444" stroke-width="2"/>
          <circle cx="80" cy="80" r="6" fill="#FBBF24"/>
          <text x="80" y="18" text-anchor="middle" font-size="11" font-weight="800" fill="#FDE68A">8 m</text>
          <text x="80" y="55" text-anchor="middle" font-size="11" font-weight="800" fill="#fff">4 m</text></svg>
        <span class="linha" style="gap:6px;font-size:12px"><i style="width:10px;height:10px;border-radius:2px;background:#EF4444"></i>até 4 m: abate na hora</span>
        <span class="linha" style="gap:6px;font-size:12px"><i style="width:10px;height:10px;border-radius:2px;background:#F59E0B"></i>4 a 8 m: 100 até 20 de dano</span>
        <span class="dica" style="font-size:11px">Explode 3 s depois de jogar. Parede entre você e a granada protege. $ 300 cada, máx. 2.</span>
      </div></div></div>`), 'armas');
  t.prepend(topo('ARSENAL', telaInicio, '<span class="dica">vida 100 · cabeça = corpo × 2,5 · de longe o dano cai até 30%</span>', ''));
}

// ---------------- configurações ----------------
function telaConfig(aba = 'sens') {
  motor.girarMenu(true);
  const abas = [['sens', 'Sensibilidade'], ['graficos', 'Gráficos'], ['audio', 'Som e voz'], ['hud', 'Controles (HUD)'], ['conta', 'Conta'], ['creditos', 'Créditos']];
  const t = mostrar(html(`<div class="tela"><div class="config">
    <div class="menu">${abas.map(([k, n]) => `<button class="aba ${k === aba ? 'sel' : ''}" data-aba="${k}">${n}</button>`).join('')}</div>
    <div class="corpo" id="c-corpo"></div></div></div>`), 'config');
  t.prepend(topo('CONFIGURAÇÕES', telaInicio, '', chipMoedas(false)));
  ligarAtalhos(t);
  for (const b of $$('[data-aba]', t)) b.onclick = () => telaConfig(b.dataset.aba);
  const c = $('#c-corpo', t);
  const pct = (v) => Math.round(v * 100) + '%';
  if (aba === 'sens') {
    c.innerHTML = ajusteHtml('sens', 'Câmera', 0.2, 3, 0.05, 'arrastar a tela / mouse') +
      ajusteHtml('sensMira', 'Mirando', 0.2, 2, 0.05, 'segurando MIRA (fração da câmera)') +
      ajusteHtml('sensLuneta', 'Luneta', 0.1, 1.5, 0.05, 'olhando pela luneta da M4 Colt') +
      `<div class="ajuste"><span class="nome">Inverter o eixo vertical<small>arrastar para cima olha para baixo</small></span><span class="cresce"></span><button class="chave ${config.inverterY ? 'ligada' : ''}" id="c-inv"></button></div>
       <div class="ajuste"><span class="nome">Vibrar ao levar tiro</span><span class="cresce"></span><button class="chave ${config.vibrar ? 'ligada' : ''}" id="c-vib"></button></div>
       <div class="ajuste"><span class="nome">Medidor (FPS, ms e ping)</span><span class="cresce"></span><button class="chave ${config.medidor ? 'ligada' : ''}" id="c-med"></button></div>
       <button class="btn btn-fraco" id="c-padrao" style="align-self:flex-start">Voltar ao padrão</button>`;
    ligarAjustes(c);
    $('#c-inv', c).onclick = (e) => { config.inverterY = !config.inverterY; e.target.classList.toggle('ligada', config.inverterY); salvarConfig(); };
    $('#c-vib', c).onclick = (e) => { config.vibrar = !config.vibrar; e.target.classList.toggle('ligada', config.vibrar); salvarConfig(); };
    $('#c-med', c).onclick = (e) => { config.medidor = !config.medidor; e.target.classList.toggle('ligada', config.medidor); salvarConfig(); };
    $('#c-padrao', c).onclick = () => { for (const k of ['sens', 'sensMira', 'sensLuneta', 'inverterY', 'vibrar', 'medidor']) config[k] = CONFIG_PADRAO[k]; salvarConfig(); telaConfig('sens'); };
  } else if (aba === 'graficos') {
    const atual = motor.nivel();
    c.innerHTML = `<div class="niveis">${NIVEIS.map((n, i) => `<button class="nivel ${i === atual ? 'sel' : ''}" data-n="${i}"><span class="n">${n.nome}</span><small>${esc(n.info)}</small></button>`).join('')}</div>
      <span class="dica">Trocar a qualidade reabre o jogo (as texturas e os modelos são preparados ao abrir). Se o jogo travar ou esquentar, use um nível abaixo.</span>`;
    for (const b of $$('[data-n]', c)) b.onclick = async () => {
      const n = Number(b.dataset.n);
      if (n === motor.nivel()) return;
      if (await confirmar('Qualidade ' + NIVEIS[n].nome + '?', 'O jogo vai reabrir para trocar as texturas e os modelos. Sua conta continua conectada.', 'Trocar')) motor.mudarNivel(n);
    };
  } else if (aba === 'audio') {
    c.innerHTML = ajusteHtml('volume', 'Efeitos', 0, 1, 0.05, 'tiros, passos, explosões', pct) + ajusteHtml('volumeVoz', 'Voz do time', 0, 1.5, 0.05, 'chat de voz no online', pct) +
      '<span class="dica">No online, segure o botão do microfone (ou V no computador) para falar com o seu time.</span>';
    ligarAjustes(c, { volume: pct, volumeVoz: pct });
  } else if (aba === 'hud') {
    c.innerHTML = `<div class="cartao coluna"><span class="bebas" style="font-size:26px">MONTE O SEU HUD</span>
      <span class="dica">Arraste cada botão para onde quiser, deixe maior ou menor e mais ou menos visível. Vale para todas as partidas.</span>
      <div class="linha"><button class="btn btn-ouro" id="c-editar">Editar o HUD</button><button class="btn btn-fraco" id="c-hud-padrao">Voltar ao padrão</button></div></div>`;
    $('#c-editar', c).onclick = () => abrirEditorHud(() => telaConfig('hud'));
    $('#c-hud-padrao', c).onclick = () => { guardado.del('hud_layout'); layout = carregarLayout(); aviso('HUD de volta ao padrão'); };
  } else if (aba === 'creditos') {
    // modelos e texturas de outros autores (as licenças CC BY pedem o crédito; a lista completa está em CREDITOS.txt)
    const itens = [
      ['Colete dos personagens', '"Armor vest" por 3DWorkbench (Sketchfab)', 'CC BY 4.0 · simplificado e encaixado no boneco'],
      ['Fuzil M4A1', '"M4A1" por Firewarden3D (Sketchfab)', 'CC BY 4.0 · texturas reduzidas'],
      ['Animações', 'Mixamo (Adobe)', 'uso livre em jogos'],
      ['Prédios e peças da cidade', 'Downtown City MegaKit por Quaternius', 'CC0'],
      ['Texturas do chão', 'Poly Haven', 'CC0'],
      ['Água', 'ocean por Jérémy Bouny (jbouny)', 'MIT'],
      ['Fontes', 'Bebas Neue (Dharma Type), Inter (Rasmus Andersson), Permanent Marker, Bangers, Rubik Wet Paint', 'SIL OFL 1.1 / Apache 2.0'],
      ['Bibliotecas', 'GLM, tinygltf, nlohmann/json, stb, ufbx, Bullet Physics', 'MIT / zlib / domínio público'],
    ];
    c.innerHTML = itens.map(([o, quem, lic]) => `<div class="ajuste"><span class="nome">${esc(o)}<small>${esc(lic)}</small></span><span class="cresce" style="font-size:13px">${esc(quem)}</span></div>`).join('') +
      '<span class="dica">CC BY 4.0: creativecommons.org/licenses/by/4.0 · lista completa no arquivo CREDITOS.txt do jogo.</span>';
  } else if (aba === 'conta') {
    const quem = !fb ? 'Sem conta (só neste aparelho)' : fb.anonimo ? 'Convidado (crie uma conta para não perder nada)' : fb.email;
    c.innerHTML = `<div class="ajuste"><span class="nome">Nome no jogo</span><input type="text" id="c-nome" maxlength="16" value="${esc(conta.nome())}"><button class="btn btn-fraco" id="c-salvar-nome">Salvar</button></div>
      <div class="ajuste"><span class="nome">Seu ID</span><span class="cresce bebas ouro" style="font-size:24px;letter-spacing:2px">${social.id ? idBonito(social.id) : '—'}</span>
        ${social.id ? '<button class="btn btn-fraco" id="c-copiar-id">Copiar</button>' : `<span class="dica">${esc(social.erro || 'criando…')}</span>`}</div>
      <div class="ajuste"><span class="nome">Conta</span><span class="cresce" style="font-size:13px">${esc(quem)}</span></div>
      <div class="ajuste"><span class="nome">Moedas</span><span class="cresce amarelo" style="font-weight:800">${fmt(conta.moedas)}</span></div>
      <div class="ajuste"><span class="nome">Nível</span>${insignia(nivelDe(conta.xp).patente, 26)}<span class="cresce" style="font-size:13px"><b>${nivelDe(conta.xp).nivel}</b> · ${esc(nivelDe(conta.xp).patente.nome)} ${nivelDe(conta.xp).divisao} · ${fmt(conta.xp)} XP no total</span></div>
      <div class="ajuste" style="flex-wrap:wrap;gap:8px"><span class="nome">Patentes</span>${PATENTES.map((p) => `<span class="linha" style="gap:4px;font-size:11px;color:#C3C8D2">${insignia(p, 20)}${esc(p.nome)} <span class="apagado">(nv ${p.de})</span></span>`).join('')}</div>
      <button class="btn btn-fraco" id="c-sair" style="align-self:flex-start">Sair da conta</button>`;
    $('#c-salvar-nome', c).onclick = async () => {
      const n = $('#c-nome', c).value.trim().slice(0, 16);
      if (!n) return;
      if (!fb || !fb.token) { guardado.set('nome', n); aviso('Nome salvo'); return; }
      const erro = await trocarNick(n);
      if (erro) janela('Nick', erro);
    };
    const cp = $('#c-copiar-id', c);
    if (cp) cp.onclick = () => copiarTexto(social.id);
    $('#c-sair', c).onclick = async () => {
      if (!(await confirmar('Sair da conta?', 'Você volta para a tela de login.', 'Sair'))) return;
      sairDaSala();
      fb = null;
      guardado.del('login_refresh');
      telaLogin();
    };
  }
}

// ---------------- multiplayer ----------------
let sala = null;
// O servidor da partida ao vivo (e do chat de voz) dorme quando ninguém usa e leva até 1 minuto para acordar; se
// a partida começar com ele dormindo, ela vai pelo Firebase, sem voz. Então ele é acordado já no Multiplayer e
// mantido acordado enquanto a pessoa está na sala.
let ultimoToqueServidor = 0;
function acordarServidor() {
  if (Date.now() - ultimoToqueServidor < 60000) return;
  ultimoToqueServidor = Date.now();
  try { fetch(SERVIDOR + '/saude', { cache: 'no-store', mode: 'no-cors' }).catch(() => {}); } catch (_) {}
}
setInterval(() => { const t = telaAtual(); if (t === 'multiplayer' || t === 'sala') acordarServidor(); }, 120000);

function telaMultiplayer() {
  acordarServidor();
  motor.girarMenu(true);
  const t = mostrar(html(`<div class="tela">
    <div class="linha"><span class="dica" style="font-weight:700">Seu nome:</span><input type="text" id="mp-nome" maxlength="16" style="max-width:260px"></div>
    <div class="linha cresce" style="align-items:stretch;gap:14px">
      <div class="cartao coluna cresce" style="border-color:rgba(251,191,36,.35)"><span class="bebas ouro" style="font-size:32px">CRIAR SALA</span>
        <span class="dica">Gera um código. Mande para os amigos: até 10 pessoas na mesma sala, jogador contra jogador, com chat de voz do time.</span>
        <span class="cresce"></span><button class="btn btn-ouro" id="mp-criar">CRIAR</button></div>
      <div class="cartao coluna cresce" style="border-color:rgba(96,165,250,.35)"><span class="bebas azul" style="font-size:32px">ENTRAR COM CÓDIGO</span>
        <input type="text" id="mp-codigo" maxlength="6" placeholder="CÓDIGO" style="font-size:22px;text-transform:uppercase;letter-spacing:.2em">
        <span class="cresce"></span><button class="btn btn-azul" id="mp-entrar">ENTRAR</button></div>
    </div><div class="status" id="mp-status"></div></div>`), 'multiplayer');
  t.prepend(topo('MULTIPLAYER', telaInicio));
  $('#mp-nome', t).value = conta.nome();
  const st = $('#mp-status', t);
  const conectar = async (acao) => {
    const nm = $('#mp-nome', t).value.trim().slice(0, 16) || 'Jogador';
    if (nm !== conta.nome() && fb && fb.token && social.pronto) { const erro = await trocarNick(nm, true); if (erro) { st.textContent = erro; return; } }
    else if (nm !== conta.nome()) { guardado.set('nome', nm); salvarConta(); }
    if (!fb || !fb.token) { st.textContent = 'Entre na sua conta (ou como convidado) para jogar online'; return; }
    st.textContent = 'Conectando…';
    try {
      const [s, erro] = await acao();
      if (!s) { st.textContent = erro; return; }
      sala = s;
      ligarSala(s);
      telaSala();
    } catch (e) { st.textContent = 'sem conexão: ' + e.message; }
  };
  $('#mp-criar', t).onclick = () => conectar(() => Sala.criar(minhaEntrada(guardado.get('lado', 0))));
  $('#mp-entrar', t).onclick = () => {
    const c = $('#mp-codigo', t).value.trim().toUpperCase();
    if (c.length !== 6) { st.textContent = 'O código tem 6 letras/números'; return; }
    conectar(() => Sala.entrar(c, minhaEntrada(guardado.get('lado', 0))));
  };
}

// ---------------- amigos ----------------
function copiarTexto(t) {
  try { navigator.clipboard.writeText(String(t)).then(() => aviso('Copiado: ' + t)).catch(() => aviso(String(t))); } catch (_) { aviso(String(t)); }
}
async function telaAmigos(aba = 'amigos') {
  motor.girarMenu(true);
  const npedidos = Object.keys(social.pedidos).length;
  const t = mostrar(html(`<div class="tela" style="gap:10px">
    <div class="cartao linha" style="gap:14px;padding:10px 16px">
      <div class="coluna" style="gap:0"><span class="dica" style="font-size:11px;font-weight:800;letter-spacing:2px">SEU ID</span>
        <span class="bebas ouro" style="font-size:30px;letter-spacing:3px">${social.id ? idBonito(social.id) : '— — —'}</span></div>
      ${social.id ? '<button class="btn btn-fraco" id="a-copiar">Copiar</button>' : ''}
      <span class="dica cresce">${social.pronto ? `Nick: <b>${esc(conta.nome())}</b> · passe o ID para os amigos te acharem (e use ele no site para comprar moedas)` : esc(social.erro || 'Criando o seu ID…')}</span></div>
    <div class="cartao coluna cresce" id="a-conteudo" style="overflow:auto;gap:6px"><span class="dica">Carregando…</span></div></div>`), 'amigos');
  const nl = naoLidas(), np = presentesPendentes();
  const abas = `<div class="abas">${[['amigos', 'Amigos'], ['conversas', `Conversas${nl ? ' (' + nl + ')' : ''}`], ['presentes', `Presentes${np ? ' (' + np + ')' : ''}`],
    ['pedidos', `Pedidos${npedidos ? ' (' + npedidos + ')' : ''}`], ['adicionar', 'Adicionar']]
    .map(([k, n]) => `<button class="aba ${k === aba ? 'sel' : ''}" data-aba="${k}">${n}</button>`).join('')}</div>`;
  t.prepend(topo('AMIGOS', telaInicio, abas, ''));
  for (const b of $$('[data-aba]', t)) b.onclick = () => telaAmigos(b.dataset.aba);
  const cp = $('#a-copiar', t);
  if (cp) cp.onclick = () => copiarTexto(social.id);
  social.aoMudar = () => { if (telaAtual() === 'amigos' && ['pedidos', 'conversas', 'presentes'].includes(aba) && !palco.querySelector('.janela-chat')) telaAmigos(aba); };
  const c = $('#a-conteudo', t);
  if (!fb || !fb.token) { c.innerHTML = '<span class="dica">Entre na sua conta para ter amigos</span>'; return; }
  if (!social.pronto) { c.innerHTML = `<span class="dica">${esc(social.erro || 'Criando o seu ID…')}</span>`; return; }
  const linha = (p, botoes) => `<div class="linha amigo" data-uid="${esc(p.uid)}">
    <span class="ponto ${estaOnline(p) ? 'on' : ''}"></span>
    <div class="coluna cresce" style="gap:0"><b>${esc(p.nome || 'Jogador')}</b>
      <span class="dica" style="font-size:11px">ID ${idBonito(p.id)} · NV ${p.nivel || 1} · ${estaOnline(p) ? (p.jogando ? 'jogando ' + esc(p.jogando) : 'online') : 'offline'}</span></div>${botoes}</div>`;
  try {
    if (aba === 'amigos') {
      const lista = await meusAmigos();
      c.innerHTML = lista.length ? lista.map((p) => linha(p, `${estaOnline(p) && !p.jogando ? '<button class="btn btn-ouro" data-convidar>Convidar</button>' : ''}
        <button class="btn btn-azul" data-conversar>Conversar${chat.caixa[p.uid] && chat.caixa[p.uid].nao ? ` (${chat.caixa[p.uid].nao})` : ''}</button>
        <button class="btn btn-fraco" data-presente>🎁 Presente</button>
        <button class="btn btn-fraco" data-remover>Remover</button>`)).join('') : '<span class="dica">Nenhum amigo ainda. Vá em Adicionar e procure pelo ID ou pelo nick.</span>';
      const porUid = (b) => lista.find((p) => p.uid === b.closest('[data-uid]').dataset.uid);
      for (const b of $$('[data-conversar]', c)) b.onclick = () => telaConversa(porUid(b));
      for (const b of $$('[data-presente]', c)) b.onclick = () => escolherPresente(porUid(b));
      for (const b of $$('[data-convidar]', c)) b.onclick = async () => {
        const erro = await convidarAmigo(b.closest('[data-uid]').dataset.uid);
        aviso(erro || 'Convite enviado! Ele vai entrar no seu grupo da Ressurgência');
      };
      for (const b of $$('[data-remover]', c)) b.onclick = async () => {
        if (!(await confirmar('Desfazer amizade?', '', 'Remover'))) return;
        await desfazerAmizade(b.closest('[data-uid]').dataset.uid);
        telaAmigos('amigos');
      };
    } else if (aba === 'conversas') {
      const conv = Object.entries(chat.caixa).sort((a, b) => (b[1].em || 0) - (a[1].em || 0));
      const perfis = await Promise.all(conv.map(([uid]) => perfil(uid)));
      c.innerHTML = conv.length ? conv.map(([uid, cv], i) => {
        const p = perfis[i] || { uid, nome: cv.nome };
        return linha({ ...p, uid }, `<span class="dica cresce" style="max-width:240px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${esc(cv.txt || '')}</span>
          ${cv.nao ? `<b class="badge-msg">${cv.nao}</b>` : ''}<button class="btn btn-azul" data-abrir-conversa>Abrir</button>`);
      }).join('') : '<span class="dica">Nenhuma conversa ainda. Em Amigos, toque em Conversar.</span>';
      for (const b of $$('[data-abrir-conversa]', c)) b.onclick = () => {
        const uid = b.closest('[data-uid]').dataset.uid, i = conv.findIndex(([u]) => u === uid);
        telaConversa({ ...(perfis[i] || {}), uid, nome: (perfis[i] && perfis[i].nome) || chat.caixa[uid].nome });
      };
    } else if (aba === 'presentes') {
      c.innerHTML = await listaPresentesHtml();
      for (const b of $$('[data-abrir]', c)) b.onclick = async () => { await abrirPresente(b.dataset.abrir); telaAmigos('presentes'); };
    } else if (aba === 'pedidos') {
      const ps = Object.entries(social.pedidos).map(([uid, p]) => ({ uid, ...p }));
      c.innerHTML = ps.length ? ps.map((p) => linha(p, '<button class="btn btn-ouro" data-aceitar>Aceitar</button><button class="btn btn-fraco" data-recusar>Recusar</button>')).join('')
        : '<span class="dica">Nenhum pedido de amizade agora.</span>';
      for (const b of $$('[data-aceitar]', c)) b.onclick = async () => { await aceitarPedido(b.closest('[data-uid]').dataset.uid); aviso('Agora vocês são amigos!'); telaAmigos('pedidos'); };
      for (const b of $$('[data-recusar]', c)) b.onclick = async () => { await recusarPedido(b.closest('[data-uid]').dataset.uid); telaAmigos('pedidos'); };
    } else {
      c.innerHTML = `<div class="linha" style="gap:8px"><input type="text" id="a-busca" maxlength="16" placeholder="ID (9 números) ou nick" class="cresce">
        <button class="btn btn-ouro" id="a-buscar">Procurar</button></div><div class="coluna" id="a-achados" style="gap:6px"></div>`;
      const buscar = async () => {
        const res = $('#a-achados', c);
        res.innerHTML = '<span class="dica">Procurando…</span>';
        try {
          const ps = (await buscarJogadores($('#a-busca', c).value)).filter((p) => p.uid !== fb.uid);
          res.innerHTML = ps.length ? ps.map((p) => linha(p, '<button class="btn btn-ouro" data-pedir>Adicionar</button>')).join('') : '<span class="dica">Ninguém com esse ID ou nick.</span>';
          for (const b of $$('[data-pedir]', res)) b.onclick = async () => {
            const erro = await enviarPedido(b.closest('[data-uid]').dataset.uid);
            if (erro) aviso(erro); else { b.textContent = 'Pedido enviado'; b.disabled = true; }
          };
        } catch (e) { res.innerHTML = `<span class="dica">${esc(e.message)}</span>`; }
      };
      $('#a-buscar', c).onclick = buscar;
      $('#a-busca', c).addEventListener('keydown', (e) => { if (e.key === 'Enter') buscar(); });
    }
  } catch (e) { c.innerHTML = `<span class="dica">Sem conexão: ${esc(e.message)}</span>`; }
}
// escolher um amigo online para chamar para o grupo
async function escolherAmigoParaConvidar() {
  if (!social.pronto) { aviso(social.erro || 'Criando o seu ID…'); return; }
  const online = (await meusAmigos()).filter((p) => estaOnline(p) && !p.jogando);
  if (!online.length) { janela('Convidar', 'Nenhum amigo online agora. Adicione amigos na tela Amigos (pelo ID ou nick).'); return; }
  const el = html(`<div class="camada fixo"><div class="janela" style="min-width:320px">
    <span class="t">Convidar para o grupo</span>
    ${online.map((p) => `<button class="btn btn-fraco" data-uid="${esc(p.uid)}" style="justify-content:space-between;display:flex">${esc(p.nome)} <span class="dica">NV ${p.nivel || 1}</span></button>`).join('')}
    <button class="btn btn-escuro" id="cv-fechar">Fechar</button></div></div>`);
  palco.appendChild(el);
  $('#cv-fechar', el).onclick = () => el.remove();
  for (const b of $$('[data-uid]', el)) b.onclick = async () => {
    const erro = await convidarAmigo(b.dataset.uid);
    aviso(erro || 'Convite enviado!');
    el.remove();
    telaRessurgencia();
  };
}

// ---------------- Ressurgência online: partida rápida, sala com amigos ou código ----------------
function telaRessurgencia() {
  acordarServidor();
  motor.girarMenu(true);
  const lado = guardado.get('lado', 0);
  const g = social.grupo;
  const formato = g ? g.formato : guardado.get('formato_br', 'squad');
  const tam = FORMATOS[formato].tam;
  const souLider = !g || g.lider === fb.uid;
  const membros = g ? Object.entries(g.membros || {}) : [[fb ? fb.uid : 'eu', { nome: conta.nome() }]];
  const vagas = Array.from({ length: tam }, (_, i) => membros[i]);
  const t = mostrar(html(`<div class="tela">
    <div class="linha" style="gap:8px;flex-wrap:wrap"><span class="dica" style="font-weight:800;letter-spacing:1px">FORMATO</span>
      ${Object.entries(FORMATOS).map(([k, f]) => `<button class="aba ${k === formato ? 'sel' : ''}" data-formato="${k}" ${souLider ? '' : 'disabled'}>${f.nome}<span class="dica" style="font-size:10px"> · ${f.tam === 1 ? 'sozinho' : f.tam + ' por time'}</span></button>`).join('')}
      <span class="cresce"></span><span class="dica">${g && !souLider ? 'o líder escolhe o formato e procura a partida' : ''}</span></div>
    <div class="linha cresce" style="align-items:stretch;gap:14px">
      <div class="cartao coluna cresce" style="border-color:rgba(248,113,113,.5);background:linear-gradient(160deg,#3B0D0D,#1A0B0B)">
        <span class="bebas" style="font-size:34px;color:#FCA5A5">PARTIDA RÁPIDA · ${FORMATOS[formato].nome.toUpperCase()}</span>
        <div class="grupo-vagas">${vagas.map((m, i) => m
          ? `<div class="vaga cheia"><b>${esc(m[1].nome || 'Jogador')}</b><span class="dica">${i === 0 || m[1].lider ? 'líder' : 'no grupo'}</span></div>`
          : `<button class="vaga" data-convidar ${souLider ? '' : 'disabled'}>+ convidar amigo</button>`).join('')}</div>
        <span class="cresce"></span>
        ${souLider ? '<button class="btn" id="br-rapida" style="background:#DC2626;color:#fff">PROCURAR PARTIDA</button>' : '<span class="dica" style="color:#FEE2E2">Esperando o líder procurar a partida…</span>'}
        ${g ? '<button class="btn btn-fraco" id="br-sair-grupo" style="margin-top:6px">Sair do grupo</button>' : ''}</div>
      <div class="cartao coluna cresce" style="border-color:rgba(251,191,36,.35)"><span class="bebas ouro" style="font-size:34px">SALA COM AMIGOS</span>
        <span class="dica">Cria um código: até 40 pessoas. Quem entra junto cai no mesmo time. Você começa quando quiser.</span>
        <span class="cresce"></span><button class="btn btn-ouro" id="br-criar">CRIAR SALA</button></div>
      <div class="cartao coluna cresce" style="border-color:rgba(96,165,250,.35)"><span class="bebas azul" style="font-size:34px">ENTRAR COM CÓDIGO</span>
        <input type="text" id="br-codigo" maxlength="6" placeholder="CÓDIGO" style="font-size:22px;text-transform:uppercase;letter-spacing:.2em">
        <span class="cresce"></span><button class="btn btn-azul" id="br-entrar">ENTRAR</button></div>
    </div><div class="status" id="br-status"></div></div>`), 'ressurgencia');
  t.prepend(topo('RESSURGÊNCIA ONLINE', () => telaRoupa(lado, null, 1), '', '<span class="dica">só gente de verdade · voz do time</span>'));
  const st = $('#br-status', t);
  social.aoMudar = () => { if (telaAtual() === 'ressurgencia') telaRessurgencia(); };
  for (const b of $$('[data-formato]', t)) b.onclick = async () => { await mudarFormatoGrupo(b.dataset.formato); telaRessurgencia(); };
  for (const b of $$('[data-convidar]', t)) b.onclick = () => escolherAmigoParaConvidar();
  const sg = $('#br-sair-grupo', t);
  if (sg) sg.onclick = async () => { await sairDoGrupo(); telaRessurgencia(); };
  const conectar = async (acao) => {
    if (!fb || !fb.token) { st.textContent = 'Entre na sua conta (ou como convidado) para jogar online'; return; }
    st.textContent = 'Conectando…';
    try {
      const [s, erro] = await acao();
      if (!s) { st.textContent = erro; return; }
      if (s.modo !== 'br') { s.sair().catch(() => {}); st.textContent = 'Esse código é de uma sala de Polícia x Bandidos (entre pelo Multiplayer)'; return; }
      sala = s;
      ligarSala(s);
      telaSala();
    } catch (e) { st.textContent = 'sem conexão: ' + e.message; }
  };
  const eu = () => ({ ...minhaEntrada(guardado.get('lado', 0)), grupo: social.grupo ? social.grupo.lider : '' });
  const rapida = $('#br-rapida', t);
  // o líder procura para o grupo todo: a sala precisa ter vaga para todos; depois avisa o grupo onde entrar
  if (rapida) rapida.onclick = () => conectar(async () => {
    const n = social.grupo ? Object.keys(social.grupo.membros || {}).length : 1;
    const [s, erro] = await Sala.partidaRapida(eu(), formato, n);
    if (s && social.grupo && social.grupo.lider === fb.uid) await fb.patch('grupos/' + fb.uid, { sala: s.codigo });
    return [s, erro];
  });
  $('#br-criar', t).onclick = () => conectar(() => Sala.criar(eu(), 'br', false, formato));
  $('#br-entrar', t).onclick = () => {
    const c = $('#br-codigo', t).value.trim().toUpperCase();
    if (c.length !== 6) { st.textContent = 'O código tem 6 letras/números'; return; }
    conectar(() => Sala.entrar(c, eu()));
  };
}

// Sala de espera da Ressurgência: quem já entrou, contagem da sala pública e o botão de começar de quem criou
let contagemBR = null;
function telaSalaBR() {
  acordarServidor();
  const s = sala;
  if (!s) { telaInicio(); return; }
  motor.girarMenu(true);
  const lista = s.lista().sort((a, b) => (Number(a.entrou) || 0) - (Number(b.entrou) || 0));
  const n = lista.length;
  const t = mostrar(html(`<div class="tela">
    <div class="topo"><button class="voltar" id="s-sair" aria-label="Sair da sala">${ICONE.voltar}</button>
      <div class="coluna" style="gap:0"><span class="dica" style="font-size:11px;font-weight:800;letter-spacing:2px">${s.publica ? 'PARTIDA RÁPIDA · RESSURGÊNCIA' : 'SALA DA RESSURGÊNCIA · CÓDIGO'}</span>
        <span class="bebas" style="font-size:40px;letter-spacing:${s.publica ? 1 : 6}px;color:#FCA5A5">${s.publica ? 'PROCURANDO JOGADORES' : s.codigo}</span></div>
      <span class="cresce"></span><span class="etiqueta" style="background:#7F1D1D;color:#FECACA">${(FORMATOS[s.formato] || FORMATOS.squad).nome.toUpperCase()}</span>
      <span class="bebas" style="font-size:44px;color:#F9FAFB">${n}<span class="dica" style="font-size:20px">/${s.max}</span></span></div>
    <div class="cartao cresce" style="display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px 14px;align-content:start;overflow:auto">
      ${lista.map((p) => `<span style="font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;${p.id === fb.uid ? 'font-weight:800;color:#86EFAC' : ''}">${p.id === fb.uid ? '▶ ' : ''}${esc(p.nome || 'Jogador')}</span>`).join('')}</div>
    <div class="linha"><span class="dica cresce" id="s-info"></span>
      ${s.host && !s.publica ? `<button class="btn btn-grande" id="s-iniciar" style="height:48px;font-size:24px;background:#DC2626;color:#fff" ${n < 2 ? 'disabled' : ''}>INICIAR ${ICONE.play}</button>` : ''}</div></div>`), 'salabr');
  $('#s-sair', t).onclick = async () => {
    if (await confirmar('Sair da sala?', s.host && !s.publica ? 'Você criou a sala: ela fecha para todos.' : '', 'Sair')) {
      sairDaSala();
      if (social.grupo && social.grupo.lider === fb.uid) fb.patch('grupos/' + fb.uid, { sala: '' });
      telaRessurgencia();
    }
  };
  if (s.host && !s.publica) $('#s-iniciar', t).onclick = async () => { if (!(await s.comecarBR())) aviso('Precisa de pelo menos 2 pessoas (e conexão)'); };
  // contagem: a sala pública começa 60 s depois de ter 2 pessoas (ou na hora, com 40)
  clearInterval(contagemBR);
  const info = $('#s-info', t);
  const tique = async () => {
    if (sala !== s || telaAtual() !== 'salabr') { clearInterval(contagemBR); return; }
    const m = s.lista().length;
    if (!s.publica) { info.textContent = m < 2 ? 'Mande o código para os amigos · precisa de pelo menos 2 pessoas' : s.host ? 'Quando todo mundo entrar, aperte INICIAR' : 'Esperando quem criou a sala iniciar…'; return; }
    if (s.host) {
      if (m >= 2 && !s.comecaEm) { s.comecaEm = Date.now() + 60000; fb.patch('salas/' + s.codigo, { comeca_em: s.comecaEm }); }
      if (m < 2 && s.comecaEm) { s.comecaEm = 0; fb.patch('salas/' + s.codigo, { comeca_em: 0 }); }
    }
    // quem criou começa na hora; se o celular dele sumiu (app fechado, sem internet), qualquer um da sala começa 10 s
    // depois (a gravação condicional em comecarBR garante que só um começa)
    const atrasado = !s.host && s.comecaEm && Date.now() >= s.comecaEm + 10000;
    if (s.estado === 'espera' && (s.host || atrasado) && (m >= s.max || (s.comecaEm && Date.now() >= s.comecaEm))) {
      s.estado = 'comecando';
      if (!(await s.comecarBR())) s.estado = 'espera';
    }
    const falta = s.comecaEm ? Math.max(0, Math.ceil((s.comecaEm - Date.now()) / 1000)) : 0;
    info.textContent = m < 2 ? 'Esperando mais gente entrar (precisa de pelo menos 2)…' : `A partida começa em ${falta} s · ou na hora com ${s.max} pessoas`;
  };
  tique();
  contagemBR = setInterval(tique, 1000);
}

function ligarSala(s) {
  setTimeout(() => motor.prepararPartida(), 300);  // já vai carregando o mapa enquanto espera na sala
  s.ouvir(
    () => { if (telaAtual() === 'sala') telaSala(); else if (telaAtual() === 'salabr') telaSalaBR(); },
    (estado) => { if (sala === s && estado === 'jogo' && !partida) comecarOnline(s); },
    () => {
      if (sala !== s) return;
      if (partida) return;  // jogando: a partida continua (o ao vivo é pelo servidor; a sala só serve para a espera)
      sala = null; aviso('A sala fechou (quem criou saiu)'); telaInicio();
    },
    (id) => motor.redeEvento(JSON.stringify({ t: 'saiu', id })));
}
function sairDaSala() { const s = sala; sala = null; if (s) s.sair().catch(() => {}); }

function telaSala() {
  acordarServidor();
  const s = sala;
  if (!s) { telaInicio(); return; }
  if (s.modo === 'br') { telaSalaBR(); return; }
  motor.girarMenu(true);
  const lista = s.lista();
  const eu = lista.find((p) => p.id === fb.uid);
  const meuTime = eu ? eu.time | 0 : guardado.get('lado', 0);
  const coluna = (time) => {
    const doTime = lista.filter((p) => (p.time | 0) === time);
    return `<div class="cartao coluna cresce" style="background:${time ? '#2A1C08' : '#0F1D36'};border-color:${time ? '#F59E0B' : '#3B82F6'}">
      <span style="font-size:12px;font-weight:800;letter-spacing:1px;color:${time ? '#FCD34D' : '#93C5FD'}">${time ? 'BANDIDOS' : 'POLÍCIA'} · ${doTime.length}</span>
      ${doTime.map((p) => `<span style="font-size:14px;${p.id === fb.uid ? 'font-weight:800' : ''}">${p.id === fb.uid ? '▶ ' : ''}${esc(p.nome || 'Jogador')}${p.id === fb.uid ? ' (você)' : ''}</span>`).join('') || '<span class="dica">ninguém ainda</span>'}</div>`;
  };
  const t = mostrar(html(`<div class="tela">
    <div class="topo"><button class="voltar" id="s-sair" aria-label="Sair da sala">${ICONE.voltar}</button>
      <div class="coluna" style="gap:0"><span class="dica" style="font-size:11px;font-weight:800;letter-spacing:2px">CÓDIGO DA SALA</span><span class="bebas ouro" style="font-size:40px;letter-spacing:6px">${s.codigo}</span></div>
      <span class="dica cresce">Mande o código para os amigos · ${lista.length}/10 na sala</span></div>
    <div class="linha cresce" style="align-items:stretch;gap:12px">${coluna(0)}${coluna(1)}</div>
    <div class="linha"><button class="btn btn-fraco" id="s-time">${meuTime ? 'Ir para a Polícia' : 'Ir para os Bandidos'}</button><button class="btn btn-fraco" id="s-roupa">Roupa</button><span class="cresce"></span>
      ${s.host ? `<button class="btn btn-ouro btn-grande" id="s-iniciar" style="height:48px;font-size:24px">INICIAR ${ICONE.play}</button>` : '<span class="dica">Esperando quem criou a sala iniciar…</span>'}</div></div>`), 'sala');
  $('#s-sair', t).onclick = async () => {
    if (await confirmar('Sair da sala?', s.host ? 'Você criou a sala: ela fecha para todos.' : '', 'Sair')) { sairDaSala(); telaInicio(); }
  };
  $('#s-time', t).onclick = () => { const outro = 1 - meuTime; guardado.set('lado', outro); s.atualizar(minhaEntrada(outro)); };
  $('#s-roupa', t).onclick = () => telaRoupa(meuTime, () => { s.atualizar(minhaEntrada(meuTime)); telaSala(); });
  if (s.host) $('#s-iniciar', t).onclick = async () => { if (!(await s.comecar())) aviso('Não consegui iniciar (sem conexão?)'); };
}

async function comecarOnline(s) {
  if (s.modo === 'br') {  // Ressurgência: times e semente que quem criou a sala sorteou
    clearInterval(contagemBR);
    const eu = s.lista().find((p) => p.id === fb.uid) || minhaEntrada(guardado.get('lado', 0));
    const lado = eu.time | 0;
    const roupa = ROUPAS.find((r) => r.time === lado && r.i === (eu.skin | 0)) || conta.roupa(lado);
    s.rosterBRpronto = await s.rosterBR();
    iniciarPartida(lado, roupa, s, eu.arma || armaInicial(lado), 1);
    return;
  }
  const eu = s.lista().find((p) => p.id === fb.uid) || minhaEntrada(guardado.get('lado', 0));
  const time = eu.time | 0;
  const roupa = ROUPAS.find((r) => r.time === time && r.i === (eu.skin | 0)) || conta.roupa(time);
  iniciarPartida(time, roupa, s, eu.arma || armaInicial(time));
}

// ---------------- fim de partida ----------------
// Ressurgência: em que lugar o time ficou e os abates do time
function fimRessurgencia(j) {
  const br = j.br || {};
  const lugar = br.colocacao | 0 || 10;
  const online = !!(partida && partida.sala);
  pararPartida();
  if (online) sairDaSala();
  const xpAbates = (j.abates | 0) * XP_ABATE, xpResultado = lugar === 1 ? XP_VITORIA : lugar <= 3 ? XP_EMPATE : XP_DERROTA;
  const xp = conta.ganharXp(xpAbates + xpResultado);
  const subiu = xp.depois.nivel > xp.antes.nivel;
  const pctXp = xp.depois.precisa ? Math.round(100 * xp.depois.noNivel / xp.depois.precisa) : 100;
  const meus = (j.jogadores || []).filter((p) => p.time === j.time).sort((a, b) => b.abates - a.abates);
  const cor = lugar === 1 ? '#FCD34D' : lugar <= 3 ? '#86EFAC' : '#FCA5A5';
  const t = mostrar(html(`<div class="tela" style="padding:18px 26px">
    <div class="linha" style="align-items:flex-start">
      <div class="coluna cresce" style="gap:6px"><span style="font-size:12px;font-weight:800;letter-spacing:2px;color:${cor}">${lugar === 1 ? 'RESSURGÊNCIA · VITÓRIA' : 'RESSURGÊNCIA · SEU TIME FICOU EM'}</span>
        <span class="bebas" style="font-size:52px;line-height:.92;color:${cor}">${lugar === 1 ? 'ÚLTIMO TIME DE PÉ' : lugar + 'º LUGAR'}</span>
        <span class="dica">de ${br.totalTimes || 10} times · time ${br.meuTime || 1}</span></div>
      <div class="cartao coluna" style="padding:8px 18px;gap:0;align-items:center"><span class="bebas ouro" style="font-size:46px;line-height:1">#${lugar}</span><span class="dica" style="font-size:11px">COLOCAÇÃO</span></div>
    </div>
    <div class="cartao linha fim-xp">
      <span class="insignia">${insignia(xp.depois.patente, 34)}</span>
      <div class="coluna cresce" style="gap:4px">
        <div class="linha" style="gap:8px;flex-wrap:wrap"><b>${subiu ? 'SUBIU PARA O NÍVEL ' + xp.depois.nivel : 'NÍVEL ' + xp.depois.nivel}</b>
          <span class="apagado">${esc(xp.depois.patente.nome)}${xp.depois.divisao ? ' ' + xp.depois.divisao : ''}</span>
          ${xp.depois.patente !== xp.antes.patente ? '<span class="etiqueta fundo-lendaria">PATENTE NOVA</span>' : ''}</div>
        <span class="barra-xp"><i style="width:${pctXp}%"></i></span>
      </div>
      <div class="coluna" style="gap:2px;align-items:flex-end;font-size:12px">
        <b class="ouro" style="font-size:15px">+${fmt(xpAbates + xpResultado)} XP</b>
        <span class="apagado">${j.abates | 0} abates: +${fmt(xpAbates)} · ${lugar}º lugar: +${fmt(xpResultado)}</span>
      </div>
    </div>
    <div class="cartao coluna cresce" style="gap:2px;padding:12px;overflow:auto">
      <div class="linha" style="font-size:11px;font-weight:800;letter-spacing:1px;padding:0 6px;color:#86EFAC"><span class="cresce">SEU TIME</span>
        <span style="width:58px;text-align:right">ABATES</span><span style="width:58px;text-align:right">QUEDAS</span></div>
      ${meus.map((p) => `<div class="linha" style="font-size:13px;padding:6px;border-radius:8px;${p.voce ? 'font-weight:800;background:rgba(255,255,255,.12)' : ''}">
        <span class="cresce">${p.voce ? 'Você' : esc(p.nome)}</span><b style="width:58px;text-align:right">${p.abates}</b><span class="apagado" style="width:58px;text-align:right">${p.mortes}</span></div>`).join('')}</div>
    <div class="linha" style="justify-content:flex-end"><button class="btn btn-escuro" id="f-menu" style="height:48px;font-size:14px">Menu inicial</button>
      <button class="btn btn-ouro" id="f-novo" style="height:48px;font-size:15px;letter-spacing:1px;padding:0 22px">JOGAR DE NOVO</button></div>
  </div>`), 'fim');
  $('#f-menu', t).onclick = telaInicio;
  $('#f-novo', t).onclick = () => telaRessurgencia();
}

function fimDePartida(j) {
  if (j.modo === 1) { fimRessurgencia(j); return; }
  const sc = j.placar || [0, 0];
  const venceu = sc[0] > sc[1] ? 0 : sc[1] > sc[0] ? 1 : -1;
  const time = partida ? partida.time : 0;
  const s = partida && partida.sala;
  pararPartida();
  const xpAbates = (j.abates | 0) * XP_ABATE, xpResultado = venceu === time ? XP_VITORIA : venceu < 0 ? XP_EMPATE : XP_DERROTA;
  const xp = conta.ganharXp(xpAbates + xpResultado);
  const subiu = xp.depois.nivel > xp.antes.nivel;
  const pctXp = xp.depois.precisa ? Math.round(100 * xp.depois.noNivel / xp.depois.precisa) : 100;
  const tabela = (tm) => {
    const l = (j.jogadores || []).filter((p) => p.time === tm).sort((a, b) => b.abates - a.abates);
    return `<div class="cartao coluna cresce" style="background:${tm ? '#2A1C08' : '#0F1D36'};gap:2px;padding:12px 12px">
      <div class="linha" style="font-size:11px;font-weight:800;letter-spacing:1px;padding:0 6px"><span class="cresce" style="color:${tm ? '#FCD34D' : '#93C5FD'}">${tm ? 'BANDIDOS' : 'POLÍCIA'}</span>
        <span style="width:58px;text-align:right;color:${tm ? '#FCD34D' : '#93C5FD'}">ABATES</span><span style="width:58px;text-align:right;color:${tm ? '#FCD34D' : '#93C5FD'}">MORTES</span></div>
      ${l.map((p) => `<div class="linha" style="font-size:13px;padding:6px;border-radius:8px;${p.voce ? 'font-weight:800;background:rgba(255,255,255,.12)' : ''}">
        <span class="cresce">${p.voce ? 'Você' : esc(p.nome)}</span><b style="width:58px;text-align:right">${p.abates}</b><span class="apagado" style="width:58px;text-align:right">${p.mortes}</span></div>`).join('')}</div>`;
  };
  const corV = venceu === 1 ? '#FCD34D' : '#60A5FA';
  const t = mostrar(html(`<div class="tela" style="padding:18px 26px">
    <div class="linha" style="align-items:flex-start">
      <div class="coluna cresce" style="gap:6px"><span style="font-size:12px;font-weight:800;letter-spacing:2px;color:${corV}">${venceu < 0 ? 'FIM DE JOGO' : 'VITÓRIA DA'}</span>
        <span class="bebas" style="font-size:46px;line-height:.92;color:${corV}">${venceu < 0 ? 'EMPATE' : venceu === 0 ? 'POLÍCIA' : 'BANDIDOS'}</span></div>
      <div class="coluna" style="align-items:flex-end;gap:4px"><div class="cartao linha" style="padding:8px 18px;gap:14px"><span class="bebas" style="font-size:46px;color:#60A5FA">${sc[0]}</span><span class="dica">x</span><span class="bebas ouro" style="font-size:46px">${sc[1]}</span></div></div>
    </div>
    <div class="cartao linha fim-xp">
      <span class="insignia">${insignia(xp.depois.patente, 34)}</span>
      <div class="coluna cresce" style="gap:4px">
        <div class="linha" style="gap:8px;flex-wrap:wrap"><b>${subiu ? 'SUBIU PARA O NÍVEL ' + xp.depois.nivel : 'NÍVEL ' + xp.depois.nivel}</b>
          <span class="apagado">${esc(xp.depois.patente.nome)}${xp.depois.divisao ? ' ' + xp.depois.divisao : ''}</span>
          ${xp.depois.patente !== xp.antes.patente ? '<span class="etiqueta fundo-lendaria">PATENTE NOVA</span>' : ''}</div>
        <span class="barra-xp"><i style="width:${pctXp}%"></i></span>
      </div>
      <div class="coluna" style="gap:2px;align-items:flex-end;font-size:12px">
        <b class="ouro" style="font-size:15px">+${fmt(xpAbates + xpResultado)} XP</b>
        <span class="apagado">${j.abates | 0} abates: +${fmt(xpAbates)} · ${venceu === time ? 'vitória' : venceu < 0 ? 'empate' : 'derrota'}: +${fmt(xpResultado)}</span>
      </div>
    </div>
    <div class="linha cresce" style="align-items:flex-start;gap:12px;overflow:auto">${tabela(0)}${tabela(1)}</div>
    <div class="linha" style="justify-content:flex-end">${s
      ? '<button class="btn btn-escuro" id="f-sair" style="height:48px">Sair da sala</button><button class="btn btn-ouro" id="f-volta" style="height:48px;font-size:15px;letter-spacing:1px">VOLTAR À SALA</button>'
      : '<button class="btn btn-escuro" id="f-menu" style="height:48px;font-size:14px">Menu inicial</button><button class="btn btn-ouro" id="f-novo" style="height:48px;font-size:15px;letter-spacing:1px;padding:0 22px">JOGAR DE NOVO</button>'}</div>
  </div>`), 'fim');
  if (s) { $('#f-sair', t).onclick = () => { sairDaSala(); telaInicio(); }; $('#f-volta', t).onclick = () => { s.voltar(); telaSala(); }; }
  else { $('#f-menu', t).onclick = telaInicio; $('#f-novo', t).onclick = () => telaRoupa(time); }
}

// ---------------- início ----------------
(async function iniciar() {
  motor.volume(config.volume);
  volumeSons = config.volume;
  const carregando = $('#carregando');
  const st = $('#status'), prog = $('#progresso');
  if (ANDROID) st.textContent = 'Abrindo o mapa…';
  try {
    await iniciarMotor((t) => {
      const m = /\((\d+(?:\.\d+)?)\/(\d+)\)/.exec(t || '');
      if (m) { prog.style.width = (100 * m[1] / m[2]) + '%'; st.textContent = `Baixando o jogo… ${Math.round(m[1] / 1048576)} de ${Math.round(m[2] / 1048576)} MB`; }
      else if (t) st.textContent = t.replace('Downloading data...', 'Baixando o jogo…').replace('Running...', 'Abrindo…');
    });
  } catch (e) { st.textContent = String(e.message || e); return; }
  prog.style.width = '100%';
  carregando.remove();
  comecarLogin();
})();
