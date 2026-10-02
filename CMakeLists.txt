cmake_minimum_required(VERSION 3.21)
project(MoonLauncher CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

if(ANDROID)
    add_library(main SHARED
        src/main.cpp
        src/LoadingLauncher.cpp
        src/ModRuntime.cpp
        project.cpp
        ${MINIZ_SOURCES}
    )
    target_link_options(main PRIVATE "-u" "ANativeActivity_onCreate")
else()
    add_executable(MoonLauncher
        src/main.cpp
        src/LoadingLauncher.cpp
        src/ModRuntime.cpp
        project.cpp
        ${MINIZ_SOURCES}
    )
    if(WIN32)
        target_sources(MoonLauncher PRIVATE app.rc)
        set_target_properties(MoonLauncher PROPERTIES
            WIN32_EXECUTABLE TRUE
        )

        add_executable(LauncherUpdater src/LauncherUpdater.cpp)
        target_link_options(LauncherUpdater PRIVATE -municode)
        set_target_properties(LauncherUpdater PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/export/windows/release/bin"
        )
        add_dependencies(MoonLauncher LauncherUpdater)
    endif()
    target_include_directories(MoonLauncher PRIVATE . third_party/miniz)
    target_link_libraries(MoonLauncher PRIVATE
        SFML::Graphics
        SFML::Window
        SFML::System
        SFML::Audio
        CURL::libcurl
    )

    if(WIN32)
        target_link_libraries(MoonLauncher PRIVATE Lua::Lua shell32)
    endif()

    if(WIN32)
        set_target_properties(MoonLauncher PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/export/windows/release/bin"
        )

        add_custom_command(TARGET MoonLauncher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SFML::Graphics>
                $<TARGET_FILE:SFML::Window>
                $<TARGET_FILE:SFML::System>
                $<TARGET_FILE:SFML::Audio>
                $<TARGET_FILE:CURL::libcurl>
                $<TARGET_RUNTIME_DLLS:MoonLauncher>
                "${CMAKE_SOURCE_DIR}/export/windows/release/bin"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
endif()

    if(WIN32)
        set(LAUNCHER_OUTPUT_DIR
            "${CMAKE_SOURCE_DIR}/export/windows/release/bin"
        )
        file(MAKE_DIRECTORY "${LAUNCHER_OUTPUT_DIR}")
        if(NOT EXISTS "${LAUNCHER_OUTPUT_DIR}/launcher-settings.json")
            configure_file(
                "${CMAKE_SOURCE_DIR}/launcher-settings.json"
                "${LAUNCHER_OUTPUT_DIR}/launcher-settings.json"
                COPYONLY
            )
        endif()
    endif()
