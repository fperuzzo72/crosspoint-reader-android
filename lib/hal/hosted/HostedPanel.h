#pragma once
#include <BoardConfig.h>

// Seletor do painel para os alvos hospedados.
//
// Kindle e HiBreak nao tem nada em comum no hardware: um fala com o EPDC do
// kernel por /dev/fb0, o outro entrega pixels ao SurfaceFlinger. Mas a FORMA
// que o HalDisplay precisa e a mesma (compoe um quadro, encena, apresenta),
// e por isso os dois compartilham HalDisplayHosted.cpp em vez de terem uma
// copia de 345 linhas cada.
//
// Um alvo hospedado novo entra aqui e implementa a mesma superficie de
// metodos. Nao ha classe base virtual de proposito: a escolha e em tempo de
// compilacao, um binario por aparelho, e uma vtable so pagaria indirecao por
// uma decisao que o preprocessador ja tomou.

#include "HostedTouch.h"

#if FREEINK_DEVICE_KINDLE
#include "kindle/KindleFrameBuffer.h"
#include "kindle/KindleTouch.h"
namespace crosspoint::hosted {
using Panel = kindle::KindleFrameBuffer;
using Waveform = kindle::Waveform;
// PENDENTE no repositorio Kindle: o KindleTouch.h ainda declara Gesture,
// GestureResult e TouchTuning dentro de crosspoint::kindle, e o HostedTouch.h
// declara os mesmos tres em crosspoint::hosted. Enquanto o backport nao mover
// os do Kindle para ca, este ramo nao compila. Esta assim de proposito, em vez
// de aliases que escondessem a duplicata: os dois alvos tem que falar UMA
// lingua de gesto, nao duas identicas.
using Touch = kindle::KindleTouchDevice;
inline constexpr uint16_t PANEL_WIDTH = kindle::KT3_WIDTH;
inline constexpr uint16_t PANEL_HEIGHT = kindle::KT3_HEIGHT;
}  // namespace crosspoint::hosted

#elif FREEINK_DEVICE_HIBREAK
#include "android/AndroidPanel.h"
#include "android/AndroidTouchDevice.h"
namespace crosspoint::hosted {
using Panel = android::AndroidPanel;
using Waveform = android::Waveform;
using Touch = android::AndroidTouchDevice;
inline constexpr uint16_t PANEL_WIDTH = android::HIBREAK_WIDTH;
inline constexpr uint16_t PANEL_HEIGHT = android::HIBREAK_HEIGHT;
}  // namespace crosspoint::hosted
#endif
