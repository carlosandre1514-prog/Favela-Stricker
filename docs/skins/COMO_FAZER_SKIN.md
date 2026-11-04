# Como fazer uma skin para o boneco (estilo Roblox)

O boneco do Favela Stricker é feito de blocos: cabeça, tronco, 2 braços, 2 pernas e 2 pés.
A skin é **uma imagem PNG quadrada** onde cada bloco aparece "desmontado", como uma caixa de papelão aberta.

## Arquivos

| Arquivo | Para que serve |
|---|---|
| `molde_personagem.png` | Molde com o nome de cada parte (use como guia e para mandar para a IA) |
| `molde_limpo.png` | Só as bordas, sem texto (para pintar por cima no editor de imagem) |
| `exemplo_pm.png` | Uma skin pronta de exemplo (policial) |
| `molde_cabeca.png` | Só a cabeça, grande, com as linhas do rosto (cabelo, olhos, nariz, boca, queixo) |

## Como cada parte está aberta

Cada parte é uma caixa aberta em cruz:

```
            [ TOPO ]
[LADO DIR.][ FRENTE ][LADO ESQ.][ COSTAS ]
            [ BASE ]
```

- **FRENTE**: o boneco olhando para você (rosto, peito, joelhos).
- **COSTAS**: o boneco de costas.
- **LADO DIR. / LADO ESQ.**: lado direito e esquerdo **do próprio boneco**. O lado direito dele fica à esquerda de quem olha de frente.
- **TOPO**: em cima da FRENTE (alto da cabeça, ombros). **BASE**: embaixo da FRENTE (sola do pé, ponta da mão).
- **Linhas tracejadas**: onde um bloco acaba e o próximo começa (cotovelo, joelho, cintura). Elas mostram até onde vai a manga, a calça, a luva etc.
- O que ficar **fora dos retângulos não aparece** no jogo.

## Medidas (imagem de 1024 x 1024; em 2048 é só dobrar)

| Parte | Frente / Costas (largura x altura) | Lados | Topo / Base | Divisões (de cima para baixo) |
|---|---|---|---|---|
| Cabeça | 131 x 131 | 122 x 131 | 131 x 123 | — |
| Tronco | 168 x 238 | 84 x 238 | 168 x 84 | peito (47%), barriga (24%), cintura (29%) |
| Braço (cada um) | 57 x 266 | 57 x 266 | 57 x 57 | ombro/braço (45%), antebraço (37%), mão (18%) |
| Perna (cada uma) | 63 x 251 | 63 x 251 | 63 x 63 | coxa (53%), canela (47%) |
| Pé (cada um) | 59 x 32 | 72 x 32 | 59 x 72 | — |

## O que pintar em cada parte

- **Cabeça**:
  - FRENTE: o rosto (olhos, sobrancelhas, boca, barba, máscara ou balaclava).
  - LADOS: orelhas e cabelo.
  - COSTAS: a nuca e o cabelo.
  - TOPO: o cabelo, o boné ou o capacete visto de cima.
  - BASE: o queixo e o pescoço (cor da pele).
- **Tronco**:
  - FRENTE: o peito e a barriga da camisa (escudo, número, colete, bolsos, logotipo); o cinto e a calça ficam na parte de baixo ("cintura").
  - COSTAS: as costas da camisa (nome, número, "POLÍCIA").
  - LADOS: as laterais da camisa.
  - TOPO: os ombros e a gola.
  - BASE: o fundo da calça (fica escondido).
- **Braços**:
  - "ombro": a manga.
  - "antebraço": manga comprida ou pele.
  - "mão": pele ou luva.
  - BASE: a ponta da mão.
  - TOPO: o ombro.
- **Pernas**:
  - "coxa": a calça ou a bermuda.
  - "canela": a calça, ou a pele se for bermuda; meião ou caneleira também vão aqui.
- **Pés**:
  - Os lados, a frente e as costas: o tênis ou a bota.
  - TOPO: o cadarço.
  - BASE: a sola.

## Prompt para a IA (imagem inteira)

Mande o `molde_personagem.png` junto e escreva:

