#pragma once

#include <SFML/Graphics.hpp>

namespace Project
{
    // ========================================
    // Informações do projeto
    // ========================================

    inline constexpr const char* NAME =
        "Moon Launcher";

    inline constexpr const char* WINDOW_TITLE =
        "Moon Launcher";

    inline constexpr const char* EXECUTABLE_NAME =
        "Moon Launcher.exe";

    inline constexpr const char* VERSION =
        "0.1.1";

    inline constexpr const char* DEVELOPER =
        "The Moon' Crew";

    inline constexpr const char* GITHUB_REPOSITORY =
        "The-Moon-Crew/Moon-Engine-Launcher";


    // ========================================
    // Janela principal
    // ========================================

    inline constexpr unsigned int WINDOW_WIDTH =
        1280;

    inline constexpr unsigned int WINDOW_HEIGHT =
        720;


    // ========================================
    // Loading Launcher
    // ========================================

    inline constexpr unsigned int LOADING_WIDTH =
        500;

    inline constexpr unsigned int LOADING_HEIGHT =
        300;


    // ========================================
    // Ícones
    // ========================================

    inline constexpr const char* ICON_550 =
        "assets/app/icons/iconMoon-550x550.png";

    inline constexpr const char* ICON_256 =
        "assets/app/icons/iconMoon-256x256.png";

    inline constexpr const char* ICON_128 =
        "assets/app/icons/iconMoon-128x128.png";

    inline constexpr const char* ICON_64 =
        "assets/app/icons/iconMoon-64x64.png";

    inline constexpr const char* ICON_48 =
        "assets/app/icons/iconMoon-48x48.png";

    inline constexpr const char* ICON_32 =
        "assets/app/icons/iconMoon-32x32.png";

    inline constexpr const char* ICON_16 =
        "assets/app/icons/iconMoon-16x16.png";

    inline constexpr const char* ICON_ICO =
        "assets/app/icons/app.ico";


    // ========================================
    // Inicialização
    // ========================================

    bool initialize();

    bool loadIcon(sf::RenderWindow& window);
}