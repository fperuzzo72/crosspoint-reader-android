package org.crosspoint.hibreak

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.widget.Toast
import java.io.File
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowManager

/**
 * CrossPoint's entire Activity. One SurfaceView, one GestureDetector, and the
 * bridge.
 *
 * There is no XML layout and no view beyond the surface. CrossPoint draws its
 * own interface from scratch in 1bpp: any Android widget here would be a second
 * interface competing for the same screen.
 */
class ReaderActivity : Activity(), SurfaceHolder.Callback {

  private lateinit var surfaceView: SurfaceView
  private lateinit var gestures: GestureDetector

  /**
   * Where the finger landed in the contact in progress, in pixels. Kept because
   * a swipe needs the start and [GestureDetector] only hands the initial event
   * to some callbacks, not all.
   */
  private var downX = 0f
  private var downY = 0f
  private var downAtMs = 0L

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    // The screen does not sleep while reading. On e-ink that costs almost
    // nothing, because the panel only draws power when it changes, and a screen
    // that blanks mid-page is the kind of thing that loses your line.
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

    surfaceView = SurfaceView(this)
    surfaceView.holder.addCallback(this)
    setContentView(surfaceView)

    // The notch eats the top 49px (measured: Rect(375, 0 - 450, 49)).
    // CrossPoint draws its own status bar exactly there, so the app goes
    // fullscreen under the cutout and the reader never learns it exists.
    // Reclaiming those pixels would mean
    // LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES and teaching CrossPoint the
    // inset.
    hideSystemBars()

    gestures = GestureDetector(this, GestureListener())
    // Android's detector does not deliver long presses through the normal path
    // unless asked; we want them, because that is how CrossPoint opens context
    // menus and deletes books.
    gestures.setIsLongpressEnabled(true)

    // Without the permission we do not start, and that is not strictness: the
    // root is read ONCE, inside CrossPoint's setup(), and nativeStart is
    // idempotent. Starting with the hidden folder and granting afterwards would
    // leave the reader stuck on it until the process dies, which is worse than
    // not starting because it looks like it worked.
    if (!Environment.isExternalStorageManager()) {
      Toast.makeText(this, R.string.needs_all_files, Toast.LENGTH_LONG).show()
      requestAllFilesAccess()
      finish()
      return
    }

    // Before the root and before the thread: C++ asks for network state as
    // soon as anyone opens OPDS, and without a context the answer would be
    // "no network".
    CrossPointNet.init(this)

    val root = resolveStorageRoot()
    CrossPointNative.nativeSetStorageRoot(root.absolutePath)

