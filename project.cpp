#include "project.hpp"

#include <iostream>

namespace Project
{
    bool initialize()
    {
        std::cout
            << "========================================\n"
            << "          Moon Launcher\n"
            << "========================================\n"
            << "Name: " << NAME << '\n'
            << "Version: " << VERSION << '\n'
            << "Developer: " << DEVELOPER << '\n'
            << "Repository: " << GITHUB_REPOSITORY << '\n'
            << "========================================\n";

        return true;
    }


    bool loadIcon(sf::RenderWindow& window)
    {
        sf::Image icon;

        if (!icon.loadFromFile(ICON_256))
        {
            std::cerr
                << "[Project] Failed to load launcher icon: "
                << ICON_256
                << '\n';

            return false;
        }

        window.setIcon(icon);

        return true;
    }
}