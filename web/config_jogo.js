// Configuração do site: o mesmo Firebase do APK. servidor vazio = o próprio endereço do site (Render).
// As chaves web do Firebase são públicas por natureza; a segurança vem das regras do banco (veja o README).
// googleClientId: o "ID do cliente da Web" do login com Google (Firebase → Authentication → Google); vazio = sem o botão.
window.CONFIG_JOGO = {
  "firebase": {
    "apiKey": "AIzaSyDRup9wl0iBDq7g4vAQQJ1s6fcicsst1ys",
    "authDomain": "navegador-f06a8.firebaseapp.com",
    "databaseURL": "https://navegador-f06a8-default-rtdb.firebaseio.com",
    "projectId": "navegador-f06a8"
  },
  "servidor": "",
  "googleClientId": "55916264760-aiisdj12jfphvq35q3k8arhvl6o6ai1l.apps.googleusercontent.com"
};
