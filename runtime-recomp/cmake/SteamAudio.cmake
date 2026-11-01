# Steam Audio (phonon), the binaural (HRTF) renderer for positioned sounds in
# HEADPHONES mode. It is built from the pinned source checkouts without Intel
# IPP, Embree or the AMD GPU components; PFFFT does the FFTs.
#
# Steam Audio's CMake looks for its dependencies under its own top-level
# source directory, so it cannot be added as a subdirectory of DKR-R. Each
# library is configured as its own CMake project at build time, and a
# dependency provider (SteamAudioDependencyProvider.cmake) answers Steam
# Audio's find_package() calls with the libraries built here. CMake downloads
# nothing and edits no checkout; FlatBuffers' reviewed patches are applied by
# scripts/apply_dependency_patches.py like every other dependency's.
include(ExternalProject)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    set(_dkr_steam_audio_platform linux-x64)
endif()
if(DKR_RUNTIME_BUILD_RT64 AND _dkr_steam_audio_platform)
    set(_dkr_steam_audio_default ON)
else()
    set(_dkr_steam_audio_default OFF)
endif()
option(DKR_STEAM_AUDIO "Binaural rendering of positioned sounds through Steam Audio" ${_dkr_steam_audio_default})
# DKR-R's spatial audio module (src/game/spatial_audio). Without Steam Audio
# it is a set of no-ops and the game keeps today's stereo mix.
if(NOT DKR_STEAM_AUDIO)
    add_library(DKRSpatialAudio STATIC src/game/spatial_audio/spatial_audio_disabled.cpp)
    set(DKR_SPATIAL_AUDIO_GAME_SOURCES
        src/game/spatial_audio/audio_task_disabled.cpp
        src/game/spatial_audio/game_capture_disabled.cpp
    )
    target_include_directories(DKRSpatialAudio PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/game")
    target_compile_features(DKRSpatialAudio PUBLIC cxx_std_20)
    return()
endif()
if(NOT _dkr_steam_audio_platform)
    message(FATAL_ERROR
        "DKR_STEAM_AUDIO is only set up for Linux x64 so far. "
        "Configure DKR_STEAM_AUDIO=OFF for this platform.")
endif()

set(DKR_STEAM_AUDIO_SOURCE "${DKR_DEPENDENCIES_DIR}/steam-audio")
set(_dkr_steam_audio_deps "${DKR_DEPENDENCIES_DIR}/steam-audio-deps")
set(_dkr_steam_audio_missing)
foreach(input IN ITEMS
        "${DKR_STEAM_AUDIO_SOURCE}/core/CMakeLists.txt"
        "${_dkr_steam_audio_deps}/flatbuffers/CMakeLists.txt"
        "${_dkr_steam_audio_deps}/pffft/CMakeLists.txt"
        "${_dkr_steam_audio_deps}/zlib/CMakeLists.txt"
        "${_dkr_steam_audio_deps}/libmysofa/CMakeLists.txt")
    if(NOT EXISTS "${input}")
        list(APPEND _dkr_steam_audio_missing "  ${input}")
    endif()
endforeach()
if(_dkr_steam_audio_missing)
    list(JOIN _dkr_steam_audio_missing "\n" _dkr_steam_audio_missing_text)
    message(FATAL_ERROR
        "Steam Audio has not been prepared:\n${_dkr_steam_audio_missing_text}\n"
        "Run scripts/bootstrap_dependencies.py --only steam-audio --only steam-audio-flatbuffers "
        "--only steam-audio-pffft --only steam-audio-zlib --only steam-audio-libmysofa, "
        "or configure DKR_STEAM_AUDIO=OFF for a build without it.")
endif()

set(_dkr_steam_audio_root "${CMAKE_BINARY_DIR}/steam-audio")
set(_dkr_steam_audio_prefix "${_dkr_steam_audio_root}/prefix")
# PFFFT and FlatBuffers declare CMake 2.8 minimums, which CMake 4 refuses
# unless a policy floor is given.
set(_dkr_steam_audio_common_args
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_INSTALL_PREFIX=${_dkr_steam_audio_prefix}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON)

# The libraries below are static and end up inside libphonon.so, whose link
# hides their symbols (--exclude-libs,ALL). Every project builds on each
# build (BUILD_ALWAYS) so a changed pin or reviewed patch is picked up; an
# unchanged one is a no-op.
ExternalProject_Add(dkr_steam_audio_zlib
    SOURCE_DIR "${_dkr_steam_audio_deps}/zlib"
    PREFIX "${_dkr_steam_audio_root}/zlib"
    BUILD_ALWAYS ON
    CMAKE_ARGS ${_dkr_steam_audio_common_args}
    BUILD_BYPRODUCTS "${_dkr_steam_audio_prefix}/lib/libz.a")

ExternalProject_Add(dkr_steam_audio_libmysofa
    SOURCE_DIR "${_dkr_steam_audio_deps}/libmysofa"
    PREFIX "${_dkr_steam_audio_root}/libmysofa"
    BUILD_ALWAYS ON
    CMAKE_ARGS ${_dkr_steam_audio_common_args}
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_TESTS=OFF
        # Its static target sets POSITION_INDEPENDENT_CODE from
        # BUILD_SHARED_LIBS, so -fPIC is passed directly.
        -DCMAKE_C_FLAGS=-fPIC
        -DZLIB_INCLUDE_DIR=${_dkr_steam_audio_prefix}/include
        -DZLIB_LIBRARY=${_dkr_steam_audio_prefix}/lib/libz.a
    DEPENDS dkr_steam_audio_zlib
    BUILD_BYPRODUCTS "${_dkr_steam_audio_prefix}/lib/libmysofa.a")

ExternalProject_Add(dkr_steam_audio_pffft
    SOURCE_DIR "${_dkr_steam_audio_deps}/pffft"
    PREFIX "${_dkr_steam_audio_root}/pffft"
    BUILD_ALWAYS ON
    CMAKE_ARGS ${_dkr_steam_audio_common_args}
        -DPFFFT_USE_TYPE_DOUBLE=OFF
        -DPFFFT_USE_FFTPACK=OFF
        -DPFFFT_USE_BENCH_GREEN=OFF
        -DPFFFT_USE_BENCH_KISS=OFF
        -DPFFFT_USE_BENCH_POCKET=OFF
    BUILD_BYPRODUCTS "${_dkr_steam_audio_prefix}/lib/libpffft.a")

# flatc compiles Steam Audio's serialisation schemas on the build host; the
# headers are installed with it.
ExternalProject_Add(dkr_steam_audio_flatc
    SOURCE_DIR "${_dkr_steam_audio_deps}/flatbuffers"
    PREFIX "${_dkr_steam_audio_root}/flatbuffers"
    BUILD_ALWAYS ON
    CMAKE_ARGS ${_dkr_steam_audio_common_args}
        -DFLATBUFFERS_BUILD_FLATLIB=OFF
        -DFLATBUFFERS_BUILD_FLATHASH=OFF
        -DFLATBUFFERS_BUILD_TESTS=OFF
        # flatc builds with -Werror; newer compilers warn on this 2020 code
        # (Steam Audio passes the same flag for its macOS build).
        "-DCMAKE_CXX_FLAGS=-Wno-error=unused-but-set-variable"
    BUILD_BYPRODUCTS "${_dkr_steam_audio_prefix}/bin/flatc")

# With AVX on, Steam Audio's Linux build adds the GCC-only -fabi-version=6.
# The AVX code paths (IIR filters, delays, reverb) are chosen at run time and
# have SSE equivalents, so other compilers build without them.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_dkr_steam_audio_avx ON)
else()
    set(_dkr_steam_audio_avx OFF)
endif()
set(_dkr_steam_audio_install "${_dkr_steam_audio_root}/phonon-install")
set(DKR_STEAM_AUDIO_LIBRARY "${_dkr_steam_audio_install}/lib/${_dkr_steam_audio_platform}/libphonon.so")
ExternalProject_Add(dkr_steam_audio_phonon
    SOURCE_DIR "${DKR_STEAM_AUDIO_SOURCE}/core"
    PREFIX "${_dkr_steam_audio_root}/phonon"
    BUILD_ALWAYS ON
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_INSTALL_PREFIX=${_dkr_steam_audio_install}
        "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${CMAKE_CURRENT_LIST_DIR}/SteamAudioDependencyProvider.cmake"
        -DDKR_STEAM_AUDIO_PREFIX=${_dkr_steam_audio_prefix}
        -DBUILD_SHARED_LIBS=ON
        -DSTEAMAUDIO_ENABLE_IPP=OFF
        -DSTEAMAUDIO_ENABLE_MKL=OFF
        -DSTEAMAUDIO_ENABLE_EMBREE=OFF
        -DSTEAMAUDIO_ENABLE_AVX=${_dkr_steam_audio_avx}
        -DSTEAMAUDIO_BUILD_TESTS=OFF
        -DSTEAMAUDIO_BUILD_BENCHMARKS=OFF
        -DSTEAMAUDIO_BUILD_SAMPLES=OFF
        -DSTEAMAUDIO_BUILD_ITESTS=OFF
        -DSTEAMAUDIO_BUILD_DOCS=OFF
    DEPENDS dkr_steam_audio_libmysofa dkr_steam_audio_pffft dkr_steam_audio_flatc
    BUILD_BYPRODUCTS "${DKR_STEAM_AUDIO_LIBRARY}")

# The include directory must exist at configure time for the imported target.
file(MAKE_DIRECTORY "${_dkr_steam_audio_install}/include")
add_library(DKRSteamAudio SHARED IMPORTED GLOBAL)
set_target_properties(DKRSteamAudio PROPERTIES
    IMPORTED_LOCATION "${DKR_STEAM_AUDIO_LIBRARY}"
    IMPORTED_SONAME libphonon.so
    INTERFACE_INCLUDE_DIRECTORIES "${_dkr_steam_audio_install}/include")
add_dependencies(DKRSteamAudio dkr_steam_audio_phonon)

add_library(DKRSpatialAudio STATIC
    src/game/spatial_audio/alist_rewrite.cpp
    src/game/spatial_audio/binaural_renderer.cpp
    src/game/spatial_audio/envelope.cpp
    src/game/spatial_audio/half_band_decimator.cpp
    src/game/spatial_audio/snapshot.cpp
    src/game/spatial_audio/spatial_audio.cpp
    src/game/spatial_audio/test_tone.cpp
    src/game/spatial_audio/trace.cpp
    src/game/spatial_audio/voice_capture.cpp
)
# These read and write game memory, so they are compiled into the game
# executable.
set(DKR_SPATIAL_AUDIO_GAME_SOURCES
    src/game/spatial_audio/audio_task.cpp
    src/game/spatial_audio/game_capture.cpp
)
target_include_directories(DKRSpatialAudio PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/game")
target_compile_features(DKRSpatialAudio PUBLIC cxx_std_20)
target_link_libraries(DKRSpatialAudio PUBLIC DKRSteamAudio)

if(BUILD_TESTING)
    add_executable(DKRSpatialAudioTests tests/spatial_audio_tests.cpp)
    target_link_libraries(DKRSpatialAudioTests PRIVATE DKRSpatialAudio)
    target_compile_options(DKRSpatialAudioTests PRIVATE -UNDEBUG)
    add_test(NAME DKRSpatialAudio COMMAND DKRSpatialAudioTests)

    # Runs the recompiled audio microcode on synthetic command lists. Its data
    # segment comes from the ROM (aspMain.databin.bin in the decomp's extracted
    # assets) and is never committed; without it the test reports a skip.
    set(DKR_ASPMAIN_DATA "" CACHE FILEPATH
        "DKR audio microcode data segment, for the voice capture test")
    add_executable(DKRVoiceCaptureTests
        tests/voice_capture_tests.cpp
        src/game/spatial_audio/alist_rewrite.cpp
        src/game/spatial_audio/envelope.cpp
        "${DKR_GENERATED_RSP_SOURCE}/aspMain.cpp"
    )
    target_include_directories(DKRVoiceCaptureTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src/game"
        "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/librecomp/include"
        "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/ultramodern/include"
        "${DKR_DEPENDENCIES_DIR}/n64-modern-runtime/N64Recomp/include"
    )
    target_compile_features(DKRVoiceCaptureTests PRIVATE cxx_std_20)
    # The recompiled RSP vector unit uses SSE4.1, as in DKRPortGame.
    target_compile_options(DKRVoiceCaptureTests PRIVATE -UNDEBUG -msse4.1)
    add_test(NAME DKRVoiceCapture COMMAND DKRVoiceCaptureTests ${DKR_ASPMAIN_DATA})
    set_tests_properties(DKRVoiceCapture PROPERTIES SKIP_RETURN_CODE 77)

    # Checks the built library itself: the default HRTF loads at the output
    # rate and the binaural effect places sound on the expected side.
    add_executable(DKRSteamAudioTests tests/steam_audio_tests.cpp)
    target_compile_features(DKRSteamAudioTests PRIVATE cxx_std_20)
    target_link_libraries(DKRSteamAudioTests PRIVATE DKRSteamAudio)
    target_compile_options(DKRSteamAudioTests PRIVATE -UNDEBUG)
    add_test(NAME DKRSteamAudio COMMAND DKRSteamAudioTests)
endif()
