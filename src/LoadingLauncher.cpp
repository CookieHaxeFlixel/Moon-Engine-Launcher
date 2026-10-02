#include "LoadingLauncher.hpp"

#include "project.hpp"
#include "third_party/miniz/miniz.h"

#include <SFML/Graphics.hpp>

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#endif

#include <curl/curl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
namespace
{
    bool consoleEnabledBySettings()
    {
        std::ifstream settingsFile(
            launcherDirectory() / "launcher-settings.json"
        );

        if (!settingsFile)
            return false;

        const std::string json{
            std::istreambuf_iterator<char>(settingsFile),
            std::istreambuf_iterator<char>()
        };

        const size_t keyPosition = json.find("\"showConsole\"");
        if (keyPosition == std::string::npos)
            return false;

        const size_t colonPosition = json.find(':', keyPosition);
        if (colonPosition == std::string::npos)
            return false;

        const size_t valuePosition = json.find_first_not_of(
            " \t\r\n",
            colonPosition + 1
        );

        return
            valuePosition != std::string::npos &&
            json.compare(valuePosition, 4, "true") == 0;
    }

    void configureLauncherConsole()
    {
        if (!consoleEnabledBySettings())
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


extern void runLauncher();

namespace fs = std::filesystem;

namespace
{
    // ============================================================
    // GitHub
    // ============================================================

    constexpr const char* GITHUB_API =
        "https://api.github.com/repos/"
        "The-Moon-Crew/Moon-Engine-Launcher/"
        "releases/latest";


    // ============================================================
    // Download state
    // ============================================================

    std::atomic<double> downloadProgress = 0.0;

    std::atomic<bool> downloadFinished = false;

    std::atomic<bool> downloadSuccess = false;


    // ============================================================
    // HTTP memory callback
    // ============================================================

    size_t writeStringCallback(
        void* contents,
        size_t size,
        size_t nmemb,
        void* userData
    )
    {
        const size_t total =
            size * nmemb;

        std::string* output =
            static_cast<std::string*>(userData);

        output->append(
            static_cast<char*>(contents),
            total
#if defined(_WIN32)
    configureLauncherConsole();
#endif
        );

        return total;
    }


    // ============================================================
    // HTTP file callback
    // ============================================================

    size_t writeFileCallback(
        void* contents,
        size_t size,
        size_t nmemb,
        void* userData
    )
    {
        const size_t total =
            size * nmemb;

        std::ofstream* file =
            static_cast<std::ofstream*>(userData);

        file->write(
            static_cast<const char*>(contents),
            static_cast<std::streamsize>(total)
        );

        return total;
    }


    // ============================================================
    // Download progress
    // ============================================================

    int downloadProgressCallback(
        void*,
        curl_off_t totalDownload,
        curl_off_t downloaded,
        curl_off_t,
        curl_off_t
    )
    {
        if (totalDownload > 0)
        {
            const double progress =
                static_cast<double>(downloaded) /
                static_cast<double>(totalDownload);

            downloadProgress.store(
                progress
            );
        }

        return 0;
    }


    // ============================================================
    // HTTP GET
    // ============================================================

    bool httpGet(
        const std::string& url,
        std::string& result
    )
    {
        CURL* curl =
            curl_easy_init();

        if (!curl)
            return false;

        curl_easy_setopt(
            curl,
            CURLOPT_URL,
            url.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_WRITEFUNCTION,
            writeStringCallback
        );

        curl_easy_setopt(
            curl,
            CURLOPT_WRITEDATA,
            &result
        );

        curl_easy_setopt(
            curl,
            CURLOPT_FOLLOWLOCATION,
            1L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_USERAGENT,
            "Moon Engine Launcher"
        );

        curl_easy_setopt(
            curl,
            CURLOPT_TIMEOUT,
            15L
        );

#if defined(_WIN32)
        curl_easy_setopt(
            curl,
            CURLOPT_SSL_OPTIONS,
            CURLSSLOPT_NATIVE_CA
        );
#endif

        const CURLcode code =
            curl_easy_perform(curl);

        long responseCode = 0;

        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &responseCode
        );

        curl_easy_cleanup(curl);

        return
            code == CURLE_OK &&
            responseCode >= 200 &&
            responseCode < 300;
    }


    // ============================================================
    // JSON string extraction
    // ============================================================

    std::string extractJsonString(
        const std::string& json,
        const std::string& key
    )
    {
        const std::string search =
            "\"" + key + "\"";

        const size_t keyPosition =
            json.find(search);

        if (
            keyPosition ==
            std::string::npos
        )
        {
            return "";
        }

        const size_t colon =
            json.find(
                ':',
                keyPosition + search.size()
            );

        if (
            colon ==
            std::string::npos
        )
        {
            return "";
        }

        const size_t firstQuote =
            json.find(
                '"',
                colon + 1
            );

        if (
            firstQuote ==
            std::string::npos
        )
        {
            return "";
        }

        const size_t secondQuote =
            json.find(
                '"',
                firstQuote + 1
            );

        if (
            secondQuote ==
            std::string::npos
        )
        {
            return "";
        }

        return json.substr(
            firstQuote + 1,
            secondQuote - firstQuote - 1
        );
    }


    // ============================================================
    // Update information
    // ============================================================

    struct UpdateInfo
    {
        bool available = false;
        bool packageArchive = false;

        std::string version;

        std::string downloadUrl;
    };


    std::array<unsigned int, 3> versionParts(std::string version)
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


    bool isNewerVersion(
        const std::string& candidate,
        const std::string& current
    )
    {
        return versionParts(candidate) > versionParts(current);
    }


    std::vector<std::string> extractJsonArrayObjects(
        const std::string& json,
        const std::string& key
    )
    {
        std::vector<std::string> objects;
        const size_t keyPosition = json.find("\"" + key + "\"");
        if (keyPosition == std::string::npos)
            return objects;

        const size_t arrayStart = json.find('[', keyPosition);
        if (arrayStart == std::string::npos)
            return objects;

        size_t objectStart = std::string::npos;
        int objectDepth = 0;
        bool inString = false;
        bool escaped = false;

        for (size_t index = arrayStart + 1; index < json.size(); ++index)
        {
            const char character = json[index];

            if (inString)
            {
                if (escaped)
                    escaped = false;
                else if (character == '\\')
                    escaped = true;
                else if (character == '"')
                    inString = false;
                continue;
            }

            if (character == '"')
            {
                inString = true;
            }
            else if (character == '{')
            {
                if (objectDepth == 0)
                    objectStart = index;
                ++objectDepth;
            }
            else if (character == '}')
            {
                --objectDepth;
                if (objectDepth == 0 && objectStart != std::string::npos)
                {
                    objects.push_back(
                        json.substr(objectStart, index - objectStart + 1)
                    );
                    objectStart = std::string::npos;
                }
            }
            else if (character == ']' && objectDepth == 0)
            {
                break;
            }
        }

        return objects;
    }


    // ============================================================
    // Check GitHub
    // ============================================================

    UpdateInfo checkForUpdate()
    {
        UpdateInfo result;

        std::string response;

        if (
            !httpGet(
                GITHUB_API,
                response
            )
        )
        {
            std::cerr
                << "[LoadingLauncher] "
                << "Failed to contact GitHub.\n";

            return result;
        }

        const std::string latestVersion =
            extractJsonString(
                response,
                "tag_name"
            );

        if (
            latestVersion.empty()
        )
        {
            std::cerr
                << "[LoadingLauncher] "
                << "GitHub did not return a version.\n";

            return result;
        }

        result.version =
            latestVersion;


        // --------------------------------------------------------
        // Already up to date
        // --------------------------------------------------------

        if (!isNewerVersion(latestVersion, Project::VERSION))
        {
            return result;
        }

        std::string exeAssetUrl;
        std::string packageAssetUrl;

        for (const std::string& asset : extractJsonArrayObjects(response, "assets"))
        {
            const std::string assetName = extractJsonString(asset, "name");
            const std::string assetUrl = extractJsonString(asset, "browser_download_url");

            if (assetUrl.empty())
                continue;

            if (assetName == Project::EXECUTABLE_NAME)
                exeAssetUrl = assetUrl;

            std::string lowerName = assetName;
            std::transform(
                lowerName.begin(),
                lowerName.end(),
                lowerName.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                }
            );

            if (
                lowerName.find("moon-launcher") != std::string::npos &&
                lowerName.size() >= 4 &&
                lowerName.substr(lowerName.size() - 4) == ".zip"
            )
                packageAssetUrl = assetUrl;
        }

        if (!packageAssetUrl.empty())
        {
            result.downloadUrl = packageAssetUrl;
            result.packageArchive = true;
        }
        else if (!exeAssetUrl.empty())
        {
            result.downloadUrl = exeAssetUrl;
        }
        else
        {
            std::cerr
                << "[LoadingLauncher] No launcher ZIP or EXE asset in release.\n";
            return result;
        }

        result.available =
            !result.downloadUrl.empty();

        return result;
    }


