package org.crosspoint.hibreak

import android.util.Log
import java.io.InputStream
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.atomic.AtomicInteger

/**
 * HTTP for the C++ side, and there is one reason it exists: TLS.
 *
 * The POSIX shim inherited from the Kindle port speaks HTTP over raw sockets
 * and REFUSES https explicitly, with no silent downgrade. The refusal is right
 * (sending OPDS credentials in the clear would be worse than failing), but it
 * rules out almost every real catalogue and KOReader sync.
 *
 * The two ways out were embedding mbedtls in C++ or coming here. Here TLS
 * already exists, uses the SYSTEM trust store (which this device keeps current
 * on its own, and which C++ would have to carry and let age along with the
 * binary), honours proxies and VPNs, and costs no new dependency.
 *
 * The shape is streaming rather than "hand me the whole body", because that is
 * how the C++ caller is written: esp_http_client opens, reads in pieces and
 * closes. A 40MB book should not pass through memory all at once.
 *
 * Threading: called from the reader thread, which is native and attaches to the
 * JVM. The connection map is synchronised because nothing guarantees it will
 * stay a single thread.
 */
object CrossPointHttp {

    // Throwable rather than Exception in every catch here, and the difference
    // is not pedantry: OutOfMemoryError and StackOverflowError are Error, not
    // Exception. An Error escaping a function called from JNI crosses the
    // boundary into a C++ that has no way to handle it, and the runtime aborts
    // the process. Better to return -1 and let the caller report failure.
    private const val TAG = "CrossPointHttp"

    private class Conn(val http: HttpURLConnection) {
        var out: OutputStream? = null
        var input: InputStream? = null
        var status: Int = -1
    }

    private val conns = HashMap<Int, Conn>()
    private val nextHandle = AtomicInteger(1)

    /**
     * Opens the connection and sends the headers. Does NOT read the response
     * yet: anyone with a body to send writes before [finish].
     *
     * [headers] arrives packed as "Key: Value" lines, which is how the C++ side
     * already keeps them and avoids crossing the boundary with a map.
     *
     * Returns the handle, or -1 on failure.
     */
    @JvmStatic
    fun open(method: String, url: String, headers: String, hasBody: Boolean, timeoutMs: Int): Int {
        return try {
            val http = URL(url).openConnection() as HttpURLConnection
            http.requestMethod = method
            http.connectTimeout = timeoutMs
            http.readTimeout = timeoutMs
            // Redirects stay with the caller: CrossPoint has its own logic in
            // esp_http_client_set_redirection, and two layers following the
            // same 302 would lose the second one's headers.
            http.instanceFollowRedirects = false
            for (line in headers.lineSequence()) {
                val i = line.indexOf(':')
                if (i > 0) {
                    http.setRequestProperty(line.substring(0, i).trim(), line.substring(i + 1).trim())
                }
            }
            if (hasBody) {
                http.doOutput = true
                http.setChunkedStreamingMode(0)
            }
            val handle = nextHandle.getAndIncrement()
            val conn = Conn(http)
            if (hasBody) conn.out = http.outputStream
            synchronized(conns) { conns[handle] = conn }
            handle
        } catch (e: Throwable) {
            Log.w(TAG, "open failed: $url", e)
            -1
        }
    }

    /** Writes the request body. Returns bytes written, or -1. */
    @JvmStatic
    fun write(handle: Int, data: ByteArray, len: Int): Int {
        val conn = get(handle) ?: return -1
        return try {
            conn.out?.write(data, 0, len) ?: return -1
            len
        } catch (e: Throwable) {
            Log.w(TAG, "write failed", e); -1
        }
    }

    /**
     * Closes the request body, reads the status line and leaves the response
     * body ready for [read]. Returns the HTTP code, or -1.
     *
     * getErrorStream for 4xx and 5xx: getInputStream throws in those cases, and
     * the error body usually says what the server disliked.
     */
    @JvmStatic
    fun finish(handle: Int): Int {
        val conn = get(handle) ?: return -1
        return try {
            conn.out?.flush()
            conn.out?.close()
            conn.out = null
            conn.status = conn.http.responseCode
            conn.input = try {
                conn.http.inputStream
            } catch (e: Throwable) {
                conn.http.errorStream
            }
            conn.status
        } catch (e: Throwable) {
            Log.w(TAG, "finish failed", e); -1
        }
    }

    /** Reads up to buf.size bytes. Returns how many, 0 at the end, -1 on error. */
    @JvmStatic
    fun read(handle: Int, buf: ByteArray): Int {
        val conn = get(handle) ?: return -1
        val input = conn.input ?: return -1
        return try {
            val n = input.read(buf, 0, buf.size)
            if (n < 0) 0 else n
        } catch (e: Throwable) {
            Log.w(TAG, "read failed", e); -1
        }
    }

    /** A response header, or null. */
    @JvmStatic
    fun header(handle: Int, name: String): String? = get(handle)?.http?.getHeaderField(name)

    /** The size announced by Content-Length, or -1 when there is none. */
    @JvmStatic
    fun contentLength(handle: Int): Long = get(handle)?.http?.contentLengthLong ?: -1L

    @JvmStatic
    fun close(handle: Int) {
        val conn = synchronized(conns) { conns.remove(handle) } ?: return
        runCatching { conn.out?.close() }
        runCatching { conn.input?.close() }
        runCatching { conn.http.disconnect() }
    }

    private fun get(handle: Int): Conn? = synchronized(conns) { conns[handle] }
}
