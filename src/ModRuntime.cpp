#include "ModRuntime.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

namespace {

void appendTrace(
    const std::filesystem::path& traceFile,
    const std::string& message
) {
    std::cout << message << '\n';

    if (traceFile.empty())
        return;

    std::error_code error;
    if (!traceFile.parent_path().empty())
        std::filesystem::create_directories(traceFile.parent_path(), error);

    std::ofstream output(traceFile, std::ios::app);
    if (output)
        output << message << '\n';
}

} // namespace

#if defined(_WIN32)
namespace {

constexpr int kNoLuaReference = -2;
constexpr int kInstructionLimit = 100000;

void instructionLimitHook(lua_State* state, lua_Debug* debug) {
    (void)debug;
    luaL_error(state, "mod exceeded its Lua instruction budget");
}

int protectedCall(
    lua_State* state,
    int argumentCount,
    int resultCount
) {
    const lua_Hook previousHook = lua_gethook(state);
    const int previousMask = lua_gethookmask(state);
    const int previousCount = lua_gethookcount(state);

    lua_sethook(
        state,
        instructionLimitHook,
        LUA_MASKCOUNT,
        kInstructionLimit
    );

    const int status = lua_pcall(
        state,
        argumentCount,
        resultCount,
        0
    );

    lua_sethook(
        state,
        previousHook,
        previousMask,
        previousCount
    );
    return status;
}

std::array<int, 3> parseVersion(const std::string& version) {
    std::array<int, 3> parts{};
    std::istringstream stream(version);
    std::string part;

    for (size_t index = 0; index < parts.size(); ++index) {
        if (!std::getline(stream, part, '.'))
            break;

        try {
            parts[index] = std::stoi(part);
        } catch (...) {
            return {};
        }
    }

    return parts;
}

bool isVersionAtLeast(
    const std::string& current,
    const std::string& minimum
) {
    if (minimum.empty())
        return true;

    return parseVersion(current) >= parseVersion(minimum);
}

bool pathIsInside(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    const std::filesystem::path relative =
        candidate.lexically_relative(root);

    if (relative.empty() || relative.is_absolute())
        return false;

    for (const auto& component : relative) {
        if (component == "..")
            return false;
    }

    return true;
}

void logLuaFailure(
    const std::filesystem::path& traceFile,
    lua_State* state,
    const std::string& modName,
    const char* phase
) {
    const char* message = lua_tostring(state, -1);
    appendTrace(
        traceFile,
        "[Lua mod: " + modName + "] " +
            phase + ": " +
            (message ? message : "unknown Lua error")
    );
    lua_pop(state, 1);
}

} // namespace

struct LuaModState {
    lua_State* state = nullptr;
    std::filesystem::path root;
    std::string id;
    std::string name;
    std::string launcherVersion;
    std::filesystem::path traceFile;
    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> textures;
    int moduleReference = kNoLuaReference;
    int apiReference = kNoLuaReference;
    int requireCacheReference = kNoLuaReference;
    int homeEffectReference = kNoLuaReference;
    int effectStateReference = kNoLuaReference;
    bool effectCreated = false;
    bool effectDrawTraced = false;
    bool disabled = false;
    sf::RenderTarget* currentTarget = nullptr;
    sf::Vector2f currentOrigin{};

    ~LuaModState() {
        if (state)
            lua_close(state);
    }
};

