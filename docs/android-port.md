# CrossPoint no Bigme HiBreak Pro

Alvo: **Bigme HiBreak Pro**, telefone e-ink com Android. MediaTek
(`idVendor 0x0E8D`), arm64-v8a, número de série `B651DNW2GG1C006000099`.
Resolução e densidade do painel: **ainda não medidas**, dependem de adb.

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

A Bigme não publica SDK. O framework interno chama-se `xrz` e foi levantado por
engenharia reversa por terceiros (`imedwei/inksdk`), num **HiBreak Plus**, não
no Pro. Nada abaixo foi confirmado no nosso aparelho ainda.

```
xrz.framework.manager.XrzEinkManager
  setRefreshModeForSurfaceView(SurfaceView, int)
  setRefreshModeByView(View, int)
  forceGlobalRefresh(int)
xrz.framework.manager.EinkRefreshMode          // tabela int -> waveform
/system/framework/xrz.framework.server.jar     // 124KB, world-readable
ro.vendor.xrz.default_refresh_mode = 178
```

O que torna isso utilizável: o app de notas da própria Bigme que usa essa API
roda em UID de app comum e não é assinado pela plataforma. Um app de terceiro
alcança o mesmo por reflexão, sem permissão.

Mapeamento pretendido, a confirmar:

| CrossPoint | Kindle (FBInk) | Bigme (xrz) |
| --- | --- | --- |
| `FULL_REFRESH` | `WFM_GC16` flashing | `MODE_GC16` |
| `HALF_REFRESH` | `WFM_GL16` | `MODE_GU16` |
| `FAST_REFRESH` | `WFM_DU` | `MODE_HANDWRITE` |

**Decisão de projeto:** a v1 não usa nada disso. Sai pelo caminho Android
padrão e deixa o sistema decidir o refresh, que é o que ele já faz para
qualquer aplicativo que nunca pensou no assunto. O `XrzEinkManager` é a saída
de emergência se a cadência incomodar, e é uma chamada reflexiva de distância.
A HAL já define os três modos, então ligar é ligar, não reprojetar.

Um aviso do material levantado que vale guardar: no Bigme os dois compositores
coexistem, e árvore de views repintando em cadência alta custa caro (1062ms p95
a 30Hz). Um leitor que repinta por virada de página é o perfil bom desse
hardware, não o ruim.

## Estado do aparelho

Ainda não conectado. O adb enumera a interface correta (classe 255, subclasse
66, protocolo 1, dois endpoints) e o aparelho não responde no pipe de leitura:

```
usb_osx.cpp:322  Add usb device B651DNW2GG1C006000099
usb_osx.cpp:631  usb_read failed with status: e00002ed   <- kIOReturnNotResponding
transport.cpp    connection terminated: read failed
```

Lado do Mac inteiro. É o daemon adb do Android que não atende. Enquanto isso
não resolver, tudo nesta seção e a geometria do painel ficam sem medição, e o
perfil `FREEINK_DEVICE_HIBREAK` não pode ser escrito com números de verdade.

## O que ainda não foi decidido

- Pasta de ebooks sob scoped storage. É a parte que não vem de graça de lugar
  nenhum, e provavelmente o maior item de design do porte.
- Ciclo de vida: o CrossPoint tem um loop principal que presume ser dono da
  máquina; uma Activity é pausada, morta e recriada.
- O que compilar fora por capability: servidor web, modo AP, OTA, flasher,
  Calibre. Tudo redundante num telefone.
