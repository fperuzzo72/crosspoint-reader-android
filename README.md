# CrossPoint Reader no Bigme HiBreak Pro

Um porte do [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
para rodar como aplicativo Android num telefone e-ink, em vez de como firmware
num microcontrolador.

**Funciona no aparelho.** Lê EPUB, navega catálogos OPDS por HTTPS, instala
fontes do cartão, e responde a toque. Não é uma prova de conceito.

---

## O que é isto

O CrossPoint é firmware de e-reader escrito para aparelhos ESP32, onde ele é
dono da máquina inteira: 380KB de RAM, um painel e-ink no barramento SPI, e
nenhum sistema operacional entre ele e o hardware.

Um Bigme HiBreak Pro não é isso. É um telefone Android com kernel, userspace e
interface próprios, onde o painel pertence ao sistema. Então este porte não
substitui nada: ele roda como aplicativo, ao lado de tudo o mais.

O motor de leitura não mudou. O que se escreveu aqui foi a camada de baixo.

### O aparelho, medido

| | |
| --- | --- |
| Modelo | Bigme HiBreak Pro, build `Bigme_HiBreak_V1.0_20260306` |
| Android | 14 (SDK 34) |
| SoC | MediaTek MT6877, arm64-v8a |
| Painel | 824x1648, 300 dpi, rotação física 270 |
| Recorte | notch de 49px no topo |

---

## Instalação

Não há release ainda. Para construir:

```bash
./gradlew :app:assembleDebug
```

Precisa de JDK 17, Android SDK 35 e NDK. O `scripts/fetch-thirdparty.sh` tem de
rodar uma vez antes, porque busca as dependências que o `platformio.ini` declara
e que o PlatformIO resolveria sozinho.

Na primeira abertura o aplicativo pede **acesso a todos os arquivos** e fecha.
Conceda nos Ajustes e abra de novo. Sem isso ele não sobe, e isso é deliberado:
a raiz do armazenamento é lida uma vez, dentro do `setup()`, então subir com a
pasta errada e conceder depois deixaria o leitor preso nela.

### O layout no cartão

```
/sdcard/CrossPoint/
  books/                  os EPUB; o navegador abre direto aqui
  fonts/                  fontes .cpfont por família
  .crosspoint/            cache por livro, progresso, marcadores
  crosspoint.log          log da execução atual
  crosspoint.log.anterior log da execução anterior
```

---

## O que funciona

- **Leitura de EPUB**, com o motor e o toolkit de UI do CrossPoint intactos.
- **Toque**: toque, toque longo e swipe, classificados pelo `GestureDetector` do
  Android e entregues ao C++ já prontos.
- **OPDS sobre HTTPS**, incluindo redirecionamento e download para `books/`.
- **Fontes**: 14, 16, 18 e 20 embutidas em Noto Serif e Noto Sans, mais
  famílias instaláveis em `fonts/`.
- **Símbolos**: setas, marcadores de lista, formas geométricas e os operadores
  matemáticos que aparecem em texto corrido.
- **Log em arquivo**, legível por qualquer gerenciador de arquivos, sem cabo.

## O que não funciona

Listado porque um porte que esconde as bordas desperdiça a tarde do próximo.

- **O servidor web de transferência de arquivos não sobe.** O `WebServerPosix`
  recusa iniciar se alguma rota registrar handler de upload, e o multipart nunca
  foi implementado no porte Kindle, de onde a camada POSIX veio. Num telefone dá
  para contornar com qualquer app de transferência.
- **O nome da rede não aparece.** Lê-lo exige permissão de localização no
  Android 10+, porque o SSID identifica onde você está. Mostramos IP e estado de
  conectado, que não exigem permissão nenhuma.
- **OTA e flash de firmware** não se aplicam e deviam estar escondidos por
  capacidade em vez de aparecerem no menu.

---

## Como foi feito

O caminho está em [docs/android-port.md](docs/android-port.md), com os números
medidos em cada etapa. O resumo:

| | |
| --- | --- |
| Censo de portabilidade | 61% → 100% |
| Referências indefinidas no link | 20 → 0 |
| `libcrosspoint.so` | 9,5 MB, arm64-v8a |
| APK | 16 MB |

A camada POSIX inteira (`String`, `millis()`, FreeRTOS sobre pthreads, SdFat
sobre POSIX, sockets, MD5, base64) veio pronta do
[porte Kindle](https://github.com/fperuzzo72/crosspoint-reader-kindle), que já
havia tirado o CrossPoint do ESP32. Sem ele este porte teria sido outra ordem de
grandeza de trabalho.

### A fronteira com o Kotlin

O Kotlin fica com o que o Android faz melhor: ciclo de vida, a Surface, a
classificação de gestos com os limiares do próprio aparelho, o armazenamento, e
TLS. O C++ fica com o CrossPoint inteiro, que não sabe nada disso.

```
Kotlin -> C++    nativeSetSurface, nativeGesture, nativeContact,
                 nativeSetStorageRoot, nativeStart
C++ -> Kotlin    HTTP (CrossPointHttp), estado da rede (CrossPointNet)
```

A segunda direção é a mais delicada: o C++ chama a JVM de dentro do loop do
leitor, que é uma thread nativa. Ver `lib/hal/android/JniBridge.h` para as duas
regras que isso impõe.

### O que voltar para o porte Kindle

Em [docs/backport-to-kindle.md](docs/backport-to-kindle.md), com cada achado
marcado como POSIX (vale nos dois), Android (fica aqui) ou upstream (é bug do
CrossPoint).

---

## Licença

MIT, como o CrossPoint e o freeink-sdk. Ver [LICENSE](LICENSE).
