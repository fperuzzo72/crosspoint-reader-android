// O proxy Serial do Logging.h, em unidade de traducao propria.
//
// MySerialImpl e declarado em lib/Logging/Logging.h e NAO e definido em lugar
// nenhum desta arvore: nem o membro estatico `instance`, nem write(), nem
// flush(), nem printf(). O unico membro com corpo e o operator bool(), inline
// no header.
//
// Isso nao e do porte. src/main.cpp:813 tem `if (Serial && ...)` fora de
// qualquer guarda, entao a referencia e emitida sempre; o que variava era se
// algum lugar do build de firmware a satisfazia. Neste repositorio nao ha
// esse lugar, e o linker do NDK cobra.
//
// Por que um arquivo so para isto: o Logging.h termina com
// `#define Serial MySerialImpl::instance`. Incluir esse header dentro do
// ArduinoShim.cpp faz o `HardwareSerial Serial;` de la virar
// `HardwareSerial MySerialImpl::instance;` e o compilador acusa redefinicao
// com tipo diferente. Os dois nao podem coexistir na mesma unidade, entao
// cada um fica na sua.

#include <Logging.h>

#include <cstdarg>
#include <cstdio>

MySerialImpl MySerialImpl::instance;

size_t MySerialImpl::write(const uint8_t b) {
  std::fputc(b, stderr);
  return 1;
}

size_t MySerialImpl::write(const uint8_t* buffer, const size_t size) {
  if (buffer == nullptr || size == 0) {
    return 0;
  }
  return std::fwrite(buffer, 1, size, stderr);
}

void MySerialImpl::flush() { std::fflush(stderr); }

size_t MySerialImpl::printf(const char* format, ...) {
  va_list args;
  va_start(args, format);
  const int written = std::vfprintf(stderr, format, args);
  va_end(args);
  return written < 0 ? 0 : static_cast<size_t>(written);
}