> Use the attached image as an exact UV layout template. Keep every rectangle in exactly the same position and size, on a square 1024x1024 canvas, and fill each rectangle with flat, game-ready texture art. This is a blocky low-poly game character (Roblox style) skin. Character: **[DESCREVA A ROUPA, ex.: Brazilian military police officer, light blue long-sleeve shirt, black tactical vest with white "POLÍCIA" text on chest and back, dark navy trousers, black boots, navy cap]**.
> Each part is an unfolded box: the middle row is [RIGHT SIDE][FRONT][LEFT SIDE][BACK], with TOP above the FRONT and BOTTOM below it.
> - HEAD: face on FRONT (simple cartoon eyes and mouth), hair or cap on TOP, SIDES and BACK.
> - TORSO: shirt and vest. The top 47% is the chest, then 24% belly, and the bottom 29% is the waist with belt and trousers.
> - ARMS: top 45% is the sleeve, next 37% the forearm, last 18% the hand (skin or glove).
> - LEGS: top 53% is the thigh, bottom 47% the shin.
> - FEET: shoes or boots.
>
> Flat front-facing orthographic view, no perspective, no shadows, no lighting, no background scenery, no text labels, no outlines between rectangles. Paint inside the rectangles only; the area outside them is solid gray.

Troque o trecho em **negrito** pela roupa que você quer. O prompt em inglês costuma dar mais certo nas IAs de imagem; a tradução está logo abaixo.

**Em português:**

> Use a imagem anexada como molde de UV exato. Mantenha todos os retângulos na mesma posição e tamanho, em uma tela quadrada de 1024x1024, e preencha cada retângulo com textura chapada pronta para jogo. É a skin de um personagem de blocos low-poly (estilo Roblox). Personagem: [ROUPA]. Cada parte é uma caixa aberta: na fileira do meio [LADO DIREITO][FRENTE][LADO ESQUERDO][COSTAS], com o TOPO acima da FRENTE e a BASE abaixo. Vista de frente, sem perspectiva, sem sombra, sem luz, sem cenário, sem textos de legenda e sem contorno entre os retângulos. Pinte só dentro dos retângulos; fora deles, cinza liso.

## Rosto proporcional à cabeça (o mais importante)

A cabeça do boneco é **um cubo**. O rosto tem que ocupar o **quadrado da FRENTE inteiro**, como um adesivo colado na frente do cubo.
O erro mais comum é a IA desenhar a **cabeça inteira** (com cabelo alto, orelhas e pescoço) dentro do quadrado da frente.
Aí o rosto fica pequeno e espremido, e sobra fundo em volta.

Use o `molde_cabeca.png` (a cabeça grande, com as linhas do rosto).

**Regras para o rosto:**
- O rosto vai **de uma borda à outra**: as bochechas encostam nos lados do quadrado e o queixo encosta na borda de baixo.
- Linhas do rosto, de cima para baixo (veja as linhas tracejadas no molde da cabeça):

| Linha | Distância a partir do alto do quadrado |
|---|---|
| Linha do cabelo | 16% |
| Sobrancelhas | 36% |
| Olhos | 45% |
| Ponta do nariz | 62% |
| Boca | 74% |
| Queixo | 92% |

- **Orelhas** só nos LADOS. Na frente não aparecem.
- **Pescoço** só na BASE.
- **Cabelo** no TOPO, nas COSTAS e na parte de cima dos lados.
- Vista **reta de frente**, sem sombra, sem luz de lado e **sem nenhum fundo** (nem preto, nem branco).
- Estilo: para combinar com o boneco de blocos, um rosto **mais simples e chapado** (estilo desenho ou pixel) fica melhor do que foto realista.
  Foto realista esticada num cubo sempre parece "máscara".

### Prompt da cabeça (mande junto o `molde_cabeca.png`)

> Use the attached image as an exact UV template for a cube-shaped head of a blocky game character (Roblox style). Keep every rectangle in the same position and size on a square 1024x1024 canvas and fill each one completely, edge to edge, with no background visible.
> FRONT square: a flat front-facing face that fills the whole square like a sticker on a cube: cheeks touching the left and right edges, chin touching the bottom edge, hairline at 16% from the top, eyebrows at 36%, eyes at 45%, nose tip at 62%, mouth at 74%. No ears, no neck, no hair below the hairline on the front.
> LEFT SIDE and RIGHT SIDE: side of the head with one ear each (the ear is closer to the back edge), hair on top 16%, skin below.
> BACK: back of the head, hair on top, skin of the nape at the bottom.
> TOP: hair seen from above. BOTTOM: neck skin.
> Character: **[DESCREVA, ex.: young Brazilian man, brown skin, short black fade haircut, thin goatee, small diamond earrings]**.
> Style: **[clean cartoon / semi-realistic painted / pixel art]**, flat lighting, no shadows, no perspective, no text, no labels, no lines between rectangles.

**Em português:**

