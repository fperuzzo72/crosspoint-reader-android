#pragma once

// Backend de display do Bigme HiBreak Pro.
//
// Nos alvos ESP32 o FreeInk dirige um painel cru por SPI/i80. No Kindle o EPDC
// do kernel e dono do painel e o userspace ganha um /dev/fb0. Aqui nem isso:
// a superficie vem do SurfaceFlinger, entregue por uma Activity Kotlin via
// JNI, e quem decide o waveform e o framework xrz do fabricante.
//
// O que isso muda em relacao aos outros dois: a superficie pode NAO EXISTIR a
// qualquer momento. Uma Activity Android e pausada e destruida sob os pes do
// processo, e a thread do CrossPoint continua viva e pintando. Entao cada
// metodo aqui trata "sem superficie" como estado normal e nao como erro, e o
// quadro composto sobrevive em `stage` para ser reapresentado quando a
// superficie voltar.

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "hosted/HostedGray.h"

struct ANativeWindow;

namespace crosspoint::android {

// Medidos no aparelho, nao supostos: 824x1648 a 300 dpi.
// 824 / 8 = 103 exato, entao nao ha padding de linha na origem.
inline constexpr uint16_t HIBREAK_WIDTH = 824;
inline constexpr uint16_t HIBREAK_HEIGHT = 1648;
inline constexpr uint16_t HIBREAK_WIDTH_BYTES = HIBREAK_WIDTH / 8;
inline constexpr uint32_t HIBREAK_BUFFER_SIZE = static_cast<uint32_t>(HIBREAK_WIDTH_BYTES) * HIBREAK_HEIGHT;

// Espelha HalDisplay::RefreshMode para o HalDisplay hospedado poder repassar o
// argumento direto. Na v1 o valor e registrado e nao usado: o caminho e o
// Android padrao e o sistema escolhe o waveform. Fica aqui porque o
// XrzEinkManager e uma chamada reflexiva de distancia e a escada de modos ja
// esta levantada (177/178/179/180).
enum class Waveform : uint8_t { Full, Half, Fast };

class AndroidPanel {
 public:
  AndroidPanel() = default;
  ~AndroidPanel();

  AndroidPanel(const AndroidPanel&) = delete;
  AndroidPanel& operator=(const AndroidPanel&) = delete;

  // Aloca o quadro de composicao. Nao espera superficie: ela chega quando a
  // Activity resolver, e ate la tudo compoe normalmente e nada aparece.
  bool begin();
  void end();
  bool isOpen() const { return stage != nullptr; }

  uint16_t width() const { return HIBREAK_WIDTH; }
  uint16_t height() const { return HIBREAK_HEIGHT; }

  bool display(const uint8_t* frame, Waveform waveform);
  // Sem marcador de conclusao do lado do Android: o post e assincrono mas nao
  // ha o que esperar. Sempre retorna false, que no contrato do HalDisplay
  // significa "terminou inline, nao chame waitComplete".
  bool displayStart(const uint8_t* frame, Waveform waveform);
  bool reopen();

  bool stageFrame(const uint8_t* frame);
  bool stageGrayOverlay(const uint8_t* lsbPlane, const uint8_t* msbPlane);
  bool refresh(Waveform waveform);

  // Ninguem mais pinta a nossa Surface: o compositor e quem compoe, e o que
  // ele poe por cima nao substitui os nossos pixels no nosso buffer. A
  // pergunta que isto responde no Kindle (o framework apagou a tela que
  // dividimos?) nao existe aqui.
  bool panelContentWasReplaced() const { return false; }

  uint8_t peekPixel(uint16_t x, uint16_t y) const;
  void waitComplete() {}
  void deepSleep() {}

  // Chamados do JNI, na thread da Activity, nao na do CrossPoint.
  // Tomam o mesmo mutex que a apresentacao.
  void attachSurface(ANativeWindow* window);
  void detachSurface();

  // Instancia unica: o JNI precisa alcancar o painel sem carregar um ponteiro
  // pela cadeia inteira do HalDisplay.
  static AndroidPanel& instance();

 private:
  // Os tres assumem o mutex ja tomado. display() precisa compor e apresentar
  // sob UM unico lock: entre as duas metades a Activity pode trocar a
  // superficie, e o quadro sairia pela metade.
  bool stageFrameLocked(const uint8_t* frame);
  bool present();

  mutable std::mutex mtx;
  ANativeWindow* win = nullptr;
  // Quadro composto em 8bpp cinza, do tamanho do painel. 824*1648 = 1,36MB,
  // irrelevante num telefone e o que permite stageFrame e stageGrayOverlay
  // chegarem ao painel numa apresentacao so, como no Kindle.
  uint8_t* stage = nullptr;
  Waveform lastWaveform = Waveform::Half;
  // A superficie apareceu depois do ultimo quadro composto: o proximo
  // attachSurface reapresenta em vez de deixar a tela com lixo.
  bool stageHasContent = false;
};

}  // namespace crosspoint::android
