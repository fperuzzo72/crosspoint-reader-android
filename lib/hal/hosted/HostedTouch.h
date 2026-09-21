#pragma once
#include <cstdint>

// Vocabulario de toque dos alvos hospedados.
//
// Identico ao que o backend do Kindle ja usava, movido para ca porque nao tem
// nada de Kindle nele: e como o HalGPIO fala de gestos, e os dois aparelhos
// falam a mesma lingua mesmo classificando por caminhos completamente
// diferentes (evdev cru de um lado, GestureDetector do Android do outro).

namespace crosspoint::hosted {

enum class Gesture : uint8_t {
  None,
  // Dispara na soltura: contato breve que ficou parado.
  Tap,
  // Dispara ENQUANTO o dedo ainda esta em baixo, ao passar o limiar. O resto
  // daquele contato e suprimido, senao a soltura tambem viraria toque e
  // fecharia o que o toque longo acabou de abrir.
  LongPress,
  // Dispara na soltura: contato que andou o bastante para ser arrasto.
  Swipe,
};

struct GestureResult {
  Gesture kind = Gesture::None;
  // Normalizado em 0..1 para o chamador nunca ver pixel de painel. Num swipe,
  // start e onde o dedo pousou e end e onde saiu.
  float nx = 0.0F;
  float ny = 0.0F;
  float nxEnd = 0.0F;
  float nyEnd = 0.0F;
  uint32_t heldMs = 0;

  explicit operator bool() const { return kind != Gesture::None; }
};

struct TouchTuning {
  uint16_t slopPx = 20;
  uint16_t swipeMinPx = 45;
  uint32_t longPressMs = 550;
  uint32_t tapMaxMs = 500;
};

}  // namespace crosspoint::hosted
