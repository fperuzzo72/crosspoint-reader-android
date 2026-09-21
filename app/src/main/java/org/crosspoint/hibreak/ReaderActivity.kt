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
 * A Activity inteira do CrossPoint. Uma SurfaceView, um GestureDetector, e a
 * ponte.
 *
 * Nao ha layout XML e nao ha view alguma alem da superficie. O CrossPoint
 * desenha a interface dele por conta propria, do zero, em 1bpp: qualquer
 * widget Android aqui seria uma segunda interface disputando a mesma tela.
 */
class ReaderActivity : Activity(), SurfaceHolder.Callback {

  private lateinit var surfaceView: SurfaceView
  private lateinit var gestures: GestureDetector

  /**
   * Onde o dedo pousou no contato em andamento, em pixels. Guardado porque o
   * swipe precisa do inicio e o [GestureDetector] so entrega o evento inicial
   * em alguns callbacks, nao em todos.
   */
  private var downX = 0f
  private var downY = 0f
  private var downAtMs = 0L

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    // A tela nao apaga enquanto se le. Num e-ink isso custa quase nada,
    // porque o painel so consome ao mudar, e uma tela que apaga no meio de uma
    // pagina e o tipo de coisa que faz perder a linha.
    window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

    surfaceView = SurfaceView(this)
    surfaceView.holder.addCallback(this)
    setContentView(surfaceView)

    // O notch come os 49px do topo (medido: Rect(375, 0 - 450, 49)). O
    // CrossPoint desenha a propria barra de status exatamente ali, entao a
    // tela vai inteira para baixo do recorte e o leitor nao precisa saber que
    // ele existe. Se um dia quisermos os 49px de volta, a alternativa e
    // LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES e ensinar o CrossPoint sobre
    // o inset.
    hideSystemBars()

    gestures = GestureDetector(this, GestureListener())
    // O detector do Android nao entrega toque longo pelo caminho normal a
    // menos que se peca; e queremos, porque e como o CrossPoint abre menu de
    // contexto e apaga livro.
    gestures.setIsLongpressEnabled(true)

    // Sem a permissao nao se comeca, e isto nao e rigor: a raiz e lida UMA
    // vez, dentro do setup() do CrossPoint, e o nativeStart e idempotente.
    // Subir com a pasta escondida e conceder a permissao depois deixaria o
    // leitor presa nela ate o processo morrer, o que e pior do que nao subir,
    // porque parece funcionar.
    if (!Environment.isExternalStorageManager()) {
      Toast.makeText(this, R.string.needs_all_files, Toast.LENGTH_LONG).show()
      requestAllFilesAccess()
      finish()
      return
    }

    // Antes da raiz e antes da thread: o C++ pergunta o estado da rede assim
    // que alguem abre o OPDS, e sem contexto a resposta seria "sem rede".
    CrossPointNet.init(this)

    val root = resolveStorageRoot()
    CrossPointNative.nativeSetStorageRoot(root.absolutePath)

    CrossPointNative.nativeStart()
  }

  /**
   * Onde os livros ficam.
   *
   * Com acesso a todos os arquivos concedido: `/sdcard/CrossPoint`. E uma
   * pasta comum, que voce enxerga em qualquer gerenciador de arquivos, copia
   * EPUB para dentro por USB ou LocalSend, e que sobrevive a desinstalar o
   * aplicativo.
   *
   * Sem a permissao: `getExternalFilesDir()`, que funciona mas fica em
   * `Android/data/`, um caminho que o Android 11+ esconde de gerenciadores de
   * arquivos. O leitor roda, a biblioteca fica vazia, e nao ha como pôr nada
   * la sem cabo. Por isso a permissao e pedida, e nao apenas aceita se vier.
   */
  private fun resolveStorageRoot(): File {
    if (Environment.isExternalStorageManager()) {
      val dir = File(Environment.getExternalStorageDirectory(), "CrossPoint")
      if (dir.mkdirs() || dir.isDirectory) {
        // O layout e criado aqui e nao pelo leitor porque uma pasta que ja
        // existe e um convite: voce abre o gerenciador de arquivos, ve
        // "books", e sabe onde soltar o EPUB. Uma pasta que so aparece depois
        // que o leitor decide cria-la nao ensina nada.
        //
        // .crosspoint nao esta aqui: o PersistableStore cria quando precisa, e
        // uma pasta de cache vazia so confunde.
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
   * Abre a tela do sistema onde a permissao e concedida.
   *
   * Nao ha dialogo em linha para esta: o Android exige que o usuario va aos
   * Ajustes e ligue explicitamente, que e o preco de uma permissao ampla. Se
   * ele nao ligar, o leitor continua funcionando com a pasta escondida.
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
    // Reentregar e barato e cobre o caso em que o compositor trocou o buffer
    // por baixo sem destruir a superficie.
    CrossPointNative.nativeSetSurface(holder.surface)
  }

  override fun surfaceDestroyed(holder: SurfaceHolder) {
    // Tem de ser SINCRONO. Depois que este metodo retorna a superficie deixa
    // de ser valida, e a thread do leitor continua rodando: se ela ainda
    // estiver segurando o ANativeWindow no proximo frame, escreve em memoria
    // que nao e mais dela. O lado C++ toma o mesmo mutex da apresentacao, o
    // que faz esta chamada esperar um paint em andamento terminar.
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
   * A classificacao acontece aqui e nao em C++, e de proposito.
   *
   * No Kindle o backend le o stream evdev cru e decide sozinho o que e toque,
   * toque longo e swipe, porque naquele aparelho ninguem mais vai fazer isso.
   * Aqui essa peca ja existe e e melhor do que a que escreveriamos: o
   * [GestureDetector] conhece o slop deste aparelho, o limiar de toque longo
   * deste sistema e a velocidade de fling que o usuario percebe como
   * intencional. Reimplementar isso em C++ seria trocar uma peca calibrada por
   * uma com constantes chutadas.
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
      // Dispara com o dedo AINDA em baixo, que e o contrato que o CrossPoint
      // espera. O lado C++ suprime o resto do contato, senao a soltura viraria
      // um toque e fecharia o que o toque longo acabou de abrir.
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
