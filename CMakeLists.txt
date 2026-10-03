cmake_minimum_required(VERSION 3.21)
project(MoonLauncher CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # Needed for app.rc (launcher icon)
    enable_language(RC)
endif()

if(ANDROID)
    find_package(SFML COMPONENTS Graphics Window System Audio Main REQUIRED)
else()
    find_package(SFML COMPONENTS Graphics Window System Audio REQUIRED)
endif()
find_package(CURL REQUIRED)
if(WIN32)
    find_package(Lua REQUIRED)
endif()

set(MINIZ_SOURCES
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)

set(LAUNCHER_SOURCES
    src/main.cpp
    src/LoadingLauncher.cpp
    src/ModRuntime.cpp
    project.cpp
    ${MINIZ_SOURCES}
)

# ------------------------------------------------------------
# Main target: libmain.so on Android, MoonLauncher executable elsewhere
# ------------------------------------------------------------
if(ANDROID)
    add_library(main SHARED ${LAUNCHER_SOURCES})
    set(LAUNCHER_TARGET main)
    target_link_options(main PRIVATE "-u" "ANativeActivity_onCreate")
else()
    add_executable(MoonLauncher ${LAUNCHER_SOURCES})
    set(LAUNCHER_TARGET MoonLauncher)
endif()

# Shared by every platform (these used to be missing for the Android target)
target_include_directories(${LAUNCHER_TARGET} PRIVATE . third_party/miniz)
target_link_libraries(${LAUNCHER_TARGET} PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Audio
    CURL::libcurl
)

if(ANDROID)
    target_link_libraries(main PRIVATE SFML::Main log android)
endif()

# ------------------------------------------------------------
# Windows: launcher + LauncherUpdater.exe -> export/windows/release/bin
# ------------------------------------------------------------
if(WIN32)
    set(LAUNCHER_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/export/windows/release/bin")
    file(MAKE_DIRECTORY "${LAUNCHER_OUTPUT_DIR}")

    target_sources(MoonLauncher PRIVATE app.rc)
    target_link_libraries(MoonLauncher PRIVATE Lua::Lua shell32)
    set_target_properties(MoonLauncher PROPERTIES
        WIN32_EXECUTABLE TRUE
        RUNTIME_OUTPUT_DIRECTORY "${LAUNCHER_OUTPUT_DIR}"
    )

    if(MSVC)
        # WIN32_EXECUTABLE uses WinMainCRTStartup; the launcher defines main()
        target_link_options(MoonLauncher PRIVATE /ENTRY:mainCRTStartup)
    endif()

    # Helper that swaps the files after the launcher closes (src/LauncherUpdater.cpp)
    add_executable(LauncherUpdater src/LauncherUpdater.cpp)
    set_target_properties(LauncherUpdater PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${LAUNCHER_OUTPUT_DIR}"
    )
    if(MINGW)
        # -municode: wmain entry point (MSVC picks wmain automatically).
        # -static: the updater runs from a temp folder, keep it self-contained.
        target_link_options(LauncherUpdater PRIVATE -municode -static)
    endif()
    add_dependencies(MoonLauncher LauncherUpdater)

    # Runtime files next to the EXE (DLLs, assets, versions.json)
    add_custom_command(TARGET MoonLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
            $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:MoonLauncher>>,copy_if_different,true>
            $<TARGET_RUNTIME_DLLS:MoonLauncher>
            "${LAUNCHER_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${LAUNCHER_OUTPUT_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/versions.json"
            "${LAUNCHER_OUTPUT_DIR}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    # Default settings, never overwrite the user's file
    if(NOT EXISTS "${LAUNCHER_OUTPUT_DIR}/launcher-settings.json")
        configure_file(
            "${CMAKE_SOURCE_DIR}/launcher-settings.json"
            "${LAUNCHER_OUTPUT_DIR}/launcher-settings.json"
            COPYONLY
        )
    endif()
endif()
