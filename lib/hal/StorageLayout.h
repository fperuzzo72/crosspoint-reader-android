#pragma once
#include <BoardConfig.h>

// O layout do armazenamento, num lugar so.
//
// Tres caminhos e uma raiz. A raiz varia por aparelho (o cartao SD num ESP32,
// /mnt/us num Kindle, /sdcard/CrossPoint no HiBreak) e os tres caminhos abaixo
// sao sempre relativos a ela, resolvidos pelo shim de armazenamento.
//
//   <raiz>/.crosspoint   cache por livro, progresso, marcadores, credenciais
//   <raiz>/fonts         fontes instaladas (tambem /.fonts, oculto)
//   LIBRARY_ROOT         onde o navegador de arquivos comeca
//
// Os dois primeiros ja eram constantes espalhadas pela arvore e continuam
// onde estao; o que faltava era o terceiro ser nomeavel, porque o navegador
// comecava na raiz e via os outros dois ao lado dos livros.

namespace crosspoint::storage {

// Onde o navegador de arquivos comeca quando ninguem pede um caminho.
//
// Nos aparelhos de cartao SD a raiz E a biblioteca: o cartao e do leitor e
// mais nada escreve nele. No HiBreak a raiz e uma pasta dentro do
// armazenamento compartilhado do telefone, e misturar EPUB com cache e fonte
// na mesma listagem e ruim por dois motivos: o cache cresce e polui a
// navegacao, e o usuario copia livro para dentro por LocalSend sem saber onde
// e seguro soltar.
#if FREEINK_DEVICE_HIBREAK
inline constexpr const char* LIBRARY_ROOT = "/books";
#else
inline constexpr const char* LIBRARY_ROOT = "/";
#endif

}  // namespace crosspoint::storage
