# CrossPoint no Bigme HiBreak Pro

Alvo: **Bigme HiBreak Pro**, telefone e-ink com Android. Tudo abaixo foi lido
do aparelho, não inferido.

| | |
| --- | --- |
| Modelo | Bigme HiBreak (`ro.product.model`), build `Bigme_HiBreak_V1.0_20260306` |
| Android | 14, SDK 34 |
| SoC | MediaTek MT6877, arm64-v8a |
| Painel | **824x1648**, densidade **300 dpi** |
| Rotação física | `phy_rotation = 270`, e o Android confirma: `installOrientation ROTATION_270` |
| Recorte | notch de 49px no topo, `Rect(375, 0 - 450, 49)` |
| Escala | `density=1.875`, ou seja 439 x 879 dp lógicos |
| Prazo de apresentação | `presDeadline 31000000` (31ms), não os 16ms de um LCD |
| Série | `B651DNW2GG1C006000099` |
| OEM por trás | xrztech (`ro.build.locale.area`) |

Duas consequências imediatas da geometria:

- **824 / 8 = 103 exato.** O renderer do CrossPoint trabalha com planos de 1bpp
  e `DISPLAY_WIDTH_BYTES = WIDTH / 8`. Largura múltipla de 8 evita a classe de
  bug de padding por linha de cara. Cada plano dá 103 x 1648 = 169.744 bytes.
- **A dpi física reportada é lixo:** `density 300 (188.554 x 667.61) dpi`. Um
  painel e-ink tem pixel quadrado; 188 na horizontal contra 667 na vertical é
  firmware reportando tamanho físico errado. O que vale é a densidade lógica
  300 com escala 1.875. Não usar a dpi física para nada.
- **O notch come os 49px do topo.** O CrossPoint desenha barra de status
  exatamente ali. Vai precisar respeitar o inset, ou o relógio fica embaixo da
  câmera.
- **`installOrientation ROTATION_270`.** O eixo nativo do painel está a um
  quarto de volta do lógico. Pelo caminho Android padrão o sistema resolve; se
  algum dia formos para o buffer ION do `handwrittenservice`, isso volta cru,
  que foi precisamente o que o porte Kindle encontrou.
- **300 dpi contra os ~212 do X4.** A UI do CrossPoint foi desenhada para
  painéis bem menos densos. Em 300 dpi tudo sai fisicamente menor, e as fontes
  são bitmap (EpdFont), não vetoriais. Ou se geram tamanhos maiores, ou se
  renderiza em escala. Isto é trabalho de UI, não de porte, mas é melhor saber
  agora.

Este porte é primo do porte Kindle, não do porte M5PaperS3. Os dois tiram o
CrossPoint do ESP32 e o colocam num sistema operacional que já é dono da
máquina. A diferença é que no Kindle o processo fala com o painel através do
EPDC do kernel, e aqui ele fala com o SurfaceFlinger como qualquer aplicativo
Android, com o framework do fabricante decidindo o waveform.

## Ponto de partida, medido

Censo com o clang do NDK (`aarch64-linux-android28-clang++`, arm64-v8a),
`-fsyntax-only` sobre cada `.cpp` de `src/`, `lib/` e `freeink-sdk/libs`:

| Depois de | Compila | Causa dominante restante |
| --- | --- | --- |
| primeira rodada, camada POSIX do Kindle herdada | 61% (144/235) | `ArduinoJson.h` (73 arquivos) |
| buscar as dependencias declaradas | 94% (221/235) | `CROSSPOINT_VERSION` (6) |
| shims, o conserto do `RecentBook` e a flag do ArduinoJson | **100% (221/221)** | nenhuma |

Duas ressalvas, porque um censo diz menos do que parece. Ele responde "este
arquivo compilaria", nao "isto linka" e muito menos "isto roda". E o
denominador caiu de 235 para 221 porque `FreeInkDisplay/src` saiu da conta:
sao os `PanelDriver` e o `EpdBus`, que falam com um painel cru por SPI ou i80,
e aqui nao ha painel cru. O porte Kindle excluiu pelo mesmo motivo, e foi o
maior passo unico do `trylink` dele.

