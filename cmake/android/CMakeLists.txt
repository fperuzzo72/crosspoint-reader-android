# Build nativo do CrossPoint para o Bigme HiBreak Pro.
#
# Produz libcrosspoint.so, que a Activity Kotlin carrega. Nao produz
# executavel: um aplicativo Android nao entra por main(), entra pela Activity,
# e a thread do leitor sobe pelo nativeStart() do Jni.cpp.
#
# Adaptado do cmake/kindle/CMakeLists.txt. O que muda em relacao a ele:
#   - biblioteca compartilhada em vez de executavel;
#   - sem FBInk, sem librt (o bionic poe clock_gettime na libc);
#   - sem -static-libstdc++: a STL vem pelo ANDROID_STL do Gradle;
#   - as bibliotecas de terceiros vem de third_party/, buscadas por
#     scripts/fetch-thirdparty.sh, em vez de build/kindle/thirdparty.

cmake_minimum_required(VERSION 3.22)
project(crosspoint_hibreak CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(HAL_DIR "${REPO_ROOT}/lib/hal")
set(THIRD_PARTY "${REPO_ROOT}/third_party")

if(NOT EXISTS "${THIRD_PARTY}/ArduinoJson.h")
  message(FATAL_ERROR
    "third_party/ nao foi populado. Rode scripts/fetch-thirdparty.sh uma vez: "
    "ele busca as dependencias que o platformio.ini declara e que o PlatformIO "
    "resolveria sozinho. Sem elas 73 arquivos nao compilam.")
endif()

if(NOT CROSSPOINT_VERSION)
  set(CROSSPOINT_VERSION "hibreak-dev")
endif()

# Section GC, como o build de firmware faz. Nao e so tamanho: alguns helpers
# vendorizados sao declarados e chamados de funcoes que nada alcanca, e sem
# isto aparecem como referencia indefinida para codigo morto.
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

# Visibilidade escondida por padrao, e isto nao e so higiene de ABI. Numa
# biblioteca COMPARTILHADA tudo e exportado por padrao, entao nada e morto e o
# --gc-sections nao descarta nada. Os helpers de checksum do uzlib
# (uzlib_crc32, uzlib_adler32) sao declarados e chamados de uma funcao que nada
# alcanca, e sem isto aparecem como referencia indefinida para codigo que nunca
# roda. O que precisa ser exportado ja e marcado: as funcoes JNI carregam
# JNIEXPORT, que e visibility("default").
add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)

# Os globs levam CONFIGURE_DEPENDS: sem isso o CMake guarda a lista e um
# arquivo novo so entra no build depois de uma reconfiguracao manual. O
# sintoma e um simbolo indefinido de algo que voce acabou de escrever, que e
# um jeito caro de descobrir uma opcao de glob.

# --- vocabulario comum ------------------------------------------------------
# Um alvo INTERFACE carrega includes e defines para todos os outros, senao cada
# biblioteca teria de repetir a lista inteira.
add_library(cp_common INTERFACE)

target_include_directories(cp_common INTERFACE
  # arduino-shim vem PRIMEIRO para o `#include <Arduino.h>` de dentro do SDK
  # resolver aqui, que e o que evita ter de editar o SDK.
  ${HAL_DIR}/posix/arduino-shim
  ${THIRD_PARTY}
  ${HAL_DIR}/posix
  ${HAL_DIR}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook/third_party/expat
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/components
  ${REPO_ROOT}/src/activities
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook/third_party/libunibreak
  ${REPO_ROOT}/freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd
)

# Cada biblioteca de lib/ e um diretorio de include por si so: os headers ficam
# na raiz de cada uma e se incluem entre si por nome simples.
file(GLOB LIB_DIRS LIST_DIRECTORIES true ${REPO_ROOT}/lib/*)
foreach(dir ${LIB_DIRS})
  if(IS_DIRECTORY ${dir})
    target_include_directories(cp_common INTERFACE ${dir})
  endif()
endforeach()
file(GLOB SDK_INCLUDES CONFIGURE_DEPENDS ${REPO_ROOT}/freeink-sdk/libs/*/*/include)
target_include_directories(cp_common INTERFACE ${SDK_INCLUDES})

