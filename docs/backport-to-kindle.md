# What travels between the two hosted ports

This repository was born as a copy of `crosspoint-reader-kindle` at `f002274b`.
That port paid the cost of taking CrossPoint off the ESP32; this one inherits
the work. They share `lib/hal/posix/`, so a fix that belongs to the POSIX layer
rather than to one device belongs to both, and this is the ledger.

It started out named for one direction, on the assumption that the newer port
would be the one finding things. In practice the traffic runs both ways: the
multipart upload work went from here to the Kindle, and the two bugs that made
it usable came back from there: WebDAV answering for `"/"`, and streamed
responses going out as `Content-Length: 0`. Whichever port is being exercised
at the time finds the bug, and both have the file it lives in.

What makes that cheap is keeping the shared files literally identical. The 405
patch and the chunked patch each crossed without a line of adjustment, and the
one time this port wrote its own version of a fix the Kindle had already made,
it was rewritten to match rather than left as a second spelling of the same
thing.

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
| **Upstream** | `DirectPixelWriter` indexed the framebuffer through a `uint16_t`, reaching 65,535 bytes. That is all of an 800x480 panel (48,000 B) and still covers a 600x800 one (60,000 B), so it was invisible while those were the only targets. Any panel over 524,288 pixels wraps. Only images use this writer, so a page looks right except for its illustration. |
| **Upstream** | `logPrintf` had no `format(printf, 3, 4)` attribute, so no LOG_ call site in the tree was ever checked. Adding it found 68 wrong specifiers. None of them corrupt anything but the log, which is the instrument everything else is diagnosed with. |
| **Upstream** | The 68 above are portable fixes (`%u` for `uint32_t`, `%zu` for `size_t`, a cast for `uint64_t`), not 64-bit ones. Worth taking anywhere. Note that clang's own fix-its are *not* portable: on LP64 it suggests `%lu` for `size_t` and `uint64_t`, which is wrong on the 32-bit targets. |
| **Upstream** | `build-font-ids.sh` had a ruby block copied per size. It became a loop when the list went from four sizes to six and the copy grew larger than the logic. |

## Done: multipart uploads

Applied to `crosspoint-reader-kindle` on the `kindle-port` branch, commit
`b187982d`, and since cross-built and pushed (`ed8a453f`).

It was the cleanest backport this ledger has recorded, and the diff is why:
`WebServerPosix.cpp` and `arduino-shim/WebServer.h` had diverged between the two
repositories **in the upload work and nothing else**, so the port was those two
files plus `MultipartParser.{h,cpp}`, which has zero Android dependencies.

What came with it: the host test, which passes there, and the removal of the
`#if !FREEINK_DEVICE_KINDLE` that hid the File Transfer menu entry. The entry was
hidden because it could only ever have opened a server that refuses to run.

What did NOT come with it: the `LIBRARY_ROOT` default for the upload
destination. On a Kindle the card root *is* the library, so `"/"` was already
right and changing it would have been a difference for its own sake.

It cross-builds: 230 sources, zero undefined references, and both
`crosspoint::multipart::parse` and `WebServer::readMultipart` are in the ARM
binary. The refusal text is gone from it entirely. The host test passes there
too.

Two things the build turned up. The header-watching rule added to
`trylink.sh` the week before earned itself back immediately: `WebServer.h`
changed, every object was discarded, and the binary that came out was whole —
under the old source-mtime rule the objects that merely include it would have
been kept. And `cmake/kindle/CMakeLists.txt` enumerated the platform sources
and had gone stale unnoticed: `HttpClientPosix.cpp` and `WebSocketsPosix.cpp`
had been written and never added, and `MultipartParser.cpp` would have been the
third. It globs now, which is what `trylink.sh` — the thing that actually
builds that port — always did.

**Verified end to end on the device**: the file list loads, an upload lands on
the card and a delete removes it. The Kindle README now carries it in the list
that opens "All of this has been watched working on the device".

Two bugs stood between the backport and that, and neither was in the upload
work. They are recorded under "What came back" below, because both of them
turned out to be here too.

