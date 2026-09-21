package org.crosspoint.hibreak

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities

/**
 * Estado da rede para o lado C++.
 *
 * O shim POSIX responde "conectado" perguntando ao getifaddrs se existe
 * interface no ar com endereco IPv4. Isso vale num Kindle e nao vale aqui: o
 * Android 11 fechou o acesso a NETLINK para aplicativo comum, entao o
 * getifaddrs tende a enxergar so o loopback, que o filtro descarta. O
 * resultado e o CrossPoint concluir que nao ha rede e abrir a tela de escolha
 * de Wi-Fi, que neste aparelho nao tem o que escolher porque quem associa e o
 * sistema.
 *
 * O ConnectivityManager responde melhor por um segundo motivo, independente da
 * restricao: ele distingue "tem endereco" de "tem internet". Um portal
 * cativo de hotel da endereco e nao da internet, e VALIDATED e a diferenca.
 *
 * Usa ACCESS_NETWORK_STATE, que ja esta no manifesto. Nao usa localizacao:
 * nada aqui precisa do nome da rede.
 */
object CrossPointNet {

    private var appContext: Context? = null

    /** Chamado uma vez pela Activity, antes de subir a thread do leitor. */
    @JvmStatic
    fun init(context: Context) {
        appContext = context.applicationContext
    }

    private fun cm(): ConnectivityManager? =
        appContext?.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager

    /** Ha rede com internet de fato, nao apenas um endereco atribuido. */
    @JvmStatic
    fun isOnline(): Boolean {
        val cm = cm() ?: return false
        val net = cm.activeNetwork ?: return false
        val caps = cm.getNetworkCapabilities(net) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
    }

    /**
     * IPv4 local em ordem de rede (o mesmo formato que o sin_addr do shim),
     * ou 0 quando nao ha.
     */
    @JvmStatic
    fun localIpV4(): Int {
        val cm = cm() ?: return 0
        val net = cm.activeNetwork ?: return 0
        val link = cm.getLinkProperties(net) ?: return 0
        for (addr in link.linkAddresses) {
            val bytes = addr.address.address
            if (bytes.size == 4) {
                // Ordem de rede: primeiro octeto no byte baixo, que e como o
                // sin_addr chega ao IPAddress do lado C++.
                return (bytes[0].toInt() and 0xFF) or
                    ((bytes[1].toInt() and 0xFF) shl 8) or
                    ((bytes[2].toInt() and 0xFF) shl 16) or
                    ((bytes[3].toInt() and 0xFF) shl 24)
            }
        }
        return 0
    }
}
