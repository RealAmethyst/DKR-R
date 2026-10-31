# Included at the top of Steam Audio's own configure (CMAKE_PROJECT_TOP_LEVEL_INCLUDES,
# set by SteamAudio.cmake). Steam Audio's find modules only search under its
# source tree; this provider answers those find_package() calls with the
# libraries DKR-R built into DKR_STEAM_AUDIO_PREFIX instead. Any other package
# goes to the normal search.
if(NOT DKR_STEAM_AUDIO_PREFIX)
    message(FATAL_ERROR "SteamAudioDependencyProvider.cmake is only used by DKR-R's SteamAudio.cmake.")
endif()

macro(dkr_steam_audio_provide_dependency method package_name)
    if("${package_name}" STREQUAL "PFFFT")
        if(NOT TARGET PFFFT::PFFFT)
            add_library(PFFFT::PFFFT STATIC IMPORTED)
            set_target_properties(PFFFT::PFFFT PROPERTIES
                IMPORTED_LOCATION "${DKR_STEAM_AUDIO_PREFIX}/lib/libpffft.a"
                INTERFACE_INCLUDE_DIRECTORIES "${DKR_STEAM_AUDIO_PREFIX}/include")
        endif()
        set(PFFFT_FOUND TRUE)
    elseif("${package_name}" STREQUAL "MySOFA")
        if(NOT TARGET MySOFA::MySOFA)
            add_library(MySOFA::MySOFA STATIC IMPORTED)
            set_target_properties(MySOFA::MySOFA PROPERTIES
                IMPORTED_LOCATION "${DKR_STEAM_AUDIO_PREFIX}/lib/libmysofa.a"
                INTERFACE_INCLUDE_DIRECTORIES "${DKR_STEAM_AUDIO_PREFIX}/include"
                INTERFACE_LINK_LIBRARIES "${DKR_STEAM_AUDIO_PREFIX}/lib/libz.a")
        endif()
        set(MySOFA_FOUND TRUE)
    elseif("${package_name}" STREQUAL "FlatBuffers")
        if(NOT TARGET FlatBuffers::FlatBuffers)
            add_library(FlatBuffers::FlatBuffers INTERFACE IMPORTED)
            set_target_properties(FlatBuffers::FlatBuffers PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${DKR_STEAM_AUDIO_PREFIX}/include")
        endif()
        set(FlatBuffers_EXECUTABLE "${DKR_STEAM_AUDIO_PREFIX}/bin/flatc")
        set(FlatBuffers_FOUND TRUE)
    else()
        find_package(${package_name} ${ARGN} BYPASS_PROVIDER)
    endif()
endmacro()

cmake_language(SET_DEPENDENCY_PROVIDER dkr_steam_audio_provide_dependency
    SUPPORTED_METHODS FIND_PACKAGE)
