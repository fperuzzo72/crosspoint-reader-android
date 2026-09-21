#pragma once
#include <cstdint>

// Estado da rede pelo framework, e nao pelo getifaddrs.
//
// O shim POSIX pergunta ao getifaddrs se ha interface no ar com IPv4. Num
// Kindle isso responde certo; aqui nao, porque o Android 11 fechou o acesso a
// NETLINK para aplicativo comum e o getifaddrs passa a enxergar pouco mais que
// o loopback. O CrossPoint conclui que nao ha rede e abre a tela de escolha de
// Wi-Fi, que neste aparelho nao tem o que escolher.
//
// De quebra, o ConnectivityManager distingue "tem endereco" de "tem
// internet": um portal cativo da a primeira e nao a segunda.

namespace crosspoint::android {

// true quando ha rede ativa E validada.
bool netIsOnline();

// IPv4 local em ordem de rede, igual ao sin_addr. Zero quando nao ha.
uint32_t netLocalIpV4();

}  // namespace crosspoint::android
