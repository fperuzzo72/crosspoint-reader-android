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

**This tree builds one target and no other.** It is not a multi-device build
with Android among the devices, so code describing hardware this phone does not
have is cut rather than guarded: the settings that named page buttons, the
battery gauge, firmware updates and the ESP32 sleep were deleted outright, not
put behind `#if FREEINK_DEVICE_HIBREAK`. A guard there would be pretending at a
generality nothing exercises, and the pretence costs a reader's attention on
every pass through the file.

**One exception, and it is the whole reason this document exists.**
`lib/hal/posix/` and the shim under it stay as they are, conditionals and all.
That is the code the Kindle shares, and the two ports have been trading fixes
through it: the 405 patch and the chunked patch each crossed without a line of
adjustment because those files were identical. Flattening the conditionals
there would buy tidiness in a file nobody reads and spend the property that has
been paying for itself weekly.

So: cut freely in `src/` and in the parts of `lib/` this port owns; leave the
shared POSIX layer alone. When a fix in it holds anywhere, it still goes in
unguarded and gets an entry below, because the Kindle will want it.

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
| **Upstream** | `DirectPixelWriter` indexed the framebuffer through a `uint16_t`, reaching 65,535 bytes. That is all of an 800x480 panel (48,000 B) and still covers a 600x800 one (60,000 B), so it was invisible while those were the only targets. Fixed on the Kindle too, where it is latent rather than visible: 5,535 B of slack, which is luck and not design. Any panel over 524,288 pixels wraps. Only images use this writer, so a page looks right except for its illustration. |
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

### Removed here, on purpose: Calibre wireless and the mode menu

Two deliberate divergences, both the owner's call rather than a fix.

**Calibre wireless device transfers are gone from this tree.** Not broken here
in some port-specific way: the plugin converts for the X3 and X4 specifically
and does not work properly. `CalibreConnectActivity` is deleted,
`NetworkMode::CONNECT_CALIBRE` with it, and the row is out of the menu for
every target this tree builds, not only the hosted one. The Calibre **content
server** is untouched and still works: that is OPDS, a different thing, and
`STR_CALIBRE_URL_HINT` in the OPDS settings is the one Calibre reference left
in the code.

**File Transfer opens the server directly on a hosted target.** With Calibre
gone and "Join a Network" and "Create Hotspot" being the same act where the
system owns the radio, the menu had one row left, which is a keypress asking
permission to do the only possible thing. `FREEINK_MCU_HOSTED` in
`CrossPointWebServerActivity::onEnter()` calls `onNetworkModeSelected` straight
away. The screen still exists and still opens on the ESP32 targets.

Both landed on the Kindle the same day (`db5c42fb`), and the menu skip is
guarded `FREEINK_MCU_HOSTED` there too. That tree defines the macro as exactly
`FREEINK_DEVICE_KINDLE`, so a device guard would have protected an ESP32 branch
that does not exist in it while spending a line of divergence for nothing. Note that
`CrossPointWebServerActivity.cpp` has now diverged in a second place, so the
next patch crossing through it will not apply as cleanly as the last two did.

The unused `STR_CALIBRE_WIRELESS` and friends are still in the translation
tables. Pruning them regenerates every language for a few kilobytes.

## Counter-current

Things the Kindle solved that need rethinking here, not copying:

- **`arduino-shim/WiFi.h`** reads the SSID through wireless extensions
  (`SIOCGIWESSID`) and RSSI through `/proc/net/wireless`. Wireless extensions
  are dead on modern Android and `/proc/net/wireless` is not readable by an
  ordinary app. Here the network state comes from `ConnectivityManager`, which
  additionally distinguishes "has an address" from "has internet".
- **TLS** now works on the Kindle, through wolfSSL in the shim. Here it crosses
  into Kotlin, where the system trust store already exists, so none of that
  build is reachable from this port. Three findings from it are worth keeping
  anyway, because all three fail *silently* and two of them read as the wrong
  problem entirely:
  - `--with-max-rsa-bits=4096`. Without it `sp_int.h` applies its own 3072-bit
    default and an RSA-4096 signature fails as `ASN_SIG_CONFIRM_E`, which reads
    like a bad certificate. 59 of the 121 CAs in a current Mozilla bundle are
    RSA-4096.
  - `--enable-altcertchains`. Without it the chain a server *presents* must end
    at a trusted root, and a cross-signed chain does not: gutenberg.org ends at
    AAA Certificate Services, no longer in the bundle, while the trusted anchor
    sits mid-chain. Fails as `ASN_NO_SIGNER_E`, which reads as "the root is
    missing" while the root is loaded.
  - `wolfSSL_CTX_load_verify_buffer` walks a multi-cert PEM in order and stops
    at the first it cannot parse, and `SecureClient` does not check its return.
    One bad certificate silently discards every one after it. Loading them one
    at a time is the fix.
  - Six of the 121 CAs are then still rejected, and the reported
    `RSA_KEY_SIZE_E` / `ECC_KEY_SIZE_E` is not why. The bundle disproves the
    size reading on its own: it holds 21 RSA-2048 certificates and only four
    fail, and no size bound discriminates within one key size. The real check is
    in `wolfcrypt/src/asn.c` (5.7.2-stable): a serial number of zero is
    non-conforming under RFC 5280 section 4.1.2.2, so strict parsing returns
    `ASN_PARSE_E`. The six certificates with serial 0 are exactly the six
    rejected, and the size error is applied afterwards and on top. **In this
    library that error name is not evidence about key size.**

  The switch is `WOLFSSL_NO_ASN_STRICT`, and the Kindle has not taken it: it
  relaxes seventeen conformance checks that apply to every certificate parsed,
  including the ones a server presents, which is a poor trade in a build that
  verifies certificates precisely so it does not have to trust whatever
  answers. The cost is written into that README instead: Go Daddy Root G2, both
  Starfield roots and a Hellenic Academic pair are unusable there, and a
  catalogue chaining to them fails.

  **None of that applies here, and the contrast is the point of this section.**
  This port never links wolfSSL: TLS crosses into Kotlin and Android's own
  trust store, which accepts those six roots like any other client. The same
  OPDS catalogue that fails on the Kindle works on this device, without a
  conformance trade being made on anyone's behalf.
