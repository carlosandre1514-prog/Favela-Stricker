# Favela Strike em Android, PC e iPhone/iPad

Um jogo só, uma infraestrutura só. As três plataformas usam o mesmo login, o mesmo banco, o mesmo servidor de
partidas e o mesmo protocolo: quem joga no Android cai na mesma sala de quem joga no PC ou no iPhone.

| | Android | PC | iPhone / iPad |
|---|---|---|---|
| Como roda | APK (motor nativo C++ + telas em WebView) | site no navegador (motor em WebAssembly + WebGL 2) | o mesmo site no Safari (iOS 15+) ou instalado pela "Tela de Início" (tela cheia) |
| Telas | `web/` (as mesmas) | `web/` | `web/` |
| Login e conta | Firebase Auth do projeto (e-mail/senha ou convidado) | o mesmo | o mesmo |
| Amigos, convites, grupo, conversas | Firebase Realtime Database (`web/social.js`, `web/chat.js`) | o mesmo | o mesmo |
| Partida ao vivo | servidor do jogo (Render), WebSocket `/partida`, protocolo 2 (`web/protocolo.js`) | o mesmo | o mesmo |
| Chat global e presentes | servidor do jogo: WebSocket `/chat` e `/api/social/...` (`servidor/social.js`) | o mesmo | o mesmo |

## O que garante que as três jogam juntas

- **Identificação única**: o `uid` do Firebase e o ID de 9 dígitos (`ids/`) valem em qualquer aparelho; o perfil
  guarda a `plataforma` (aparece no chat e na lista de amigos), mas nada no jogo depende dela.
- **Protocolo único**: `web/protocolo.js` é o mesmo arquivo no APK, no site e nos testes do servidor. O servidor recusa
  versão diferente (`v=2`) com "atualize o jogo".
- **Mapa e regras iguais**: o motor é o mesmo código C++ (compilado para Android e para WebAssembly); a Ressurgência usa
  a semente da sala (zona e pontos de queda iguais em todos os aparelhos).
- **Validação no servidor**: login conferido pela assinatura do Google (token do Firebase) no chat e nos presentes;
  tamanho e frequência dos pacotes da partida; limite de conexões e salas; presentes processados só no servidor.
- O servidor conta os jogadores por plataforma (`/api/estado` → `por_plataforma`).

## iPhone e iPad: o que funciona hoje e o que falta para um app na App Store

**Hoje (sem custo):** o site roda no Safari do iOS 15 ou mais novo (WebGL 2 e WebAssembly). Pelo Safari →
Compartilhar → **Adicionar à Tela de Início**, o jogo abre em tela cheia com ícone, como um app
(`web/manifest.webmanifest` e as metas `apple-mobile-web-app-*` do `index.html`). Toque, áudio (a chuva tem versão
`.m4a` para o Safari antigo, que não toca `.ogg`), microfone do chat de voz e o multiplayer são os mesmos do site.
No iPhone o jogo começa na qualidade Leve (como em todo aparelho de toque).

**App nativo na App Store (não dá para fazer daqui):** precisa de um Mac com Xcode e de uma conta Apple Developer
(US$ 99/ano). O caminho com menos trabalho é um app iOS com WKWebView abrindo o site (como o APK faz com a WebView),
porque o motor nativo usa OpenGL ES, que a Apple descontinuou (o caminho nativo completo seria portar o renderizador
para Metal). Pontos a conferir num app iOS: permissão de microfone (`NSMicrophoneUsageDescription`), áudio liberado
só depois de um toque e a política de compras da Apple (moedas compradas fora do app).
