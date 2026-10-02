# utagoe-core の build 定義。
# 同梱ライブラリ (../third_party、deps.ps1 が取得する) をそれぞれ専用の DLL にし、utagoe_core.dll / utagoe.exe / test.exe を作る。

cmake_minimum_required(VERSION 3.21)
project(utagoe_core C CXX)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION UTAGOE_VERSION LIMIT_COUNT 1)
string(STRIP "${UTAGOE_VERSION}" UTAGOE_VERSION)
string(REPLACE "." ";" UTAGOE_VERSION_PARTS "${UTAGOE_VERSION}")
list(GET UTAGOE_VERSION_PARTS 0 UTAGOE_VERSION_MAJOR)
list(GET UTAGOE_VERSION_PARTS 1 UTAGOE_VERSION_MINOR)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

# 配布物に build 環境の絶対 path を残さない。
get_filename_component(TOOLCHAIN_ROOT ${CMAKE_CXX_COMPILER} DIRECTORY)
get_filename_component(TOOLCHAIN_ROOT ${TOOLCHAIN_ROOT}/.. ABSOLUTE)
add_compile_options(
    -ffile-prefix-map=${REPO_ROOT}=.
    -ffile-prefix-map=${CMAKE_BINARY_DIR}=./build
    -ffile-prefix-map=${TOOLCHAIN_ROOT}=/toolchain)


# libogg: upstream の CMake をそのまま使う。
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/libogg-1.3.6 libogg EXCLUDE_FROM_ALL)

# opus: upstream の CMake。DNN 系の拡張機能は使わない。
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_DRED OFF CACHE BOOL "" FORCE)
set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
set(OPUS_DEEP_PLC OFF CACHE BOOL "" FORCE)
# stack protector は libssp-0.dll を必要にする。利用者の PC には無いので切る。
set(OPUS_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)
set(OPUS_FORTIFY_SOURCE OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/opus-1.6.1 opus EXCLUDE_FROM_ALL)

# libFLAC: upstream の CMake。C API と codec 本体だけ使う。
set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG_MODULES OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(WITH_OGG OFF CACHE BOOL "" FORCE)
set(ENABLE_MULTITHREADING OFF CACHE BOOL "" FORCE)
# stack protector は -Wl,-Bdynamic を末尾に残し、libstdc++ が動的 link になるため切る。
set(WITH_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)
set(WITH_FORTIFY_SOURCE OFF CACHE BOOL "" FORCE)
add_subdirectory(${TP}/flac-1.5.0 flac EXCLUDE_FROM_ALL)
# MinGW の windres は include path の空白を扱えないため、version 情報だけの .rc は外す。
get_target_property(FLAC_SOURCES FLAC SOURCES)
list(FILTER FLAC_SOURCES EXCLUDE REGEX "\\.rc$")
set_target_properties(FLAC PROPERTIES SOURCES "${FLAC_SOURCES}")

# libvorbis: upstream CMake は find_package(Ogg) 前提なので、source 一覧を直接持つ。
set(VORBIS ${TP}/libvorbis-1.3.7)
add_library(vorbis_all STATIC
    ${VORBIS}/lib/analysis.c ${VORBIS}/lib/bitrate.c ${VORBIS}/lib/block.c
    ${VORBIS}/lib/codebook.c ${VORBIS}/lib/envelope.c ${VORBIS}/lib/floor0.c
    ${VORBIS}/lib/floor1.c ${VORBIS}/lib/info.c ${VORBIS}/lib/lookup.c
    ${VORBIS}/lib/lpc.c ${VORBIS}/lib/lsp.c ${VORBIS}/lib/mapping0.c
    ${VORBIS}/lib/mdct.c ${VORBIS}/lib/psy.c ${VORBIS}/lib/registry.c
    ${VORBIS}/lib/res0.c ${VORBIS}/lib/sharedbook.c ${VORBIS}/lib/smallft.c
    ${VORBIS}/lib/synthesis.c ${VORBIS}/lib/window.c
    ${VORBIS}/lib/vorbisenc.c ${VORBIS}/lib/vorbisfile.c)
target_include_directories(vorbis_all PUBLIC ${VORBIS}/include $<TARGET_PROPERTY:ogg,INTERFACE_INCLUDE_DIRECTORIES>
                           PRIVATE ${VORBIS}/lib)
