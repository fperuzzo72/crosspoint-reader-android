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
