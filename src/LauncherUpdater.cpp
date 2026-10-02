#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

static void writeUpdaterLog(
    const fs::path& installDirectory,
    const std::string& message
) {
    std::ofstream log(installDirectory / "launcher-update.log", std::ios::app);
    if (log)
        log << message << '\n';
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 5) {
        return 2;
    }

    const DWORD parentProcessId =
        static_cast<DWORD>(_wcstoui64(argv[1], nullptr, 10));
    const fs::path stagedBin(argv[2]);
    const fs::path installDirectory(argv[3]);
    const fs::path launcherPath(argv[4]);

    HANDLE parentProcess = OpenProcess(
        SYNCHRONIZE,
        FALSE,
        parentProcessId
    );

    if (parentProcess) {
        WaitForSingleObject(parentProcess, INFINITE);
        CloseHandle(parentProcess);
    }

    std::error_code error;
    for (
        fs::recursive_directory_iterator iterator(
            stagedBin,
            fs::directory_options::skip_permission_denied,
            error
        ),
        end;
        iterator != end && !error;
        iterator.increment(error)
    ) {
        const fs::path relativePath =
            iterator->path().lexically_relative(stagedBin);

        if (relativePath.empty())
            continue;

        const fs::path firstComponent = *relativePath.begin();
        if (
            firstComponent == L"com.funkinmoon" ||
            firstComponent == L"mods"
        ) {
            if (iterator->is_directory(error))
                iterator.disable_recursion_pending();
            continue;
        }

        if (
            !iterator->is_directory(error) &&
            relativePath.filename() == L"launcher-settings.json"
        )
            continue;

        const fs::path destination = installDirectory / relativePath;
        if (iterator->is_directory(error)) {
            fs::create_directories(destination, error);
            if (error) {
                writeUpdaterLog(installDirectory, "Failed creating: " + destination.string());
                return 3;
            }
            continue;
        }

        fs::create_directories(destination.parent_path(), error);
        if (!error) {
            fs::copy_file(
                iterator->path(),
                destination,
                fs::copy_options::overwrite_existing,
                error
            );
        }

        if (error) {
            writeUpdaterLog(installDirectory, "Failed copying: " + destination.string());
            return 4;
        }
    }

    if (error) {
        writeUpdaterLog(installDirectory, "Failed reading staged package: " + error.message());
        return 5;
    }

    if (!fs::exists(launcherPath)) {
        writeUpdaterLog(installDirectory, "Updated launcher executable is missing.");
        return 6;
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    if (!CreateProcessW(
            launcherPath.c_str(),
            nullptr,
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            installDirectory.c_str(),
            &startupInfo,
            &processInfo
        )) {
        writeUpdaterLog(installDirectory, "Failed to restart launcher.");
        return 7;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    fs::remove_all(stagedBin.parent_path(), error);
    return 0;
}
