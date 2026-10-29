// Favela Stricker — as telas do jogo (as mesmas no site e no APK), no visual do modelo das telas.
'use strict';

const chipMoedas = (mais = true) => `<button class="chip-moedas ${mais ? '' : 'sem-mais'}" data-ir="moedas">${ICONE.moeda(16)}<span>${fmt(conta.moedas)}</span>${mais ? '<span class="mais">+</span>' : ''}</button>`;
function topo(titulo, voltar, extra = '', direita = chipMoedas(false)) {
  const h = html(`<div class="topo"><button class="voltar" aria-label="Voltar">${ICONE.voltar}</button><span class="titulo">${esc(titulo)}</span>${extra}<span class="cresce"></span>${direita}</div>`);
  h.querySelector('.voltar').onclick = voltar;
  return h;
}
function ligarAtalhos(t) {
  for (const b of $$('[data-ir]', t)) b.onclick = () => ({ moedas: telaMoedas, mercado: () => telaMercado('todas'), armas: telaArmas, config: () => telaConfig('sens'), multi: telaMultiplayer })[b.dataset.ir]();
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
    <div class="coluna" style="gap:2px">
      <span class="bebas azul" style="font-size:56px;line-height:.9">POLÍCIA</span>
      <div class="linha" style="align-items:baseline"><span class="bebas" style="font-size:32px">x</span><span class="bebas ouro" style="font-size:56px;line-height:.9">BANDIDOS</span></div>
      <span style="font-size:12px;color:#D1D5DB;margin-top:6px;letter-spacing:2px;font-weight:600">FAVELA STRICKER · 5 CONTRA 5 · PRIMEIRA PESSOA</span>
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
    </div><span class="versao">Favela Stricker ${esc(VERSAO)}</span></div>`), 'login');
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
  if (await f.restaurar(salvo)) { fb = f; await carregarConta(); telaInicio(); } else telaLogin();
}

// ---------------- tela inicial ----------------
function destaqueDoMercado() {
  const naoTem = SKINS_ARMA.filter((s) => !conta.temSkinArma(s));
  return naoTem.sort((a, b) => b.preco - a.preco)[0] || SKINS_ARMA[0];
}
function telaInicio() {
  motor.girarMenu(true);
  const d = destaqueDoMercado();
  const t = mostrar(html(`<div class="tela sobre-mapa" id="inicio" style="padding:0">
    <div class="esquerda">
      <div class="coluna" style="gap:0">
        <span class="bebas" style="font-size:56px;line-height:.9;color:#60A5FA">POLÍCIA</span>
        <div class="linha" style="align-items:baseline"><span class="bebas" style="font-size:32px">x</span><span class="bebas ouro" style="font-size:56px;line-height:.9">BANDIDOS</span></div>
        <span style="font-size:12px;color:#D1D5DB;margin-top:4px;letter-spacing:2px;font-weight:600">FAVELA · 5 CONTRA 5 · PRIMEIRA PESSOA</span>
      </div>
      <span class="cresce"></span>
      <button class="btn btn-ouro btn-grande" id="i-jogar" style="width:260px">${ICONE.play} JOGAR</button>
      <div class="grade">
        <button class="btn btn-fraco" data-ir="mercado">${ICONE.skins} Skins</button>
        <button class="btn btn-fraco" data-ir="moedas">${ICONE.moedas} Moedas</button>
        <button class="btn btn-fraco" data-ir="armas">${ICONE.armas} Armas</button>
        <button class="btn btn-fraco" data-ir="multi">${ICONE.grupo} Multiplayer</button>
      </div>
    </div>
    <div class="direita-topo">
      ${chipMoedas()}
      <button class="chip" id="i-graficos">Gráficos: ${esc(nomeNivel())}</button>
      <button class="chip" data-ir="config" aria-label="Configurações">${ICONE.engrenagem}</button>
      <button class="chip" id="i-conta">${ICONE.pessoa}<span>${esc(conta.nome())}</span></button>
    </div>
    <span class="versao">Favela Stricker ${esc(VERSAO)} · gráficos ${esc(nomeNivel())}</span>
    <button class="destaque" data-ir="mercado">
      <div class="linha" style="gap:8px"><span class="etiqueta fundo-${d.raridade}">${RARIDADES[d.raridade].toUpperCase()}</span><span class="dica" style="color:#D1D5DB">Destaque do mercado</span></div>
      <img src="armas/${d.arma}_${d.key}.png" alt="${esc(d.nome)}">
      <div class="linha" style="justify-content:space-between"><span class="bebas" style="font-size:28px">${esc(d.nome.toUpperCase().replace(' ', ' · '))}</span>
        <span class="linha" style="gap:6px;font-size:15px;font-weight:800;color:#FCD34D">${ICONE.moeda(14)} ${fmt(d.preco)}</span></div>
    </button>
  </div>`), 'inicio');
  ligarAtalhos(t);
  $('#i-jogar', t).onclick = telaLado;
  $('#i-graficos', t).onclick = () => telaConfig('graficos');
  $('#i-conta', t).onclick = () => telaConfig('conta');
}

// ---------------- escolha do lado ----------------
function telaLado() {
  motor.girarMenu(true);
  const t = mostrar(html(`<div class="tela">
    <div class="lados">
      <button class="lado pol" data-time="0">
        <div class="linha" style="gap:12px"><div class="icone" style="background:#3B82F6">${ICONE.escudo}</div><span class="nome" style="color:#93C5FD">POLÍCIA</span></div>
        <span class="texto" style="color:#DBEAFE">Nasce fora da favela, na rua dos prédios. Sobe as escadarias para tomar o morro.</span>
        <span class="cresce"></span>
        <div class="linha" style="gap:6px;flex-wrap:wrap"><span class="tag" style="background:rgba(59,130,246,.22);color:#DBEAFE">M4A1 · M4 Colt com luneta</span>
          <span class="tag" style="background:rgba(59,130,246,.22);color:#DBEAFE">5 fardas grátis: PM, Civil, Exército, Choque, Especial</span></div>
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
  t.prepend(topo('ESCOLHA SEU LADO', telaInicio, '', '<span class="dica">Partida até 25 abates · 10 min</span>'));
  for (const b of $$('[data-time]', t)) b.onclick = () => telaRoupa(Number(b.dataset.time));
}

