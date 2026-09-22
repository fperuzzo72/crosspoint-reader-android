# CrossPoint on the Bigme HiBreak Pro

Port target: **Bigme HiBreak Pro**, an Android e-ink phone. Everything below was
read from the device, not inferred.

| | |
| --- | --- |
| Model | Bigme HiBreak, build `Bigme_HiBreak_V1.0_20260306` |
| Android | 14, SDK 34 |
| SoC | MediaTek MT6877, arm64-v8a |
| Panel | **824x1648**, density **300 dpi** |
| Physical rotation | `phy_rotation = 270`, and Android agrees: `installOrientation ROTATION_270` |
| Cutout | 49px notch at the top, `Rect(375, 0 - 450, 49)` |
| Scale | `density=1.875`, so 439 x 879 logical dp |
| Presentation deadline | `presDeadline 31000000` (31ms), not an LCD's 16ms |

This is a cousin of the Kindle port, not of the M5PaperS3 one. Both take
CrossPoint off the ESP32 and onto a machine that already has an operating
system. The difference is that on a Kindle the process talks to the kernel's
EPDC; here it hands pixels to SurfaceFlinger like any other app, and the
vendor's framework decides the waveform.

Four consequences of the geometry:

- **824 / 8 = 103 exactly.** The renderer works in 1bpp planes with
  `DISPLAY_WIDTH_BYTES = WIDTH / 8`. A width that is a multiple of 8 avoids the
  row-padding class of bug outright. Each plane is 103 x 1648 = 169,744 bytes.
- **The reported physical dpi is garbage:** `density 300 (188.554 x 667.61) dpi`.
  An e-ink panel has square pixels; 188 horizontal against 667 vertical is
  firmware reporting the wrong physical size. What counts is the logical density
  300 with scale 1.875. Do not use the physical dpi for anything.
- **The notch eats the top 49px.** CrossPoint draws its own status bar there, so
  the app goes fullscreen under the cutout and the reader never learns it
  exists. Reclaiming those pixels would need
  `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES` and teaching CrossPoint the inset.
- **`installOrientation ROTATION_270`.** The panel's native axis is a quarter
  turn from the logical one. On the ordinary Android path the system resolves
  it; it would come back raw only on the ION overlay path.

## Where the port cuts

The structural finding, inherited from the Kindle port: **no file outside
`lib/hal/` includes `EInkDisplay.h`**. The HAL is a real seam. That reframed the
job from "286k lines coupled to the ESP32" into "re-implement the HAL, and reuse
the POSIX shim somebody already wrote".

## The census

Running the NDK's clang (`aarch64-linux-android28-clang++`, arm64-v8a) with
`-fsyntax-only` over every `.cpp` in `src/`, `lib/` and `freeink-sdk/libs`:

| After | Compiling | Dominant remaining cause |
| --- | --- | --- |
| first run, with the Kindle's POSIX layer inherited | 61% (144/235) | `ArduinoJson.h` (73 files) |
| fetching the declared dependencies | 94% (221/235) | `CROSSPOINT_VERSION` (6) |
| shims, the `RecentBook` fix, the ArduinoJson flag | **100% (221/221)** | none |

For comparison, the Kindle port's first run was 25% and it took six steps to
reach 82%. The entire difference is the POSIX layer arriving ready.

Two caveats, because a census says less than it looks. It answers "would this
file compile", not "does this link" and certainly not "does this run". And the
denominator fell from 235 to 221 because `FreeInkDisplay/src` left the count:
those are the `PanelDriver` implementations and the `EpdBus` they talk through,
and there is no raw panel here.

83 of the 91 first-run failures were an unvendored third-party header, not a
portability problem. The only real code finding was `RecentBook`: `HomeActivity.h`
forward-declared `struct RecentBook;` and then had a `std::vector<RecentBook>`
as a member. Instantiating a vector's members with an incomplete type is
ill-formed. The ESP32's libstdc++ accepts it, the NDK's libc++ refuses, and
libc++ is right. That is a CrossPoint bug, not an Android one.

