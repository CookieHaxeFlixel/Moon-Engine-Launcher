// Build & run (Linux/macOS/MSYS2):  see tests/run_tests.sh
#include "src/UpdateUtils.hpp"
#include "src/UpdateInstall.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;
using namespace UpdateUtils;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; } } while (0)

static void writeFile(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

static std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

static bool makeZip(const fs::path& zipPath, const std::vector<std::pair<std::string, std::string>>& entries)
{
    fs::remove(zipPath);
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, zipPath.string().c_str(), 0))
        return false;
    for (const auto& [name, data] : entries)
        mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL);
    mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return true;
}

int main()
{
    const fs::path tmp = fs::temp_directory_path() / "moon-launcher-tests";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // ---- versions: 0.1.0 installed, only strictly newer updates ----
    CHECK(isNewerVersion("v0.1.1", "0.1.0"));
    CHECK(isNewerVersion("v0.2.0", "0.1.9"));
    CHECK(isNewerVersion("v1.0", "0.9.9"));
    CHECK(isNewerVersion("v0.1.10", "0.1.9"));
    CHECK(!isNewerVersion("v0.1.0", "0.1.0"));   // same release (today's GitHub state)
    CHECK(!isNewerVersion("v0.0.9", "0.1.0"));   // downgrade
    CHECK(!isNewerVersion("v0.0.1", "0.1.0"));   // downgrade
    CHECK(!isNewerVersion("latest", "0.1.0"));   // garbage tag
    CHECK(!isNewerVersion("", "0.1.0"));

    // ---- release asset name ----
    CHECK(isLauncherZipAsset("moon-launcher-v0.1.1.zip"));
    CHECK(isLauncherZipAsset("Moon-Launcher-v0.1.1.ZIP"));
    CHECK(!isLauncherZipAsset("Moon-Launcher-Setup-v0.1.1.exe"));
    CHECK(!isLauncherZipAsset("funkin-moon-windows-64bit.zip"));
    CHECK(!isLauncherZipAsset("moon-launcher-.zip"));
    CHECK(!isLauncherZipAsset("other-moon-launcher-v1.zip"));

    // ---- archive entry names ----
    CHECK(isSafeArchiveEntry("bin/Moon Launcher.exe"));
    CHECK(isSafeArchiveEntry("bin/assets/ui/a.png"));
    CHECK(isSafeArchiveEntry("bin/"));
    CHECK(!isSafeArchiveEntry("../evil.txt"));
    CHECK(!isSafeArchiveEntry("bin/../../evil.txt"));
    CHECK(!isSafeArchiveEntry("bin\\..\\..\\evil.txt"));
    CHECK(!isSafeArchiveEntry("/etc/passwd"));
    CHECK(!isSafeArchiveEntry("\\evil.txt"));
    CHECK(!isSafeArchiveEntry("C:\\evil.txt"));
    CHECK(!isSafeArchiveEntry("C:evil.txt"));
    CHECK(!isSafeArchiveEntry("bin/a.txt:stream"));
    CHECK(!isSafeArchiveEntry(""));

    // ---- showConsole parsing ----
    CHECK(!parseShowConsole("{\"showConsole\": false}"));
    CHECK(parseShowConsole("{\"showConsole\": true}"));
    CHECK(parseShowConsole("{\n  \"showConsole\"  :\n   true\n}"));
    CHECK(parseShowConsole("{\"showConsole\":true,\"x\":1}"));
    CHECK(!parseShowConsole("{}"));
    CHECK(!parseShowConsole(""));
    CHECK(!parseShowConsole("{\"showConsole\": \"true\"}"));
    CHECK(!parseShowConsole("{\"showConsole\": trueish}"));

    // ---- good package: bin/ at the root ----
    {
        const fs::path zip = tmp / "good.zip";
        CHECK(makeZip(zip, {
            {"bin/Moon Launcher.exe", "NEWEXE"},
            {"bin/LauncherUpdater.exe", "NEWUPD"},
            {"bin/assets/ui/a.txt", "asset"},
            {"bin/launcher-settings.json", "{\"showConsole\": false}"},
            {"bin/com.funkinmoon/data/saves/save.json", "SHOULD-NOT-OVERWRITE"},
            {"bin/mods/Evil/mod.lua", "SHOULD-NOT-INSTALL"},
        }));
        const fs::path out = tmp / "good";
        CHECK(extractReleasePackage(zip, out));
        const fs::path bin = findLauncherBin(out, "Moon Launcher.exe");
        CHECK(bin == (out / "bin"));

        // ---- install over an existing installation ----
        const fs::path install = tmp / "install";
        writeFile(install / "Moon Launcher.exe", "OLDEXE");
        writeFile(install / "assets/old.txt", "keep-old-asset");
        writeFile(install / "launcher-settings.json", "{\"showConsole\": true}");
        writeFile(install / "mods/MyMod/mod.lua", "MY-MOD");
        writeFile(install / "com.funkinmoon/data/saves/save.json", "MY-SAVE");
        writeFile(install / "com.funkinmoon/versions/v0.1.0/game.exe", "GAME");

        const int code = UpdateInstall::installStagedFiles(bin, install, 2, std::chrono::milliseconds(1));
        CHECK(code == 0);
        CHECK(readFile(install / "Moon Launcher.exe") == "NEWEXE");
        CHECK(readFile(install / "LauncherUpdater.exe") == "NEWUPD");
        CHECK(readFile(install / "assets/ui/a.txt") == "asset");
        CHECK(readFile(install / "launcher-settings.json") == "{\"showConsole\": true}");       // preserved
        CHECK(readFile(install / "mods/MyMod/mod.lua") == "MY-MOD");                            // preserved
        CHECK(!fs::exists(install / "mods/Evil"));                                               // not installed
        CHECK(readFile(install / "com.funkinmoon/data/saves/save.json") == "MY-SAVE");           // preserved
        CHECK(readFile(install / "com.funkinmoon/versions/v0.1.0/game.exe") == "GAME");          // preserved

        // fresh install gets default settings
        const fs::path fresh = tmp / "fresh";
        fs::create_directories(fresh);
        CHECK(UpdateInstall::installStagedFiles(bin, fresh, 2, std::chrono::milliseconds(1)) == 0);
        CHECK(readFile(fresh / "launcher-settings.json") == "{\"showConsole\": false}");
    }

    // ---- good package with one top-level folder ----
    {
        const fs::path zip = tmp / "wrapped.zip";
        CHECK(makeZip(zip, {{"moon-launcher-v0.1.1/bin/Moon Launcher.exe", "X"}}));
        const fs::path out = tmp / "wrapped";
        CHECK(extractReleasePackage(zip, out));
        CHECK(findLauncherBin(out, "Moon Launcher.exe") == (out / "moon-launcher-v0.1.1/bin"));
    }

    // ---- malicious package (zip-slip) ----
    {
        const fs::path zip = tmp / "evil.zip";
        CHECK(makeZip(zip, {
            {"bin/Moon Launcher.exe", "X"},
            {"../escaped.txt", "pwned"},
        }));
        const fs::path out = tmp / "evil_out" / "package";
        CHECK(!extractReleasePackage(zip, out));
        CHECK(!fs::exists(tmp / "evil_out" / "escaped.txt"));
        CHECK(!fs::exists(tmp / "escaped.txt"));
    }
    {
        const fs::path zip = tmp / "evil2.zip";
        // miniz's writer strips leading '/', so this entry lands INSIDE the
        // destination; the point is that nothing is written at the absolute path.
        CHECK(makeZip(zip, {{"/tmp/moon-abs-escape.txt", "pwned"}}));
        extractReleasePackage(zip, tmp / "evil2_out");
        CHECK(!fs::exists("/tmp/moon-abs-escape.txt"));
    }

    // ---- exe in the wrong place is rejected ----
    {
        const fs::path zip = tmp / "wrong.zip";
        CHECK(makeZip(zip, {
            {"Moon Launcher.exe", "X"},                              // no bin/
            {"com.funkinmoon/x/y/z/bin/Moon Launcher.exe", "X"},     // too deep
        }));
        const fs::path out = tmp / "wrong";
        CHECK(extractReleasePackage(zip, out));
        CHECK(findLauncherBin(out, "Moon Launcher.exe").empty());
    }

    // ---- stale staging cleanup only touches MoonLauncherUpdate-* ----
    {
        const fs::path t = tmp / "tempdir";
        writeFile(t / "MoonLauncherUpdate-v0.1.1/package/bin/x.txt", "x");
        writeFile(t / "MoonLauncherUpdate-v0.0.9/y.txt", "y");
        writeFile(t / "OtherApp/keep.txt", "keep");
        writeFile(t / "MoonLauncherUpdate-file-not-dir", "z");
        removeStaleUpdateFolders(t);
        CHECK(!fs::exists(t / "MoonLauncherUpdate-v0.1.1"));
        CHECK(!fs::exists(t / "MoonLauncherUpdate-v0.0.9"));
        CHECK(fs::exists(t / "OtherApp/keep.txt"));
        CHECK(fs::exists(t / "MoonLauncherUpdate-file-not-dir"));
        removeStaleUpdateFolders(tmp / "does-not-exist");   // must not throw
    }

    // ---- corrupt / missing archive ----
    writeFile(tmp / "corrupt.zip", "this is not a zip");
    CHECK(!extractReleasePackage(tmp / "corrupt.zip", tmp / "corrupt_out"));
    CHECK(!extractReleasePackage(tmp / "missing.zip", tmp / "missing_out"));

    fs::remove_all(tmp);

    if (failures == 0)
        std::cout << "ALL TESTS PASSED\n";
    else
        std::cout << failures << " check(s) FAILED\n";

    return failures == 0 ? 0 : 1;
}
