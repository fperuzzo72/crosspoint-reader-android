package org.crosspoint.hibreak

import android.view.Surface

/**
 * The boundary with C++, and nothing beyond it.
 *
 * Every crossing here goes one way: Kotlin calls, C++ obeys. CrossPoint never
 * calls back through this object. That is not a limitation, it is what keeps
 * the bridge readable: no cached JNIEnv, no callback crossing threads, and
 * nothing here needs to know which thread the reader runs on.
 *
 * The division of labour behind it: Kotlin keeps what Android does better than
 * we would (lifecycle, the Surface, gesture classification with the device's
 * own thresholds) and C++ keeps the whole of CrossPoint, which knows none of
 * that and does not need to.
 */
object CrossPointNative {

  init {
    System.loadLibrary("crosspoint")
  }

  /**
   * Hands over or withdraws the drawing surface.
   *
   * Passing `null` is the NORMAL pause path, not an error: an Activity is
   * destroyed and recreated many times over the life of the process while the
   * reader thread stays alive throughout. The C++ side keeps the last composed
   * frame and re-presents it when the surface returns.
   */
  @JvmStatic external fun nativeSetSurface(surface: Surface?)

  /**
   * A gesture already classified. [kind] matches `crosspoint::hosted::Gesture`:
   * 0 none, 1 tap, 2 long press, 3 swipe.
   *
   * Coordinates normalised to 0..1. C++ never sees a screen pixel, which is the
   * same contract the Kindle backend already used and what leaves the reader
   * indifferent to the panel's resolution.
   */
  @JvmStatic external fun nativeGesture(
    kind: Int,
    nx: Float,
    ny: Float,
    nxEnd: Float,
    nyEnd: Float,
    heldMs: Int,
  )

  /** Finger down or not, independent of the contact having become a gesture. */
  @JvmStatic external fun nativeContact(down: Boolean)

  /**
   * The storage root, as an absolute path.
   *
   * Must be called BEFORE [nativeStart]: CrossPoint's `setup()` already builds
   * the file browser and reads the library, and a wrong root at that moment is
   * an empty library.
   *
   * This side decides, because only the framework knows this app's directory.
   */
  @JvmStatic external fun nativeSetStorageRoot(path: String)

  /**
   * Starts the reader thread. Idempotent on purpose: the Activity can be
   * recreated (rotation, configuration change) without the process dying, and
   * restarting CrossPoint would lose the reading position.
   */
  /**
   * The panel's real size in pixels and its density.
   *
   * Must be called BEFORE [nativeStart], for the same reason as
   * [nativeSetStorageRoot]: `setup()` allocates the composition frame and lays
   * out the first screen, and a wrong size at that moment is a page composed
   * for a panel that is not there. Without this call the reader falls back to
   * the size it was built for.
   */
  @JvmStatic external fun nativeSetDisplay(width: Int, height: Int, densityDpi: Int)

  @JvmStatic external fun nativeStart()

  // Mirrors crosspoint::hosted::Gesture. A Kotlin enum with explicit values
  // rather than loose numbers at the call sites: the day the C++ side gains a
  // new gesture the compiler here will not notice on its own, but at least
  // there is one place to fix.
  enum class Gesture(val code: Int) {
    NONE(0),
    TAP(1),
    LONG_PRESS(2),
    SWIPE(3),
  }
}