    CrossPointNative.nativeStart()
  }

  /**
   * Where the books live.
   *
   * With All files access granted: `/sdcard/CrossPoint`. An ordinary folder you
   * can see in any file manager, copy EPUBs into over USB or the network, and
   * which survives uninstalling the app.
   *
   * Without the permission: `getExternalFilesDir()`, which works but sits in
   * `Android/data/`, a path Android 11+ hides from file managers. The reader
   * runs, the library stays empty, and there is no way to put anything there
   * without a cable. That is why the permission is asked for rather than merely
   * accepted if it happens to be there.
   */
  private fun resolveStorageRoot(): File {
    if (Environment.isExternalStorageManager()) {
      val dir = File(Environment.getExternalStorageDirectory(), "CrossPoint")
      if (dir.mkdirs() || dir.isDirectory) {
        // The layout is created here and not by the reader because a folder
        // that already exists is an invitation: you open a file manager, see
        // "books", and know where to drop the EPUB. A folder that only appears
        // after the reader decides to create it teaches nothing.
        //
        // .crosspoint is not here: PersistableStore creates it when needed, and
        // an empty cache folder only confuses.
        File(dir, "books").mkdirs()
        File(dir, "fonts").mkdirs()
        return dir
      }
    }
    val fallback = getExternalFilesDir(null) ?: filesDir
    fallback.mkdirs()
    return fallback
  }

  /**
   * Opens the system screen where the permission is granted.
   *
   * There is no inline dialog for this one: Android requires the user to go to
   * Settings and enable it explicitly, which is the price of a broad
   * permission.
   */
  private fun requestAllFilesAccess() {
    if (Environment.isExternalStorageManager()) {
      return
    }
    runCatching {
      startActivity(
        Intent(
          Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
          Uri.parse("package:$packageName"),
        )
      )
    }.onFailure {
      runCatching { startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) }
    }
  }

  // --- superficie ------------------------------------------------------------

  override fun surfaceCreated(holder: SurfaceHolder) {
    CrossPointNative.nativeSetSurface(holder.surface)
  }

  override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
    // Re-handing it over is cheap and covers the case where the compositor
    // swapped the buffer underneath without destroying the surface.
    CrossPointNative.nativeSetSurface(holder.surface)
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    // Must be SYNCHRONOUS. After this method returns the surface stops being
    // valid, and the reader thread keeps running: if it is still holding the
    // ANativeWindow on the next frame it writes into memory that is no longer
    // its own. The C++ side takes the same mutex as presentation, which makes
    // this call wait for a paint in progress to finish.
    CrossPointNative.nativeSetSurface(null)
  }

  // --- entrada ---------------------------------------------------------------

  @SuppressLint("ClickableViewAccessibility")
  override fun onTouchEvent(event: MotionEvent): Boolean {
    when (event.actionMasked) {
      MotionEvent.ACTION_DOWN -> {
        downX = event.x
        downY = event.y
        downAtMs = event.eventTime
        CrossPointNative.nativeContact(true)
      }
      MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
        CrossPointNative.nativeContact(false)
      }
    }
    gestures.onTouchEvent(event)
    return true
  }

  private fun nx(x: Float) = (x / surfaceView.width.toFloat()).coerceIn(0f, 1f)
  private fun ny(y: Float) = (y / surfaceView.height.toFloat()).coerceIn(0f, 1f)

  /**
   * Classification happens here and not in C++, on purpose.
   *
   * On the Kindle the backend reads the raw evdev stream and decides for itself
   * what is a tap, a long press and a swipe, because on that device nothing
   * else will. Here that piece already exists and is better than the one we
   * would write: [GestureDetector] knows this device's slop, this system's
   * long-press threshold, and the fling velocity the user perceives as
   * deliberate. Reimplementing it in C++ would trade a calibrated part for one
   * with guessed constants.
   */
  private inner class GestureListener : GestureDetector.SimpleOnGestureListener() {

    override fun onDown(e: MotionEvent): Boolean = true

    override fun onSingleTapUp(e: MotionEvent): Boolean {
      CrossPointNative.nativeGesture(
        CrossPointNative.Gesture.TAP.code,
        nx(e.x), ny(e.y), 0f, 0f,
        (e.eventTime - downAtMs).toInt(),
      )
      return true
    }

    override fun onLongPress(e: MotionEvent) {
      // Fires with the finger STILL down, which is the contract CrossPoint
      // expects. The C++ side suppresses the rest of the contact, or the
      // release would read as a tap and dismiss whatever the long press just
      // opened.
      CrossPointNative.nativeGesture(
        CrossPointNative.Gesture.LONG_PRESS.code,
        nx(e.x), ny(e.y), 0f, 0f,
        (e.eventTime - downAtMs).toInt(),
      )
    }

    override fun onFling(
      e1: MotionEvent?,
      e2: MotionEvent,
      velocityX: Float,
      velocityY: Float,
    ): Boolean {
      val start = e1 ?: return false
      CrossPointNative.nativeGesture(
        CrossPointNative.Gesture.SWIPE.code,
        nx(start.x), ny(start.y),
        nx(e2.x), ny(e2.y),
        (e2.eventTime - start.eventTime).toInt(),
      )
      return true
    }
  }

  // --- barras do sistema -----------------------------------------------------

  private fun hideSystemBars() {
    @Suppress("DEPRECATION")
    surfaceView.systemUiVisibility = (
      View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        or View.SYSTEM_UI_FLAG_FULLSCREEN
        or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
        or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
        or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
      )
  }

  override fun onWindowFocusChanged(hasFocus: Boolean) {
    super.onWindowFocusChanged(hasFocus)
    if (hasFocus) {
      hideSystemBars()
    }
  }
}