target_compile_options(vorbis_all PRIVATE -w)

# opusfile: HTTP 対応は不要なので http.c / wincerts.c は外す。
set(OPUSFILE ${TP}/opusfile-0.12)
add_library(opusfile STATIC
    ${OPUSFILE}/src/info.c ${OPUSFILE}/src/internal.c
    ${OPUSFILE}/src/opusfile.c ${OPUSFILE}/src/stream.c)
target_include_directories(opusfile PUBLIC ${OPUSFILE}/include $<TARGET_PROPERTY:ogg,INTERFACE_INCLUDE_DIRECTORIES>
                           $<TARGET_PROPERTY:opus,INTERFACE_INCLUDE_DIRECTORIES> PRIVATE ${OPUSFILE}/src)
target_compile_definitions(opusfile PRIVATE OP_DISABLE_HTTP)
target_compile_options(opusfile PRIVATE -w)

# libopusenc: upstream の Makefile.am と同じ define を付ける。
set(OPUSENC ${TP}/libopusenc-0.2.1)
add_library(opusenc STATIC
    ${OPUSENC}/src/ogg_packer.c ${OPUSENC}/src/opus_header.c
    ${OPUSENC}/src/opusenc.c ${OPUSENC}/src/picture.c
    ${OPUSENC}/src/resample.c ${OPUSENC}/src/unicode_support.c)
target_include_directories(opusenc PUBLIC ${OPUSENC}/include PRIVATE ${OPUSENC}/src)
target_compile_definitions(opusenc PRIVATE
    RANDOM_PREFIX=libopusenc OUTSIDE_SPEEX OPE_BUILD
    PACKAGE_NAME="libopusenc" PACKAGE_VERSION="0.2.1")