O proximo numero que importa e o de referencias indefinidas, nao o de arquivos
que compilam.

## O link

`tools/android/trylink.sh`, adaptado do `trylink.sh` do porte Kindle, com as
licoes duras dele intactas: invalidacao de objeto por qualquer header, exclusao
dos `.c` que um wrapper ja inclui, e arquivamento por biblioteca em vez de
objetos soltos (tres copias de miniz nesta arvore tornam isso obrigatorio).

| Depois de | Indefinidas |
| --- | --- |
| primeira tentativa | 20 |
| `third_party/*.cpp` no link e os `.inl` na busca | 18 |
| `-lz` (o Android traz zlib; o PNGdec a usa) | **14** |

Para comparacao, o porte Kindle comecou em 255 e desceu em cinco degraus ate 92
antes de chegar a zero.

**As 14 que restam sao uma coisa so:**

```
crosspoint::kindle::KindleFrameBuffer     11 metodos
  begin, display, displayStart, waitComplete, stageFrame,
  stageGrayOverlay, refresh, reopen, panelContentWasReplaced,
  deepSleep, ~KindleFrameBuffer
crosspoint::kindle::KindleTouchDevice      3 metodos
  begin, update, ~KindleTouchDevice
```

Nada espalhado, nada de rede, nada de sistema de arquivos, nada de FreeRTOS.
A `lib/hal/HalDisplay.cpp` e a `HalGPIO.cpp` desta arvore ainda sao as do
Kindle e chamam o backend de la. Essa lista nao e uma lista de problemas: e a
**especificacao do `lib/hal/android/`**. Implementar esses 14 metodos e o porte
linkar sao a mesma frase.

Duas armadilhas de build que custaram tempo e ficam registradas:

- **`-D` com aspas nao sobrevive a ser escrito dentro do `cc-one.sh`**: o shell
  interno come as aspas e `CROSSPOINT_VERSION` vira identificador em vez de
  string. Os defines agora saem num header gerado com `-include`, que e como o
  porte Kindle ja fazia.
- **Um arquivo que nao compila esconde referencias em vez de criar.** Quando o
  `CROSSPOINT_VERSION` quebrou quatro arquivos, o total continuou 20 e o
  conjunto mudou inteiro. O numero so significa alguma coisa quando se sabe
  quais objetos faltam, entao conferir isso faz parte de ler a medida.

Para comparação, a primeira rodada do porte Kindle deu 25% e ele levou seis
etapas para chegar a 82%. A diferença é toda a camada POSIX que veio pronta.

Das 91 falhas da primeira rodada, 83 eram header de terceiro não vendorizado e
nenhuma era problema de portabilidade. As oito restantes viraram seis consertos,
listados e classificados em [backport-to-kindle.md](backport-to-kindle.md).

O único achado de código real foi o `RecentBook`: `HomeActivity.h` declarava
`struct RecentBook;` adiante e depois tinha um `std::vector<RecentBook>` como
membro. Instanciar os membros de um `vector` com tipo incompleto é mal formado.
O libstdc++ do ESP32 aceita, o libc++ do NDK recusa, e o libc++ está certo. Isso
é bug do CrossPoint, não do Android.

## Os headers que faltam

Todos são dependências já declaradas no `platformio.ini`, todos são C ou C++
portável, nenhum foi buscado:

| Header | Biblioteca | Versão declarada | Arquivos |
| --- | --- | --- | --- |
| `ArduinoJson.h` | bblanchon/ArduinoJson | 7.4.2 | 73 |
| `PNGdec.h` | bitbank2/PNGdec | 1.1.6 | 2 |
| `JPEGDEC.h` | bitbank2/JPEGDEC | pin de commit | 2 |
| `qrcode.h` | ricmoo/QRCode | 0.0.1 | 1 |
| `pngle.h`, `stb_truetype.h` | vendorizadas em outro lugar | | 2 |

