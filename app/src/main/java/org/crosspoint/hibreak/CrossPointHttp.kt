package org.crosspoint.hibreak

import android.util.Log
import java.io.InputStream
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.atomic.AtomicInteger

/**
 * HTTP para o lado C++, e a razao de ele existir e uma so: TLS.
 *
 * O shim POSIX herdado do porte Kindle fala HTTP sobre socket cru e RECUSA
 * https explicitamente, sem downgrade silencioso. A recusa esta certa (mandar
 * credencial de OPDS em claro seria pior que falhar), mas deixa de fora quase
 * todo catalogo real e a sincronizacao KOReader.
 *
 * As duas saidas eram embarcar mbedtls no C++ ou vir para ca. Aqui o TLS ja
 * existe, usa a loja de certificados do SISTEMA (que este aparelho mantem
 * atualizada sozinho, e que o C++ teria de carregar e envelhecer junto com o
 * binario), respeita proxy e VPN, e nao custa nenhuma dependencia nova.
 *
 * O formato e de streaming e nao de "me devolve o corpo inteiro", porque e
 * assim que o chamador C++ e escrito: o esp_http_client abre, le em pedacos e
 * fecha. Um livro de 40MB nao deve passar pela memoria de uma vez so.
 *
 * Threading: chamado da thread do leitor, que e nativa e se anexa a JVM. O
 * mapa de conexoes e sincronizado porque nada garante que continuara sendo uma
 * thread so.
 */
object CrossPointHttp {

    private const val TAG = "CrossPointHttp"

    private class Conn(val http: HttpURLConnection) {
        var out: OutputStream? = null
        var input: InputStream? = null
        var status: Int = -1
    }

    private val conns = HashMap<Int, Conn>()
    private val nextHandle = AtomicInteger(1)

    /**
     * Abre a conexao e envia os cabecalhos. NAO le a resposta ainda: quem
     * tiver corpo para mandar escreve antes de [finish].
     *
     * [headers] vem empacotado como linhas "Chave: Valor", que e como o lado
     * C++ ja guarda e evita atravessar a fronteira com um mapa.
     *
     * Retorna o handle, ou -1 em falha.
     */
    @JvmStatic
    fun open(method: String, url: String, headers: String, hasBody: Boolean, timeoutMs: Int): Int {
        return try {
            val http = URL(url).openConnection() as HttpURLConnection
            http.requestMethod = method
            http.connectTimeout = timeoutMs
            http.readTimeout = timeoutMs
            // Redirecionamento fica com o chamador: o CrossPoint tem a própria
            // lógica em esp_http_client_set_redirection, e duas camadas
            // seguindo o mesmo 302 perderiam os cabeçalhos da segunda.
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
        } catch (e: Exception) {
            Log.w(TAG, "open falhou: $url", e)
            -1
        }
    }

    /** Escreve corpo da requisicao. Retorna bytes escritos, ou -1. */
    @JvmStatic
    fun write(handle: Int, data: ByteArray, len: Int): Int {
        val conn = get(handle) ?: return -1
        return try {
            conn.out?.write(data, 0, len) ?: return -1
            len
        } catch (e: Exception) {
            Log.w(TAG, "write falhou", e); -1
        }
    }

    /**
     * Fecha o corpo da requisicao, le a linha de status e deixa o corpo da
     * resposta pronto para [read]. Retorna o codigo HTTP, ou -1.
     *
     * getErrorStream para 4xx e 5xx: o getInputStream lanca nesses casos, e o
     * corpo do erro costuma dizer o que o servidor nao gostou.
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
            } catch (e: Exception) {
                conn.http.errorStream
            }
            conn.status
        } catch (e: Exception) {
            Log.w(TAG, "finish falhou", e); -1
        }
    }

    /** Le ate buf.size bytes. Retorna quantos, 0 no fim, -1 em erro. */
    @JvmStatic
    fun read(handle: Int, buf: ByteArray): Int {
        val conn = get(handle) ?: return -1
        val input = conn.input ?: return -1
        return try {
            val n = input.read(buf, 0, buf.size)
            if (n < 0) 0 else n
        } catch (e: Exception) {
            Log.w(TAG, "read falhou", e); -1
        }
    }

    /** Cabecalho da resposta, ou null. */
    @JvmStatic
    fun header(handle: Int, name: String): String? = get(handle)?.http?.getHeaderField(name)

    /** Tamanho anunciado pelo Content-Length, ou -1 quando nao ha. */
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