    // ============================================================
    // Download launcher
    // ============================================================

    void downloadLauncher(
        const std::string& url,
        const fs::path& output
    )
    {
        downloadProgress.store(
            0.0
        );

        downloadFinished.store(
            false
        );

        downloadSuccess.store(
            false
        );


        std::ofstream file(
            output,
            std::ios::binary
        );

        if (!file)
        {
            downloadFinished.store(
                true
            );

            return;
        }


        CURL* curl =
            curl_easy_init();

        if (!curl)
        {
            file.close();

            downloadFinished.store(
                true
            );

            return;
        }


        curl_easy_setopt(
            curl,
            CURLOPT_URL,
            url.c_str()
        );

        curl_easy_setopt(
            curl,
            CURLOPT_WRITEFUNCTION,
            writeFileCallback
        );

        curl_easy_setopt(
            curl,
            CURLOPT_WRITEDATA,
            &file
        );

        curl_easy_setopt(
            curl,
            CURLOPT_FOLLOWLOCATION,
            1L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_USERAGENT,
            "Moon Engine Launcher"
        );

        curl_easy_setopt(
            curl,
            CURLOPT_TIMEOUT,
            0L
        );

#if defined(_WIN32)
        curl_easy_setopt(
            curl,
            CURLOPT_SSL_OPTIONS,
            CURLSSLOPT_NATIVE_CA
        );
#endif


        curl_easy_setopt(
            curl,
            CURLOPT_NOPROGRESS,
            0L
        );

        curl_easy_setopt(
            curl,
            CURLOPT_XFERINFOFUNCTION,
            downloadProgressCallback
        );


        const CURLcode code =
            curl_easy_perform(curl);

        long responseCode = 0;
        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &responseCode
        );


