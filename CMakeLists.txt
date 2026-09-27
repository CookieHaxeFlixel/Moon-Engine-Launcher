cmake_minimum_required(VERSION 3.21)
project(MoonLauncher CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(ANDROID)
    find_package(SFML COMPONENTS Graphics Window System Main REQUIRED)
else()
    find_package(SFML COMPONENTS Graphics Window System REQUIRED)
endif()
find_package(CURL REQUIRED)

set(MINIZ_SOURCES
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)

if(ANDROID)
    add_library(main SHARED main.cpp ${MINIZ_SOURCES})
    target_include_directories(main PRIVATE third_party/miniz)
    target_link_libraries(main PRIVATE
        SFML::Graphics
        SFML::Window
        SFML::System
        SFML::Main
        CURL::libcurl
        android
        log
    )
    target_link_options(main PRIVATE "-u" "ANativeActivity_onCreate")
else()
    add_executable(MoonLauncher main.cpp ${MINIZ_SOURCES})
    target_include_directories(MoonLauncher PRIVATE third_party/miniz)
    target_link_libraries(MoonLauncher PRIVATE
        SFML::Graphics
        SFML::Window
        SFML::System
        CURL::libcurl
    )
endif()
