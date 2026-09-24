# Native build of CrossPoint for the Bigme HiBreak Pro.
#
# Produces libcrosspoint.so, which the Kotlin Activity loads. It produces no
# executable: an Android app does not enter through main(), it enters through an
# Activity, and the reader thread starts from Jni.cpp's nativeStart().
#
# Adapted from cmake/kindle/CMakeLists.txt. What differs:
#   - a shared library instead of an executable;
#   - no FBInk, no librt (bionic puts clock_gettime in libc);
#   - no -static-libstdc++: the STL comes from Gradle's ANDROID_STL;
#   - third-party libraries come from third_party/, fetched by
#     scripts/fetch-thirdparty.sh, rather than build/kindle/thirdparty.

cmake_minimum_required(VERSION 3.22)
project(crosspoint_hibreak CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(HAL_DIR "${REPO_ROOT}/lib/hal")
set(THIRD_PARTY "${REPO_ROOT}/third_party")

if(NOT EXISTS "${THIRD_PARTY}/ArduinoJson.h")
  message(FATAL_ERROR
    "third_party/ has not been populated. Run scripts/fetch-thirdparty.sh once: "
    "it fetches the dependencies platformio.ini declares and PlatformIO would "
    "resolve on its own. Without them 73 files do not compile.")
endif()

if(NOT CROSSPOINT_VERSION)
  set(CROSSPOINT_VERSION "hibreak-dev")
endif()

# Section GC, as the firmware build does. Not only about size: some vendored
# helpers are declared and called from functions nothing reaches, and without
# this they show up as undefined references to dead code.
add_compile_options(-ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections)

# Hidden visibility by default, and this is not merely ABI hygiene. In a SHARED
# library everything is exported by default, so nothing is dead and
# --gc-sections discards nothing. uzlib's checksum helpers (uzlib_crc32,
# uzlib_adler32) are declared and called from a function nothing reaches, and
# without this they show up as undefined references to code that never runs.
# What needs exporting is already marked: the JNI functions carry JNIEXPORT,
# which is visibility("default").
add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)

# The globs carry CONFIGURE_DEPENDS: without it CMake caches the list and a new
# file only enters the build after a manual reconfigure. The symptom is an
# undefined symbol for something you just wrote, which is an expensive way to
# discover a glob option.

# --- shared vocabulary ------------------------------------------------------
# An INTERFACE target carries includes and defines for everything else, or each
# library would have to repeat the whole list.
add_library(cp_common INTERFACE)

target_include_directories(cp_common INTERFACE
  # arduino-shim goes FIRST so that `#include <Arduino.h>` inside the SDK
  # resolves here, which is what avoids having to edit the SDK.
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

# Each lib/ library is an include directory of its own: the headers sit at each
# root and include one another by bare name.
file(GLOB LIB_DIRS LIST_DIRECTORIES true ${REPO_ROOT}/lib/*)
foreach(dir ${LIB_DIRS})
  if(IS_DIRECTORY ${dir})
    target_include_directories(cp_common INTERFACE ${dir})
  endif()
endforeach()
file(GLOB SDK_INCLUDES CONFIGURE_DEPENDS ${REPO_ROOT}/freeink-sdk/libs/*/*/include)
target_include_directories(cp_common INTERFACE ${SDK_INCLUDES})

# --- geradores que o PlatformIO rodava, e este build tambem precisa ---------
#
# build_html.py e gen_i18n.py sao pre-build do PlatformIO. Aqui nao ha
# PlatformIO, e os arquivos gerados estao versionados, o que funciona ate
# alguem editar um .html ou um .yaml e esquecer de gerar: a fonte fica certa e
# o binario nao recebe. Mesma familia do patch do JPEGDEC, que embarcou um
# ponteiro selvagem por meses exatamente assim.
#
# Rodam na configuracao, nao na compilacao, porque sao rapidos e idempotentes:
# sem mudanca na fonte, nao reescrevem nada. Sem python3 o build segue com um
# aviso, em vez de quebrar por causa de uma ferramenta de conveniencia.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  execute_process(COMMAND ${Python3_EXECUTABLE} scripts/build_html.py
                  WORKING_DIRECTORY ${REPO_ROOT} RESULT_VARIABLE html_rc OUTPUT_QUIET ERROR_QUIET)
  execute_process(COMMAND ${Python3_EXECUTABLE} scripts/gen_i18n.py lib/I18n/translations lib/I18n/
                  WORKING_DIRECTORY ${REPO_ROOT} RESULT_VARIABLE i18n_rc OUTPUT_QUIET ERROR_QUIET)
  if(NOT html_rc EQUAL 0)
    message(WARNING "build_html.py falhou (${html_rc}); as paginas web podem estar desatualizadas")
  endif()
  if(NOT i18n_rc EQUAL 0)
    message(WARNING "gen_i18n.py falhou (${i18n_rc}); as tabelas de traducao podem estar desatualizadas")
  endif()
