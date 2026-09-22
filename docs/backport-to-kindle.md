# What should go back to the Kindle port

This repository was born as a copy of `crosspoint-reader-kindle` at `f002274b`.
That port paid the cost of taking CrossPoint off the ESP32; this one inherits
the work. When something fixed here belongs to the POSIX layer rather than to
Android, it belongs to both, and this is the ledger.

## When to guard by device, and when not

Decided: this port exists for **one** device. There is no ambition of
generality, because the hardware diverges too far between an e-ink phone, a
Kindle and an ESP32 for an abstraction to be worth what it costs.

That does not mean everything goes behind `#if FREEINK_DEVICE_HIBREAK`. The
criterion is what would break *inside this tree*, where the Kindle branch
compiles from the same code:

- **Guarded** when the right value here would be wrong for the Kindle: font
  sizes, margin bounds, the storage root, the refresh ladder.
- **Unguarded** when it is a fix that holds anywhere. The status bar aligning
  with the text column is the case: the user changes a setting called "screen
  margin" expecting it to apply to the whole page, and the bar ignored it.
  Guarding that would pretend a correction is a local preference.

When in doubt, unguarded and noted here. One guard too many hides a fix; one fix
too many shows up in the diff and somebody argues about it.

## The structural item

In the Kindle repository the whole shim lives in `lib/hal/kindle/`, mixing two
things of different nature: what is **generic POSIX** and holds on any Unix
(`String`, `millis()`, FreeRTOS over pthreads, SdFat over POSIX, sockets, the
HTTP server, MD5, base64, and the whole `arduino-shim/`), and what is **the
Kindle's** and holds nowhere else (the framebuffer over `/dev/fb0` via FBInk,
touch over the `zforce2` evdev stream, the 1bpp to 8bpp expansion in the format
the EPDC wants).

Here they are separated:

| Here | There | Nature |
| --- | --- | --- |
| `lib/hal/posix/` (5,621 lines) | `lib/hal/kindle/` | reusable |
| `lib/hal/kindle/` (only the six `Kindle*` files) | same | device-specific |
| `lib/hal/android/` | does not exist | device-specific |

And three files changed name and guard, which holds there too:

| Kindle today | Here | Guard |
| --- | --- | --- |
| `HalDisplayKindle.cpp` | `HalDisplayHosted.cpp` | `FREEINK_MCU_HOSTED` |
| `HalGPIOKindle.cpp` | `HalGPIOHosted.cpp` | same |
| `HalSystemKindle.cpp` | `HalSystemHosted.cpp` | same |

None of them got more complicated: the guard stopped naming a device and started
naming the family BoardConfig already derived, and the panel type moved out of
`crosspoint::kindle` into an alias in `lib/hal/hosted/HostedPanel.h`.

**A trap the backport will hit:** `FREEINK_MCU_HOSTED` is *derived* inside
`BoardConfig.h`. Testing it before including that header reads zero and picks
the wrong branch, and the error only surfaces dozens of lines later as
`undeclared identifier`. With `FREEINK_DEVICE_KINDLE` that never happened
because it comes from the command line. It bit `HalGPIO.h`, whose own comment
already recorded an earlier version of the same fall with `FREEINK_CAP_TOUCH`,
and then bit `CrossPointSettings.h`.

**Pending item that stops the Kindle branch compiling here:** `KindleTouch.h`
still declares `Gesture`, `GestureResult` and `TouchTuning` inside
`crosspoint::kindle`, and `lib/hal/hosted/HostedTouch.h` declares the same three
in `crosspoint::hosted`. That is deliberate rather than papered over with
aliases: the two targets must speak *one* gesture language, not two identical
ones. The backport moves the Kindle's into `HostedTouch.h`.

The same holds for `KindleGrayExpand.cpp`, whose two pure functions became
`lib/hal/hosted/HostedGray.cpp` with no change in logic.

## Classified findings

Each fix carries one of three marks:

- **POSIX** — holds on both, backport owed.
- **Android** — bionic, NDK, lifecycle or sandbox. Stays here.
- **Upstream** — a CrossPoint or freeink-sdk bug that should go to the origin
  repository, not just to the Kindle.

| Mark | What |
| --- | --- |
| **POSIX** | `esp_restart()`, the ESP-IDF free function alongside the `ESP.restart()` that already existed. The SDK's `RecoveryBoot` and `MemoryManager` call this one. |
| **POSIX** | `StaticTask_t`, opaque, only so `sizeof()` compiles. `xTaskCreateStatic` stays deliberately absent, so anyone genuinely trying static task creation breaks at link and not in silence. |
| **POSIX** | Inert `adc_attenuation_t` and `analogSetAttenuation()`, next to the `analogRead()` that was already inert. |
| **POSIX** | `ARDUINOJSON_ENABLE_ARDUINO_STRING=1`. Without it ArduinoJson does not detect an Arduino environment, falls back to `std::string`, and every `as<String>()` fails. The Kindle has the same shim `String` and the same problem. |
| **POSIX** | `MySerialImpl` is declared in `lib/Logging/Logging.h` and **defined nowhere in the tree**: not the static member, not `write()`, not `flush()`, not `printf()`. `src/main.cpp:813` references `Serial` outside any guard. Defined here in `lib/hal/posix/SerialProxyPosix.cpp`, which must be its own translation unit because of the `#define Serial` at the end of `Logging.h`. |
| **POSIX** | The guards in `HalGPIO.h`, `HalSystem.cpp` and `HalGPIO.cpp` said `FREEINK_DEVICE_KINDLE` where they meant `FREEINK_MCU_HOSTED`. It worked only because there was one hosted target. |
| **Android** | An 8MB stack for the reader thread. Does not apply to the Kindle, where `main()` runs on the process's main thread, which already has a large stack. But the *lesson* applies: any hosted target creating the reader thread by hand must size it. |
| **Upstream** | `HomeActivity.h` forward-declared `struct RecentBook;` and had a `std::vector<RecentBook>` as a member. Ill-formed: instantiating a vector's members requires a complete type. The ESP32's libstdc++ accepts it, libc++ refuses. |
| **Upstream** | The size-to-ID `switch` in `CrossPointSettings.cpp` is a hand-maintained mirror of `BUILTIN_READER_POINT_SIZES`. A size present in one list and absent from the other falls to `default` and draws at the wrong size without complaining. |
| **Upstream** | `FirmwareBoardTag.cpp` has an `#error` for an unknown device, so every new target needs an entry. Correct, and worth recording that it is deliberate. |
| **Upstream** | `build-font-ids.sh` had a ruby block copied per size. It became a loop when the list went from four sizes to six and the copy grew larger than the logic. |

## Counter-current

Things the Kindle solved that need rethinking here, not copying:

- **`arduino-shim/WiFi.h`** reads the SSID through wireless extensions
  (`SIOCGIWESSID`) and RSSI through `/proc/net/wireless`. Wireless extensions
  are dead on modern Android and `/proc/net/wireless` is not readable by an
  ordinary app. Here the network state comes from `ConnectivityManager`, which
  additionally distinguishes "has an address" from "has internet".
- **TLS** is unimplemented in the shim. On the Kindle that rules out OPDS and
  KOReader sync in practice. Here it crosses into Kotlin, where the system trust
  store already exists.
- **`ESP.restart()`** re-execs the process on the Kindle. On Android restarting
  a process is not the same as restarting the Activity, and the right semantics
  are still undecided.

## Build traps that hold for both

- **A quoted `-D` does not survive being written into a generated script.** The
  inner shell eats the quotes and the macro becomes an identifier. Defines go in
  a header via `-include`.
- **In an incremental Gradle build the strip task may not re-run**, and the
  unstripped `.so` goes into the APK. The symptom is the APK jumping from 14MB
  to 22MB. A clean build fixes it, and the size difference is the symptom to
  look for.