namespace {

LuaModState* currentMod(lua_State* state) {
    return static_cast<LuaModState*>(
        lua_touserdata(state, lua_upvalueindex(1))
    );
}

bool resolveModulePath(
    const LuaModState& mod,
    const char* requestedName,
    char* output,
    size_t outputSize,
    const char*& errorMessage
) {
    std::string moduleName(requestedName);

    if (
        moduleName.empty() ||
        moduleName.find("..") != std::string::npos ||
        moduleName.find('\\') != std::string::npos ||
        moduleName.front() == '/'
    ) {
        errorMessage = "invalid mod module path";
        return false;
    }

    for (char& character : moduleName) {
        const unsigned char value =
            static_cast<unsigned char>(character);

        if (character == '.') {
            character = '/';
        } else if (
            !std::isalnum(value) &&
            character != '_' &&
            character != '-' &&
            character != '/'
        ) {
            errorMessage = "invalid character in module path";
            return false;
        }
    }

    std::filesystem::path relative(moduleName);
    if (relative.extension() != ".lua")
        relative += ".lua";

    std::error_code error;
    const auto candidate = std::filesystem::weakly_canonical(
        mod.root / relative,
        error
    );

    if (
        error ||
        !pathIsInside(mod.root, candidate) ||
        !std::filesystem::is_regular_file(candidate, error)
    ) {
        errorMessage = "mod module is missing or outside its folder";
        return false;
    }

    const std::string pathString = candidate.string();
    if (pathString.size() + 1 > outputSize) {
        errorMessage = "mod module path is too long";
        return false;
    }

    std::copy(pathString.begin(), pathString.end(), output);
    output[pathString.size()] = '\0';
    return true;
}

bool resolveAssetPath(
    const LuaModState& mod,
    const char* requestedPath,
    char* output,
    size_t outputSize,
    const char*& errorMessage
) {
    const std::string assetName(requestedPath);
    const std::filesystem::path relative(assetName);

    if (
        assetName.empty() ||
        assetName.find("..") != std::string::npos ||
        assetName.find('\\') != std::string::npos ||
        relative.is_absolute()
    ) {
        errorMessage = "invalid mod asset path";
        return false;
    }

    std::error_code error;
    const auto candidate = std::filesystem::weakly_canonical(
        mod.root / relative,
        error
    );

    if (
        error ||
        !pathIsInside(mod.root, candidate) ||
        !std::filesystem::is_regular_file(candidate, error)
    ) {
        errorMessage = "mod asset is missing or outside its folder";
        return false;
    }

    const std::string pathString = candidate.string();
    if (pathString.size() + 1 > outputSize) {
        errorMessage = "mod asset path is too long";
        return false;
    }

    std::copy(pathString.begin(), pathString.end(), output);
    output[pathString.size()] = '\0';
    return true;
}

int modRequire(lua_State* state) {
    LuaModState* mod = currentMod(state);
    const char* requestedName = luaL_checkstring(state, 1);
    const size_t requestedLength = std::char_traits<char>::length(requestedName);
    std::array<char, 32768> candidatePath{};
    const char* pathError = nullptr;

    if (!resolveModulePath(
            *mod,
            requestedName,
            candidatePath.data(),
            candidatePath.size(),
            pathError
        ))
        return luaL_error(state, "%s", pathError);

    lua_rawgeti(state, LUA_REGISTRYINDEX, mod->requireCacheReference);
    lua_pushlstring(state, requestedName, requestedLength);
    lua_rawget(state, -2);

    if (!lua_isnil(state, -1)) {
        lua_remove(state, -2);
        return 1;
    }

    lua_pop(state, 1);

    if (luaL_loadfilex(state, candidatePath.data(), nullptr) != LUA_OK)
        return lua_error(state);

    if (protectedCall(state, 0, 1) != LUA_OK)
        return lua_error(state);

    if (lua_isnil(state, -1)) {
        lua_pop(state, 1);
        lua_pushboolean(state, 1);
    }

    lua_pushlstring(state, requestedName, requestedLength);
    lua_pushvalue(state, -2);
    lua_settable(state, 2);
    lua_remove(state, 2);

    return 1;
}

int apiLog(lua_State* state) {
    LuaModState* mod = currentMod(state);
    size_t messageLength = 0;
    const char* message = luaL_tolstring(state, 1, &messageLength);

    appendTrace(
        mod->traceFile,
        "[Lua mod: " + mod->name + "] " +
            std::string(message, messageLength)
    );

    lua_pop(state, 1);
    return 0;
}

int apiRegisterHomeEffect(lua_State* state) {
    LuaModState* mod = currentMod(state);

    if (!lua_istable(state, 1))
        return luaL_error(state, "registerHomeEffect expects a table");

    for (const char* field : {"create", "update", "draw"}) {
        lua_getfield(state, 1, field);
        const bool isFunction = lua_isfunction(state, -1);
        lua_pop(state, 1);

        if (!isFunction)
            return luaL_error(
                state,
                "home effect must provide create, update, and draw functions"
            );
    }

    if (mod->homeEffectReference != kNoLuaReference)
        luaL_unref(state, LUA_REGISTRYINDEX, mod->homeEffectReference);

    lua_pushvalue(state, 1);
    mod->homeEffectReference = luaL_ref(state, LUA_REGISTRYINDEX);
    appendTrace(
        mod->traceFile,
        "[Lua mod: " + mod->name + "] registered home effect"
    );
    return 0;
}

int apiDrawCircle(lua_State* state) {
    LuaModState* mod = currentMod(state);

    if (!mod->currentTarget)
        return 0;

    const float x = static_cast<float>(luaL_checknumber(state, 1));
    const float y = static_cast<float>(luaL_checknumber(state, 2));
    const float radius = std::clamp(
        static_cast<float>(luaL_checknumber(state, 3)),
        0.5f,
        40.f
    );

    sf::CircleShape flake(radius);
    flake.setFillColor(sf::Color(240, 245, 255, 190));
    flake.setPosition({
        mod->currentOrigin.x + x - radius,
        mod->currentOrigin.y + y - radius
    });
    mod->currentTarget->draw(flake);
    return 0;
}

int apiDrawImage(lua_State* state) {
    LuaModState* mod = currentMod(state);
    if (!mod->currentTarget)
        return 0;

    const char* requestedPath = luaL_checkstring(state, 1);
    const float x = static_cast<float>(luaL_checknumber(state, 2));
    const float y = static_cast<float>(luaL_checknumber(state, 3));
    const float size = std::clamp(
        static_cast<float>(luaL_checknumber(state, 4)),
        1.f,
        256.f
    );
    const auto alpha = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(luaL_optnumber(state, 5, 255.0)),
        0,
        255
    ));
    const float rotation = static_cast<float>(luaL_optnumber(state, 6, 0.0));

    const std::string assetKey(requestedPath);
    auto [textureEntry, inserted] =
        mod->textures.try_emplace(assetKey, nullptr);

    if (inserted) {
        std::array<char, 32768> resolvedPath{};
        const char* pathError = nullptr;

        if (!resolveAssetPath(
                *mod,
                requestedPath,
                resolvedPath.data(),
                resolvedPath.size(),
                pathError
            )) {
            appendTrace(
                mod->traceFile,
                "[Lua mod: " + mod->name + "] drawImage " +
                    assetKey + ": " + pathError
            );
        } else {
            auto texture = std::make_unique<sf::Texture>();
            if (texture->loadFromFile(resolvedPath.data())) {
                texture->setSmooth(true);
                textureEntry->second = std::move(texture);
                appendTrace(
                    mod->traceFile,
                    "[Lua mod: " + mod->name + "] loaded asset " + assetKey
                );
            } else {
                appendTrace(
                    mod->traceFile,
                    "[Lua mod: " + mod->name + "] failed to load asset " + assetKey
                );
            }
        }
    }

    if (!textureEntry->second)
        return 0;

    const sf::Vector2u textureSize = textureEntry->second->getSize();
    const float scale = size / std::max(
        static_cast<float>(textureSize.x),
        static_cast<float>(textureSize.y)
    );

    sf::Sprite sprite(*textureEntry->second);
    sprite.setOrigin({
        textureSize.x / 2.f,
        textureSize.y / 2.f
    });
    sprite.setScale({scale, scale});
    sprite.setRotation(sf::degrees(rotation));
    sprite.setPosition({
        mod->currentOrigin.x + x,
        mod->currentOrigin.y + y
    });
    sprite.setColor(sf::Color(255, 255, 255, alpha));
    mod->currentTarget->draw(sprite);
    return 0;
}