`ArduinoJson` sozinho levou o censo de 61% para 94%. Todos estão presos por
versão em [`scripts/fetch-thirdparty.sh`](../scripts/fetch-thirdparty.sh), que é
o que o doc do porte Kindle dizia valer mais do que qualquer shim a mais: virar
a tabela de busca manual num passo de dependência de verdade.

Uma armadilha do ArduinoJson que custou o último arquivo: ele só registra o
conversor para `String` quando detecta ambiente Arduino, olhando por `ARDUINO`.
Com o `String` vindo do nosso shim a detecção não dispara, ele cai no
`std::string` e todo `as<String>()` falha. A flag é
`ARDUINOJSON_ENABLE_ARDUINO_STRING=1`.

## O painel: o que a Bigme expõe

A Bigme não publica SDK. O framework interno chama-se `xrz`. Um levantamento
por engenharia reversa de terceiros (`imedwei/inksdk`) o descreveu num **HiBreak
Plus**; **confirmamos no nosso Pro** que é o mesmo framework, e encontramos mais
do que aquele material descrevia, porque ele olhou o caminho da caneta e não o
do sistema.

### O que está no aparelho, confirmado

```
handwrittenservice  (PID 1066, root)  [com.xrz.IHandwrittenService]
/system/framework/xrz.framework.server.jar          114.534 bytes, world-readable
```

O jar contém só o lado servidor (`DisplayPolicyService`,
`DisplayPolicyController`, `AppFreezeService`, `SplitScreenService`,
`DatabaseHelper`). As classes `xrz.framework.manager.*`, incluindo a
`EinkRefreshMode`, são apenas **referenciadas** aqui: elas moram no
`framework.jar` do boot classpath, que ainda não foi puxado.

Superfície de API lida das strings do dex, bem maior que a publicada:

```
setRefreshMode / getRefreshMode              setLayerRefreshMode
setRefreshModeForPackage(packageName, ...)   getRefreshModeForPackage
setRefreshFrequency / setRefreshFrequencyForPackage
setIsRefreshSetting / setIsRefreshSettingForPackage
forceGlobalRefresh                           getEinkMode
readWaveForm / readFactoryWaveForm           sendGlobalHandwrittenRequest
```

### A escada de modos

Os valores saem das propriedades do sistema, que são as que o próprio aparelho
usa:

| Propriedade | Valor | Leitura |
| --- | --- | --- |
| `ro.vendor.xrz.default_refresh_mode` | 178 | padrão, texto e UI |
| `sys.video_refresh_mode` | 179 | vídeo: o mais rápido, pior qualidade |
| `sys.comic_refresh_mode` | 180 | quadrinhos: tons de cinza |
| `sys.maga_refresh_mode` | -2147483471 | revista |
| `vendor.xrz.logo_refresh_mode` | -2147483471 | logo do boot |
| `vendor.xrz.bootanimation_refresh_mode` | 180 | animação de boot |
| `vendor.xrz.force_global_refresh_mode` | -1 | desligado, e **gravável** |

`-2147483471` é `0x80000000 | 177`. Então **o inteiro carrega flag no bit
alto**, não é enum simples, e os modos base se agrupam em 177, 178, 179, 180.

**Hipótese de mapeamento**, ainda não verificada no painel:

| CrossPoint | Bigme | Por quê |
| --- | --- | --- |
| `FULL_REFRESH` | `0x80000000 \| 177` | o que revista e logo usam: melhor qualidade |
| `HALF_REFRESH` | 178 | o padrão de texto e UI |
| `FAST_REFRESH` | 179 | o de vídeo: mais rápido |
| imagens | 180 | o de quadrinhos: cinza |

### A descoberta que pode dispensar API nenhuma

O `DatabaseHelper` do jar carrega esta tabela:

```sql
CREATE TABLE policy_org (
  package_name text, refresh_mode integer default '-1',
  refresh_frequency integer, app_contrast integer, app_anti_flicker integer,
  app_anti_alias integer, app_text_enhance integer, app_dark_level integer,
  app_color_enhance integer, app_brightness_level integer,
  app_auto_clean integer, app_color_mode integer, app_scroll_flip integer,
  app_dpi integer, ... )
```

