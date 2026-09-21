# CrossPoint Reader no Bigme HiBreak Pro

Um porte do [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
para rodar como aplicativo Android comum num telefone e-ink, em vez de como
firmware num microcontrolador.

O CrossPoint é firmware de e-reader escrito para aparelhos ESP32, onde ele é
dono da máquina inteira. Um HiBreak Pro não é isso. É um telefone Android com
kernel, userspace e interface próprios, e o painel pertence ao sistema. Então
este porte não substitui nada: ele roda como aplicativo, ao lado de tudo o
mais, e deixa o framework do fabricante decidir o waveform.

O motor de leitura não muda. O que se escreve aqui é a camada de baixo.

## De onde vem

Este repositório é uma cópia de
[`crosspoint-reader-kindle`](../crosspoint-reader-kindle) em `f002274b`, que já
havia tirado o CrossPoint do ESP32 e o feito rodar sobre glibc num ARM Linux.
Daquele trabalho vêm 5.621 linhas de camada POSIX que aqui atravessam quase
intactas: `String`, `millis()`, FreeRTOS sobre pthreads, SdFat sobre POSIX,
sockets, servidor HTTP, MD5 e base64.

O que este porte reorganizou foi a fronteira. Lá tudo morava em
`lib/hal/kindle/`; aqui o que é POSIX genérico está em `lib/hal/posix/` e o que
é de um aparelho está no diretório desse aparelho. Ver
[docs/backport-to-kindle.md](docs/backport-to-kindle.md) para o que deve voltar.

## Estado

**Nada roda ainda.** O aparelho nem foi conectado com sucesso.

O censo de portabilidade diz 61% da árvore compilando para arm64-v8a na
primeira rodada, contra 25% da primeira rodada do porte Kindle. Das falhas,
91%  são header de terceiro não vendorizado, não problema de portabilidade.

Detalhes, números e o que a Bigme expõe de API de refresh estão em
[docs/android-port.md](docs/android-port.md).

## Censo

A bússola do porte. Ataque a causa mais frequente, remeça, repita.

```
./build/android/census.sh
```

Precisa do NDK. O script procura em `ANDROID_NDK_HOME` e nos caminhos usuais do
Homebrew. `ANDROID_API` e `FREEINK_DEVICE` sobrescrevem os padrões (28 e
`FREEINK_DEVICE_KINDLE`, este último só até existir um perfil do HiBreak).

## Licença

MIT, como o CrossPoint e o freeink-sdk. Ver [LICENSE](LICENSE).
