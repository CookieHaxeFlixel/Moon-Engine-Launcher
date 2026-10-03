#pragma once

// ============================================================
// Update helpers that do NOT depend on Windows / SFML / curl,
// so they can be unit-tested on any platform (see tests/).
// ============================================================

#include "third_party/miniz/miniz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace UpdateUtils
{
    namespace fs = std::filesystem;

    // ------------------------------------------------------------
    // Versions ("v0.1.1" -> {0, 1, 1}); anything unparsable -> 0.0.0
    // ------------------------------------------------------------

    inline std::array<unsigned int, 3> versionParts(std::string version)
    {
        if (!version.empty() && (version.front() == 'v' || version.front() == 'V'))
            version.erase(version.begin());

        std::array<unsigned int, 3> parts{};
        size_t position = 0;

        for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex)
        {
            while (position < version.size() && !std::isdigit(static_cast<unsigned char>(version[position])))
                ++position;

            if (position == version.size())
                break;

            unsigned int value = 0;
            while (position < version.size() && std::isdigit(static_cast<unsigned char>(version[position])))
            {
                if (value < 100000000u)
                    value = value * 10 + static_cast<unsigned int>(version[position] - '0');
                ++position;
            }

            parts[partIndex] = value;

            if (position < version.size() && version[position] == '.')
                ++position;
            else
                break;
        }

        return parts;
    }

    // True only when `candidate` is strictly newer: equal or older
    // versions never trigger an update (no downgrades).
    inline bool isNewerVersion(const std::string& candidate, const std::string& current)
    {
        return versionParts(candidate) > versionParts(current);
    }

    // ------------------------------------------------------------
    // Release asset name: moon-launcher-*.zip (case-insensitive)
    // ------------------------------------------------------------

    inline bool isLauncherZipAsset(const std::string& assetName)
    {
        std::string lower = assetName;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        const std::string prefix = "moon-launcher-";
        const std::string suffix = ".zip";

        return lower.size() > prefix.size() + suffix.size() &&
               lower.compare(0, prefix.size(), prefix) == 0 &&
               lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    // ------------------------------------------------------------
    // Archive entry validation (zip-slip / absolute paths / drives / ADS)
    // ------------------------------------------------------------

    inline bool isSafeArchiveEntry(const std::string& name)
    {
        if (name.empty())
            return false;

        if (name.find('\0') != std::string::npos || name.find(':') != std::string::npos)
            return false;

        if (name.front() == '/' || name.front() == '\\')
            return false;

        size_t start = 0;
        while (start <= name.size())
        {
            size_t end = name.find_first_of("/\\", start);
            if (end == std::string::npos)
                end = name.size();

            const std::string component = name.substr(start, end - start);
            if (component == ".." || component == ".")
                return false;

            start = end + 1;
        }

        return true;
    }

    // ------------------------------------------------------------
    // Extraction
    // ------------------------------------------------------------

    inline fs::path pathFromUtf8(const std::string& text)
    {
#if defined(__cpp_lib_char8_t)
        return fs::path(std::u8string(text.begin(), text.end()));
#else
        return fs::u8path(text);
#endif
    }

    inline std::FILE* openFile(const fs::path& path, bool write)
    {
#if defined(_WIN32)
        return _wfopen(path.c_str(), write ? L"wb" : L"rb");
#else
        return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
    }

    // Extracts every entry of `archivePath` into `destination`.
    // Returns false if ANY entry is unsafe or fails (nothing outside
    // `destination` is ever written).
    inline bool extractReleasePackage(
        const fs::path& archivePath,
        const fs::path& destination,
        std::uint64_t maxTotalBytes = 2ull * 1024 * 1024 * 1024)
    {
        std::FILE* archiveFile = openFile(archivePath, false);
        if (!archiveFile)
            return false;

        mz_zip_archive archive{};
        if (!mz_zip_reader_init_cfile(&archive, archiveFile, 0, 0))
        {
            std::fclose(archiveFile);
            return false;
        }

        bool success = true;
        std::uint64_t totalBytes = 0;

        std::error_code error;
        fs::create_directories(destination, error);
        const fs::path root = fs::absolute(destination, error).lexically_normal();

        const mz_uint fileCount = mz_zip_reader_get_num_files(&archive);

        for (mz_uint index = 0; index < fileCount && success; ++index)
        {
            mz_zip_archive_file_stat fileStat{};
            if (!mz_zip_reader_file_stat(&archive, index, &fileStat))
            {
                success = false;
                break;
            }

            const std::string entryName = fileStat.m_filename;

            if (!isSafeArchiveEntry(entryName))
            {
                success = false;
                break;
            }

            totalBytes += fileStat.m_uncomp_size;
            if (totalBytes > maxTotalBytes)
            {
                success = false;
                break;
            }

            const fs::path outputPath = (root / pathFromUtf8(entryName)).lexically_normal();
            const fs::path relative = outputPath.lexically_relative(root);

            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            {
                success = false;
                break;
            }

            if (mz_zip_reader_is_file_a_directory(&archive, index))
            {
                fs::create_directories(outputPath, error);
                if (error)
                    success = false;
                continue;
            }

            fs::create_directories(outputPath.parent_path(), error);
            if (error)
            {
                success = false;
                break;
            }

            std::FILE* out = openFile(outputPath, true);
            if (!out)
            {
                success = false;
                break;
            }

            const bool extracted = mz_zip_reader_extract_to_cfile(&archive, index, out, 0);
            const bool closed = std::fclose(out) == 0;

            if (!extracted || !closed)
                success = false;
        }

        mz_zip_reader_end(&archive);
        std::fclose(archiveFile);
        return success;
    }

    // ------------------------------------------------------------
    // Finds `<root>/bin/<exe>` or `<root>/<single folder>/bin/<exe>`
    // and returns that `bin` directory (empty path if not found).
    // ------------------------------------------------------------

    inline fs::path findLauncherBin(const fs::path& extractedRoot, const std::string& executableName)
    {
        std::error_code error;
        fs::path best;
        size_t bestDepth = 0;

        for (fs::recursive_directory_iterator it(extractedRoot, fs::directory_options::skip_permission_denied, error), end;
             it != end && !error;
             it.increment(error))
        {
            if (!it->is_regular_file(error) || it->path().filename() != executableName)
                continue;

            const fs::path binDir = it->path().parent_path();
            if (binDir.filename() != "bin")
                continue;

            const fs::path relative = binDir.lexically_relative(extractedRoot);
            size_t depth = 0;
            for (const fs::path& part : relative)
            {
                (void)part;
                ++depth;
            }

            // "bin" or "<one folder>/bin" only
            if (depth > 2)
                continue;

            if (best.empty() || depth < bestDepth)
            {
                best = binDir;
                bestDepth = depth;
            }
        }

        return best;
    }

    // ------------------------------------------------------------
    // Best-effort cleanup of old "MoonLauncherUpdate-*" staging folders in
    // `tempRoot` (the updater cannot delete its own running EXE). Only folders
    // with exactly that prefix are touched; failures are ignored.
    // ------------------------------------------------------------

    inline void removeStaleUpdateFolders(const fs::path& tempRoot)
    {
        std::error_code error;

        if (tempRoot.empty() || !fs::is_directory(tempRoot, error))
            return;

        for (fs::directory_iterator it(tempRoot, fs::directory_options::skip_permission_denied, error), end;
             it != end && !error;
             it.increment(error))
        {
            std::error_code entryError;
            const std::string name = it->path().filename().string();

            if (name.rfind("MoonLauncherUpdate-", 0) == 0 && it->is_directory(entryError))
                fs::remove_all(it->path(), entryError);
        }
    }

    // ------------------------------------------------------------
    // launcher-settings.json: { "showConsole": true }
    // ------------------------------------------------------------

    inline bool parseShowConsole(const std::string& json)
    {
        const size_t key = json.find("\"showConsole\"");
        if (key == std::string::npos)
            return false;

        const size_t colon = json.find(':', key);
        if (colon == std::string::npos)
            return false;

        const size_t value = json.find_first_not_of(" \t\r\n", colon + 1);
        if (value == std::string::npos || json.compare(value, 4, "true") != 0)
            return false;

        const size_t after = value + 4;
        return after >= json.size() ||
               !std::isalnum(static_cast<unsigned char>(json[after]));
    }
}