**O sistema já guarda modo de refresh por aplicativo.** É isso que o
`com.xrz.sys.control` (rodando, em `/data/app`) expõe ao usuário. Ou seja: para
a v1, provavelmente não precisamos de reflexão, nem de JNI, nem de API nenhuma.
O usuário escolhe o modo do CrossPoint no painel de controle do próprio
aparelho, e o sistema aplica. A API programática vira otimização, não
requisito.

**Decisão de projeto:** a v1 não usa nada disso. Sai pelo caminho Android
padrão e deixa o sistema decidir o refresh, que é o que ele já faz para
qualquer aplicativo que nunca pensou no assunto. O `XrzEinkManager` é a saída
de emergência se a cadência incomodar, e é uma chamada reflexiva de distância.
A HAL já define os três modos, então ligar é ligar, não reprojetar.

Um aviso do material levantado que vale guardar: no Bigme os dois compositores
coexistem, e árvore de views repintando em cadência alta custa caro (1062ms p95
a 30Hz). Um leitor que repinta por virada de página é o perfil bom desse
hardware, não o ruim.

## O adb deste aparelho é instável

Vale registrar porque custou tempo. A interface USB é correta (classe 255,
subclasse 66, protocolo 1) e o adb acha o aparelho, mas a conexão cai depois de
poucos comandos:

```
usb_osx.cpp:322  Add usb device B651DNW2GG1C006000099
usb_osx.cpp:631  usb_read failed with status: e00002ed   <- kIOReturnNotResponding
transport.cpp    connection terminated: read failed      <- em loop
```

O lado do Mac está inteiro; é o daemon do Android que larga a conexão. O
contorno é não depender de sessões longas: um comando por invocação, com retry,
e nada refeito. O jar de 114KB precisou de três tentativas.

Para trabalho de verdade, a depuração sem fio (`adb pair` / `adb connect`)
provavelmente vale mais do que insistir no cabo.

### Ainda não puxado

- `/system/framework/framework.jar`, onde moram `XrzEinkManager` e a
  `EinkRefreshMode` com os valores nomeados. É grande, e sobre esta conexão vai
  doer.

## O que ainda não foi decidido

- Pasta de ebooks sob scoped storage. É a parte que não vem de graça de lugar
  nenhum, e provavelmente o maior item de design do porte.
- Ciclo de vida: o CrossPoint tem um loop principal que presume ser dono da
  máquina; uma Activity é pausada, morta e recriada.
- O que compilar fora por capability: servidor web, modo AP, OTA, flasher,
  Calibre. Tudo redundante num telefone.


## O backend, e o refactor que ele forcou

Escrever um `HalDisplayAndroid.cpp` ao lado do `HalDisplayKindle.cpp` teria
duplicado 345 linhas. Os dois aparelhos nao tem nada em comum no hardware, mas
o HalDisplay precisa da mesma sequencia dos dois (compoe, encena a base, pinta
os planos de cinza por cima, apresenta uma vez), e o arquivo do Kindle ja
estava escrito contra essa forma.

Entao a guarda deixou de ser `FREEINK_DEVICE_KINDLE` e passou a ser
`FREEINK_MCU_HOSTED`, que o BoardConfig ja derivava:

| Antes | Agora | Linhas |
| --- | --- | --- |
| `HalDisplayKindle.cpp` | `HalDisplayHosted.cpp` | 345 |
| `HalGPIOKindle.cpp` | `HalGPIOHosted.cpp` | 206 |
| `HalSystemKindle.cpp` | `HalSystemHosted.cpp` | 231 |

`lib/hal/hosted/HostedPanel.h` escolhe o painel e o toque em tempo de
compilacao. Sem classe base virtual de proposito: um binario por aparelho, e
uma vtable so pagaria indirecao por uma decisao que o preprocessador ja tomou.

O que e novo e especifico:

```
lib/hal/hosted/HostedGray.{h,cpp}     expansao 1bpp->8bpp e overlay de cinza
lib/hal/hosted/HostedTouch.h          Gesture, GestureResult, TouchTuning
lib/hal/hosted/HostedPanel.h          o seletor
lib/hal/android/AndroidPanel.{h,cpp}  ANativeWindow, 824x1648, RGBX_8888
lib/hal/android/AndroidTouchDevice.*  caixa de correio, nao classificador
lib/hal/android/Jni.cpp               as quatro travessias da fronteira
```