        curl_easy_cleanup(curl);

        file.close();


        downloadSuccess.store(
            code == CURLE_OK &&
            responseCode >= 200 &&
            responseCode < 300 &&
            fs::exists(output) &&
            fs::file_size(output) > 0
        );

        downloadFinished.store(
            true
        );
    }


#if defined(_WIN32)
    fs::path launcherDirectory()
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


    bool extractReleasePackage(
        const fs::path& archivePath,
        const fs::path& destination
    )
    {
        mz_zip_archive archive{};
        if (!mz_zip_reader_init_file(&archive, archivePath.string().c_str(), 0))
            return false;

        bool success = true;
        const fs::path normalizedRoot = destination.lexically_normal();

        const int fileCount = static_cast<int>(
            mz_zip_reader_get_num_files(&archive)
        );

        for (int index = 0; index < fileCount; ++index)
        {
            mz_zip_archive_file_stat fileStat{};
            if (!mz_zip_reader_file_stat(&archive, index, &fileStat))
            {
                success = false;
                continue;
            }

            const fs::path relativePath =
                fs::u8path(fileStat.m_filename).lexically_normal();

            if (relativePath.empty() || relativePath.is_absolute())
            {
                success = false;
                continue;
            }

            bool unsafePath = false;
            for (const fs::path& component : relativePath)
            {
                if (component == ".." || component == ".")
                    unsafePath = true;
            }

            const fs::path outputPath =
                (normalizedRoot / relativePath).lexically_normal();
            const fs::path checkPath =
                outputPath.lexically_relative(normalizedRoot);

            if (
                unsafePath ||
                checkPath.empty() ||
                checkPath.is_absolute() ||
                *checkPath.begin() == ".."
            )
            {
                success = false;
                continue;
            }

            std::error_code error;
            if (mz_zip_reader_is_file_a_directory(&archive, index))
            {
                fs::create_directories(outputPath, error);
                if (error)
                    success = false;
                continue;
            }

            fs::create_directories(outputPath.parent_path(), error);
            if (
                error ||
                !mz_zip_reader_extract_to_file(
                    &archive,
                    index,
                    outputPath.string().c_str(),
                    0
                )
            )
                success = false;
        }

        mz_zip_reader_end(&archive);
        return success;
    }


    fs::path findLauncherBin(const fs::path& extractedRoot)
    {
        std::error_code error;
        for (
            fs::recursive_directory_iterator iterator(
                extractedRoot,
                fs::directory_options::skip_permission_denied,
                error
            ),
            end;
            iterator != end && !error;
            iterator.increment(error)
        )
        {
            if (
                iterator->is_regular_file(error) &&
                iterator->path().filename() == Project::EXECUTABLE_NAME
            )
                return iterator->path().parent_path();
        }

        return {};
    }


    bool startUpdater(
        const fs::path& stagedBin,
        const fs::path& installDirectory
    )
    {
        const fs::path updaterPath =
            stagedBin / "LauncherUpdater.exe";
        const fs::path launcherPath =
            installDirectory / Project::EXECUTABLE_NAME;

        if (!fs::exists(updaterPath) || !fs::exists(launcherPath))
            return false;

        std::wstring commandLine =
            L"\"" + updaterPath.wstring() + L"\" " +
            std::to_wstring(GetCurrentProcessId()) + L" \"" +
            stagedBin.wstring() + L"\" \"" +
            installDirectory.wstring() + L"\" \"" +
            launcherPath.wstring() + L"\"";

        STARTUPINFOW startupInfo{};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo{};

        if (!CreateProcessW(
                updaterPath.c_str(),
                commandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                installDirectory.c_str(),
                &startupInfo,
                &processInfo
            ))
            return false;

        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return true;
    }
