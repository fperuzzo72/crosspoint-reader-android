# CrossPoint Reader on the Bigme HiBreak Pro

A port of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
to run as an ordinary Android application on an e-ink phone, instead of as
firmware on a microcontroller.

**It runs on the device.** It reads EPUBs, browses OPDS catalogues over HTTPS,
loads fonts from storage, and answers touch. This is not a proof of concept.

---

## What this is

CrossPoint is e-reader firmware written for ESP32 devices, where it owns the
whole machine: 380KB of RAM, an e-ink panel on a SPI bus, and no operating
system between it and the hardware.

A Bigme HiBreak Pro is not that. It is an Android phone with its own kernel,
userspace and UI, where the panel belongs to the system. So this port replaces
nothing: it runs as an app, alongside everything else.

The reading engine is unchanged. What was written here is the layer underneath.

### The device, as measured

| | |
| --- | --- |
| Model | Bigme HiBreak Pro, build `Bigme_HiBreak_V1.0_20260306` |
| Android | 14 (SDK 34) |
| SoC | MediaTek MT6877, arm64-v8a |
| Panel | 824x1648, 300 dpi, physical rotation 270 |
| Cutout | 49px notch at the top |

---

## Building

There is no release yet.

```bash
scripts/fetch-thirdparty.sh   # once: dependencies platformio.ini declares
./gradlew :app:assembleDebug
```

Needs JDK 17, Android SDK 35 and the NDK. The fetch step exists because outside
PlatformIO nobody resolves `lib_deps`, and 73 files do not compile without
ArduinoJson alone.

On first launch the app asks for **All files access** and exits. Grant it in
Settings and open it again. It refuses to start without it, deliberately: the
storage root is read once, inside `setup()`, so starting with the wrong folder
and granting afterwards would leave the reader stuck on it until the process
dies, which is worse than not starting because it looks like it worked.

### Layout on storage

```
/sdcard/CrossPoint/
  books/                  the EPUBs; the file browser opens here
  fonts/                  installable .cpfont families, one folder each
  .crosspoint/            per-book cache, reading progress, bookmarks
  crosspoint.log          current run
  crosspoint.log.previous the run before it
```

---

## What works

- **EPUB reading**, with CrossPoint's engine and UI toolkit untouched.
- **Touch**: taps, long presses and swipes, classified by Android's
  `GestureDetector` and handed to C++ already decided.
- **OPDS over HTTPS**, including redirects and downloads into `books/`.
- **File transfer over Wi-Fi**: the web UI accepts multipart uploads, so a
  browser can push books to the device. The streaming parser is covered by a
  host test (`test/multipart`); the end-to-end path has not yet been exercised
  on the device itself.
- **Fonts**: 14, 16, 18 and 20 built in, in Noto Serif and Noto Sans, plus
  families installable into `fonts/`.
- **Symbols**: arrows, list bullets, geometric shapes and the mathematical
  operators that appear in running text.
- **A log file**, readable by any file manager, with no cable and no adb.

## What does not

Listed because a port that hides its edges wastes the next person's afternoon.

- **The network name is not shown.** Reading the SSID needs location permission
  on Android 10+, because it identifies where you are. The status bar shows the
  IP and the connected state, which need no permission at all.
- **OTA and firmware flashing** do not apply here and should be hidden by
  capability rather than appearing in the menu.
- **Wi-Fi association flows** are the system's job. The selection screen now
  resolves itself instead of opening an empty list, but the menu entries that
  lead there are still worth hiding.
- **Scoped storage.** The app uses All files access with a real folder, because
  CrossPoint's file browser, cache and progress store all speak paths. Moving to
  the Storage Access Framework would mean rewriting the three of them against
  URIs.

---

## How it was done

The path is in [docs/android-port.md](docs/android-port.md), with the numbers
measured at each step. The short version:

| | |
| --- | --- |
| Portability census | 61% → 100% |
| Undefined references at link | 20 → 0 |
| `libcrosspoint.so` | 9.5 MB, arm64-v8a |
| APK | 16 MB |

The whole POSIX layer (`String`, `millis()`, FreeRTOS over pthreads, SdFat over
POSIX, sockets, MD5, base64) came ready from the
[Kindle port](https://github.com/fperuzzo72/crosspoint-reader-kindle), which had
already taken CrossPoint off the ESP32. Without it this port would have been
another order of magnitude of work.

### The Kotlin boundary

Kotlin keeps what Android does better: the activity lifecycle, the Surface,
gesture classification with the device's own thresholds, storage, and TLS. C++
keeps the whole of CrossPoint, which knows none of that.

```
Kotlin -> C++    nativeSetSurface, nativeGesture, nativeContact,
                 nativeSetStorageRoot, nativeStart
C++ -> Kotlin    HTTP (CrossPointHttp), network state (CrossPointNet)
```

The second direction is the delicate one: C++ calls into the JVM from inside the
reader loop, which runs on a native thread. See `lib/hal/android/JniBridge.h`
for the two rules that imposes.

### What should go back to the Kindle port

In [docs/backport-to-kindle.md](docs/backport-to-kindle.md), each finding marked
POSIX (applies to both), Android (stays here) or upstream (it is a CrossPoint
bug).

---

## License

MIT, like CrossPoint and the freeink-sdk. See [LICENSE](LICENSE).