### Duas decisoes que valem registro

**O toque nao e classificado em C++.** No Kindle o backend le evdev cru e
decide sozinho o que e toque, toque longo e swipe, porque ninguem mais vai
fazer isso. No Android essa peca ja existe e e melhor do que a que
escreveriamos: o `GestureDetector` conhece o slop do aparelho e os limiares do
sistema. Entao a classificacao fica no Kotlin e o que atravessa o JNI e o
gesto pronto. O `AndroidTouchDevice` e so o encaixe de threads.

**Sem superficie nao e erro.** Uma Activity e pausada e destruida sob os pes do
processo, e a thread do CrossPoint continua viva e pintando. O quadro composto
sobrevive em `stage` e e reapresentado quando a superficie volta. Esta e a
diferenca de fundo entre este alvo e os outros dois, onde o painel esta sempre
la.

### O link

```
0 referencias indefinidas
```

E aqui vale a mesma desconfianca que o resto deste documento: **linkar nao e
funcionar.** O primeiro rename do `HalSystemKindle.cpp` para `Hosted` linkou
perfeitamente enquanto levava junto `access("/mnt/us/crosspoint/crosspoint")` e
uma leitura de bateria por `lipc-get-prop com.lab126.powerd`. Num HiBreak as
duas falham em silencio e o medidor de bateria le 0. Foi separado por aparelho
depois, mas o link nao teria reclamado nunca.

## O .so existe

`cmake/android/CMakeLists.txt` constroi de verdade, com o toolchain do NDK:

```
libcrosspoint.so   ELF 64-bit LSB shared object, ARM aarch64
                   41MB com simbolos, 7,9MB depois do strip
                   4 simbolos JNI exportados, o resto escondido
```

Isto e mais forte que o trylink: o trylink pergunta "linkaria", isto linkou.

### O que o CMake teve de aprender

Tres coisas, e as tres estao comentadas no arquivo porque nenhuma e obvia:

**Arquivar por biblioteca, nao linkar objetos soltos.** Ha tres copias de
miniz nesta arvore, cada uma com uma config que prefixa so parte dos simbolos,
e o resto colide. O PlatformIO nunca ve o problema porque arquiva cada
biblioteca: o linker entao puxa um membro so quando ele resolve algo ainda
indefinido, e duplicata entre arquivos e "o primeiro vence". A primeira versao
deste CMakeLists linkava solto e produziu dezenas de erros de multipla
definicao, exatamente como o comentario do `trylink.sh` do Kindle avisava.

**Uma copia de expat, nao duas.** O CrossPoint e o SDK vendorizam expat cada
um e os dois exportam os mesmos `XML_*` sem prefixo. Vence o do SDK, pelos
mesmos dois motivos que o porte Kindle registrou: carrega um `expat_config.h`
de verdade em vez de depender de flags, e o include dele ja vem primeiro.

**`-fvisibility=hidden`, e nao por higiene de ABI.** Numa biblioteca
COMPARTILHADA tudo e exportado por padrao, entao nada e morto e o
`--gc-sections` nao descarta nada. Os helpers de checksum do uzlib
(`uzlib_crc32`, `uzlib_adler32`) sao declarados e chamados de uma funcao que
nada alcanca, e sem visibilidade escondida apareciam como referencia
indefinida para codigo que nunca roda.

### Um achado upstream

`MySerialImpl` e declarado em `lib/Logging/Logging.h` e **nao e definido em
lugar nenhum desta arvore**: nem o membro estatico `instance`, nem `write()`,
nem `flush()`, nem `printf()`. O unico membro com corpo e o `operator bool()`,
inline no header.