// ---------------- roupa do lado (antes de jogar). pronto != null: veio da sala do multiplayer ----------------
function telaRoupa(time, pronto) {
  motor.girarMenu(true);
  const minhas = ROUPAS.filter((r) => r.time === time);
  let sel = conta.roupa(time);
  const t = mostrar(html(`<div class="tela">
    <div class="linha cresce" style="align-items:stretch;gap:14px">
      <div class="grade-itens cresce" id="r-grade" style="grid-template-columns:repeat(5,minmax(0,1fr));grid-auto-rows:1fr;align-content:stretch;overflow:hidden"></div>
    </div>
    <div class="linha"><span class="dica cresce">Você começa com a ${esc(armaPorKey(armaInicial(time)).nome)} e a TT-33. As outras armas compra na base, com o $ dos abates.</span>
      <button class="btn btn-ouro btn-grande" id="r-ir" style="height:48px;font-size:24px;width:200px">${pronto ? 'PRONTO' : `${ICONE.play} COMEÇAR`}</button></div>
  </div>`), 'roupa');
  t.prepend(topo(time ? 'SUA ROUPA · BANDIDOS' : 'SUA FARDA · POLÍCIA', () => (pronto ? telaSala() : telaLado())));
  const desenhar = () => {
    $('#r-grade', t).innerHTML = minhas.map((r) => {
      const tem = conta.temRoupa(r);
      return `<button class="item ${r === sel ? 'sel' : ''} ${tem ? '' : 'travado'}" data-k="${r.key}" style="justify-content:flex-end">
        <img src="retratos/${r.key}.png" alt="" style="flex:1 1 0;min-height:0;height:0">
        <span class="n" style="font-size:13px">${esc(r.nome)}</span>
        <span class="p ${tem ? 'verde' : 'amarelo'}">${tem ? (r === sel ? 'equipada' : 'grátis') : `${ICONE.moeda(12)} ${fmt(r.preco)}`}</span></button>`;
    }).join('');
    for (const d of $$('.item', t)) d.onclick = async () => {
      const r = minhas.find((x) => x.key === d.dataset.k);
      if (conta.temRoupa(r)) { sel = r; desenhar(); return; }
      if (await comprarComMoedas(r.nome, r.preco, 'skin:' + r.key)) { sel = r; desenhar(); }
    };
  };
  desenhar();
  $('#r-ir', t).onclick = () => {
    conta.usarRoupa(time, sel.key);
    guardado.set('lado', time);
    if (pronto) pronto(); else iniciarPartida(time, sel, null);
  };
}

