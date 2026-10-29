# Favela Stricker

Jogo de tiro em primeira pessoa **Polícia x Bandidos** no mapa da favela.

- **Offline:** você contra os robôs, 5 contra 5.
- **Online:** jogadores contra jogadores, em salas com código (até 10 pessoas). A partida é ao vivo: os outros
  veem você andar, pular, agachar, mirar, atirar, recarregar e trocar de arma na hora. Tem chat de voz com o seu time.
- **Login:** conta com e-mail e senha (ou convidado). Moedas e itens comprados ficam na conta.
- **Skins:** cada lado tem as suas (polícia só usa skin de polícia, bandido só de bandido); quantos quiserem podem usar a mesma.

Tem duas versões, as duas ligadas ao mesmo Firebase e ao mesmo servidor:

- **APK (Android):** o GitHub Actions monta a cada envio → aba **Actions** → **APK do jogo** → **favela-stricker-apk**.
- **Site:** o GitHub Actions monta a versão para navegador (WebAssembly) e publica no ramo `site`; o Render coloca no ar.

## O que tem em cada pasta

| Pasta | O que é |
|---|---|
| `src/` | o motor 3D (C++): gráficos, física, personagens, armas, a partida e o online |
| `shaders/` | os arquivos do jogo: shaders, modelos (skins, armas, mapa), animações, texturas |
| `app/` | o app Android (Kotlin): telas, HUD, login, salas, partida ao vivo e chat de voz |
| `web/` | a versão site: telas em HTML e o motor compilado para o navegador |
| `servidor/` | o servidor (Node.js) do Render: entrega o site e faz as partidas ao vivo e a voz |
| `tools/` | testes do motor sem celular e um Firebase de mentira para testar as salas |

A configuração do Firebase e o endereço do servidor ficam em `app/src/main/assets/config_jogo.json` (APK)
e `web/config_jogo.js` (site).

## Colocar o servidor no ar (Render)

1. Em [render.com](https://render.com), entre com a sua conta do GitHub.
2. **New → Blueprint** → escolha este repositório. O Render lê o `render.yaml` e cria o serviço **favela-stricker**
   (grátis) a partir do ramo `site`.
3. O endereço fica `https://favela-stricker.onrender.com`. Se o Render der outro nome, troque o `"servidor"` em
   `app/src/main/assets/config_jogo.json` e em `web/config_jogo.js` e envie de novo.

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

## Créditos

Veja `CREDITOS.txt` (modelos, texturas, animações e bibliotecas de terceiros).