Isso nao e do porte. `src/main.cpp:813` tem `if (Serial && ...)` fora de
qualquer guarda, entao a referencia e emitida sempre. Esta definido agora em
`lib/hal/posix/SerialProxyPosix.cpp`, que precisa ser unidade de traducao
propria: o `Logging.h` termina com `#define Serial MySerialImpl::instance`, e
incluir esse header dentro do `ArduinoShim.cpp` transforma o
`HardwareSerial Serial;` de la em redefinicao com tipo diferente.

## O lado Kotlin

```
app/src/main/java/org/crosspoint/hibreak/
  CrossPointNative.kt    as quatro travessias, e nada mais
  ReaderActivity.kt      SurfaceView + GestureDetector
app/src/main/AndroidManifest.xml
cmake/android/CMakeLists.txt
build.gradle.kts, settings.gradle.kts, app/build.gradle.kts
```

A Activity nao tem layout XML e nao tem view alguma alem da superficie. O
CrossPoint desenha a interface dele do zero em 1bpp; qualquer widget Android
aqui seria uma segunda interface disputando a mesma tela.

Tres decisoes registradas no codigo:

- **`surfaceDestroyed` chama `nativeSetSurface(null)` sincronamente.** Depois
  que esse metodo retorna a superficie deixa de ser valida e a thread do leitor
  continua rodando. O lado C++ toma o mesmo mutex da apresentacao, o que faz a
  chamada esperar um paint em andamento terminar.
- **A tela nao apaga.** Num e-ink isso custa quase nada, porque o painel so
  consome ao mudar.
- **O notch nao e tratado.** A tela vai inteira para baixo do recorte de 49px e
  o leitor nao precisa saber que ele existe. Recuperar esses pixels exigiria
  `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES` e ensinar o CrossPoint sobre o
  inset.

## Estado, sem arredondar

| | |
| --- | --- |
| Censo | 100% (224/224) |
| Trylink | 0 indefinidas |
| `libcrosspoint.so` | **construido e verificado** |
| APK | **nao construido** |
| Rodando no aparelho | **nao** |

O APK nao existe porque esta maquina nao tem JDK, nem Gradle, nem Android SDK.
Os arquivos do Gradle estao escritos e nao foram executados nenhuma vez, o que
quer dizer que podem ter erros bobos que so um build revela. O `.so`, esse sim,
foi construido de verdade.

## O bug que custou mais caro, e como ele foi achado

O download de livros pelo OPDS fechava o aplicativo. Sem mensagem, sem erro, sem
nada no log: o processo simplesmente sumia.

**Três hipóteses erradas antes da certa**, e vale registrar as tres porque duas
delas viraram consertos legitimos mesmo sem serem o bug:

1. *"O Android 11 fechou o NETLINK e o getifaddrs nao enxerga as interfaces."*
   Falso neste aparelho, provado por um log que imprimiu as duas respostas lado
   a lado: `framework=1 getifaddrs=1`. O conserto (usar o ConnectivityManager)
   ficou por outro motivo, que e bom: ele distingue "tem endereco" de "tem
   internet", e um portal cativo da a primeira e nao a segunda.
2. *"Excecao pendente no JNI."* O `GetStaticMethodID` com assinatura errada
   lanca `NoSuchMethodError` e deixa a excecao pendente; a proxima chamada JNI
   feita assim aborta a VM. Era um caminho de crash real e foi consertado, mas
   nao era este crash.
3. *"Um `Error` escapando pela fronteira JNI."* O bridge capturava `Exception`,
   que nao cobre `OutOfMemoryError`. Tambem real, tambem consertado, tambem nao
   era.

**O que resolveu foi parar de deduzir.** Um handler de sinal registrando o
sinal da morte deu os dois fatos que decidiram:

```
*** morreu com sinal 11 (Segmentation fault), endereco 0xe5c ***
```

- **SIGSEGV e nao SIGABRT** eliminou de uma vez as duas familias que eu vinha
  perseguindo: nao era a runtime reclamando de JNI nem falta de memoria na ART.
- **O ponto da morte MUDAVA entre execucoes.** Uma vez dentro do `finish()` do
  JNI, outra antes dele chegar. Ponteiro nulo fixo nao anda pelo codigo;
  estouro de pilha anda, porque o endereco que falta depende da profundidade em
  que se estava.