## The link

`tools/android/trylink.sh`, adapted from the Kindle port's, with its hard-won
lessons intact: object invalidation on any header change, exclusion of the `.c`
files a wrapper already includes, and archiving per library rather than linking
loose objects.

| After | Undefined |
| --- | --- |
| first attempt | 20 |
| `third_party/*.cpp` in the link and the `.inl` files in the fetch | 18 |
| `-lz` (Android ships zlib; PNGdec uses it) | **14** |

The Kindle port started at 255 and came down in five steps to 92 before
reaching zero.

**The remaining 14 were one thing:** `KindleFrameBuffer` (11 methods) and
`KindleTouchDevice` (3). Nothing scattered, no networking, no filesystem, no
FreeRTOS. That list was not a list of problems; it was the specification for
`lib/hal/android/`.

Two build traps worth recording:

- **A quoted `-D` does not survive being written into a generated script.** The
  inner shell eats the quotes and `CROSSPOINT_VERSION` becomes an identifier
  rather than a string. Defines now come from a generated header via `-include`.
- **A file that fails to compile hides references rather than creating them.**
  When `CROSSPOINT_VERSION` broke four files, the total stayed at 20 and the set
  changed completely. The number means nothing unless you know which objects are
  missing.

## The hosted HAL

Writing a `HalDisplayAndroid.cpp` next to `HalDisplayKindle.cpp` would have
duplicated 345 lines. The two devices share nothing in hardware, but HalDisplay
needs the same sequence from both (compose, stage the base, paint the gray
planes over it, present once), and the Kindle file was already written against
that shape.

So the guard stopped naming a device and started naming the family BoardConfig
already derived:

| Before | Now | Lines |
| --- | --- | --- |
| `HalDisplayKindle.cpp` | `HalDisplayHosted.cpp` | 345 |
| `HalGPIOKindle.cpp` | `HalGPIOHosted.cpp` | 206 |
| `HalSystemKindle.cpp` | `HalSystemHosted.cpp` | 231 |

`lib/hal/hosted/HostedPanel.h` picks the panel and the touch device at compile
time. There is deliberately no virtual base class: one binary per device, and a
vtable would only pay for indirection on a decision the preprocessor already
made.

Two design decisions worth stating:

**Touch is not classified in C++.** On the Kindle the backend reads the raw
evdev stream and decides what is a tap, a long press and a swipe, because
nothing else will. On Android that piece already exists and is better than the
one we would write: `GestureDetector` knows this device's slop and this system's
long-press threshold. So classification stays in Kotlin and what crosses JNI is
a finished gesture. `AndroidTouchDevice` is only the thread handoff.

**No surface is not an error.** An Activity is paused and destroyed out from
under the process while the reader thread stays alive and painting. The composed
frame survives in `stage` and is re-presented when the surface returns. That is
the deep difference between this target and the other two, where the panel is
always there.

## The shared library

`cmake/android/CMakeLists.txt` builds for real with the NDK toolchain:
`libcrosspoint.so`, ELF 64-bit aarch64, 9.5 MB, four JNI symbols exported and
the rest hidden. That is stronger than the trylink: the trylink asks whether it
would link, this linked.

Three things the CMake had to learn, all commented in the file:

**Archive per library, do not link loose objects.** There are three copies of
miniz in this tree, each with a config that prefixes only some of its symbols.
PlatformIO never sees the problem because it archives each library: the linker
then pulls a member only when it resolves something still undefined, and
duplicates across archives are "first wins". The first version of this file
linked loose and produced dozens of multiple-definition errors, exactly as the
Kindle's `trylink.sh` comment warned.

