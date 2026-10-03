// GUI-subsystem probe for the REAL showConsole code (src/LauncherConsole.hpp).
// Build:  x86_64-w64-mingw32-g++ -std=c++17 -mwindows -I. -Ithird_party/miniz tests/console_probe.cpp -o console_probe.exe
// Run it from a folder containing launcher-settings.json; it writes console_probe.txt next to the EXE.
#include "src/LauncherConsole.hpp"
#include <io.h>

int main()
{
    LauncherConsole::configure();

    std::cout << "hello from stdout\n";
    std::cerr << "hello from stderr\n";

    auto isConsole = [](FILE* f) {
        DWORD mode = 0;
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(f)));
        return h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) != 0;
    };

    std::ofstream out(LauncherConsole::launcherDirectory() / "console_probe.txt");
    out << "consoleAttached=" << (GetConsoleWindow() != nullptr || isConsole(stdout)) << '\n'
        << "stdoutIsConsole=" << isConsole(stdout) << '\n'
        << "stderrIsConsole=" << isConsole(stderr) << '\n';
    return 0;
}
