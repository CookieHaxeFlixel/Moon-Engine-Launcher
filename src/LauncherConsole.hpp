#pragma once

// ============================================================
// launcher-settings.json -> { "showConsole": true | false }
//   false (default, also when the file/key is missing): no console window.
//   true : AllocConsole() and redirect stdin/stdout/stderr to it.
// Windows only; the JSON parsing itself is UpdateUtils::parseShowConsole().
// ============================================================

#if defined(_WIN32)

#include "UpdateUtils.hpp"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace LauncherConsole
{
    namespace fs = std::filesystem;

    inline fs::path launcherDirectory()
    {
        std::wstring executablePath(MAX_PATH, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr,
            executablePath.data(),
            static_cast<DWORD>(executablePath.size())
        );

        if (length == 0 || length >= executablePath.size())
            return fs::current_path();

        executablePath.resize(length);
        return fs::path(executablePath).parent_path();
    }

    inline bool enabledBySettings()
    {
        std::ifstream settingsFile(launcherDirectory() / "launcher-settings.json");

        if (!settingsFile)
            return false;

        const std::string json{
            std::istreambuf_iterator<char>(settingsFile),
            std::istreambuf_iterator<char>()
        };

        return UpdateUtils::parseShowConsole(json);
    }

    inline void configure()
    {
        if (!enabledBySettings())
            return;

        if (!GetConsoleWindow() && !AllocConsole())
            return;

        std::freopen("CONIN$", "r", stdin);
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        std::ios::sync_with_stdio(true);
        std::cout.clear();
        std::cerr.clear();
    }
}

#endif