**One copy of expat, not two.** CrossPoint and the SDK each vendor expat and
both export the same unprefixed `XML_*`. The SDK's wins, for the same two
reasons the Kindle port recorded: it carries a real `expat_config.h` instead of
relying on build flags, and its include directory already comes first.

**`-fvisibility=hidden`, and not for ABI hygiene.** In a *shared* library
everything is exported by default, so nothing is dead and `--gc-sections`
discards nothing. uzlib's checksum helpers are declared and called from a
function nothing reaches, and without hidden visibility they surfaced as
undefined references to code that never runs.

### An upstream finding

`MySerialImpl` is declared in `lib/Logging/Logging.h` and **is not defined
anywhere in this tree**: not the static member, not `write()`, not `flush()`,
not `printf()`. The only member with a body is the inline `operator bool()`.

That is not a port problem. `src/main.cpp:813` has `if (Serial && ...)` outside
any guard, so the reference is always emitted. It is defined now in
`lib/hal/posix/SerialProxyPosix.cpp`, which has to be its own translation unit:
`Logging.h` ends with `#define Serial MySerialImpl::instance`, and including
that header inside `ArduinoShim.cpp` turns its `HardwareSerial Serial;` into a
redefinition with a different type.

## Networking

The POSIX shim speaks HTTP over raw sockets and **refuses https explicitly**,
with no silent downgrade. The refusal is correct (sending OPDS credentials in
the clear would be worse than failing) but it rules out almost every real
catalogue and KOReader sync.

The two ways out were to embed mbedtls in C++ or to cross into Kotlin. We
crossed: TLS already exists there, uses the *system* trust store (which the
device keeps current, and which C++ would have to carry and let age along with
the binary), honours proxies and VPNs, and costs no new dependency.

This inverted the bridge for the first time. Until then Kotlin called C++ and
every call arrived with a ready `JNIEnv`. Now C++ calls the JVM from inside the
reader loop, which runs on a `std::thread` the JVM has never heard of. Hence
`JniBridge`: the `JavaVM` cached in `JNI_OnLoad`, per-thread attach with detach
only by whoever attached, and the application's class loader captured at load
time. That last one is not fussiness: on a native attached thread `FindClass`
resolves against the *system* class loader, which cannot see our classes.

Redirects stay with C++ on purpose. CrossPoint has its own logic in
`set_redirection`, and two layers following the same 302 would lose the second
one's headers.

`ACCESS_FINE_LOCATION` is deliberately absent. Reading the SSID needs location
permission on Android 10+. The shim's `SSID()` already returns empty outside
wireless-extensions and `RSSI()` returns 0, so the bar shows the IP and the
connected state without inventing a name.

## The bug that cost the most, and how it was found

Downloading a book over OPDS killed the app. No message, no error, nothing in
the log: the process simply vanished.

**Three wrong hypotheses before the right one**, recorded because two of them
became legitimate fixes without being the bug:

1. *"Android 11 closed NETLINK and getifaddrs cannot see the interfaces."*
   False on this device, proved by a log printing both answers side by side:
   `framework=1 getifaddrs=1`. The fix (using ConnectivityManager) stayed for a
   different and good reason: it distinguishes "has an address" from "has
   internet", and a captive portal gives the first and not the second.
2. *"A pending JNI exception."* `GetStaticMethodID` with a wrong signature throws
   `NoSuchMethodError` and leaves the exception pending; the next JNI call made
   that way aborts the VM. A real crash path, fixed, not this crash.
3. *"An `Error` escaping across the JNI boundary."* The bridge caught
   `Exception`, which does not cover `OutOfMemoryError`. Also real, also fixed,
   also not it.

**What solved it was to stop deducing.** A signal handler recording the killing
signal gave the two facts that decided it:

```
*** died with signal 11 (Segmentation fault), address 0xe5c ***
```

- **SIGSEGV and not SIGABRT** eliminated both families at once: it was not the
  runtime complaining about JNI, nor an ART allocation failure.