A thread do leitor subia com `std::thread`, que no bionic pega o padrao de
**1MB**. E pouco para esta arvore: no ESP32 as tarefas tem pilha dimensionada a
mao justamente porque o parser de XML, o layout de capitulo e a cadeia de render
descem fundo, e o proprio CrossPoint carrega um `TaskWatchdog` e medicoes de
high water mark por causa disso. Agora sao **8MB**, via
`pthread_attr_setstacksize`, que e o que a thread principal de um processo
Android ja tem.

### O que fica de metodo

O handler de sinal fica no binario para sempre. Ele transforma "o aplicativo
sumiu" em "SIGSEGV no endereco tal", e essa diferenca decidiu um bug que tres
rodadas de leitura de codigo nao tinham decidido.

O log em arquivo tambem, e com uma lição: ele era aberto TRUNCANDO a cada
abertura do aplicativo. O caso em que o log importa e quando o processo morre, e
a unica forma de ler o arquivo e reabrindo o aplicativo, que era exatamente o
que apagava a evidencia. Duas sessoes de depuracao foram perdidas assim antes de
alguem notar. Agora a execucao anterior vira `crosspoint.log.anterior`.

## Fontes

Os corpos foram decididos lendo no aparelho, nao por escala.

Eu previ que os 300 dpi exigiriam multiplicar os corpos por 1,42 em relacao aos
~212 dpi do X4. Medido com os olhos: 18 e agradavel, 16 e bom, 14 e legivel, 12
e pequeno demais, e 22 e 24 sao grandes demais para uso real. A lista embutida e
`{14, 16, 18, 20}`.

A interface usa outra familia (Ubuntu, dois estilos, fundida com um recorte
vietnamita). Os temas pedem `UI_10_FONT_ID` e `UI_12_FONT_ID` em dezenas de
lugares, entao o que muda neste aparelho e o que cada ID ENTREGA, nao cada
chamada: o ID e uma chave, nao uma medida.

```
SMALL   -> ubuntu 12    barra de status (12 usos no tema, contra 3 do UI_10)
UI_10   -> ubuntu 14    linhas de configuracao
UI_12   -> ubuntu 16    corpo da interface
```

### Simbolos

Os quadrados com "?" nao eram configuracao. **Nenhuma fonte de origem do
repositorio tinha aqueles glifos**: NotoSerif e Ubuntu davam 0 de 112 setas e 1
de 96 formas geometricas. Os intervalos estavam ligados no conversor e nao havia
o que converter.

Duas fontes entraram na pilha porque uma nao bastava: a Symbols2 cobre formas
geometricas (96/96) e dingbats (145/192) mas so 13 de 112 setas e nao tem a
U+2192; a Math tem 99 de 112 setas e tem a U+2192.

E os intervalos padrao tiveram de ser estreitados junto, o que nao e economia de
enfeite: enquanto nenhuma fonte tinha esses glifos, pedir os blocos inteiros de
Setas e Matematica custava **zero**. Com a Math na pilha eles passaram a ser
encontrados e a custar +180KB por arquivo. Estreitados para o que aparece em
texto corrido, o custo e +27,6KB (13%).

Arabe, hebraico, cirilico e grego sairam: este porte le em portugues e ingles.
Arabe e hebraico sozinhos eram metade do peso da fonte de interface, medido em
440,8 KB contra 219,3 KB no corpo 16.

## A margem inferior

O leitor faz `orientedMarginBottom += std::max(screenMargin, statusBarHeight)`.
**Maximo, nao soma.** Com a barra em 46px e o maximo da configuracao em 40, a
margem inferior nunca teve efeito neste aparelho: a barra sempre ganhava.

Maximo agora 130 (~11mm a 300 dpi), passo 10, padrao 60. Como 60 > 46, a margem
existe ja na primeira abertura.

E a propria barra nao respeitava a margem lateral: o leitor soma `screenMargin`
ao recuo do bezel antes de compor a pagina, e a barra nao somava. Com a margem
pequena isso passava despercebido; num painel de cantos arredondados, com a
margem em 60px, as pontas caiam fora da area visivel.
