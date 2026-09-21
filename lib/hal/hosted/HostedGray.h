#pragma once
#include <cstdint>

// A expansao 1bpp -> 8bpp cinza, e a composicao dos dois planos de overlay.
//
// Nao dependem de hardware nenhum, e por isso sao a unica parte de um backend
// hospedado que um teste de host consegue cobrar. Vivem aqui, e nao dentro do
// backend de um aparelho, porque Kindle e Android precisam exatamente das
// mesmas duas funcoes.
//
// Contrato do framebuffer, casando com o HalDisplay: o CrossPoint compoe num
// buffer 1bpp empacotado, linhas alinhadas em byte a widthBytes cada, MSB = o
// pixel mais a esquerda, bit SET = BRANCO (o clearScreen enche com 0xFF).

namespace crosspoint::hosted {

inline constexpr uint8_t GRAY_BLACK = 0x00;
inline constexpr uint8_t GRAY_WHITE = 0xFF;
// Os dois niveis intermediarios que os planos de cinza do renderer expressam.
inline constexpr uint8_t GRAY_DARK = 0x55;
inline constexpr uint8_t GRAY_LIGHT = 0xAA;

// dstRowBytes e o stride do destino, que pode ser maior que a largura: no KT3
// o framebuffer reporta 608 bytes por linha num painel de 600px, e avancar por
// largura em vez de stride cisalha a imagem progressivamente.
//
// Bits alem de `width` no ultimo byte de origem de cada linha sao padding e
// nao sao emitidos, entao largura que nao e multipla de 8 continua correta.
void expand1bppToGray8(const uint8_t* src, uint8_t* dst, uint16_t width, uint16_t height, uint16_t srcRowBytes,
                       uint32_t dstRowBytes);

// Pinta os dois planos 1bpp de cinza sobre um quadro 8bpp que ja tem a base
// preto-e-branco. O renderer codifica um pixel cinza como um par de bits, um
// de cada plano, como (LSB, MSB):
//
//   (0,0)  nao e cinza; o que a base pintou vale
//   (1,1)  escuro
//   (0,1)  claro
//   (1,0)  a codificacao nao produz; fica com a base em vez de ser chutado
//
// Pixels que os planos nao reivindicam nao sao escritos, por isso `dst` e
// entrada e saida.
void overlayGrayPlanesOnGray8(const uint8_t* lsbPlane, const uint8_t* msbPlane, uint8_t* dst, uint16_t width,
                              uint16_t height, uint16_t srcRowBytes, uint32_t dstRowBytes);

}  // namespace crosspoint::hosted