- **`ESP.restart()`** re-execs the process on the Kindle. On Android restarting
  a process is not the same as restarting the Activity, and the right semantics
  are still undecided.

## Build traps that hold for both

- **The tree carries it and the binary does not receive it.** Three instances
  landed in one day, which is usually a sign the category is larger than the
  three. In each one, reading the source proved nothing, because the source was
  right:
  - `patch_jpegdec.py` applied two patches nothing ran, so both ports shipped a
    wild pointer (below).
  - `ENABLE_SERIAL_LOG` was never defined, so every `LOG_` macro in the tree
    expanded to nothing, **errors included**. Not "the logging was quiet": the
    logging did not exist in the binary.
  - `test/kindle_arduino_string` and `test/kindle_crypto` still pointed at
    `lib/hal/kindle/` after this port moved those files to `lib/hal/posix/`.
    CMake fails at *configure*, so the whole host suite was unavailable here,
    including the thirty tests that were fine.

  The sharpest illustration is a line that was already written. The blur in a
  progressive JPEG is fully described by `jpegScale 1/8, fineScale 7.94`, which
  `JpegToFramebufferConverter` has always logged. The explanation sat in the
  tree the whole time and neither port could read it.

  The counter-check is cheap and worth running when something is inexplicable:
  ask what the binary actually contains, with `strings`, rather than what the
  repository contains.

- **A tree can carry a fix for months and ship without it**, when the thing
  that applies it belongs to a build system the port no longer uses.
  `scripts/jpegdec_patches/` has two patches against JPEGDEC's pinned commit,
  and `scripts/patch_jpegdec.py` applies them as a **PlatformIO pre-build
  step**. Neither hosted port goes through PlatformIO, so both shipped the
  unpatched decoder: a wild pointer in `JPEGDecodeMCU_P`, about 33 MB past
  `sMCUs`, faulting on the first AC write. It needs a progressive 3-component
  JPEG decoded to grayscale, which is what this reader does with every
  progressive image in a book.

  Neither port could have found it by reading its own source, because its own
  source was right. The Kindle found it by reading the *dependency* after the
  code above it turned out to be correct.

  The fetch step that applies them is deliberately **separate from the
  download**, which has an "already present" guard: a tree fetched before the
  fix would never receive the patches if the two were one step. Idempotence is
  git's call — reverses clean means already applied, applies clean means apply,
  neither means abort rather than guess.

  Patched, progressive JPEGs stop crashing but still render badly, and that is
  JPEGDEC by design rather than a second bug: it forces eighth scale and reads
  DC coefficients only, one average per 8x8 block, which the reader then
  enlarges back. Measured across a 182-book library: 23 books carry at least
  one progressive JPEG and 9 carry five or more, so it bites a handful of books
  and passes unnoticed in the rest. `jpegtran` converts progressive to baseline
  losslessly, since both hold the same coefficients in a different order.

  It is testable off-device, which is how this port confirmed it: drive
  JPEGDEC from a host harness the way the reader does, `EIGHT_BIT_GRAYSCALE`
  at `JPEG_SCALE_EIGHTH`, on any progressive image. Unpatched exits 139.

- **Two numbers for one measurement, agreeing by accident.** The themes drew
  the home menu from their raw metric tables (`LyraMetrics::values` and
  friends) while hit testing answered from `UITheme::getMetrics()`. Identical
  values, so identical results, for as long as `getMetrics()` adjusted nothing
  but the button hints. The first adjustment that touched a row height moved
  the drawing and left the touch target behind: tapping Settings opened File
  Transfer, a whole row off.

  63 raw reads across three theme files, plus 22 hardcoded `UI_12_FONT_ID` and
  `UI_10_FONT_ID` in the same draw paths, which is why the labels stayed at 12
  point while every box around them scaled.

  Same family as the traps above: the defect was in the tree the whole time and
  nothing exercised it. A value read from two places is a bug already, not a
  bug once the two disagree, and "they are equal today" is not a defence.

  Related: `constexpr` over a value that became a measurement stops compiling,
  which is the compiler doing the search for you. The one to watch is the
  opposite case, a file-scope constant initialised from a runtime accessor: it
  compiles, runs at library load before anything has reported the device, and
  captures the default silently.

- **A quoted `-D` does not survive being written into a generated script.** The
  inner shell eats the quotes and the macro becomes an identifier. Defines go in
  a header via `-include`.
- **In an incremental Gradle build the strip task may not re-run**, and the
  unstripped `.so` goes into the APK. The symptom is the APK jumping from 14MB
  to 22MB. A clean build fixes it, and the size difference is the symptom to
  look for.
