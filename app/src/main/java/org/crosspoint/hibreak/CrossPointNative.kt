package org.crosspoint.hibreak

import android.view.Surface

/**
 * A fronteira com o C++, e nada alem dela.
 *
 * Quatro travessias, todas em uma direcao: Kotlin chama, C++ obedece. O
 * CrossPoint nunca chama de volta. Isso nao e limitacao, e o que mantem a
 * ponte legivel: nao ha JNIEnv guardado, nao ha callback atravessando thread,
 * e nada aqui precisa saber em que thread o leitor esta rodando.
 *
 * A divisao de trabalho por tras disso: o Kotlin fica com o que o Android faz
 * melhor do que nos (ciclo de vida, Surface, classificacao de gesto com os
 * limiares do proprio aparelho) e o C++ fica com o CrossPoint inteiro, que nao
 * sabe nada disso e nao precisa saber.
 */
object CrossPointNative {

  init {
    System.loadLibrary("crosspoint")
  }

  /**
   * Entrega ou tira a superficie de desenho.
   *
   * Passar `null` e o caminho NORMAL de pausa, nao um erro: uma Activity e
   * destruida e recriada varias vezes na vida do processo, e a thread do
   * leitor continua viva o tempo todo. O lado C++ guarda o ultimo quadro
   * composto e reapresenta quando a superficie volta.
   */
  @JvmStatic external fun nativeSetSurface(surface: Surface?)

  /**
   * Um gesto ja classificado. [kind] casa com `crosspoint::hosted::Gesture`:
   * 0 nenhum, 1 toque, 2 toque longo, 3 swipe.
   *
   * Coordenadas normalizadas em 0..1. O C++ nunca ve pixel de tela, o que e o
   * mesmo contrato que o backend do Kindle ja usava e o que deixa o leitor
   * indiferente a resolucao do painel.
   */
  @JvmStatic external fun nativeGesture(
    kind: Int,
    nx: Float,
    ny: Float,
    nxEnd: Float,
    nyEnd: Float,
    heldMs: Int,
  )

  /** Dedo encostado ou nao, independente de o contato ja ter virado um gesto. */
  @JvmStatic external fun nativeContact(down: Boolean)

  /**
   * A raiz do armazenamento, em caminho absoluto.
   *
   * Tem de ser chamada ANTES de [nativeStart]: o `setup()` do CrossPoint ja
   * monta o navegador de arquivos e le a biblioteca, e uma raiz errada nessa
   * hora e uma biblioteca vazia.
   *
   * Quem decide e este lado, porque so o framework sabe qual e o diretorio
   * deste aplicativo.
   */
  @JvmStatic external fun nativeSetStorageRoot(path: String)

  /**
   * Sobe a thread do leitor. Idempotente de proposito: a Activity pode ser
   * recriada (rotacao, mudanca de configuracao) sem o processo morrer, e
   * reiniciar o CrossPoint perderia a posicao de leitura.
   */
  @JvmStatic external fun nativeStart()

  // Espelha crosspoint::hosted::Gesture. Um enum Kotlin com valor explicito em
  // vez de numeros soltos nas chamadas: o dia em que o lado C++ ganhar um
  // gesto novo, o compilador daqui nao vai perceber sozinho, mas pelo menos o
  // lugar a corrigir e um so.
  enum class Gesture(val code: Int) {
    NONE(0),
    TAP(1),
    LONG_PRESS(2),
    SWIPE(3),
  }
}
