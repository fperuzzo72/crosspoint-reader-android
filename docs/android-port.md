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

| Quando | Compila | Causa dominante |
| --- | --- | --- |
| primeira rodada, camada POSIX do Kindle herdada | **61% (144/235)** | `ArduinoJson.h` (73 arquivos) |

Para comparação, a primeira rodada do porte Kindle deu 25%. A diferença é toda
a camada POSIX que veio pronta.

Das 91 falhas, 83 são header de terceiro que não está vendorizado, e nenhuma
delas é problema de portabilidade. As 8 restantes são reais e pequenas:

```
2  use of undeclared identifier 'esp_restart'
2  use of undeclared identifier 'CROSSPOINT_VERSION'
1  use of undeclared identifier 'portYIELD_FROM_ISR'
1  use of undeclared identifier 'ADC_11db'
1  expected ')'
1  arithmetic on a pointer to an incomplete type 'RecentBook'
```

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

`ArduinoJson` sozinho leva o censo de 61% para perto de 92%.

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