async function comprarComMoedas(nome, preco, id) {
  const tem = conta.moedas;
  if (tem < preco) {
    const ir = await janela('Moedas insuficientes', `${nome} custa ${fmt(preco)} moedas e você tem ${fmt(tem)}. Faltam ${fmt(preco - tem)}.\nGanhe jogando (abates e vitórias dão moedas) ou veja a loja de moedas.`,
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

// ---------------- mercado de skins (moedas) ----------------
function itensMercado(aba) {
  const lado = (time) => aba === 'todas' || (aba === 'policia' ? time !== 1 : time !== 0);
  const armas = SKINS_ARMA.filter((s) => lado(armaPorKey(s.arma).time)).map((s) => ({
    id: `skin_arma:${s.arma}:${s.key}`, nome: s.nome, raridade: s.raridade, preco: s.preco, img: `armas/${s.arma}_${s.key}.png`,
    tem: conta.temSkinArma(s), usando: conta.skinArma(s.arma) === s.key, arma: s.arma, key: s.key,
    desc: armaPorKey(s.arma).time === 0 ? 'Arma da polícia' : armaPorKey(s.arma).time === 1 ? 'Arma dos bandidos' : 'Pistola dos dois lados',
  }));
  const roupas = ROUPAS.filter((r) => r.preco > 0 && lado(r.time)).map((r) => ({
    id: 'skin:' + r.key, nome: r.nome, raridade: r.raridade, preco: r.preco, img: `retratos/${r.key}.png`, tem: conta.temRoupa(r),
    usando: guardado.get('lado', 0) === r.time && conta.roupa(r.time) === r, roupa: r, desc: r.time ? 'Roupa dos bandidos' : 'Farda da polícia',
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
  const abas = `<div class="abas">${[['todas', 'Todas'], ['policia', 'Polícia'], ['bandidos', 'Bandidos']].map(([k, n]) => `<button class="aba ${k === aba ? 'sel' : ''}" data-aba="${k}">${n}</button>`).join('')}</div>`;
  const tp = topo('MERCADO DE SKINS', telaInicio, abas, chipMoedas());
  t.prepend(tp);
  ligarAtalhos(tp);
  for (const b of $$('[data-aba]', t)) b.onclick = () => telaMercado(b.dataset.aba);
  const desenhar = () => {
    $('#m-grade', t).innerHTML = itens.map((it) => `<button class="item ${it === sel ? 'sel' : ''}" data-id="${it.id}">
      <img src="${it.img}" alt=""><span class="n">${esc(it.nome)}</span><span class="r raridade-${it.raridade}">${RARIDADES[it.raridade]}</span>
      <span class="p ${it.tem ? 'azul' : 'amarelo'}">${it.usando ? 'usando' : it.tem ? '✓ sua' : `${ICONE.moeda(12)} ${fmt(it.preco)}`}</span></button>`).join('');
    for (const d of $$('.item', t)) d.onclick = () => { sel = itens.find((i) => i.id === d.dataset.id); desenhar(); };
    const det = $('#m-det', t);
    let botao;
    if (!sel.tem) botao = `<button class="btn btn-ouro" id="m-acao" style="font-size:15px;letter-spacing:1px">COMPRAR POR ${ICONE.moeda(14).replace('#FCD34D', '#111827')} ${fmt(sel.preco)}</button>`;
    else if (sel.arma) botao = sel.usando ? '<button class="btn btn-fraco" id="m-acao">USANDO · tocar para tirar</button>' : '<button class="btn btn-azul" id="m-acao">USAR NA ' + esc(armaPorKey(sel.arma).nome) + '</button>';
    else botao = sel.usando ? '<button class="btn btn-fraco" disabled>USANDO</button>' : '<button class="btn btn-azul" id="m-acao">USAR</button>';
    det.innerHTML = `<div class="linha" style="gap:8px"><span class="etiqueta fundo-${sel.raridade}">${RARIDADES[sel.raridade].toUpperCase()}</span><span class="dica">${esc(sel.desc)}</span></div>
      <span class="bebas" style="font-size:30px">${esc(sel.nome.toUpperCase())}</span>
      <div class="previa"><img src="${sel.img}" alt="${esc(sel.nome)}"></div>
      <span class="dica" style="font-size:11px">${sel.arma ? 'Só visual, não muda o dano · a skin é da sua arma: no online os outros veem ela na sua mão e nas suas costas' : 'Roupa só do seu lado · quantos quiserem podem usar a mesma'}</span>${botao}`;
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
    <span class="dica" style="font-size:11px">Pagamento pelo Google Play. Os preços são sugestão, no padrão de jogos de tiro para celular, e podem mudar até o lançamento.</span>
  </div>`), 'moedas');
  t.prepend(topo('LOJA DE MOEDAS', telaInicio, '<span class="dica">moedas compram skins de arma e de roupa</span>', `<span class="chip-moedas sem-mais">${ICONE.moeda(16)} ${fmt(conta.moedas)}</span>`));
  for (const b of $$('[data-i]', t)) b.onclick = () => janela('Em breve',
    'A compra de moedas com dinheiro de verdade vai funcionar quando o jogo estiver na Play Store (Google Play Faturamento).\n\nPor enquanto, ganhe moedas jogando: cada abate dá 25 e a vitória dá 150.');
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
  t.prepend(topo('ARMAS E DANO', telaInicio, '<span class="dica">vida 100 · cabeça = corpo × 2,5 · de longe o dano cai até 30%</span>', ''));
}

// ---------------- configurações ----------------
function telaConfig(aba = 'sens') {
  motor.girarMenu(true);
  const abas = [['sens', 'Sensibilidade'], ['graficos', 'Gráficos'], ['audio', 'Som e voz'], ['hud', 'Controles (HUD)'], ['conta', 'Conta']];
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
       <button class="btn btn-fraco" id="c-padrao" style="align-self:flex-start">Voltar ao padrão</button>`;
    ligarAjustes(c);
    $('#c-inv', c).onclick = (e) => { config.inverterY = !config.inverterY; e.target.classList.toggle('ligada', config.inverterY); salvarConfig(); };
    $('#c-vib', c).onclick = (e) => { config.vibrar = !config.vibrar; e.target.classList.toggle('ligada', config.vibrar); salvarConfig(); };
    $('#c-padrao', c).onclick = () => { for (const k of ['sens', 'sensMira', 'sensLuneta', 'inverterY', 'vibrar']) config[k] = CONFIG_PADRAO[k]; salvarConfig(); telaConfig('sens'); };
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
  } else if (aba === 'conta') {
    const quem = !fb ? 'Sem conta (só neste aparelho)' : fb.anonimo ? 'Convidado (crie uma conta para não perder nada)' : fb.email;
    c.innerHTML = `<div class="ajuste"><span class="nome">Nome no jogo</span><input type="text" id="c-nome" maxlength="16" value="${esc(conta.nome())}"><button class="btn btn-fraco" id="c-salvar-nome">Salvar</button></div>
      <div class="ajuste"><span class="nome">Conta</span><span class="cresce" style="font-size:13px">${esc(quem)}</span></div>
      <div class="ajuste"><span class="nome">Moedas</span><span class="cresce amarelo" style="font-weight:800">${fmt(conta.moedas)}</span></div>
      <button class="btn btn-fraco" id="c-sair" style="align-self:flex-start">Sair da conta</button>`;
    $('#c-salvar-nome', c).onclick = () => { const n = $('#c-nome', c).value.trim().slice(0, 16); if (!n) return; guardado.set('nome', n); salvarConta(); aviso('Nome salvo'); };
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
function telaMultiplayer() {
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
    guardado.set('nome', nm);
    salvarConta();
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

function ligarSala(s) {
  s.ouvir(
    () => { if (telaAtual() === 'sala') telaSala(); },
    (estado) => { if (sala === s && estado === 'jogo' && !partida) comecarOnline(s); },
    () => { if (sala !== s) return; sala = null; aviso('A sala fechou (quem criou saiu)'); if (partida) pararPartida(); telaInicio(); },
    (id) => motor.redeEvento(JSON.stringify({ t: 'saiu', id })));
}
function sairDaSala() { const s = sala; sala = null; if (s) s.sair().catch(() => {}); }

function telaSala() {
  const s = sala;
  if (!s) { telaInicio(); return; }
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

function comecarOnline(s) {
  const eu = s.lista().find((p) => p.id === fb.uid) || minhaEntrada(guardado.get('lado', 0));
  const time = eu.time | 0;
  const roupa = ROUPAS.find((r) => r.time === time && r.i === (eu.skin | 0)) || conta.roupa(time);
  iniciarPartida(time, roupa, s, eu.arma || armaInicial(time));
}

// ---------------- fim de partida ----------------
function fimDePartida(j) {
  const sc = j.placar || [0, 0];
  const venceu = sc[0] > sc[1] ? 0 : sc[1] > sc[0] ? 1 : -1;
  const time = partida ? partida.time : 0;
  const ganho = (j.abates | 0) * 25 + (venceu === time ? 150 : venceu < 0 ? 80 : 50);
  const s = partida && partida.sala;
  pararPartida();
  conta.moedas = conta.moedas + ganho;
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
      <div class="coluna" style="align-items:flex-end;gap:4px"><div class="cartao linha" style="padding:8px 18px;gap:14px"><span class="bebas" style="font-size:46px;color:#60A5FA">${sc[0]}</span><span class="dica">x</span><span class="bebas ouro" style="font-size:46px">${sc[1]}</span></div>
        <span class="linha" style="gap:6px;font-size:13px;font-weight:800;color:#FCD34D">${ICONE.moeda(13)} +${fmt(ganho)} moedas</span></div>
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