- **The point of death MOVED between runs.** Once inside the JNI `finish()`,
  once before reaching it. A fixed null pointer does not walk around the code;
  a stack overflow does, because the address that faults depends on how deep you
  were.

The reader thread was started with `std::thread`, which on bionic takes the
**1MB** default. That is not enough for this tree: on the ESP32 tasks have
hand-sized stacks precisely because the XML parser, chapter layout and the
render chain go deep, and CrossPoint itself carries a `TaskWatchdog` and high
water mark measurements because of it. It is **8MB** now, via
`pthread_attr_setstacksize`, which is what a process's main thread on Android
already has.

### What stays as method

The signal handler stays in the binary permanently. It turns "the app vanished"
into "SIGSEGV at address such-and-such", and that difference decided a bug three
rounds of code reading had not.

The log file stays too, with a lesson: it was opened **truncating** on every
app launch. The case where the log matters is when the process dies, and the
only way to read the file is to reopen the app, which was exactly what erased
the evidence. Two debugging sessions were lost that way before anyone noticed.
The previous run is now kept as `crosspoint.log.previous`.

## Fonts

The sizes were decided by reading on the device, not by scaling.

The prediction was that 300 dpi would require multiplying sizes by 1.42 against
the X4's ~212 dpi. Measured with human eyes: 18 is pleasant, 16 is good, 14 is
legible, 12 is too small, and 22 and 24 are too large for real use. The built-in
list is `{14, 16, 18, 20}`.

The UI uses a different family (Ubuntu, two styles, merged with a Vietnamese
cut). The themes ask for `UI_10_FONT_ID` and `UI_12_FONT_ID` in dozens of
places, so what changes on this device is what each ID *delivers*, not each
call: the ID is a key, not a measurement.

```
SMALL   -> ubuntu 12    status bar (12 uses in the theme, against 3 for UI_10)
UI_10   -> ubuntu 14    settings rows
UI_12   -> ubuntu 16    interface body
```

### Symbols

The boxes with "?" were not a configuration problem. **No source font in the
repository had those glyphs**: NotoSerif and Ubuntu gave 0 of 112 arrows and 1
of 96 geometric shapes. The intervals were enabled in the converter and there
was nothing to convert.

Two fonts joined the stack because one was not enough: Symbols2 covers geometric
shapes (96/96) and dingbats (145/192) but only 13 of 112 arrows and lacks
U+2192; Math has 99 of 112 arrows and has U+2192.

The default intervals had to be narrowed at the same time, and that is not
cosmetic thrift: while no font had those glyphs, asking for the whole Arrows and
Math blocks cost **zero**. With Math in the stack they started being found and
costing +180KB per file. Narrowed to what appears in running text, the cost is
+27.6KB (13%).

Arabic, Hebrew, Cyrillic and Greek are out: this port reads Portuguese and
English. Arabic and Hebrew alone were half the weight of the UI font, measured
at 440.8 KB against 219.3 KB at size 16.

## The bottom margin

The reader does `orientedMarginBottom += std::max(screenMargin, statusBarHeight)`.
**Maximum, not sum.** With the bar at 46px and the setting's maximum at 40, the
bottom margin never had any effect on this device: the bar always won.

The maximum is 130 now (~11mm at 300 dpi), step 10, default 60. Since 60 > 46,
the margin exists from first launch.

And the bar itself did not respect the side margin: the reader adds
`screenMargin` to the bezel inset before composing the page, and the bar did
not. With a small margin that went unnoticed; on a rounded panel, with the
margin at 60px, the ends fell outside the visible area.

## Still open

- Scoped storage. The app currently uses All files access with a real folder,
  which is what a file-based reader needs. A user-chosen folder through the
  Storage Access Framework would mean rewriting the file browser, the cache and
  the progress store against URIs rather than paths.
- Hiding by capability the menu entries that cannot work here: OTA, firmware
  flashing, hotspot mode.
