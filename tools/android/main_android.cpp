// Entry point for the trylink measurement, not for the app.
//
// CrossPoint is written against the Arduino model: the core supplies main(),
// calls setup() once and then loop() forever. Neither Android nor a plain Linux
// has that core, so somebody has to be it.
//
// This file is NOT the shipping path. An Android app does not enter through
// main(): it enters through an Activity. This exists so tools/android/trylink.sh
// can measure, because without a main() the link reports "undefined reference
// to main" and hides the number we actually want.
//
// The three decisions below outlive it, because none of them is about main():
//
//  - SIGPIPE ignored. Writing to a socket whose peer vanished kills a Linux
//    process by default. On an ESP32 that signal does not exist, so the tree
//    never had to defend against it.
//
//  - SIGINT and SIGTERM set a flag rather than killing, so the loop can leave
//    the screen in a readable state instead of freezing half a frame.
//
//  - stdout and stderr unbuffered, or a crash loses exactly the lines that
//    explain the crash.

#include <unistd.h>

#include <csignal>
#include <cstdio>

// Defined in src/main.cpp, shared with every other target.
void setup();
void loop();

namespace {
volatile sig_atomic_t g_stop = 0;
void onStop(int) { g_stop = 1; }
}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, onStop);
  signal(SIGTERM, onStop);

  setup();
  while (!g_stop) {
    loop();
  }
  return 0;
}
