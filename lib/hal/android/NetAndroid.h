#pragma once
#include <cstdint>

// Estado da rede pelo framework, e nao pelo getifaddrs.
//
// O motivo NAO e o que este comentario dizia antes. Eu havia escrito que o
// Android 11 fecha o acesso a NETLINK para aplicativo comum e que por isso o
// getifaddrs deixava de enxergar as interfaces. Medido no aparelho, com as
// duas respostas lado a lado no log, isso e falso: o getifaddrs funciona aqui.
//
// O motivo que sobra, e que e bom por si so: o ConnectivityManager distingue
// "tem endereco" de "tem internet". Um portal cativo de hotel da a primeira e
// nao a segunda, e NET_CAPABILITY_VALIDATED e a diferenca. O getifaddrs nao
// tem como responder isso.
//
// Fica registrado que a causa original do sintoma (a tela de escolha de rede
// abrindo com Wi-Fi ligado) continua sem explicacao. O conserto funciona; o
// diagnostico que eu dei para ele estava errado.

namespace crosspoint::android {

// true quando ha rede ativa E validada.
bool netIsOnline();

// IPv4 local em ordem de rede, igual ao sin_addr. Zero quando nao ha.
uint32_t netLocalIpV4();

}  // namespace crosspoint::android