else()
  message(WARNING "python3 nao encontrado: html e i18n nao serao regerados a partir das fontes")
endif()

target_compile_definitions(cp_common INTERFACE
  FREEINK_DEVICE_HIBREAK=1
  ANDROID=1
  CROSSPOINT_VERSION="${CROSSPOINT_VERSION}"
  # ArduinoJson only registers the String converter when it detects an Arduino
  # environment by looking for ARDUINO. Here String comes from the shim, the
  # detection never fires, it falls back to std::string and every as<String>()
  # fails.
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  # expat is configured by defines, not by a config header.
  XML_GE=0
  XML_CONTEXT_BYTES=1024
)

# This port has never produced a single LOG_ line. Logging.h wraps LOG_ERR,
# LOG_INF and LOG_DBG in one #ifdef ENABLE_SERIAL_LOG, and without it all three
# expand to nothing -- errors included, not just debug. Everything the log file
# has ever carried came from fprintf(stderr) written by hand, which is why the
# crash handler and the panel report and nothing else does.
#
# Both are set, and only for Debug. The output lands on stderr through
# MySerialImpl, which is already redirected into the log file, and the log
# already rotates. LOG_LEVEL alone would have changed nothing.
target_compile_definitions(cp_common INTERFACE
  $<$<CONFIG:Debug>:ENABLE_SERIAL_LOG>
  $<$<CONFIG:Debug>:LOG_LEVEL=2>
)

# --- one static library per library ---------------------------------
# This is not tidiness, it is necessity. There are three copies of miniz in this
# tree (CrossPoint's, FreeInkBook's, ContentProtection's), each with a config
# that prefixes only some of its symbols, and the rest collide. PlatformIO never
# sees the problem because it archives each library: the linker then pulls a
# member only when it resolves something still undefined, and duplicates across
# archives are "first wins", not an error.
#
# Linking loose objects forces every definition in and turns a working layout
# into dozens of multiple-definition errors. That is exactly what happened in
# the first version of this file.
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

# One rule for vendored C, valid across the whole tree: every .c under a
# third_party/ is included by an *_impl.c wrapper that applies a symbol prefix
# first. Compiling both gives every symbol two definitions.
#
# lib/expat drops out entirely: CrossPoint and the SDK each vendor expat and
# both export the same unprefixed XML_*, so only one can go in. The SDK's wins,
# for the same two reasons the Kindle port recorded: it carries a real
# expat_config.h rather than relying on build flags, and its include directory
# already comes first.
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
  # lib/hal goes into the .so loose: it defines the globals nothing references
  # by name (the `HalDisplay display;`), which an archive would drop.
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
  # The PanelDriver stack talks to a SPI/i80 bus this target does not have; the
  # hosted HalDisplay bypasses all of it.
  if(libname STREQUAL "FreeInkDisplay")
    file(GLOB srcs CONFIGURE_DEPENDS ${dir}/src/bus/EpdBusNull.cpp)
  else()
    file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${dir}/src/*.cpp ${dir}/src/*.c)
  endif()
  cp_filter_sources(srcs)
  cp_add_archive(cp_sdk_${libname} ${srcs})
endforeach()

# The fetched libraries. pngle does NOT go in: the SDK already vendors its own
# through src/vendor/pngle_impl.c and the two export the same symbols.
cp_add_archive(cp_fetched
  ${THIRD_PARTY}/JPEGDEC.cpp
  ${THIRD_PARTY}/PNGdec.cpp
  ${THIRD_PARTY}/qrcode.c
)

# --- the .so ------------------------------------------------------------------
# src/ and lib/hal/ go in loose: they define the globals nothing references by
# name (the `HalDisplay display;`), which an archive would drop.
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${REPO_ROOT}/src/*.cpp ${HAL_DIR}/*.cpp)
list(FILTER APP_SOURCES EXCLUDE REGEX "/lib/hal/kindle/")
list(FILTER APP_SOURCES EXCLUDE REGEX "/test/|/tools/")

add_library(crosspoint SHARED ${APP_SOURCES})
target_link_libraries(crosspoint PRIVATE cp_common ${CP_ARCHIVES})


# log: __android_log_print. android: ANativeWindow_*. z: PNGdec uses zlib and
# bionic already ships one, so there is no reason to vendor another copy.
target_link_libraries(crosspoint PRIVATE log android z)
