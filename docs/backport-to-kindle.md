# O que daqui deve voltar para o porte Kindle

Este repositório nasceu de uma cópia de `crosspoint-reader-kindle` em `f002274b`.
O porte Kindle pagou o custo de tirar o CrossPoint do ESP32; este aqui herda esse
trabalho. Quando algo que consertamos aqui for de camada POSIX e não de Android,
ele pertence aos dois, e esta é a lista para não perdermos a conta.

## A reorganização estrutural, que é o item principal

No repositório Kindle o shim inteiro mora em `lib/hal/kindle/`, misturando duas
coisas de natureza diferente:

- o que é **POSIX genérico** e vale em qualquer Unix (`String`, `millis()`,
  FreeRTOS sobre pthreads, SdFat sobre POSIX, sockets, servidor HTTP, MD5,
  base64, e o `arduino-shim/` inteiro);
- o que é **do Kindle** e não vale em mais lugar nenhum (framebuffer sobre
  `/dev/fb0` via FBInk, toque via evdev do `zforce2`, expansão 1bpp para 8bpp
  no formato que o EPDC quer).

Aqui eles estão separados:

| Aqui | Lá | Natureza |
| --- | --- | --- |
| `lib/hal/posix/` (5.621 linhas) | `lib/hal/kindle/` | reaproveitável |
| `lib/hal/kindle/` (só os 6 arquivos `Kindle*`) | idem | específico |
| `lib/hal/android/` | não existe | específico |

**Backport recomendado:** mover `lib/hal/kindle/arduino-shim/` e os dez arquivos
POSIX para `lib/hal/posix/` no repositório Kindle também, e ajustar o include
path do `census.sh`, do `trylink.sh` e do `cmake/kindle/`. É um diff mecânico e
deixa a costura visível, que é o que o próprio doc do porte Kindle argumenta ser
o achado estrutural do projeto.

## Correções classificadas

Cada conserto leva uma destas três marcas:

- **POSIX** — vale nos dois, backport devido.
- **Android** — bionic, NDK, ciclo de vida ou sandbox. Fica aqui.
- **Upstream** — é bug do CrossPoint ou do freeink-sdk e devia subir para o
  repositório de origem, não só para o Kindle.

Da subida de 61% para 100% no censo:

| Marca | Conserto | Onde |
| --- | --- | --- |
| **POSIX** | `esp_restart()`, a forma livre do ESP-IDF ao lado do `ESP.restart()` que já existia. O `RecoveryBoot` e o `MemoryManager` do SDK chamam esta. | `arduino-shim/esp_system.h`, `ArduinoPlatform.cpp` |
| **POSIX** | `StaticTask_t`, opaco, só para `sizeof()` compilar. `xTaskCreateStatic` segue ausente de propósito, então quem tentar criar tarefa estática quebra no link e não em silêncio. | `arduino-shim/freertos/task.h` |
| **POSIX** | `adc_attenuation_t` e `analogSetAttenuation()` inertes, ao lado do `analogRead()` que já era inerte. | `arduino-shim/Arduino.h` |
| **POSIX** | `ARDUINOJSON_ENABLE_ARDUINO_STRING=1`. Sem isso o ArduinoJson não detecta ambiente Arduino, cai no `std::string` e todo `as<String>()` falha. O Kindle tem o mesmo `String` de shim e o mesmo problema. | flag de build |
| **Upstream** | `HomeActivity.h` declarava `struct RecentBook;` adiante e tinha `std::vector<RecentBook>` como membro. Mal formado: instanciar membros de `vector` exige tipo completo. O libstdc++ do ESP32 aceita, o libc++ recusa. Trocado por `#include "RecentBooksStore.h"`. | `src/activities/home/HomeActivity.h` |
| **POSIX** | `scripts/fetch-thirdparty.sh`: as dependências que o `platformio.ini` declara, presas por versão, buscadas fora do PlatformIO. O doc do porte Kindle já apontava isso como valendo mais que mais shim. | `scripts/` |

O `CROSSPOINT_VERSION` não entra na lista: é define de build que o
`scripts/git_branch.py` injeta, e fora do PlatformIO só precisa existir.

## Contramão

Coisas que o Kindle resolveu e que vamos precisar rever aqui, não copiar:

- **`arduino-shim/WiFi.h`** lê SSID por wireless extensions (`SIOCGIWESSID`) e
  RSSI por `/proc/net/wireless`. Wireless extensions estão mortas no Android
  moderno e `/proc/net/wireless` não é legível por app comum. Precisa de uma
  variante que passe por `WifiManager` via JNI, ou que falhe honestamente como o
  Kindle faz nos caminhos de associação.
- **TLS** continua não implementado. No Kindle isso elimina OPDS e KOReader sync
  na prática. No Android a saída é diferente e mais fácil, porque existe um
  `SSLContext` do sistema a uma ponte JNI de distância.
- **`ESP.restart()`** re-executa o processo no Kindle. No Android reiniciar um
  processo não é a mesma coisa que reiniciar a Activity, e a semântica certa
  ainda não foi decidida.