> Use a imagem anexada como molde de UV exato da cabeça em forma de cubo de um boneco de blocos (estilo Roblox). Mantenha todos os retângulos na mesma posição e tamanho, numa tela quadrada de 1024x1024, e preencha cada um por inteiro, de borda a borda, sem fundo aparecendo.
> - FRENTE: rosto de frente, chapado, ocupando o quadrado todo como um adesivo no cubo. As bochechas encostam nas laterais e o queixo encosta embaixo. Linha do cabelo a 16% do alto, sobrancelhas a 36%, olhos a 45%, ponta do nariz a 62%, boca a 74%. Sem orelhas, sem pescoço e sem cabelo abaixo da linha do cabelo na frente.
> - LADOS: o lado da cabeça com uma orelha (mais perto da borda de trás), cabelo nos 16% de cima e pele embaixo.
> - COSTAS: nuca, com cabelo em cima e pele embaixo.
> - TOPO: o cabelo visto de cima.
> - BASE: o pescoço.
>
> Personagem: [DESCREVA]. Estilo: [desenho limpo / semi-realista pintado / pixel art], luz chapada, sem sombra, sem perspectiva, sem texto e sem linhas entre os retângulos.

### Se a IA continuar errando

- Peça **só o rosto** num quadrado: "a square image of only the face, filling the whole square, chin touching the bottom edge, hairline 16% from the top, front view, flat light, no background". Depois cole esse quadrado na FRENTE.
- Peça os lados separados ("side of the head, square, one ear, short hair on top, no face").
- Quando a IA entregar a folha numa disposição diferente da nossa, me mande que eu remonto no molde (tools/montar_skin.py).

## Prompts por parte (se a IA errar o molde inteiro)

Gere cada pedaço separado, no tamanho da tabela, e cole no `molde_limpo.png` com um editor (Ibis Paint, PicsArt, Photopea).

1. **Rosto (cabeça, FRENTE, 131x131)**
   > Square flat texture of a blocky cartoon game character face, front view, [skin tone] skin, simple black eyes, small smile, [beard/mask/balaclava]. No shadows, fills the whole image, no background.
2. **Cabeça, lados e costas (122x131 e 131x131)**
   > Square flat texture of the side/back of a blocky character head, [skin tone] skin with [short black hair / navy cap], no face, no shadows, fills the whole image.
3. **Topo da cabeça (131x123)**
   > Flat top-down texture of [hair / cap / helmet], fills the whole image, no shadows.
4. **Peito, barriga e cintura (FRENTE do tronco, 168x238)**
   > Flat front texture of a [shirt description] for a blocky game character, top 47% chest, middle 24% belly, bottom 29% belt and [trousers]. Front orthographic view, no arms, no head, no shadows, fills the whole image.
5. **Costas do tronco (168x238)**
   > The same shirt seen from the back, [name/number/text on back], bottom 29% belt and trousers, flat, no shadows, fills the whole image.
6. **Laterais do tronco (84x238)**
   > Narrow flat side texture of the same shirt and trousers, same split heights, no shadows.
7. **Braço (cada lado: 57x266)**
   > Tall narrow flat texture of an arm: top 45% [sleeve], next 37% [forearm: sleeve or skin], bottom 18% [hand: skin or glove]. No shadows, fills the whole image.
8. **Perna (cada lado: 63x251)**
   > Tall narrow flat texture of a leg: top 53% [trousers/shorts], bottom 47% [trousers / bare skin / sock]. No shadows, fills the whole image.
9. **Pé (59x32 na frente, 72x32 nos lados, 59x72 em cima)**
   > Small flat texture of a [sneaker/boot], side view, no shadows, fills the whole image.

## Colocar a skin no jogo

1. Salve a imagem pronta como PNG quadrado (1024x1024 ou 2048x2048).
2. Dê a ela o nome da roupa que vai trocar:

| Polícia | Bandidos |
|---|---|
| `pm.png` | `rubro.png` |
| `civil.png` | `alvinegro.png` |
| `exercito.png` | `tricolor.png` |
| `choque.png` | `canarinho.png` |
| `especial.png` | `faixa.png` |

3. Coloque o arquivo em `shaders/kit/personagens/texturas/` no repositório e faça o push (ou me mande a imagem que eu coloco).
4. O APK novo sai pelo Actions. Com o arquivo lá, aquela roupa usa a sua imagem; sem ele, continua a roupa colorida de hoje.

Para refazer os moldes: `python3 tools/gerar_molde_personagem.py`.
