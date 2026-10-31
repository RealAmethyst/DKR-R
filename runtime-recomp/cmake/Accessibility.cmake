# Screen reader accessibility layer (src/game/accessibility) and the
# redesigned in-game Options menu (src/game/options_menu).
#
# Speech goes through Prism, built as a shared library: a static Prism forces
# its bundled fmt into the link, which clashes with N64Recomp's fmt. Prism's
# CMake writes global cache defaults (shared libraries, and the static MSVC
# runtime on Windows); both are pinned to DKR-R's values before it is added,
# and the shared build is requested only inside the function scope below.
option(DKR_ACCESSIBILITY_SPEECH "Screen reader speech through Prism" ${DKR_RUNTIME_BUILD_RT64})
set(DKR_PRISM_SOURCE "${DKR_DEPENDENCIES_DIR}/prism")

# Game-independent parts: speech, wording and text preparation.
add_library(DKRAccessibility STATIC
    src/game/accessibility/guest_text.cpp
    src/game/accessibility/localization.cpp
    src/game/accessibility/speech.cpp
    src/game/accessibility/speech_service.cpp
)
target_include_directories(DKRAccessibility PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/game")
target_compile_features(DKRAccessibility PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(DKRAccessibility PUBLIC Threads::Threads)

# Hook handlers called by the recompiled game. They are compiled into the game
# executable itself: as an archive they would precede the generated payloads
# that reference them in GNU ld's left-to-right scan.
set(DKR_ACCESSIBILITY_GAME_SOURCES
    src/game/accessibility/game_text.cpp
    src/game/accessibility/guest_memory.cpp
    src/game/accessibility/menu_speech.cpp
    src/game/accessibility/symbol_lookup.cpp
    src/game/options_menu/game_audio_memory.cpp
    src/game/options_menu/game_settings.cpp
    src/game/options_menu/guest_calls.cpp
    src/game/options_menu/options_menu.cpp
    src/game/options_menu/options_screen.cpp
)
# The Options menu's bindings to each revision's recompiled functions are
# compiled with that revision's payload (funcs.h, Rev A symbol map).
set(DKR_OPTIONS_MENU_CALLS_V77 src/game/options_menu/game_calls_v77.cpp)
set(DKR_OPTIONS_MENU_CALLS_V80 src/game/options_menu/game_calls_v80.cpp)

function(dkr_add_prism)
    set(BUILD_SHARED_LIBS ON)
    set(CMAKE_SKIP_INSTALL_RULES ON)
    set(PRISM_IPO OFF)
    set(PRISM_ENABLE_TESTS OFF)
    set(PRISM_ENABLE_DEMOS OFF)
    if(UNIX AND NOT APPLE)
        # Orca is the Linux screen reader. Require it, so a missing D-Bus
        # header fails the build instead of silently dropping speech.
        # speech-dispatcher and Spiel are plain text-to-speech, never used.
        set(PRISM_ENABLE_ORCA_BACKEND ON)
        set(PRISM_ENABLE_SPEECH_DISPATCHER_BACKEND OFF)
        set(PRISM_ENABLE_SPIEL_BACKEND OFF)
    endif()
    add_subdirectory("${DKR_PRISM_SOURCE}" "${CMAKE_BINARY_DIR}/prism" EXCLUDE_FROM_ALL)
endfunction()

if(DKR_ACCESSIBILITY_SPEECH)
    if(NOT EXISTS "${DKR_PRISM_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR
            "Prism has not been prepared at ${DKR_PRISM_SOURCE}. "
            "Run scripts/bootstrap_dependencies.py --only prism, or configure "
            "DKR_ACCESSIBILITY_SPEECH=OFF for a build without speech.")
    endif()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
    if(MSVC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
            CACHE STRING "MSVC runtime library")
    endif()
    dkr_add_prism()
    target_sources(DKRAccessibility PRIVATE src/game/accessibility/speech_prism.cpp)
    target_link_libraries(DKRAccessibility PRIVATE prism::prism)
    target_compile_definitions(DKRAccessibility PRIVATE DKR_ACCESSIBILITY_HAS_PRISM=1)
endif()

if(BUILD_TESTING)
    add_executable(DKRAccessibilitySpeechServiceTests
        tests/accessibility_speech_service_tests.cpp
        src/game/accessibility/speech_service.cpp
    )
    target_include_directories(DKRAccessibilitySpeechServiceTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src/game")
    target_compile_features(DKRAccessibilitySpeechServiceTests PRIVATE cxx_std_20)
    target_link_libraries(DKRAccessibilitySpeechServiceTests PRIVATE Threads::Threads)
    add_test(NAME DKRAccessibilitySpeechService COMMAND DKRAccessibilitySpeechServiceTests)

    add_executable(DKRAccessibilityMenuSpeechTests
        tests/accessibility_menu_speech_tests.cpp
        src/game/accessibility/guest_text.cpp
        src/game/accessibility/localization.cpp
    )
    target_include_directories(DKRAccessibilityMenuSpeechTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src/game")
    target_compile_features(DKRAccessibilityMenuSpeechTests PRIVATE cxx_std_20)
    add_test(NAME DKRAccessibilityMenuSpeech COMMAND DKRAccessibilityMenuSpeechTests)

    add_executable(DKROptionsMenuTests
        tests/options_menu_tests.cpp
        src/game/options_menu/options_menu.cpp
        src/game/accessibility/localization.cpp
    )
    target_include_directories(DKROptionsMenuTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../src/game")
    target_compile_features(DKROptionsMenuTests PRIVATE cxx_std_20)
    add_test(NAME DKROptionsMenu COMMAND DKROptionsMenuTests)
endif()