target_include_directories(opusenc PUBLIC $<TARGET_PROPERTY:opus,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_options(opusenc PRIVATE -w)

# 上の static library を丸ごと 1 つずつ DLL に包む。依存先は static に取り込まず、別の DLL として link する。
# 名前に utagoe- を付けるのは、同名の DLL (他の application の ogg.dll など) が process に読まれていた場合に、
# Windows がそれを使い回してしまうのを避けるため。
set(STATIC_LIBGCC -static-libgcc)
# short は DLL 名 (utagoe-<short>.dll)、archive は包む static library、残りは link する他の DLL。target 名は <short>_dll。
function(utagoe_codec_dll short archive)
    set(t ${short}_dll)
    add_library(${t} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/dll_stub.c)
    target_link_libraries(${t} PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,${archive}>" ${ARGN})
    target_link_options(${t} PRIVATE ${STATIC_LIBGCC} -Wl,--export-all-symbols)
    set_target_properties(${t} PROPERTIES PREFIX "" OUTPUT_NAME "utagoe-${short}")
    # header は static library と同じものを使う。
    target_include_directories(${t} INTERFACE $<TARGET_PROPERTY:${archive},INTERFACE_INCLUDE_DIRECTORIES>)
endfunction()

utagoe_codec_dll(ogg ogg)
utagoe_codec_dll(opus opus)
utagoe_codec_dll(flac FLAC)
utagoe_codec_dll(vorbis vorbis_all ogg_dll)
utagoe_codec_dll(opusfile opusfile opus_dll ogg_dll)
utagoe_codec_dll(opusenc opusenc opus_dll)
# FLAC は static build の header 設定 (FLAC_API が空) のまま使う。関数は MinGW の auto-import で解決される。
target_compile_definitions(flac_dll INTERFACE FLAC__NO_DLL)

# dr_wav / dr_mp3: header-only。実装を src/codec_impl.c で 1 回だけ展開し、DLL として export する。
add_library(drlibs_dll SHARED src/codec_impl.c)
target_include_directories(drlibs_dll PUBLIC ${TP}/dr_libs)
target_compile_definitions(drlibs_dll PUBLIC DRWAV_DLL DRMP3_DLL)
target_link_options(drlibs_dll PRIVATE ${STATIC_LIBGCC})
set_target_properties(drlibs_dll PROPERTIES PREFIX "" OUTPUT_NAME "utagoe-drlibs")

set(CODEC_DLLS ogg_dll opus_dll flac_dll vorbis_dll opusfile_dll opusenc_dll drlibs_dll)


set(CORE_SOURCES
    src/fft.cpp src/settings.cpp src/parallel.cpp src/gpu.cpp src/extract.cpp src/resample.cpp
    src/decode.cpp src/encode.cpp src/mediafoundation.cpp src/log.cpp
    src/algorithms/v3/engine.cpp src/algorithms/v3/freq_engine.cpp src/algorithms/v3/vocal_func.cpp src/algorithms/v3/fir.cpp
    src/algorithms/v3/pipeline.cpp src/algorithms/v3/worker.cpp
    src/algorithms/refcancel/rc_dsp.cpp src/algorithms/refcancel/rc_pipeline.cpp
    src/algorithms/refcancel/robust.cpp src/algorithms/refcancel/kalman.cpp src/algorithms/refcancel/hammerstein.cpp
    src/algorithms/refcancel/nmf.cpp src/algorithms/refcancel/spatial.cpp src/algorithms/refcancel/ensemble.cpp
    src/algorithms/hardpair/hp_dsp.cpp src/algorithms/hardpair/hp_grid.cpp src/algorithms/hardpair/hp_pipeline.cpp
    src/algorithms/hardpair/time_map.cpp src/algorithms/hardpair/rational.cpp src/algorithms/hardpair/surface.cpp
    src/algorithms/hardpair/ctf.cpp src/algorithms/hardpair/lowrank.cpp src/algorithms/hardpair/trend.cpp
    src/algorithms/hardpair/kick_duck.cpp
    src/algorithms/centresides/cs.cpp src/algorithms/centresides/true_ms.cpp src/algorithms/centresides/phantom.cpp
    src/algorithms/centresides/coherence.cpp src/algorithms/centresides/adress.cpp src/algorithms/centresides/duet.cpp
    src/algorithms/centresides/pca.cpp src/algorithms/centresides/pretty.cpp
    src/algorithms/findwithin/fw_dsp.cpp src/algorithms/findwithin/fw_workers.cpp src/algorithms/findwithin/fw_discovery.cpp
    src/algorithms/findwithin/fw_align.cpp src/algorithms/findwithin/fw_verify.cpp src/algorithms/findwithin/fw_run.cpp)

add_library(utagoe_objs OBJECT ${CORE_SOURCES})
target_include_directories(utagoe_objs PUBLIC include src)
target_link_libraries(utagoe_objs PUBLIC ${CODEC_DLLS})
if(WIN32)
    # AAC/M4A, ALAC, WMA などは Windows 標準の Media Foundation で扱う。
    target_link_libraries(utagoe_objs PUBLIC mfuuid ole32 shell32)
    # .NET 10 の UI 自体が Windows 10 以降専用なので、Windows 10 の SDK 定義 (ALAC / FLAC / Opus の GUID など) を使う。
    target_compile_definitions(utagoe_objs PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
endif()
set_source_files_properties(${CORE_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# __cxa_throw を包んで、例外を投げた位置の stack trace を記録する (src/log.cpp)。
set(STATIC_RUNTIME -static -static-libgcc -static-libstdc++ -Wl,--wrap=__cxa_throw)

add_library(utagoe_core SHARED src/capi.cpp)
target_compile_definitions(utagoe_core PRIVATE UTAGOE_BUILD_DLL)
target_link_libraries(utagoe_core PRIVATE utagoe_objs)
target_link_options(utagoe_core PRIVATE ${STATIC_RUNTIME})
set_target_properties(utagoe_core PROPERTIES PREFIX "")

add_executable(utagoe src/main.cpp)
target_link_libraries(utagoe PRIVATE utagoe_objs)
target_link_options(utagoe PRIVATE ${STATIC_RUNTIME})

add_executable(utagoe_test tests/test.cpp)
target_link_libraries(utagoe_test PRIVATE utagoe_objs)
target_link_options(utagoe_test PRIVATE ${STATIC_RUNTIME})

# 未署名 exe を実行できない環境 (Smart App Control など) でもテストできるよう、DLL 版も作る。
add_library(utagoe_tests SHARED tests/test.cpp)
target_compile_definitions(utagoe_tests PRIVATE UTAGOE_TEST_DLL)
target_link_libraries(utagoe_tests PRIVATE utagoe_objs)
target_link_options(utagoe_tests PRIVATE ${STATIC_RUNTIME})
set_target_properties(utagoe_tests PROPERTIES PREFIX "")

# 処理時間の計測用。配布物には含めない。
add_executable(utagoe_bench EXCLUDE_FROM_ALL tests/bench.cpp)
target_link_libraries(utagoe_bench PRIVATE utagoe_objs)
target_link_options(utagoe_bench PRIVATE ${STATIC_RUNTIME})

# By Waveform 代替モデルを Python 版の出力と照合する道具。配布物には含めない。
foreach(parity rc hp cs fw)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/${parity}_parity.cpp)
        add_executable(utagoe_${parity}_parity EXCLUDE_FROM_ALL tests/${parity}_parity.cpp)
        target_link_libraries(utagoe_${parity}_parity PRIVATE utagoe_objs)
        target_link_options(utagoe_${parity}_parity PRIVATE ${STATIC_RUNTIME})
    endif()
endforeach()


# 配布用の setup (dist\Utagoe.exe)。.NET を同梱しない Utagoe 本体を LZMS で圧縮して埋め込み、
# .NET Desktop Runtime が無ければ Microsoft から入れてから本体を起動する。build.ps1 が UTAGOE_SETUP_PAYLOAD に本体の path を渡す。
add_executable(utagoe_pack EXCLUDE_FROM_ALL setup/pack.cpp)
target_link_libraries(utagoe_pack PRIVATE cabinet)
# Compression API (LZMS) と GetDpiForSystem は Windows 8 / 10 以降の宣言。.NET 10 も Windows 10 以降。
target_compile_definitions(utagoe_pack PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_link_options(utagoe_pack PRIVATE -static)
set(UTAGOE_SETUP_PAYLOAD "" CACHE FILEPATH "Utagoe 本体 (framework-dependent の 1 ファイル版) の path")
if(UTAGOE_SETUP_PAYLOAD)
    enable_language(RC)
    set(UTAGOE_SETUP_PACKED ${CMAKE_CURRENT_BINARY_DIR}/setup_payload.bin)
    set(UTAGOE_SETUP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/../utagoe-ui/Resources/Utagoe.ico)
    set(UTAGOE_SETUP_MANIFEST ${CMAKE_CURRENT_BINARY_DIR}/setup.manifest)
    configure_file(setup/setup.manifest.in ${UTAGOE_SETUP_MANIFEST} @ONLY)
    # windres は空白を含む include path を扱えないので、header は .rc の中に完全な path で書く。
    set(UTAGOE_SETUP_IDS ${CMAKE_CURRENT_SOURCE_DIR}/setup/setup_ids.h)
    add_custom_command(OUTPUT ${UTAGOE_SETUP_PACKED}
        COMMAND utagoe_pack ${UTAGOE_SETUP_PAYLOAD} ${UTAGOE_SETUP_PACKED}
        DEPENDS utagoe_pack ${UTAGOE_SETUP_PAYLOAD}
        COMMENT "Packing the Utagoe app for the setup")
    configure_file(setup/setup.rc.in ${CMAKE_CURRENT_BINARY_DIR}/setup.rc @ONLY)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/setup.rc PROPERTIES
        OBJECT_DEPENDS "${UTAGOE_SETUP_PACKED};${UTAGOE_SETUP_ICON};${UTAGOE_SETUP_MANIFEST}")
    add_executable(utagoe_setup WIN32 EXCLUDE_FROM_ALL setup/setup.cpp ${CMAKE_CURRENT_BINARY_DIR}/setup.rc)
    target_include_directories(utagoe_setup PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/setup>)
    target_compile_definitions(utagoe_setup PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
    target_link_libraries(utagoe_setup PRIVATE cabinet urlmon wintrust crypt32 comctl32 shell32 ole32 uuid)
    # setup は小さく保つ (stack trace の名前は要らないので symbol を落とし、大きさ優先で最適化する)。
    target_compile_options(utagoe_setup PRIVATE -Os)
    target_link_options(utagoe_setup PRIVATE -municode -static -static-libgcc -static-libstdc++ -s -Wl,--gc-sections)
    set_target_properties(utagoe_setup PROPERTIES OUTPUT_NAME UtagoeSetup)
endif()