target_compile_definitions(cp_common INTERFACE
  FREEINK_DEVICE_HIBREAK=1
  ANDROID=1
  CROSSPOINT_VERSION="${CROSSPOINT_VERSION}"
  # O ArduinoJson so registra o conversor de String quando detecta ambiente
  # Arduino, olhando por ARDUINO. Aqui o String vem do shim, a deteccao nao
  # dispara, ele cai no std::string e todo as<String>() falha.
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  # expat e configurado por defines, nao por header de config.
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

# --- uma biblioteca estatica por biblioteca ---------------------------------
# Isto nao e organizacao, e necessidade. Ha tres copias de miniz nesta arvore
# (a do CrossPoint, a do FreeInkBook, a do ContentProtection), cada uma com uma
# config que prefixa so parte dos simbolos, e o resto colide. O PlatformIO
# nunca ve o problema porque arquiva cada biblioteca: o linker entao puxa um
# membro so quando ele resolve algo ainda indefinido, e duplicata entre
# arquivos e "o primeiro vence", nao erro.
#
# Linkar objetos soltos forca toda definicao para dentro e transforma um
# arranjo que funciona em dezenas de erros de multipla definicao. Foi
# exatamente o que aconteceu na primeira versao deste arquivo.
set(CP_ARCHIVES "")

function(cp_add_archive name)
  set(srcs ${ARGN})
  if(NOT srcs)
    return()
  endif()
  add_library(${name} STATIC ${srcs})
  target_link_libraries(${name} PRIVATE cp_common)
  set(CP_ARCHIVES ${CP_ARCHIVES} ${name} PARENT_SCOPE)
endfunction()

# Regra unica para C vendorizado, valida na arvore inteira: todo .c sob um
# third_party/ e incluido por um wrapper *_impl.c que aplica um prefixo de
# simbolo antes. Compilar os dois da a cada simbolo duas definicoes.
#
# lib/expat sai inteiro: o CrossPoint e o SDK vendorizam expat cada um e os
# dois exportam os mesmos XML_* sem prefixo, entao so um pode entrar. Vence o
# do SDK, pelos mesmos dois motivos que o porte Kindle registrou: ele carrega
# um expat_config.h de verdade em vez de depender de flags de build, e o
# diretorio de include dele ja vem primeiro.
macro(cp_filter_sources var)
  list(FILTER ${var} EXCLUDE REGEX "/third_party/")
  list(FILTER ${var} EXCLUDE REGEX "/lib/expat/")
  list(FILTER ${var} EXCLUDE REGEX "/test/|/tools/")
endmacro()

foreach(dir ${LIB_DIRS})
  if(NOT IS_DIRECTORY ${dir})
    continue()
  endif()
  get_filename_component(libname ${dir} NAME)
  # lib/hal entra solto no .so: define os globais que nada referencia por nome
  # (o `HalDisplay display;`), que um arquivo .a descartaria.
  if(libname STREQUAL "hal" OR libname STREQUAL "expat")
    continue()
  endif()
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${dir}/*.cpp ${dir}/*.c)
  cp_filter_sources(srcs)
  cp_add_archive(cp_lib_${libname} ${srcs})
endforeach()

file(GLOB SDK_LIB_DIRS LIST_DIRECTORIES true ${REPO_ROOT}/freeink-sdk/libs/*/*)
foreach(dir ${SDK_LIB_DIRS})
  if(NOT IS_DIRECTORY ${dir})
    continue()
  endif()
  get_filename_component(libname ${dir} NAME)
  # A pilha de PanelDriver fala com um barramento SPI/i80 que este alvo nao
  # tem; o HalDisplay hospedado desvia dela inteira.
  if(libname STREQUAL "FreeInkDisplay")
    file(GLOB srcs CONFIGURE_DEPENDS ${dir}/src/bus/EpdBusNull.cpp)
  else()
    file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${dir}/src/*.cpp ${dir}/src/*.c)
  endif()
  cp_filter_sources(srcs)
  cp_add_archive(cp_sdk_${libname} ${srcs})
endforeach()

# As bibliotecas buscadas. pngle NAO entra: o SDK ja vendoriza a dele por
# src/vendor/pngle_impl.c e as duas exportam os mesmos simbolos.
cp_add_archive(cp_fetched
  ${THIRD_PARTY}/JPEGDEC.cpp
  ${THIRD_PARTY}/PNGdec.cpp
  ${THIRD_PARTY}/qrcode.c
)

# --- o .so ------------------------------------------------------------------
# src/ e lib/hal/ entram soltos: definem os globais que nada referencia por
# nome, que um arquivo .a descartaria.
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/src/*.cpp ${HAL_DIR}/*.cpp)
list(FILTER APP_SOURCES EXCLUDE REGEX "/lib/hal/kindle/")
list(FILTER APP_SOURCES EXCLUDE REGEX "/test/|/tools/")

add_library(crosspoint SHARED ${APP_SOURCES})
target_link_libraries(crosspoint PRIVATE cp_common ${CP_ARCHIVES})


# log: __android_log_print. android: ANativeWindow_*. z: o PNGdec usa zlib, e
# o bionic ja traz uma, entao nao ha motivo para vendorizar outra copia.
target_link_libraries(crosspoint PRIVATE log android z)