#endif


    // ============================================================
    // Center text
    // ============================================================

    void centerText(
        sf::Text& text,
        float x,
        float y
    )
    {
        const sf::FloatRect bounds =
            text.getLocalBounds();

        text.setOrigin({
            bounds.position.x +
                bounds.size.x / 2.0f,

            bounds.position.y +
                bounds.size.y / 2.0f
        });

        text.setPosition({
            x,
            y
        });
    }
}


// ================================================================
// Loading Launcher
// ================================================================

namespace LoadingLauncher
{
    void run()
    {
        curl_global_init(
            CURL_GLOBAL_DEFAULT
        );


        // ========================================================
        // Window
        // ========================================================

        sf::RenderWindow window(
            sf::VideoMode({
                Project::LOADING_WIDTH,
                Project::LOADING_HEIGHT
            }),
            Project::WINDOW_TITLE,
            sf::Style::None
        );

        window.setFramerateLimit(
            60
        );


        // ========================================================
        // Icon
        // ========================================================

        Project::loadIcon(
            window
        );


        // ========================================================
        // Fonts
        // ========================================================

        sf::Font fontTitle;
        sf::Font fontText;

        if (
            !fontTitle.openFromFile(
                "assets/ui/fonts/FunkinOptions.otf"
            )
        )
        {
            std::cerr
                << "[LoadingLauncher] "
                << "Could not load FunkinOptions.otf\n";
        }

        if (
            !fontText.openFromFile(
                "assets/ui/fonts/VcrMono.ttf"
            )
        )
        {
            std::cerr
                << "[LoadingLauncher] "
                << "Could not load VcrMono.ttf\n";
        }


        // ========================================================
        // Background
        // ========================================================

        sf::RectangleShape background(
            sf::Vector2f(
                static_cast<float>(
                    Project::LOADING_WIDTH
                ),
                static_cast<float>(
                    Project::LOADING_HEIGHT
                )
            )
        );

        background.setFillColor(
            sf::Color(
                10,
                10,
                14
            )
        );


        // ========================================================
        // Moon
        // ========================================================

        sf::Texture moonTexture;

        sf::Sprite moon(
            moonTexture
        );

        bool moonLoaded =
            moonTexture.loadFromFile(
                Project::ICON_256
            );

        if (moonLoaded)
        {
            const sf::Vector2u size =
                moonTexture.getSize();

            const float moonSize =
                90.0f;

            moon.setScale({
                moonSize /
                    static_cast<float>(size.x),

                moonSize /
                    static_cast<float>(size.y)
            });

            moon.setOrigin({
                static_cast<float>(
                    size.x
                ) / 2.0f,

                static_cast<float>(
                    size.y
                ) / 2.0f
            });

            moon.setPosition({
                Project::LOADING_WIDTH / 2.0f,
                65.0f
            });
        }


        // ========================================================
        // Title
        // ========================================================

        sf::Text title(
            fontTitle,
            Project::NAME,
            30
        );

        title.setFillColor(
            sf::Color::White
        );

        centerText(
            title,
            Project::LOADING_WIDTH / 2.0f,
            135.0f
        );


        // ========================================================
        // Version
        // ========================================================

        sf::Text version(
            fontText,
            Project::VERSION,
            15
        );

        version.setFillColor(
            sf::Color(
                160,
                160,
                170
            )
        );

        centerText(
            version,
            Project::LOADING_WIDTH / 2.0f,
            168.0f
        );


        // ========================================================
        // Status
        // ========================================================

        sf::Text status(
            fontText,
            "Iniciando...",
            15
        );

        status.setFillColor(
            sf::Color(
                200,
                200,
                205
            )
        );

        centerText(
            status,
            Project::LOADING_WIDTH / 2.0f,
            205.0f
        );


        // ========================================================
        // Progress background
        // ========================================================

        constexpr float BAR_WIDTH =
            360.0f;

        constexpr float BAR_HEIGHT =
            8.0f;

        sf::RectangleShape progressBackground(
            sf::Vector2f(
                BAR_WIDTH,
                BAR_HEIGHT
            )
        );

        progressBackground.setFillColor(
            sf::Color(
                40,
                40,
                48
            )
        );

        progressBackground.setPosition({
            (
                Project::LOADING_WIDTH -
                BAR_WIDTH
            ) / 2.0f,

            245.0f
        });


        // ========================================================
        // Progress bar
        // ========================================================

        sf::RectangleShape progressBar(
            sf::Vector2f(
                0.0f,
                BAR_HEIGHT
            )
        );

        progressBar.setFillColor(
            sf::Color::White
        );

        progressBar.setPosition(
            progressBackground.getPosition()
        );


        // ========================================================
        // Check update
        // ========================================================

        status.setString(
            "Verificando atualizações..."
        );

        centerText(
            status,
            Project::LOADING_WIDTH / 2.0f,
            205.0f
        );


        UpdateInfo update =
            checkForUpdate();


        // ========================================================
        // Update available
        // ========================================================

        if (
            update.available
        )
        {
            status.setString(
                "Atualizando Moon Launcher..."
            );

            centerText(
                status,
                Project::LOADING_WIDTH / 2.0f,
                205.0f
            );


            version.setString(
                std::string(
                    Project::VERSION
                ) +
                " → " +
                update.version
            );

            centerText(
                version,
                Project::LOADING_WIDTH / 2.0f,
                168.0f
            );


            std::string versionSlug = update.version;
            for (char& character : versionSlug)
            {
                if (
                    !std::isalnum(static_cast<unsigned char>(character)) &&
                    character != '.' &&
                    character != '-'
                )
                    character = '_';
            }

            const fs::path updateRoot =
                fs::temp_directory_path() /
                ("MoonLauncherUpdate-" + versionSlug);

            std::error_code cleanupError;
            fs::remove_all(updateRoot, cleanupError);
            fs::create_directories(updateRoot);

            const fs::path updatePath = updateRoot /
                (update.packageArchive
                    ? "launcher-package.zip"
                    : "Moon Launcher.new.exe");


            std::thread downloadThread(
                downloadLauncher,
                update.downloadUrl,
                updatePath
            );


            while (
                !downloadFinished.load()
            )
            {
                while (
                    const std::optional event =
                        window.pollEvent()
                )
                {
                    if (
                        event->is<
                            sf::Event::Closed
                        >()
                    )
                    {
                        downloadThread.join();

                        curl_global_cleanup();

                        window.close();

                        return;
                    }
                }


                // =================================================
                // Rotação da lua
                // =================================================

                if (moonLoaded)
                {
                    moon.rotate(
                        sf::degrees(
                            3.0f
                        )
                    );
                }


                // =================================================
                // Download progress
                // =================================================

                const float progress =
                    static_cast<float>(
                        downloadProgress.load()
                    );

                progressBar.setSize({
                    BAR_WIDTH * progress,
                    BAR_HEIGHT
                });


                // =================================================
                // Draw
                // =================================================

                window.clear(
                    sf::Color(
                        10,
                        10,
                        14
                    )
                );

                window.draw(
                    background
                );

                if (moonLoaded)
                    window.draw(moon);

                window.draw(title);
                window.draw(version);
                window.draw(status);

                window.draw(
                    progressBackground
                );

                window.draw(
                    progressBar
                );

                window.display();
            }


            downloadThread.join();


            // ====================================================
            // Download result
            // ====================================================

            if (
                downloadSuccess.load()
            )
            {
                bool packageReady = true;
                fs::path stagedBin;

                if (update.packageArchive)
                {
                    const fs::path extractedRoot = updateRoot / "package";
                    packageReady = extractReleasePackage(updatePath, extractedRoot);
                    if (packageReady)
                        stagedBin = findLauncherBin(extractedRoot);
                }
                else
                {
                    stagedBin = updateRoot / "bin";
                    std::error_code error;
                    fs::create_directories(stagedBin, error);
                    if (!error)
                    {
                        fs::copy_file(
                            updatePath,
                            stagedBin / Project::EXECUTABLE_NAME,
                            fs::copy_options::overwrite_existing,
                            error
                        );
                    }
                    packageReady = !error;
                }

                if (
                    stagedBin.empty() ||
                    !fs::exists(stagedBin / Project::EXECUTABLE_NAME)
                )
                    packageReady = false;

                if (packageReady) {
                    const fs::path stagedUpdater =
                        stagedBin / "LauncherUpdater.exe";
                    const fs::path installedUpdater =
                        launcherDirectory() / "LauncherUpdater.exe";

                    if (
                        !fs::exists(stagedUpdater) &&
                        fs::exists(installedUpdater)
                    ) {
                        std::error_code copyError;
                        fs::copy_file(
                            installedUpdater,
                            stagedUpdater,
                            fs::copy_options::overwrite_existing,
                            copyError
                        );
                    }

                    packageReady = fs::exists(stagedUpdater);
                }

                if (
                    packageReady &&
                    startUpdater(stagedBin, launcherDirectory())
                )
                {
                    status.setString("Instalando atualização e reiniciando...");
                    centerText(
                        status,
                        Project::LOADING_WIDTH / 2.0f,
                        205.0f
                    );
                    window.clear(sf::Color(10, 10, 14));
                    window.draw(background);
                    if (moonLoaded)
                        window.draw(moon);
                    window.draw(title);
                    window.draw(version);
                    window.draw(status);
                    window.display();

                    curl_global_cleanup();
                    window.close();
                    return;
                }

                status.setString(
                    "Pacote baixado, mas nao foi possivel instalar."
                );

                centerText(
                    status,
                    Project::LOADING_WIDTH / 2.0f,
                    205.0f
                );

                std::cerr
                    << "[LoadingLauncher] Update package could not be staged or updater was not started.\n";

                window.clear(sf::Color(10, 10, 14));
                window.draw(background);
                if (moonLoaded)
                    window.draw(moon);
                window.draw(title);
                window.draw(version);
                window.draw(status);
                window.display();
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            else
            {
                status.setString(
                    "Falha ao atualizar. Continuando..."
                );

                centerText(
                    status,
                    Project::LOADING_WIDTH / 2.0f,
                    205.0f
                );

                window.clear(
                    sf::Color(
                        10,
                        10,
                        14
                    )
                );

                window.draw(background);

                if (moonLoaded)
                    window.draw(moon);

                window.draw(title);
                window.draw(version);
                window.draw(status);
                window.draw(progressBackground);
                window.draw(progressBar);

                window.display();

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        1000
                    )
                );
            }
        }
        else
        {
            // ====================================================
            // Nenhuma atualização
            // ====================================================

            status.setString(
                "Launcher atualizado."
            );

            centerText(
                status,
                Project::LOADING_WIDTH / 2.0f,
                205.0f
            );


            // Loading visual
            for (
                int i = 0;
                i <= 100;
                ++i
            )
            {
                while (
                    const std::optional event =
                        window.pollEvent()
                )
                {
                    if (
                        event->is<
                            sf::Event::Closed
                        >()
                    )
                    {
                        curl_global_cleanup();

                        window.close();

                        return;
                    }
                }


                if (moonLoaded)
                {
                    moon.rotate(
                        sf::degrees(
                            3.0f
                        )
                    );
                }


                const float progress =
                    static_cast<float>(i) /
                    100.0f;

                progressBar.setSize({
                    BAR_WIDTH * progress,
                    BAR_HEIGHT
                });


                window.clear(
                    sf::Color(
                        10,
                        10,
                        14
                    )
                );

                window.draw(background);

                if (moonLoaded)
                    window.draw(moon);

                window.draw(title);
                window.draw(version);
                window.draw(status);
                window.draw(progressBackground);
                window.draw(progressBar);

                window.display();


                std::this_thread::sleep_for(
                    std::chrono::milliseconds(
                        8
                    )
                );
            }
        }


