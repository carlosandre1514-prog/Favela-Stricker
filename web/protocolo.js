// Protocolo das partidas ao vivo (versão 2). O mesmo arquivo no jogo (APK e site) e nos testes do servidor.
//
// Binário (o primeiro byte diz o tipo):
//   jogo -> servidor   [1][estado 22 bytes]                 o meu lutador (só quando muda, ou a cada 250 ms)
//   servidor -> jogo   [1][n][n x (vaga + estado 22 bytes)]  os lutadores da sala que mudaram, juntos num pacote
//   jogo -> servidor   [2][áudio μ-law]                      voz (só para o próprio time)
//   servidor -> jogo   [2][vaga][áudio μ-law]
// Texto (JSON, raro): {"k":"ev"} eventos (dano, abate, granada), {"k":"pg"} ping e {"k":"vagas","d":{vaga: id}}
// (o servidor manda a lista de quem é cada vaga quando alguém entra ou sai da sala).
//
// Estado (22 bytes, little-endian): posição x/y/z em 2 cm (int16, até ±655 m), olhar (yaw) e inclinação (pitch)
// em 1/10000 rad (int16), bits (agachado, mirando, vivo, no ar, recarregando, fora da Ressurgência), arma (índice; 255 = outra),
// tiros dados (uint32) e velocidade x/y/z em cm/s (int16). Antes ia em texto: ~170 bytes por lutador.
const PROTO = (() => {
  const VERSAO = 2;
  const T_ESTADO = 1, T_VOZ = 2;
  const TAM = 22;
  const ARMAS_IDX = ['m4a1', 'm4colt', 'ak47', 'ak74', 'ak74_tatica', 'tt33'];
  const i16 = (v, k) => Math.max(-32768, Math.min(32767, Math.round((+v || 0) * k)));

  function escrever(d, o, s) {
    const p = s.p || [0, 0, 0], v = s.vel || [0, 0, 0];
    d.setInt16(o, i16(p[0], 50), true); d.setInt16(o + 2, i16(p[1], 50), true); d.setInt16(o + 4, i16(p[2], 50), true);
    d.setInt16(o + 6, i16(s.y, 10000), true); d.setInt16(o + 8, i16(s.pi, 10000), true);
    d.setUint8(o + 10, (s.c ? 1 : 0) | (s.a ? 2 : 0) | (s.v !== false ? 4 : 0) | (s.j ? 8 : 0) | (s.r ? 16 : 0) | (s.f ? 32 : 0));
    const w = ARMAS_IDX.indexOf(s.w);
    d.setUint8(o + 11, w < 0 ? 255 : w);
    d.setUint32(o + 12, (s.n | 0) >>> 0, true);
    d.setInt16(o + 16, i16(v[0], 100), true); d.setInt16(o + 18, i16(v[1], 100), true); d.setInt16(o + 20, i16(v[2], 100), true);
  }
  function ler(d, o) {
    const bits = d.getUint8(o + 10), w = d.getUint8(o + 11);
    const s = {
      p: [d.getInt16(o, true) / 50, d.getInt16(o + 2, true) / 50, d.getInt16(o + 4, true) / 50],
      y: d.getInt16(o + 6, true) / 10000, pi: d.getInt16(o + 8, true) / 10000,
      c: !!(bits & 1), a: !!(bits & 2), v: !!(bits & 4), j: !!(bits & 8), r: !!(bits & 16), f: !!(bits & 32),
      n: d.getUint32(o + 12, true),
      vel: [d.getInt16(o + 16, true) / 100, d.getInt16(o + 18, true) / 100, d.getInt16(o + 20, true) / 100],
    };
    if (w < ARMAS_IDX.length) s.w = ARMAS_IDX[w];
    return s;
  }
  // meu estado (objeto do motor) -> pacote para o servidor
  function codificarEstado(s) {
    const b = new Uint8Array(1 + TAM);
    b[0] = T_ESTADO;
    escrever(new DataView(b.buffer), 1, s);
    return b;
  }
  // pacote do servidor -> {id: estado} para o motor (vagas: número da vaga -> id da pessoa)
  function decodificarEstados(buf, vagas) {
    const d = new DataView(buf.buffer || buf, buf.byteOffset || 0, buf.byteLength);
    const n = d.getUint8(1), out = {};
    for (let i = 0; i < n; i++) {
      const o = 2 + i * (1 + TAM);
      if (o + 1 + TAM > d.byteLength) break;
      const id = vagas[d.getUint8(o)];
      if (id) out[id] = ler(d, o + 1);
    }
    return out;
  }
  function codificarVoz(audio) {
    const b = new Uint8Array(1 + audio.length);
    b[0] = T_VOZ;
    b.set(audio, 1);
    return b;
  }
  return { VERSAO, T_ESTADO, T_VOZ, TAM, codificarEstado, decodificarEstados, codificarVoz };
})();
if (typeof module !== 'undefined') module.exports = PROTO;
