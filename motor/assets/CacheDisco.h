#pragma once
// Cache em disco do que é caro de preparar e sempre sai igual (imagens das armas já reduzidas, malhas já
// simplificadas): na primeira vez o motor prepara e grava; nas próximas, até depois de fechar o app, só lê.
// Cada arquivo guarda o tamanho e a data do arquivo de origem: se o jogo atualizar a arma, a cópia é refeita.
// No navegador (WebAssembly) não há disco que fique: o cache fica desligado.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifndef __EMSCRIPTEN__
#include <dirent.h>
#endif

namespace eng {

class CacheDisco {
public:
    static CacheDisco& instance() { static CacheDisco c; return c; }

    void configurar(const std::string& pasta) {
#ifdef __EMSCRIPTEN__
        (void)pasta;
#else
        if (pasta.empty() || std::getenv("SEM_CACHE")) return;
        ::mkdir(pasta.c_str(), 0755);
        struct stat st;
        if (::stat(pasta.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) dir_ = pasta;
        else return;
        // trocar o nível gráfico guarda outra versão das imagens; se a pasta passar do limite, começa do zero
        std::vector<std::string> arquivos;
        uint64_t total = 0;
        if (DIR* d = ::opendir(dir_.c_str())) {
            while (dirent* e = ::readdir(d)) {
                const std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                const std::string p = dir_ + "/" + n;
                if (::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode)) { total += uint64_t(st.st_size); arquivos.push_back(p); }
            }
            ::closedir(d);
        }
        if (total > kLimite)
            for (const std::string& p : arquivos) std::remove(p.c_str());
#endif
    }
    bool ligado() const { return !dir_.empty(); }

    // lê o conteúdo guardado para a chave, se ainda vale para o arquivo de origem
    bool ler(const std::string& chave, const std::string& origem, std::vector<uint8_t>& dados) const {
        if (!ligado()) return false;
        Cab cab;
        if (!cabecalho(origem, cab)) return false;
        FILE* f = std::fopen(arquivo(chave).c_str(), "rb");
        if (!f) return false;
        Cab lido;
        bool ok = std::fread(&lido, sizeof(lido), 1, f) == 1 && std::memcmp(&lido, &cab, sizeof(cab)) == 0;
        if (ok) {
            uint64_t n = 0;
            ok = std::fread(&n, sizeof(n), 1, f) == 1 && n < (1ull << 31);
            if (ok) {
                dados.resize(size_t(n));
                ok = n == 0 || std::fread(dados.data(), 1, size_t(n), f) == size_t(n);
            }
        }
        std::fclose(f);
        return ok;
    }

    // grava (num arquivo temporário renomeado no fim: um app fechado no meio não deixa arquivo pela metade)
    void gravar(const std::string& chave, const std::string& origem, const void* dados, size_t n) const {
        if (!ligado()) return;
        Cab cab;
        if (!cabecalho(origem, cab)) return;
        const std::string final_ = arquivo(chave), tmp = final_ + ".tmp";
        FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) return;
        const uint64_t nn = n;
        bool ok = std::fwrite(&cab, sizeof(cab), 1, f) == 1 && std::fwrite(&nn, sizeof(nn), 1, f) == 1 &&
                  (n == 0 || std::fwrite(dados, 1, n, f) == n);
        ok = std::fclose(f) == 0 && ok;
        if (ok) std::rename(tmp.c_str(), final_.c_str());
        else std::remove(tmp.c_str());
    }

private:
    struct Cab { char magica[4]; uint32_t versao; int64_t tamanho, data; };
    static constexpr uint32_t kVersao = 1;
    static constexpr uint64_t kLimite = 150ull << 20;  // 150 MB  // muda se o formato dos dados guardados mudar

    static bool cabecalho(const std::string& origem, Cab& c) {
        struct stat st;
        if (::stat(origem.c_str(), &st) != 0) return false;
        std::memset(&c, 0, sizeof(c));
        std::memcpy(c.magica, "FSC1", 4);
        c.versao = kVersao;
        c.tamanho = int64_t(st.st_size);
        c.data = int64_t(st.st_mtime);
        return true;
    }
    std::string arquivo(const std::string& chave) const {
        uint64_t h = 1469598103934665603ull;  // FNV-1a da chave (caminho + peça + nível)
        for (unsigned char ch : chave) { h ^= ch; h *= 1099511628211ull; }
        char nome[32];
        std::snprintf(nome, sizeof(nome), "%016llx.bin", (unsigned long long)h);
        return dir_ + "/" + nome;
    }
    std::string dir_;
};

}  // namespace eng
