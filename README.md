# Favela Stricker

Jogo de tiro em primeira pessoa **Polícia x Bandidos** no mapa da favela.

- **Offline:** você contra os robôs, 5 contra 5.
- **Online:** jogadores contra jogadores, em salas com código (até 10 pessoas). A partida é ao vivo: os outros
  veem você andar, pular, agachar, mirar, atirar, recarregar e trocar de arma na hora. Tem chat de voz com o seu time.
- **Login:** conta com e-mail e senha (ou convidado). Moedas e itens comprados ficam na conta.
- **Lojas:** armas se compram **na partida**, na base do time, com o $ dos abates. Skins de arma e roupas se compram
  no **mercado**, com moedas. Cada lado tem as suas roupas (polícia só farda, bandido só roupa de bandido);
  quantos quiserem podem usar a mesma.
- **Gráficos:** 4 qualidades — Leve, Padrão, Intermediária e Avançada (texturas, triângulos, sombras, visão e quadros).
- **Configurações:** sensibilidade da câmera, da mira e da luneta; som e voz; **HUD editável** (arrastar cada botão,
  mudar o tamanho e a transparência).

Tem duas versões, as duas com **as mesmas telas** (pasta `web/`) e ligadas ao mesmo Firebase e ao mesmo servidor:

- **APK (Android):** o motor nativo desenha o jogo e as telas aparecem por cima num WebView.
  O GitHub Actions monta a cada envio → aba **Actions** → **APK do jogo** → **favela-stricker-apk**.
- **Site:** o motor compilado para WebAssembly. O GitHub Actions (**Site do jogo**) monta e publica no ramo `site`;
  o Render coloca no ar.

## O que tem em cada pasta

| Pasta | O que é |
|---|---|
| `src/` | o motor 3D (C++): gráficos, física, personagens, armas, a partida, a loja da partida e o online |
| `shaders/` | os arquivos do jogo: shaders, modelos (skins, armas, mapa), animações, texturas |
| `web/` | as telas do jogo (HTML/JS): `motor.js` (ponte com o motor), `conta.js` (catálogo, conta, salas), `ui.js`, `partida.js` (HUD, controles, editor do HUD, partida ao vivo, voz), `jogo.js` (telas) |
| `app/` | o app Android (Kotlin): motor + WebView com as telas (`JogoActivity`, `PonteJogo`), sons |
| `servidor/` | o servidor (Node.js) do Render: entrega o site e faz as partidas ao vivo e a voz |
| `tools/` | testes do motor sem celular (`headless_test.cpp`), gerador das skins de fuzil e das prévias da loja |

A configuração do Firebase e o endereço do servidor ficam em `app/src/main/assets/config_jogo.json` (APK)
e `web/config_jogo.js` (site).

## Se o jogo fechar sozinho

Na próxima abertura aparece **"O jogo fechou da última vez"** com o motivo e um botão **Copiar**: mande o texto.

## Colocar o servidor no ar (Render)

1. Em [render.com](https://render.com), entre com a sua conta do GitHub.
2. **New → Blueprint** → escolha este repositório. O Render lê o `render.yaml` e cria o serviço **favela-stricker**
   (grátis) a partir do ramo `site` (que o GitHub Actions cria na primeira vez que o workflow **Site do jogo** termina).
3. O endereço fica `https://favela-stricker.onrender.com`. Se o Render der outro nome, troque o `"servidor"` em
   `app/src/main/assets/config_jogo.json` e envie de novo.

No plano grátis o servidor dorme depois de 15 minutos sem ninguém e leva uns 50 segundos para acordar.
Enquanto isso, a partida online segue pelo Firebase (sem voz).

## Firebase (uma vez)

- **Authentication → Sign-in method:** ligar **E-mail/senha** e **Anônimo** (convidado).
- **Realtime Database → Regras:**

```json
{
  "rules": {
    "salas": {
      "$codigo": { ".read": "auth != null", ".write": "auth != null" }
    },
    "usuarios": {
      "$uid": { ".read": "auth != null && auth.uid === $uid", ".write": "auth != null && auth.uid === $uid" }
    }
  }
}
```

## Testes no PC (sem celular)

```
cmake -S . -B build && cmake --build build --target mobile3d_headless
SO_ONLINE=1 ./build/mobile3d_headless shaders saida        # partida online simulada
echo 0 > shaders/qualidade.txt                             # nível Leve
SO_MEMORIA=app/src/main/assets/exemplos/5_policia_x_bandidos.json ./build/mobile3d_headless shaders saida
```

## Créditos

Veja `CREDITOS.txt` (modelos, texturas, animações, fontes e bibliotecas de terceiros).