void pushApiTable(lua_State* state, LuaModState* mod) {
    lua_newtable(state);

    lua_pushlightuserdata(state, mod);
    lua_pushcclosure(state, apiLog, 1);
    lua_setfield(state, -2, "log");

    lua_pushlightuserdata(state, mod);
    lua_pushcclosure(state, apiRegisterHomeEffect, 1);
    lua_setfield(state, -2, "registerHomeEffect");

    lua_pushlightuserdata(state, mod);
    lua_pushcclosure(state, apiDrawCircle, 1);
    lua_setfield(state, -2, "drawCircle");

    lua_pushlightuserdata(state, mod);
    lua_pushcclosure(state, apiDrawImage, 1);
    lua_setfield(state, -2, "drawImage");

    lua_pushlstring(state, mod->launcherVersion.data(), mod->launcherVersion.size());
    lua_setfield(state, -2, "version");
}

void openSafeLibraries(lua_State* state) {
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);

    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(state, 1);
#if LUA_VERSION_NUM >= 503
    luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(state, 1);
#endif

    for (const char* name : {"dofile", "loadfile", "load", "collectgarbage"}) {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }
}

bool pushEffectFunction(
    LuaModState& mod,
    const char* name
) {
    lua_rawgeti(
        mod.state,
        LUA_REGISTRYINDEX,
        mod.homeEffectReference
    );
    lua_getfield(mod.state, -1, name);
    lua_remove(mod.state, -2);

    if (lua_isfunction(mod.state, -1))
        return true;

    lua_pop(mod.state, 1);
    return false;
}

