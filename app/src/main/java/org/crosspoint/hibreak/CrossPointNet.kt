package org.crosspoint.hibreak

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities

/**
 * Network state for the C++ side.
 *
 * The POSIX shim answers "connected" by asking getifaddrs whether an interface
 * is up with an IPv4 address. ConnectivityManager answers better, and for a
 * reason that has nothing to do with permissions: it distinguishes "has an
 * address" from "has internet". A hotel captive portal gives an address and no
 * internet, and VALIDATED is the difference.
 *
 * Uses ACCESS_NETWORK_STATE, already in the manifest. Does not use location:
 * nothing here needs the network's name.
 */
object CrossPointNet {

    private var appContext: Context? = null

    /** Called once by the Activity, before starting the reader thread. */
    @JvmStatic
    fun init(context: Context) {
        appContext = context.applicationContext
    }

    private fun cm(): ConnectivityManager? =
        appContext?.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager

    /** There is a network with actual internet, not merely an assigned address. */
    @JvmStatic
    fun isOnline(): Boolean {
        val cm = cm() ?: return false
        val net = cm.activeNetwork ?: return false
        val caps = cm.getNetworkCapabilities(net) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
    }

    /**
     * Local IPv4 in network order (the same layout as the shim's sin_addr), or 0
     * when there is none.
     */
    @JvmStatic
    fun localIpV4(): Int {
        val cm = cm() ?: return 0
        val net = cm.activeNetwork ?: return 0
        val link = cm.getLinkProperties(net) ?: return 0
        for (addr in link.linkAddresses) {
            val bytes = addr.address.address
            if (bytes.size == 4) {
                // Network order: first octet in the low byte, which is how
                // sin_addr reaches IPAddress on the C++ side.
                return (bytes[0].toInt() and 0xFF) or
                    ((bytes[1].toInt() and 0xFF) shl 8) or
                    ((bytes[2].toInt() and 0xFF) shl 16) or
                    ((bytes[3].toInt() and 0xFF) shl 24)
            }
        }
        return 0
    }
}
