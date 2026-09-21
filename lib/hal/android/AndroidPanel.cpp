#include "AndroidPanel.h"

#include <android/native_window.h>

#include <cstdlib>
#include <cstring>

namespace crosspoint::android {

namespace {
constexpr size_t STAGE_BYTES = static_cast<size_t>(HIBREAK_WIDTH) * HIBREAK_HEIGHT;
}  // namespace

AndroidPanel& AndroidPanel::instance() {
  static AndroidPanel panel;
  return panel;
}

AndroidPanel::~AndroidPanel() { end(); }

bool AndroidPanel::begin() {
  std::lock_guard<std::mutex> lock(mtx);
  if (stage == nullptr) {
    stage = static_cast<uint8_t*>(std::malloc(STAGE_BYTES));
    if (stage == nullptr) {
      return false;
    }
    std::memset(stage, hosted::GRAY_WHITE, STAGE_BYTES);
    stageHasContent = false;
  }
  return true;
}

void AndroidPanel::end() {
  std::lock_guard<std::mutex> lock(mtx);
  if (win != nullptr) {
    ANativeWindow_release(win);
    win = nullptr;
  }
  std::free(stage);
  stage = nullptr;
  stageHasContent = false;
}

bool AndroidPanel::reopen() {
  // Nao ha conexao para reestabelecer: a superficie vai e volta por conta
  // propria, pelo attachSurface. Isto existe para o HalDisplay hospedado
  // poder chamar a mesma coisa nos dois alvos.
  std::lock_guard<std::mutex> lock(mtx);
  return stage != nullptr;
}

void AndroidPanel::attachSurface(ANativeWindow* window) {
  std::lock_guard<std::mutex> lock(mtx);
  if (win != nullptr) {
    ANativeWindow_release(win);
  }
  win = window;
  if (win == nullptr) {
    return;
  }
  ANativeWindow_acquire(win);
  // A geometria e fixada no tamanho do painel para o compositor nao escalar:
  // uma pagina de texto em 1bpp reamostrada perde exatamente a nitidez que o
  // e-ink existe para dar.
  ANativeWindow_setBuffersGeometry(win, HIBREAK_WIDTH, HIBREAK_HEIGHT, WINDOW_FORMAT_RGBX_8888);
  // A superficie pode ter chegado depois do quadro. Reapresenta em vez de
  // deixar o buffer novo com o que o compositor tiver posto nele.
  if (stageHasContent) {
    present();
  }
}

void AndroidPanel::detachSurface() {
  std::lock_guard<std::mutex> lock(mtx);
  if (win != nullptr) {
    ANativeWindow_release(win);
    win = nullptr;
  }
}

bool AndroidPanel::stageFrameLocked(const uint8_t* frame) {
  if (frame == nullptr || stage == nullptr) {
    return false;
  }
  hosted::expand1bppToGray8(frame, stage, HIBREAK_WIDTH, HIBREAK_HEIGHT, HIBREAK_WIDTH_BYTES, HIBREAK_WIDTH);
  stageHasContent = true;
  return true;
}

bool AndroidPanel::stageFrame(const uint8_t* frame) {
  std::lock_guard<std::mutex> lock(mtx);
  return stageFrameLocked(frame);
}

bool AndroidPanel::stageGrayOverlay(const uint8_t* lsbPlane, const uint8_t* msbPlane) {
  std::lock_guard<std::mutex> lock(mtx);
  if (stage == nullptr) {
    return false;
  }
  hosted::overlayGrayPlanesOnGray8(lsbPlane, msbPlane, stage, HIBREAK_WIDTH, HIBREAK_HEIGHT, HIBREAK_WIDTH_BYTES,
                                   HIBREAK_WIDTH);
  return true;
}

bool AndroidPanel::refresh(const Waveform waveform) {
  std::lock_guard<std::mutex> lock(mtx);
  lastWaveform = waveform;
  return present();
}

bool AndroidPanel::display(const uint8_t* frame, const Waveform waveform) {
  std::lock_guard<std::mutex> lock(mtx);
  if (!stageFrameLocked(frame)) {
    return false;
  }
  lastWaveform = waveform;
  return present();
}

bool AndroidPanel::displayStart(const uint8_t* frame, const Waveform waveform) {
  display(frame, waveform);
  // Nada em voo que valha esperar; ver o comentario da declaracao.
  return false;
}

uint8_t AndroidPanel::peekPixel(const uint16_t x, const uint16_t y) const {
  std::lock_guard<std::mutex> lock(mtx);
  if (stage == nullptr || x >= HIBREAK_WIDTH || y >= HIBREAK_HEIGHT) {
    return 0;
  }
  return stage[static_cast<size_t>(y) * HIBREAK_WIDTH + x];
}

bool AndroidPanel::present() {
  if (win == nullptr || stage == nullptr) {
    // Sem superficie nao e erro: a Activity pode estar pausada e o quadro
    // fica guardado para quando ela voltar.
    return true;
  }
  ANativeWindow_Buffer buf;
  if (ANativeWindow_lock(win, &buf, nullptr) != 0) {
    return false;
  }
  // Se o compositor devolveu geometria diferente da pedida, respeita a dele e
  // pinta so a interseccao: escrever pelo tamanho que pedimos estouraria o
  // buffer que ele deu.
  const int32_t w = buf.width < HIBREAK_WIDTH ? buf.width : HIBREAK_WIDTH;
  const int32_t h = buf.height < HIBREAK_HEIGHT ? buf.height : HIBREAK_HEIGHT;
  auto* pixels = static_cast<uint8_t*>(buf.bits);
  for (int32_t y = 0; y < h; ++y) {
    const uint8_t* src = stage + static_cast<size_t>(y) * HIBREAK_WIDTH;
    // buf.stride e em PIXELS para os formatos de 32 bits, nao em bytes.
    uint32_t* out = reinterpret_cast<uint32_t*>(pixels + static_cast<size_t>(y) * buf.stride * 4);
    for (int32_t x = 0; x < w; ++x) {
      const uint32_t g = src[x];
      // RGBX_8888 em little endian: R no byte baixo, X no alto. Cinza vai nos
      // tres canais; o X e ignorado mas preenchido com 0xFF por higiene.
      out[x] = 0xFF000000u | (g << 16) | (g << 8) | g;
    }
  }
  ANativeWindow_unlockAndPost(win);
  return true;
}

}  // namespace crosspoint::android
