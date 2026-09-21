#pragma once

// Toque do HiBreak: uma caixa de correio, nao um classificador.
//
// No Kindle o backend le o stream evdev cru e classifica toque, toque longo e
// swipe ele mesmo, porque ninguem mais vai fazer isso. No Android essa peca ja
// existe e e melhor do que a que escreveriamos: o GestureDetector do framework
// conhece o slop do aparelho, o limiar de toque longo do sistema e a
// velocidade de fling. Entao a classificacao acontece em Kotlin e o que
// atravessa o JNI e o gesto ja pronto.
//
// O que sobra aqui e o encaixe de threads: a Activity entrega gestos na thread
// de UI e o CrossPoint os consome na dele. Um gesto por vez, o mais recente
// vence, que e o mesmo contrato de borda que o HalGPIO ja espera do update().

#include <cstdint>
#include <mutex>

#include "hosted/HostedTouch.h"

namespace crosspoint::android {

using hosted::Gesture;
using hosted::GestureResult;
using hosted::TouchTuning;

class AndroidTouchDevice {
 public:
  bool begin(uint16_t panelWidth, uint16_t panelHeight, TouchTuning tuning = {});
  void end();
  bool isOpen() const { return open; }
  bool reopen() { return open; }

  // Consome o gesto pendente, se houver. timeoutMs e ignorado: nao ha
  // descritor para esperar, o Kotlin empurra quando acontece.
  GestureResult update(int timeoutMs = 0);

  void suppressContact();
  bool isContactDown() const;

  // Chamados do JNI, na thread de UI.
  void postGesture(const GestureResult& g);
  void setContactDown(bool down);

  static AndroidTouchDevice& instance();

 private:
  mutable std::mutex mtx;
  GestureResult pending{};
  bool open = false;
  bool contactDown = false;
  // Um toque longo ja foi entregue neste contato: tudo ate a soltura e
  // descartado, senao a soltura viraria toque tambem.
  bool suppressed = false;
};

}  // namespace crosspoint::android
