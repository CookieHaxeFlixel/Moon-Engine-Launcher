#pragma once

// ============================================================
// File replacement used by LauncherUpdater.exe.
// Pure <filesystem> code, so it can be tested on any platform.
// ============================================================

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace UpdateInstall
{
    namespace fs = std::filesystem;

    // path -> text for logs; never throws on odd characters
    inline std::string pathText(const fs::path& path)
    {
        try { return path.string(); }
        catch (...) { return "<unprintable path>"; }
    }

    inline void writeLog(const fs::path& installDirectory, const std::string& message)
    {
        std::ofstream log(installDirectory / "launcher-update.log", std::ios::app);
        if (log)
            log << message << '\n';
    }

    // Single copy that REPLACES an existing destination file.
    // (std::filesystem::copy_file(..., overwrite_existing) returns "File exists"
    //  with MinGW-w64 GCC 13, so Windows uses CopyFileW directly.)
    inline std::error_code copyReplace(const fs::path& from, const fs::path& to)
    {
#if defined(_WIN32)
        if (CopyFileW(from.c_str(), to.c_str(), FALSE))
            return {};
        return std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
        std::error_code error;
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
        return error;
#endif
    }

    // Returns an empty error_code on success, otherwise the last error.
    inline std::error_code copyWithRetry(
        const fs::path& from,
        const fs::path& to,
        int attempts,
        std::chrono::milliseconds delay)
    {
        std::error_code error;

        for (int attempt = 0; attempt < attempts; ++attempt)
        {
            error = copyReplace(from, to);
            if (!error)
                return error;

            std::this_thread::sleep_for(delay);
        }

        return error;
    }

    // Copies everything from `stagedBin` into `installDirectory`, EXCEPT:
    //   - com.funkinmoon/   (saves, installed game versions)
    //   - mods/
    //   - launcher-settings.json (only written when it does not exist yet)
    // Returns 0 on success, otherwise an error code (also logged).
    inline int installStagedFiles(
        const fs::path& stagedBin,
        const fs::path& installDirectory,
        int attempts = 20,
        std::chrono::milliseconds delay = std::chrono::milliseconds(250))
    {
        std::error_code error;

        for (fs::recursive_directory_iterator it(stagedBin, fs::directory_options::skip_permission_denied, error), end;
             it != end && !error;
             it.increment(error))
        {
            const fs::path relativePath = it->path().lexically_relative(stagedBin);
            if (relativePath.empty())
                continue;

            const bool isDirectory = it->is_directory(error);

            const fs::path first = *relativePath.begin();
            if (first == "com.funkinmoon" || first == "mods")
            {
                if (isDirectory)
                    it.disable_recursion_pending();
                continue;
            }

            const fs::path destination = installDirectory / relativePath;

            if (isDirectory)
            {
                fs::create_directories(destination, error);
                if (error)
                {
                    writeLog(installDirectory, "Failed creating: " + pathText(destination));
                    return 3;
                }
                continue;
            }

            if (relativePath == "launcher-settings.json" && fs::exists(destination))
                continue;

            fs::create_directories(destination.parent_path(), error);
            if (!error)
                error = copyWithRetry(it->path(), destination, attempts, delay);

            if (error)
            {
                writeLog(installDirectory, "Failed copying: " + pathText(destination) + " (" + error.message() + ")");
                return 4;
            }
        }

        if (error)
        {
            writeLog(installDirectory, "Failed reading staged package: " + error.message());
            return 5;
        }

        return 0;
    }
}