bool loadModScript(
    LuaModState& mod,
    const LuaModDescriptor& descriptor
) {
    if (!isVersionAtLeast(
            descriptor.currentLauncherVersion,
            descriptor.launcherMinVersion
        )) {
        appendTrace(
            mod.traceFile,
            "[Lua mod: " + descriptor.name + "] requires launcher " +
                descriptor.launcherMinVersion + " or newer"
        );
        return false;
    }

    mod.root = std::filesystem::weakly_canonical(descriptor.directory);
    mod.id = descriptor.id;
    mod.name = descriptor.name.empty() ? descriptor.id : descriptor.name;
    mod.launcherVersion = descriptor.currentLauncherVersion;

    const std::filesystem::path relativeEntry(descriptor.entry);
    if (
        relativeEntry.empty() ||
        relativeEntry.is_absolute() ||
        relativeEntry.extension() != ".lua"
    ) {
        appendTrace(
            mod.traceFile,
            "[Lua mod: " + mod.name + "] invalid script entry"
        );
        return false;
    }

    std::error_code error;
    const auto entryPath = std::filesystem::weakly_canonical(
        mod.root / relativeEntry,
        error
    );

    if (
        error ||
        !pathIsInside(mod.root, entryPath) ||
        !std::filesystem::is_regular_file(entryPath, error)
    ) {
        appendTrace(
            mod.traceFile,
            "[Lua mod: " + mod.name + "] script entry not found"
        );
        return false;
    }

    mod.state = luaL_newstate();
    if (!mod.state) {
        appendTrace(
            mod.traceFile,
            "[Lua mod: " + mod.name + "] could not create Lua state"
        );
        return false;
    }

    openSafeLibraries(mod.state);

    lua_newtable(mod.state);
    mod.requireCacheReference = luaL_ref(
        mod.state,
        LUA_REGISTRYINDEX
    );

    lua_pushlightuserdata(mod.state, &mod);
    lua_pushcclosure(mod.state, modRequire, 1);
    lua_setglobal(mod.state, "require");

    pushApiTable(mod.state, &mod);
    lua_pushvalue(mod.state, -1);
    mod.apiReference = luaL_ref(mod.state, LUA_REGISTRYINDEX);
    lua_setglobal(mod.state, "launcher");

    if (
        luaL_loadfilex(
            mod.state,
            entryPath.string().c_str(),
            nullptr
        ) != LUA_OK
    ) {
        logLuaFailure(mod.traceFile, mod.state, mod.name, "compile");
        return false;
    }

    if (protectedCall(mod.state, 0, 1) != LUA_OK) {
        logLuaFailure(mod.traceFile, mod.state, mod.name, "load");
        return false;
    }

    if (!lua_istable(mod.state, -1)) {
        lua_pop(mod.state, 1);
        appendTrace(
            mod.traceFile,
            "[Lua mod: " + mod.name + "] entry must return a table"
        );
        return false;
    }

    mod.moduleReference = luaL_ref(mod.state, LUA_REGISTRYINDEX);

    lua_rawgeti(mod.state, LUA_REGISTRYINDEX, mod.moduleReference);
    lua_getfield(mod.state, -1, "onLoad");

    if (lua_isfunction(mod.state, -1)) {
        lua_remove(mod.state, -2);
        lua_rawgeti(mod.state, LUA_REGISTRYINDEX, mod.apiReference);

        if (protectedCall(mod.state, 1, 0) != LUA_OK) {
            logLuaFailure(mod.traceFile, mod.state, mod.name, "onLoad");
            return false;
        }
    } else {
        lua_pop(mod.state, 2);
    }

    appendTrace(mod.traceFile, "[Lua mod: " + mod.name + "] loaded");
    return true;
}

void disableAfterLuaError(LuaModState& mod, const char* phase) {
    logLuaFailure(mod.traceFile, mod.state, mod.name, phase);
    mod.disabled = true;
}

} // namespace
#endif

#if !defined(_WIN32)
struct LuaModState {};
#endif

