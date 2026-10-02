#pragma once

#include <SFML/Graphics.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct LuaModDescriptor {
    std::string id;
    std::string name;
    std::string currentLauncherVersion;
    std::string launcherMinVersion;
    std::filesystem::path directory;
    std::string entry;
};

struct LuaModState;

class ModRuntime {
public:
    ModRuntime();
    ~ModRuntime();

    ModRuntime(const ModRuntime&) = delete;
    ModRuntime& operator=(const ModRuntime&) = delete;

    bool loadEnabledMods(
        const std::vector<LuaModDescriptor>& descriptors
    );

    void setTraceFile(std::filesystem::path path);
    void trace(const std::string& message) const;

    void drawHomeEffects(
        sf::RenderTarget& target,
        sf::Vector2f origin,
        sf::Vector2f size,
        float deltaTime
    );

private:
    std::filesystem::path traceFile;
    std::vector<std::unique_ptr<LuaModState>> loadedMods;
};
