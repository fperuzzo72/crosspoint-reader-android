// Ponto de entrada provisorio do build Android.
//
// O CrossPoint e escrito contra o modelo Arduino: o core fornece main(), chama
// setup() uma vez e loop() para sempre. Nem o Android nem um Linux qualquer tem
// esse core, entao alguem precisa ser ele.
//
// Este arquivo NAO e a forma final. Um aplicativo Android nao entra por main():
// entra por uma Activity, ou por android_main() se for NativeActivity. Isto
// aqui existe para o trylink poder medir, porque sem um main() o link reporta
// "undefined reference to main" e esconde o numero que a gente quer.
//
// Quando a camada Android de verdade existir, ela substitui este arquivo, e as
// tres decisoes abaixo continuam valendo porque nenhuma delas e sobre main():
//
//  - SIGPIPE ignorado. Escrever num socket cujo par sumiu mata um processo
//    Linux por padrao. Num ESP32 esse sinal nao existe, entao a arvore nunca
//    precisou se defender disso.
//
//  - SIGINT e SIGTERM marcam uma flag em vez de matar, para o loop conseguir
//    deixar a tela num estado legivel em vez de congelar meio quadro.
//
//  - stdout e stderr sem buffer, senao um crash perde exatamente as linhas que
//    explicam o crash.

#include <csignal>
#include <cstdio>
#include <unistd.h>

// Definidos em src/main.cpp, compartilhado com todos os outros alvos.
void setup();
void loop();

namespace {
volatile sig_atomic_t g_stop = 0;
void onStop(int) { g_stop = 1; }
}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, onStop);
  signal(SIGTERM, onStop);

  setup();
  while (!g_stop) {
    loop();
  }
  return 0;
}
