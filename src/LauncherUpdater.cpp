// ============================================================
// LauncherUpdater.exe
//
// Started by Moon Launcher.exe with:
//   LauncherUpdater.exe <launcherPid> <stagedBin> <installDir> <launcherExe>
//
// Waits for the launcher to close, copies the staged files over the
// installation (keeping com.funkinmoon, mods and launcher-settings.json),
// restarts the launcher and removes the temporary staging folder.
// ============================================================

#include <windows.h>

#include "UpdateInstall.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

// Removes the "MoonLauncherUpdate-*" temp folder that contains `stagedBin`.
// Never deletes anything that is not inside such a folder.
static void cleanupStaging(const fs::path& stagedBin)
{
    for (
        fs::path current = stagedBin;
        current.has_parent_path() && current != current.parent_path();
        current = current.parent_path()
    ) {
        if (current.filename().wstring().rfind(L"MoonLauncherUpdate-", 0) == 0) {
            std::error_code error;
            fs::remove_all(current, error);
            return;
        }
    }
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc != 5)
        return 2;

    const DWORD parentProcessId =
        static_cast<DWORD>(_wcstoui64(argv[1], nullptr, 10));
    const fs::path stagedBin(argv[2]);
    const fs::path installDirectory(argv[3]);
    const fs::path launcherPath(argv[4]);

    // Wait until the launcher has fully closed (its files are locked until then).
    if (HANDLE parentProcess = OpenProcess(SYNCHRONIZE, FALSE, parentProcessId)) {
        WaitForSingleObject(parentProcess, INFINITE);
        CloseHandle(parentProcess);
    }

    int result = 0;

    if (!fs::exists(stagedBin / launcherPath.filename())) {
        UpdateInstall::writeLog(installDirectory, "Staged package has no launcher executable; update skipped.");
        result = 8;
    } else {
        result = UpdateInstall::installStagedFiles(stagedBin, installDirectory);
    }

    // Always try to start the launcher again (updated, or the old one if the update failed).
    if (!fs::exists(launcherPath)) {
        UpdateInstall::writeLog(installDirectory, "Launcher executable is missing.");
        return result != 0 ? result : 6;
    }

    std::wstring commandLine = L"\"" + launcherPath.wstring() + L"\"";

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    if (!CreateProcessW(
            launcherPath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            installDirectory.c_str(),
            &startupInfo,
            &processInfo
        )) {
        UpdateInstall::writeLog(installDirectory, "Failed to restart launcher.");
        return result != 0 ? result : 7;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (result == 0)
        cleanupStaging(stagedBin);

    return result;
}