## Done: stepping past the network picker

Applied to `crosspoint-reader-kindle`, commit `ab1be153`, pushed.

Found by testing the multipart work above: File Transfer opened and went
straight back to the home menu. Both of its modes build a network before
serving, and neither can on a Kindle for the same reason as here — Create
Hotspot calls `WiFi.softAP()`, which the shim fails on purpose, and Join
Network opens a picker over `scanNetworks()`, which returns zero on purpose,
because scanning would fight the system for the same radio. The list opened
empty, the user cancelled the only thing on screen, and the caller went home.

So the `#if FREEINK_DEVICE_HIBREAK` short-circuit in
`WifiSelectionActivity::onEnter()` travelled, guarded as `FREEINK_DEVICE_KINDLE`
there because that tree still builds the ESP32 targets, which need the real
screen. **Its placement is the transferable part**: the fix belongs in the
screen, not in the nine activities that open it (OPDS, KOReader sync, font
downloads, the clock, OTA, the web server, Calibre, settings), because patching
those one at a time leaves the next one anybody writes broken again.

One deliberate divergence, and it runs counter to the usual direction. This
port clears the SSID because reading it needs a location permission. The Kindle
shim asks the interface through `SIOCGIWESSID` and that 3.x kernel still
carries wireless extensions, so the name is genuinely available and the caller
can say which network it is serving on. The Kindle version fills it in.

### And the trap under it, which this port almost certainly shares

Leaving the web server calls `silentRestart()` whenever `WiFi.getMode()` is not
`WIFI_MODE_NULL`, and both shims always answer `WIFI_STA`. It was recorded here
that the Kindle was safe from this "because `silentRestart()` returns early on a
touch device and that target inherits the X4 profile, which has touch". **That
was wrong, asserted without opening the profile.** `XTEINK_X4` is `NO_TOUCH`.

The device said so: one launcher run carried three "suspend detection armed"
lines, and that line prints once per process. The reader was re-execing twice
while the web server was being tested.

The mechanism to watch for here: `BoardConfig::ACTIVE` falls through to a
profile that describes hardware the target does not have, so
`BoardConfig::hasTouch()` answers about that profile rather than about the
device. A touchscreen declared through `FREEINK_CAP_TOUCH`, a compile-time
capability, does not make that runtime query true. Reading one for the other is
easy and silent.

Fixed on the Kindle in `bc183a22` by refusing all three silent restarts there.
They exist because bringing the radio up, or handing storage to a USB host,
fragments an ESP32's heap badly enough that a reset is the cheapest cure.

**Checked here rather than assumed, and it is worse here.** Every condition
holds: `WiFi.getMode()` returns `WIFI_STA` unconditionally in the shim, nothing
calls `selectDevice()`, so `BoardConfig::ACTIVE` is the X4 profile and
`hasTouch()` is false, and `finishWifiSessionWithoutRestart()` declines. The
difference is where `ESP.restart()` lands: it re-execs `/proc/self/exe`, which
inside an Android app is `/system/bin/app_process64`, not this reader. Exec
replaces the process image, so the JVM, the Activity and the Surface go with
it. On the Kindle the reader came back at Home; here nothing comes back.

Refused here too, and **guarded as `FREEINK_MCU_HOSTED` rather than by device**,
because the argument is about the family and not about either machine: no
hosted target has an ESP32's heap to defragment. That is the shape the Kindle's
`FREEINK_DEVICE_KINDLE` guard should take when this goes back, and it is one
more instance of the rename already recorded in "The structural item".

## Done: the web server had nothing to serve on

The first real test of the upload work failed one step earlier than the upload:
the screen showed an IP and a QR code and no browser could reach them.

`CrossPointWebServer` binds port 80. That range is privileged on Linux, so it
needs root or `CAP_NET_BIND_SERVICE`. An ESP32 has no such concept and the
Kindle runs as root, so 80 was right on both, and an ordinary Android app is
neither. `DEFAULT_PORT` is 8080 here and 80 everywhere else, which is a genuine
per-device value and stays guarded.