ModRuntime::ModRuntime() = default;
ModRuntime::~ModRuntime() = default;

void ModRuntime::setTraceFile(std::filesystem::path path) {
    traceFile = std::move(path);
    trace("Trace file configured: " + traceFile.string());
}

void ModRuntime::trace(const std::string& message) const {
    appendTrace(traceFile, "[Lua runtime] " + message);
}

bool ModRuntime::loadEnabledMods(
    const std::vector<LuaModDescriptor>& descriptors
) {
    loadedMods.clear();
    trace("Reloading " + std::to_string(descriptors.size()) + " enabled Lua mod(s)");

#if defined(_WIN32)
    for (const LuaModDescriptor& descriptor : descriptors) {
        trace("Loading " + descriptor.id + " from " + descriptor.directory.string());
        auto mod = std::make_unique<LuaModState>();
        mod->traceFile = traceFile;

        if (loadModScript(*mod, descriptor)) {
            if (mod->homeEffectReference == kNoLuaReference) {
                appendTrace(
                    mod->traceFile,
                    "[Lua mod: " + mod->name + "] loaded without a home effect"
                );
            }
            loadedMods.push_back(std::move(mod));
        } else {
            trace("Failed to load " + descriptor.id);
        }
    }

    return true;
#else
    if (!descriptors.empty())
        trace("Lua mod runtime is currently available on Windows only");
    return descriptors.empty();
#endif
}

void ModRuntime::drawHomeEffects(
    sf::RenderTarget& target,
    sf::Vector2f origin,
    sf::Vector2f size,
    float deltaTime
) {
#if defined(_WIN32)
    deltaTime = std::clamp(deltaTime, 0.f, 0.1f);

    for (const auto& modPointer : loadedMods) {
        LuaModState& mod = *modPointer;

        if (
            mod.disabled ||
            mod.homeEffectReference == kNoLuaReference
        )
            continue;

        mod.currentTarget = &target;
        mod.currentOrigin = origin;

        if (!mod.effectCreated) {
            if (!pushEffectFunction(mod, "create")) {
                appendTrace(
                    mod.traceFile,
                    "[Lua mod: " + mod.name + "] missing create callback"
                );
                mod.disabled = true;
                mod.currentTarget = nullptr;
                continue;
            }

            lua_pushnumber(mod.state, size.x);
            lua_pushnumber(mod.state, size.y);

            if (protectedCall(mod.state, 2, 1) != LUA_OK) {
                disableAfterLuaError(mod, "create");
                mod.currentTarget = nullptr;
                continue;
            }

            if (!lua_istable(mod.state, -1)) {
                lua_pop(mod.state, 1);
                appendTrace(
                    mod.traceFile,
                    "[Lua mod: " + mod.name + "] create callback must return a table"
                );
                mod.disabled = true;
                mod.currentTarget = nullptr;
                continue;
            }

            mod.effectStateReference = luaL_ref(
                mod.state,
                LUA_REGISTRYINDEX
            );
            mod.effectCreated = true;
            appendTrace(
                mod.traceFile,
                "[Lua mod: " + mod.name + "] home effect created"
            );
        }

        if (pushEffectFunction(mod, "update")) {
            lua_rawgeti(
                mod.state,
                LUA_REGISTRYINDEX,
                mod.effectStateReference
            );
            lua_pushnumber(mod.state, deltaTime);
            lua_pushnumber(mod.state, size.x);
            lua_pushnumber(mod.state, size.y);

            if (protectedCall(mod.state, 4, 0) != LUA_OK) {
                disableAfterLuaError(mod, "update");
                mod.currentTarget = nullptr;
                continue;
            }
        }

        if (pushEffectFunction(mod, "draw")) {
            lua_rawgeti(
                mod.state,
                LUA_REGISTRYINDEX,
                mod.effectStateReference
            );
            lua_rawgeti(
                mod.state,
                LUA_REGISTRYINDEX,
                mod.apiReference
            );

            if (protectedCall(mod.state, 2, 0) != LUA_OK) {
                disableAfterLuaError(mod, "draw");
            } else if (!mod.effectDrawTraced) {
                appendTrace(
                    mod.traceFile,
                    "[Lua mod: " + mod.name + "] home effect first frame drawn"
                );
                mod.effectDrawTraced = true;
            }
        }

        mod.currentTarget = nullptr;
    }
#else
    (void)target;
    (void)origin;
    (void)size;
    (void)deltaTime;
#endif
}
