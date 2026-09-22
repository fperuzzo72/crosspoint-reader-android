#pragma once
#include <BoardConfig.h>

// The storage layout, in one place.
//
// Three paths and a root. The root varies per device (the SD card on an ESP32,
// /mnt/us on a Kindle, /sdcard/CrossPoint on the HiBreak) and the three paths
// below are always relative to it, resolved by the storage shim.
//
//   <root>/.crosspoint   per-book cache, progress, bookmarks, credentials
//   <root>/fonts         installed fonts (also /.fonts, hidden)
//   LIBRARY_ROOT         where the file browser starts
//
// The first two were already constants scattered through the tree and stay
// where they are; what was missing was the third having a name, because the
// browser started at the root and saw the other two alongside the books.

namespace crosspoint::storage {

// Where the file browser starts when nobody asks for a path.
//
// On SD card devices the root IS the library: the card belongs to the reader
// and nothing else writes to it. On the HiBreak the root is a folder inside the
// phone's shared storage, and mixing EPUBs with cache and fonts in the same
// listing is bad for two reasons: the cache grows and clutters navigation, and
// it is not obvious where it is safe to drop a book arriving over the
// network.
#if FREEINK_DEVICE_HIBREAK
inline constexpr const char* LIBRARY_ROOT = "/books";
#else
inline constexpr const char* LIBRARY_ROOT = "/";
#endif

}  // namespace crosspoint::storage
