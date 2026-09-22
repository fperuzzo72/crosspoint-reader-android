// The Serial proxy from Logging.h, in its own translation unit.
//
// MySerialImpl is declared in lib/Logging/Logging.h and is NOT defined anywhere
// in this tree: not the static member `instance`, not write(), not flush(), not
// printf(). The only member with a body is the inline operator bool().
//
// That is not the port's doing. src/main.cpp:813 has `if (Serial && ...)`
// outside any guard, so the reference is always emitted; what varied was
// whether somewhere in the firmware build satisfied it. There is no such place
// in this repository, and the NDK's linker charges for it.
//
// Why a file just for this: Logging.h ends with
// `#define Serial MySerialImpl::instance`. Including that header inside
// ArduinoShim.cpp turns its `HardwareSerial Serial;` into
// `HardwareSerial MySerialImpl::instance;` and the compiler reports a
// redefinition with a different type. The two cannot coexist in one unit, so
// each gets its own.

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
