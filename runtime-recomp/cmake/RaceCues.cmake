# DKR-R's race sound cues (src/game/race_cues): the track edge sound, the
# turn signal and the hub beacon. The game-independent parts live in
# DKRRaceCues; the readers of the game's data read game memory and are
# compiled into the game executable.
add_library(DKRRaceCues STATIC
    src/game/race_cues/cue_audio.cpp
    src/game/race_cues/cue_settings.cpp
    src/game/race_cues/hub_beacon.cpp
    src/game/race_cues/hub_route.cpp
    src/game/race_cues/level_geometry.cpp
    src/game/race_cues/turn_tracker.cpp
)
set(DKR_RACE_CUES_GAME_SOURCES
    src/game/race_cues/hub_reader.cpp
    src/game/race_cues/race_reader.cpp
)
target_include_directories(DKRRaceCues PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/game")
target_compile_features(DKRRaceCues PUBLIC cxx_std_20)
# Spoken wording (accessibility/localization).
target_link_libraries(DKRRaceCues PUBLIC DKRAccessibility)

if(BUILD_TESTING)
    add_executable(DKRRaceCuesTests tests/race_cues_tests.cpp)
    target_link_libraries(DKRRaceCuesTests PRIVATE DKRRaceCues)
    if(MSVC)
        target_compile_options(DKRRaceCuesTests PRIVATE /UNDEBUG)
    else()
        target_compile_options(DKRRaceCuesTests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME DKRRaceCues COMMAND DKRRaceCuesTests)
endif()