**The part that is owed, and is not the port number:** `bind()` and `listen()`
failed in complete silence. `WebServerPosix.cpp` closed the descriptor, set
`listenFd = -1` and returned, and from the outside that is indistinguishable
from a working server, because the IP and the QR come from the network state
rather than from a socket. It logs the failure now, with `strerror(errno)` and
a note when `EACCES` meets a port below 1024. That holds on any POSIX target
and the Kindle should have it: running as root is a reason the bind succeeds,
not a reason a failure should be quiet.

And the port had to reach the URL. It is built in three places in
`CrossPointWebServerActivity`, so a `webServerPortSuffix()` helper appends
`:8080` when the server is not on 80 and nothing when it is, which leaves the
other targets' URLs character for character as they were.

### Arriving from the Kindle: one registration order

The first thing the browser got on 8080 was `405 Method Not Allowed`,
identical to what the Kindle showed. Fixed there first (`52dfaa65`) and the
patch applied here unchanged, which is the direction this ledger does not
usually run.

The shim kept `on()` routes and `addHandler()` objects in two lists and
consulted the handler objects first. The Arduino `WebServer` it stands in for
keeps both in a single chain, so registration order decides, and the tree
relies on it: `CrossPointWebServer` registers `"/"` near the top of setup and
adds `WebDAVHandler` at the bottom, and WebDAV claims GET for every URI and
answers 405 for a directory. One list in registration order, and `"/"` wins
again.

It carried a leak with it: the bare `new` handed to `addHandler()` was never
deleted, one `WebDAVHandler` per File Transfer session.

### Arriving from the Kindle: chunked responses

The file manager and the settings page both answered "failed to load", and the
font list beside them worked. The difference is how each one sends: fonts
builds the whole JSON and sends it with a known length, the other two announce
`CONTENT_LENGTH_UNKNOWN` and stream. `send()` read that as "the caller
announced nothing" and fell back to the length of the empty string it had been
handed, so every list endpoint went out as `Content-Length: 0` and everything
streamed afterwards landed on a socket nobody was reading.

`CONTENT_LENGTH_UNKNOWN` and `CONTENT_LENGTH_NOT_SET` were **both already
declared** in the shim, two lines apart. Only `send()` conflated them. One
field with three states is the fix, and it is Arduino's own vocabulary.

Chunked rather than just closing the socket, which `Connection: close` would
have made legal and was less work. Framing is what tells a complete body from a
cut one, and this server hands out books. A handler that streams and never
sends its empty piece is closed off at the end of the request.

Fixed on the Kindle first (`4937f2ed`), and this port's version was rewritten
to match it function for function after being written differently here. The
two shims are identical again in these two files.

### Verified on the HiBreak Pro

With both of those in, File Transfer works end to end here: the file list
loads, an upload lands in the library and a delete removes it. The settings
page renders its values, and the fonts page lists what is installed. A font
family uploads through the browser: five `.cpfont` files picked as a directory,
all five arriving, and the family selectable and rendering afterwards. Deleting
one is the remaining untested corner of the web UI on either port.

### Found here because it is the first 64-bit target

Two of the three things the image bug turned up were not about the image, and
both come from this being the first port built for a 64-bit machine. `%lu`
against a `uint32_t` is correct where `unsigned long` is 32 bits and silently
wrong where it is 64: the first five variadic values ride in registers and
survive, and everything after them is read off the stack at the wrong width.
The reader's page-timing line was the visible case, printing
`gray_msb=502511173641ms`.

The `uint16_t` framebuffer index is the same shape of assumption but about
geometry rather than word size, and it is the one that broke the page.

The Kindle is 32-bit ARM, so it has none of the `%lu` problem and, at 60,000
bytes of framebuffer, none of the index problem either. Both fixes still belong
there: the format specifiers are portable spellings rather than 64-bit ones,
and a 16-bit index is wrong on the merits.

The `freeink-sdk` copy of `SDCardManager.cpp` has nine of the same specifier
bugs. Left alone: it is a separate repository.

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