        // ========================================================
        // Finish
        // ========================================================

        curl_global_cleanup();

        window.close();


        // ========================================================
        // Home
        // ========================================================
        //
        // Depois que o main.cpp for transformado na Home,
        // chamamos a função pública dele aqui:
        //
        runLauncher();
        //
        // ========================================================
    }
}


#if defined(_WIN32)
void configureConsoleFromSettings()
{
    const fs::path settingsPath =
        launcherDirectory() / "launcher-settings.json";
    std::ifstream settingsFile(settingsPath);

    if (!settingsFile)
        return;

    const std::string json{
        std::istreambuf_iterator<char>(settingsFile),
        std::istreambuf_iterator<char>()
    };

    const size_t keyPosition = json.find("\"showConsole\"");
    if (keyPosition == std::string::npos)
        return;

    const size_t colonPosition = json.find(':', keyPosition);
    if (colonPosition == std::string::npos)
        return;

    const size_t valuePosition = json.find_first_not_of(
        " \t\r\n",
        colonPosition + 1
    );

    if (
        valuePosition == std::string::npos ||
        json.compare(valuePosition, 4, "true") != 0
    )
        return;

    if (!GetConsoleWindow() && !AllocConsole())
        return;

    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::ios::sync_with_stdio(true);
}
#endif


// ================================================================
// Executable entry point
// ================================================================

int main()
{
#if defined(_WIN32)
    configureConsoleFromSettings();
#endif

    LoadingLauncher::run();

    return 0;
}