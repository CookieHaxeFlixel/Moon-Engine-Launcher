#include <SFML/Graphics.hpp>
#include <SFML/System/FileInputStream.hpp>
#include <curl/curl.h>

#include <cstdlib>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <atomic>
#include <cstring>
#include <vector>
#include <algorithm>
#include <iostream>

#if defined(_WIN32)

#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

#elif defined(__ANDROID__)

#include <SFML/System/NativeActivity.hpp>
#include <android/native_activity.h>
#include <jni.h>

#endif

#include "third_party/miniz/miniz.h"

namespace fs = std::filesystem;

const unsigned int WIN_W = 1280;
const unsigned int WIN_H = 720;

const float SIDEBAR_W = 185.f;
const float BOTTOMBAR_H = 120.f;

#if defined(_WIN32)

const std::string DISCORD_CLIENT_ID =
    "1555084590434230334";

const std::string DISCORD_REDIRECT_URI =
    "http://127.0.0.1:38741/callback";
std::string discordOAuthState;

std::string exeDir() {
    static std::string dir = []() {
        char buffer[MAX_PATH];

        GetModuleFileNameA(
            NULL,
            buffer,
            MAX_PATH
        );

        std::string path(buffer);

        size_t pos =
            path.find_last_of("\\/");

        return (pos == std::string::npos)
            ? std::string("")
            : path.substr(0, pos + 1);
    }();

    return dir;
}

std::string BASE_PATH_() {
    return exeDir() + "com.funkinmoon/";
}

const std::string GAME_EXE_NAME =
    "MoonEngine.exe";

#elif defined(__ANDROID__)

std::string BASE_PATH_() {
    static std::string dir =
        std::string(
            sf::getNativeActivity()->internalDataPath
        ) + "/";

    return dir;
}

const std::string ANDROID_GAME_PACKAGE =
    "com.funkinmoon.game";

#endif

#if defined(_WIN32)

std::string discordOAuthCode;
std::string discordOAuthExpectedState;
std::string discordOAuthVerifier;

std::mutex discordOAuthMutex;

std::atomic<bool> discordOAuthFinished =
    false;

std::string urlEncode(const std::string& value)
{
    CURL* curl = curl_easy_init();

    if (!curl)
        return value;

    char* encoded =
        curl_easy_escape(
            curl,
            value.c_str(),
            static_cast<int>(value.length())
        );

    std::string result =
        encoded ? encoded : value;

    if (encoded)
        curl_free(encoded);

    curl_easy_cleanup(curl);

    return result;
}

std::string base64UrlEncode(
    const unsigned char* data,
    size_t size
)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    std::string result;

    for (size_t i = 0; i < size; i += 3)
    {
        unsigned int value =
            static_cast<unsigned int>(
                data[i]
            ) << 16;

        if (i + 1 < size)
        {
            value |=
                static_cast<unsigned int>(
                    data[i + 1]
                ) << 8;
        }

        if (i + 2 < size)
        {
            value |=
                static_cast<unsigned int>(
                    data[i + 2]
                );
        }

        result +=
            table[
                (value >> 18) & 63
            ];

        result +=
            table[
                (value >> 12) & 63
            ];

        if (i + 1 < size)
        {
            result +=
                table[
                    (value >> 6) & 63
                ];
        }

        if (i + 2 < size)
        {
            result +=
                table[
                    value & 63
                ];
        }
    }

    for (char& c : result)
    {
        if (c == '+')
            c = '-';

        else if (c == '/')
            c = '_';
    }

    while (
        !result.empty() &&
        result.back() == '='
    )
    {
        result.pop_back();
    }

    return result;
}

std::string generateRandomString(
    size_t byteCount
)
{
    std::vector<unsigned char> bytes(
        byteCount
    );

    if (
        BCryptGenRandom(
            nullptr,
            bytes.data(),
            static_cast<ULONG>(
                bytes.size()
            ),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        ) != 0
    )
    {
        return "";
    }

    return base64UrlEncode(
        bytes.data(),
        bytes.size()
    );
}

std::string createCodeChallenge(
    const std::string& verifier
)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;

    if (
        BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0
        ) != 0
    )
    {
        return "";
    }

    DWORD hashLength = 0;

    if (
        BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(
                &hashLength
            ),
            sizeof(hashLength),
            &hashLength,
            0
        ) != 0
    )
    {
        BCryptCloseAlgorithmProvider(
            algorithm,
            0
        );

        return "";
    }

    std::vector<unsigned char> hash(
        hashLength
    );

    BCRYPT_HASH_HANDLE hashHandle = nullptr;

    if (
        BCryptCreateHash(
            algorithm,
            &hashHandle,
            nullptr,
            0,
            nullptr,
            0,
            0
        ) != 0
    )
    {
        BCryptCloseAlgorithmProvider(
            algorithm,
            0
        );

        return "";
    }

    if (
        BCryptHashData(
            hashHandle,
            reinterpret_cast<PUCHAR>(
                const_cast<char*>(
                    verifier.data()
                )
            ),
            static_cast<ULONG>(
                verifier.size()
            ),
            0
        ) != 0
    )
    {
        BCryptDestroyHash(
            hashHandle
        );

        BCryptCloseAlgorithmProvider(
            algorithm,
            0
        );

        return "";
    }

    if (
        BCryptFinishHash(
            hashHandle,
            hash.data(),
            hashLength,
            0
        ) != 0
    )
    {
        BCryptDestroyHash(
            hashHandle
        );

        BCryptCloseAlgorithmProvider(
            algorithm,
            0
        );

        return "";
    }

    BCryptDestroyHash(
        hashHandle
    );

    BCryptCloseAlgorithmProvider(
        algorithm,
        0
    );

    return base64UrlEncode(
        hash.data(),
        hash.size()
    );
}

std::string extractJsonString(
    const std::string& json,
    const std::string& key
)
{
    std::string search =
        "\"" + key + "\"";

    size_t keyPos =
        json.find(search);

    if (
        keyPos ==
        std::string::npos
    )
    {
        return "";
    }

    size_t colon =
        json.find(
            ':',
            keyPos + search.size()
        );

    if (
        colon ==
        std::string::npos
    )
    {
        return "";
    }

    size_t quote =
        json.find(
            '"',
            colon + 1
        );

    if (
        quote ==
        std::string::npos
    )
    {
        return "";
    }

    std::string result;

    for (
        size_t i = quote + 1;
        i < json.size();
        ++i
    )
    {
        char c = json[i];

        if (c == '\\')
        {
            if (i + 1 < json.size())
            {
                result +=
                    json[i + 1];

                ++i;
            }

            continue;
        }

        if (c == '"')
            break;

        result += c;
    }

    return result;
}

void startDiscordCallbackServer();

void openDiscordLogin()
{
    discordOAuthFinished =
        false;

    {
        std::lock_guard<
            std::mutex
        > lock(
            discordOAuthMutex
        );

        discordOAuthCode =
            "";

        discordOAuthExpectedState =
            "";

        discordOAuthVerifier =
            "";
    }

    std::string state =
        generateRandomString(32);

    std::string verifier =
        generateRandomString(64);

    if (
        state.empty() ||
        verifier.empty()
    )
    {
        return;
    }

    std::string challenge =
        createCodeChallenge(
            verifier
        );

    if (challenge.empty())
    {
        return;
    }

    {
        std::lock_guard<
            std::mutex
        > lock(
            discordOAuthMutex
        );

        discordOAuthExpectedState =
            state;

        discordOAuthVerifier =
            verifier;
    }

    startDiscordCallbackServer();

    std::string url =
        "https://discord.com/oauth2/authorize"
        "?client_id=" +
        DISCORD_CLIENT_ID +
        "&response_type=code"
        "&redirect_uri=" +
        urlEncode(
            DISCORD_REDIRECT_URI
        ) +
        "&scope=identify"
        "&state=" +
        urlEncode(state) +
        "&code_challenge=" +
        urlEncode(challenge) +
        "&code_challenge_method=S256";

    std::string command =
        "start \"\" \"" +
        url +
        "\"";

    system(
        command.c_str()
    );
}

void startDiscordCallbackServer()
{
    std::thread([]()
    {
        WSADATA wsaData{};

        if (
            WSAStartup(
                MAKEWORD(2, 2),
                &wsaData
            ) != 0
        )
        {
            return;
        }

        SOCKET serverSocket =
            socket(
                AF_INET,
                SOCK_STREAM,
                IPPROTO_TCP
            );

        if (
            serverSocket ==
            INVALID_SOCKET
        )
        {
            WSACleanup();
            return;
        }

        sockaddr_in address{};

        address.sin_family =
            AF_INET;

        address.sin_addr.s_addr =
            inet_addr(
                "127.0.0.1"
            );

        address.sin_port =
            htons(38741);

        if (
            bind(
                serverSocket,
                reinterpret_cast<
                    sockaddr*
                >(&address),
                sizeof(address)
            ) == SOCKET_ERROR
        )
        {
            closesocket(
                serverSocket
            );

            WSACleanup();

            return;
        }

        if (
            listen(
                serverSocket,
                1
            ) == SOCKET_ERROR
        )
        {
            closesocket(
                serverSocket
            );

            WSACleanup();

            return;
        }

        SOCKET clientSocket =
            accept(
                serverSocket,
                nullptr,
                nullptr
            );

        if (
            clientSocket ==
            INVALID_SOCKET
        )
        {
            closesocket(
                serverSocket
            );

            WSACleanup();

            return;
        }

        char buffer[8192]{};

        int received =
            recv(
                clientSocket,
                buffer,
                sizeof(buffer) - 1,
                0
            );

        if (received > 0)
        {
            std::string request(
                buffer,
                received
            );

            size_t getPos =
                request.find(
                    "GET "
                );

            if (
                getPos !=
                std::string::npos
            )
            {
                getPos += 4;

                size_t pathEnd =
                    request.find(
                        ' ',
                        getPos
                    );

                if (
                    pathEnd !=
                    std::string::npos
                )
                {
                    std::string path =
                        request.substr(
                            getPos,
                            pathEnd - getPos
                        );

                    size_t queryPos =
                        path.find('?');

                    if (
                        queryPos !=
                        std::string::npos
                    )
                    {
                        std::string query =
                            path.substr(
                                queryPos + 1
                            );

                        std::string code;
                        std::string receivedState;

                        std::stringstream ss(
                            query
                        );

                        std::string part;

                        while (
                            std::getline(
                                ss,
                                part,
                                '&'
                            )
                        )
                        {
                            size_t eq =
                                part.find('=');

                            if (
                                eq ==
                                std::string::npos
                            )
                            {
                                continue;
                            }

                            std::string key =
                                part.substr(
                                    0,
                                    eq
                                );

                            std::string value =
                                part.substr(
                                    eq + 1
                                );

                            if (
                                key == "code"
                            )
                            {
                                code =
                                    value;
                            }

                            else if (
                                key == "state"
                            )
                            {
                                receivedState =
                                    value;
                            }
                        }

                        std::string expectedState;

                        {
                            std::lock_guard<
                                std::mutex
                            > lock(
                                discordOAuthMutex
                            );

                            expectedState =
                                discordOAuthExpectedState;
                        }

                        if (
                            !code.empty() &&
                            !receivedState.empty() &&
                            receivedState ==
                                expectedState
                        )
                        {
                            {
                                std::lock_guard<
                                    std::mutex
                                > lock(
                                    discordOAuthMutex
                                );

                                discordOAuthCode =
                                    code;
                            }

                            discordOAuthFinished =
                                true;
                        }
                    }
                }
            }
        }

        const char* response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<html>"
            "<head>"
            "<title>Moon Engine Launcher</title>"
            "</head>"
            "<body>"
            "<h2>Moon Engine Launcher</h2>"
            "<p>Discord login completed.</p>"
            "<p>You can return to the launcher.</p>"
            "</body>"
            "</html>";

        send(
            clientSocket,
            response,
            static_cast<int>(
                strlen(response)
            ),
            0
        );

        closesocket(
            clientSocket
        );

        closesocket(
            serverSocket
        );

        WSACleanup();

    }).detach();
}

size_t discordWriteCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    void* userp
)
{
    size_t total =
        size * nmemb;

    std::string* output =
        static_cast<std::string*>(
            userp
        );

    output->append(
        static_cast<char*>(
            contents
        ),
        total
    );

    return total;
}

bool exchangeDiscordCode(
    const std::string& code,
    const std::string& verifier,
    std::string& accessToken
)
{
    CURL* curl =
        curl_easy_init();

    if (!curl)
    {
        std::cout
            << "[DISCORD] curl_easy_init FAILED"
            << std::endl;

        return false;
    }

    std::string response;

    std::string postData =
        "client_id=" +
        urlEncode(
            DISCORD_CLIENT_ID
        ) +
        "&grant_type=authorization_code"
        "&code=" +
        urlEncode(code) +
        "&redirect_uri=" +
        urlEncode(
            DISCORD_REDIRECT_URI
        ) +
        "&code_verifier=" +
        urlEncode(verifier);

    struct curl_slist* headers =
        nullptr;

    headers =
        curl_slist_append(
            headers,
            "Content-Type: application/x-www-form-urlencoded"
        );

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "https://discord.com/api/v10/oauth2/token"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POST,
        1L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_POSTFIELDS,
        postData.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        discordWriteCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );

#if defined(_WIN32)

    /*
     * Use the Windows certificate store.
     * This prevents libcurl from requiring
     * a separate CA bundle file.
     */
    curl_easy_setopt(
        curl,
        CURLOPT_SSL_OPTIONS,
        CURLSSLOPT_NATIVE_CA
    );

#endif

    CURLcode result =
        curl_easy_perform(curl);

    long httpCode = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &httpCode
    );

    curl_slist_free_all(
        headers
    );

    curl_easy_cleanup(curl);

    std::cout
        << "[DISCORD] HTTP status: "
        << httpCode
        << std::endl;

    std::cout
        << "[DISCORD] Response: "
        << response
        << std::endl;

    if (
        result !=
        CURLE_OK
    )
    {
        std::cout
            << "[DISCORD] CURL error: "
            << curl_easy_strerror(result)
            << std::endl;

        return false;
    }

    accessToken =
        extractJsonString(
            response,
            "access_token"
        );

    if (accessToken.empty())
    {
        std::cout
            << "[DISCORD] No access_token in response."
            << std::endl;

        return false;
    }

    return true;
}

#endif

std::string VERSIONS_PATH_() {
    return BASE_PATH_() + "versions/";
}

std::string SAVES_PATH_() {
    return BASE_PATH_() + "data/saves/";
}

std::string TEMP_PATH_() {
    return BASE_PATH_() + "temp_extract/";
}

std::string SAVE_FILE_() {
    return SAVES_PATH_() + "user.json";
}

std::string MODS_PATH_() {
    return BASE_PATH_() + "mods/";
}

std::string MODS_STATE_FILE_() {
    return SAVES_PATH_() + "mods.json";
}

const std::string VERSIONS_JSON =
    "versions.json";

const std::string A_UI = "assets/ui/";
const std::string A_HOME = A_UI + "home/";
const std::string A_LOGIN = A_UI + "login/";
const std::string A_LOADING = A_UI + "loading/";
const std::string A_NEWS = A_UI + "news/docs/";
const std::string A_FONTS = A_UI + "fonts/";

const std::string A_ICON =
    "assets/app/icons/icon-moon.png";


struct ProgressData {
    double downloaded = 0;
    double total = 0;
    double speed = 0;
};

ProgressData progressData;


struct Version {
    std::string number;
    std::string zipUrl;
    std::string state;
};


struct UserData {
    std::string name;
    std::string nickname;
    std::string id;
    std::string avatarUrl;

    bool loggedIn = false;
};

UserData currentUser;


size_t writeFileCb(
    void* p,
    size_t s,
    size_t n,
    FILE* f
) {
    return fwrite(p, s, n, f);
}


int progressCb(
    void* p,
    curl_off_t* dlt,
    curl_off_t* dln,
    curl_off_t*,
    curl_off_t*
) {
    auto* d = (ProgressData*)p;

    /*
     * dlt e dln são ponteiros.
     * Precisamos desreferenciar os valores.
     */
    d->downloaded = (double)(*dln);
    d->total = (double)(*dlt);

    return 0;
}


bool downloadToFile(
    const std::string& url,
    const std::string& destPath,
    ProgressData* progress = nullptr,
    uintmax_t minSize = 0
) {
    CURL* curl = curl_easy_init();

    if (!curl)
        return false;

    FILE* fp = fopen(destPath.c_str(), "wb");

    if (!fp) {
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        writeFileCb
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        fp
    );

    if (progress) {
        curl_easy_setopt(
            curl,
            CURLOPT_XFERINFOFUNCTION,
            progressCb
        );

        curl_easy_setopt(
            curl,
            CURLOPT_XFERINFODATA,
            progress
        );

        curl_easy_setopt(
            curl,
            CURLOPT_NOPROGRESS,
            0L
        );
    }

    curl_easy_setopt(
        curl,
        CURLOPT_FOLLOWLOCATION,
        1L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_MAXREDIRS,
        10L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_SSL_VERIFYPEER,
        0L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_SSL_VERIFYHOST,
        0L
    );

    curl_easy_setopt(
        curl,
        CURLOPT_USERAGENT,
        "Mozilla/5.0"
    );

    CURLcode res =
        curl_easy_perform(curl);

    fclose(fp);

    curl_easy_cleanup(curl);

    return
        res == CURLE_OK &&
        fs::exists(destPath) &&
        fs::file_size(destPath) > minSize;
}


std::string readTextFile(
    const std::string& path
) {
    sf::FileInputStream f;

    if (!f.open(path))
        return "";

    auto size = f.getSize();

    if (!size || *size == 0)
        return "";

    std::string data(
        *size,
        '\0'
    );

    auto read =
        f.read(
            data.data(),
            *size
        );

    if (!read)
        return "";

    data.resize(*read);

    return data;
}


std::string jStr(
    const std::string& json,
    const std::string& key
) {
    std::string s =
        "\"" + key + "\":\"";

    size_t p =
        json.find(s);

    if (p == std::string::npos)
        return "";

    p += s.size();

    size_t e =
        json.find("\"", p);

    if (e == std::string::npos)
        return "";

    std::string v =
        json.substr(
            p,
            e - p
        );

    std::string out;

    for (size_t i = 0; i < v.size(); i++) {

        if (
            v[i] == '\\' &&
            i + 1 < v.size()
        ) {
            if (v[i + 1] == '/') {
                out += '/';
                i++;
            }
            else if (v[i + 1] == 'n') {
                out += '\n';
                i++;
            }
            else if (v[i + 1] == '"') {
                out += '"';
                i++;
            }
            else {
                out += v[i];
            }
        }
        else {
            out += v[i];
        }
    }

    return out;
}


void saveUser() {

    fs::create_directories(
        SAVES_PATH_()
    );

    std::string savePath =
        SAVE_FILE_();

    std::cout
        << "[USER] Saving user to: "
        << savePath
        << std::endl;

    std::ofstream f(
        savePath
    );

    if (!f.is_open())
    {
        std::cout
            << "[USER] ERROR: Could not open user.json!"
            << std::endl;

        return;
    }

    f
        << "{\n"
        << "  \"name\":\""
        << currentUser.name
        << "\",\n"
        << "  \"nickname\":\""
        << currentUser.nickname
        << "\",\n"
        << "  \"id\":\""
        << currentUser.id
        << "\",\n"
        << "  \"avatarUrl\":\""
        << currentUser.avatarUrl
        << "\"\n"
        << "}\n";

    f.close();

    std::cout
        << "[USER] user.json saved successfully!"
        << std::endl;
}

bool getDiscordUser(
    const std::string& accessToken
)
{
    std::cout
        << "[DISCORD] getDiscordUser START"
        << std::endl;

    CURL* curl =
        curl_easy_init();

    if (!curl)
    {
        std::cout
            << "[DISCORD] curl_easy_init FAILED"
            << std::endl;

        return false;
    }

    std::string response;

    struct curl_slist* headers =
        nullptr;

    std::string auth =
        "Authorization: Bearer " +
        accessToken;

    headers =
        curl_slist_append(
            headers,
            auth.c_str()
        );

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "https://discord.com/api/v10/users/@me"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_HTTPHEADER,
        headers
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        discordWriteCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );

#if defined(_WIN32)

    curl_easy_setopt(
        curl,
        CURLOPT_SSL_OPTIONS,
        CURLSSLOPT_NATIVE_CA
    );

#endif

    CURLcode result =
        curl_easy_perform(curl);

    long httpCode = 0;

    curl_easy_getinfo(
        curl,
        CURLINFO_RESPONSE_CODE,
        &httpCode
    );

    curl_slist_free_all(
        headers
    );

    curl_easy_cleanup(curl);

    std::cout
        << "[DISCORD] User HTTP status: "
        << httpCode
        << std::endl;

    std::cout
        << "[DISCORD] User response: "
        << response
        << std::endl;

    if (
        result !=
        CURLE_OK
    )
    {
        std::cout
            << "[DISCORD] User CURL error: "
            << curl_easy_strerror(result)
            << std::endl;

        return false;
    }

    std::string id =
        extractJsonString(
            response,
            "id"
        );

    std::string username =
        extractJsonString(
            response,
            "username"
        );

    std::string globalName =
        extractJsonString(
            response,
            "global_name"
        );

    std::string avatar =
        extractJsonString(
            response,
            "avatar"
        );

    std::cout
        << "[DISCORD] ID received: "
        << (!id.empty() ? "YES" : "NO")
        << std::endl;

    if (id.empty())
    {
        std::cout
            << "[DISCORD] Discord user ID missing."
            << std::endl;

        return false;
    }

    if (globalName.empty())
        globalName = username;

    currentUser.name =
        globalName;

    currentUser.nickname =
        username;

    currentUser.id =
        id;

    if (!avatar.empty())
    {
        currentUser.avatarUrl =
            "https://cdn.discordapp.com/avatars/" +
            id +
            "/" +
            avatar +
            ".png?size=256";
    }
    else
    {
        currentUser.avatarUrl =
            "";
    }

    currentUser.loggedIn =
        true;

    std::cout
        << "[DISCORD] User received successfully."
        << std::endl;

    saveUser();

    return true;
}

bool completeDiscordLogin()
{
    std::cout
        << "[DISCORD] completeDiscordLogin START"
        << std::endl;

    std::string code;
    std::string verifier;

    {
        std::lock_guard<
            std::mutex
        > lock(
            discordOAuthMutex
        );

        code =
            discordOAuthCode;

        verifier =
            discordOAuthVerifier;
    }

    std::cout
        << "[DISCORD] Code empty: "
        << (code.empty() ? "YES" : "NO")
        << std::endl;

    std::cout
        << "[DISCORD] Verifier empty: "
        << (verifier.empty() ? "YES" : "NO")
        << std::endl;

    if (
        code.empty() ||
        verifier.empty()
    )
    {
        std::cout
            << "[DISCORD] Missing code/verifier!"
            << std::endl;

        return false;
    }

    std::string accessToken;

    std::cout
        << "[DISCORD] Exchanging code for token..."
        << std::endl;

    if (
        !exchangeDiscordCode(
            code,
            verifier,
            accessToken
        )
    )
    {
        std::cout
            << "[DISCORD] Token exchange FAILED!"
            << std::endl;

        return false;
    }

    std::cout
        << "[DISCORD] Token received."
        << std::endl;

    std::cout
        << "[DISCORD] Getting Discord user..."
        << std::endl;

    return getDiscordUser(
        accessToken
    );
}

bool loadUser() {

    std::string savePath =
        SAVE_FILE_();

    std::cout
        << "[USER] Looking for: "
        << savePath
        << std::endl;

    if (!fs::exists(savePath))
    {
        std::cout
            << "[USER] user.json does not exist."
            << std::endl;

        return false;
    }

    std::ifstream f(
        savePath
    );

    if (!f.is_open())
    {
        std::cout
            << "[USER] ERROR: Could not open user.json!"
            << std::endl;

        return false;
    }

    std::string json{
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>()
    };

    currentUser.name =
        jStr(
            json,
            "name"
        );

    currentUser.nickname =
        jStr(
            json,
            "nickname"
        );

    currentUser.id =
        jStr(
            json,
            "id"
        );

    currentUser.avatarUrl =
        jStr(
            json,
            "avatarUrl"
        );

    currentUser.loggedIn =
        !currentUser.name.empty();

    std::cout
        << "[USER] Loaded user: "
        << currentUser.name
        << std::endl;

    return currentUser.loggedIn;
}


std::vector<Version> loadVersions() {

    std::vector<Version> versions;

    std::string json =
        readTextFile(
            VERSIONS_JSON
        );

    if (json.empty())
        return versions;

    std::string token =
        "{\"numero\":";

    size_t pos = 0;

    while (
        (pos = json.find(token, pos))
        != std::string::npos
    ) {
        size_t end =
            json.find(
                token,
                pos + token.size()
            );

        if (end == std::string::npos)
            end = json.size();

        std::string chunk =
            json.substr(
                pos,
                end - pos
            );

        pos += token.size();

        Version v;

        v.number =
            jStr(chunk, "numero");

        v.zipUrl =
            jStr(chunk, "zipUrl");

        v.state =
            jStr(chunk, "estado");

        if (!v.number.empty())
            versions.push_back(v);
    }

    return versions;
}


struct InstalledEntry {

    std::string version;
    std::string path;

    bool installed = false;
};


std::string INSTALLED_FILE_() {
    return BASE_PATH_() + "save.json";
}


bool jBool(
    const std::string& json,
    const std::string& key
) {
    std::string s =
        "\"" + key + "\":";

    size_t p =
        json.find(s);

    if (p == std::string::npos)
        return false;

    p += s.size();

    while (
        p < json.size() &&
        (
            json[p] == ' ' ||
            json[p] == '\t'
        )
    ) {
        p++;
    }

    return json.compare(
        p,
        4,
        "true"
    ) == 0;
}


std::vector<std::string> jStrArray(
    const std::string& json,
    const std::string& key
) {
    std::vector<std::string> out;

    std::string s =
        "\"" + key + "\":[";

    size_t p =
        json.find(s);

    if (p == std::string::npos)
        return out;

    p += s.size();

    size_t e =
        json.find(']', p);

    if (e == std::string::npos)
        return out;

    std::string arr =
        json.substr(
            p,
            e - p
        );

    size_t i = 0;

    while (i < arr.size()) {

        size_t q1 =
            arr.find('"', i);

        if (q1 == std::string::npos)
            break;

        size_t q2 =
            arr.find(
                '"',
                q1 + 1
            );

        if (q2 == std::string::npos)
            break;

        out.push_back(
            arr.substr(
                q1 + 1,
                q2 - q1 - 1
            )
        );

        i = q2 + 1;
    }

    return out;
}


std::vector<InstalledEntry> loadInstalled()
{
    std::vector<InstalledEntry> out;

    std::string path = INSTALLED_FILE_();

    if (!fs::exists(path))
        return out;

    std::ifstream f(path);

    if (!f.is_open())
    {
        std::cout << "[ERRO] Nao foi possivel abrir: "
                  << path << std::endl;
        return out;
    }

    std::string json(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>()
    );

    std::cout << "[DEBUG] Conteudo do save.json:" << std::endl;
    std::cout << json << std::endl;

    size_t pos = 0;

    while ((pos = json.find('{', pos)) != std::string::npos)
    {
        size_t end = json.find('}', pos);

        if (end == std::string::npos)
            break;

        std::string chunk =
            json.substr(pos, end - pos + 1);

        pos = end + 1;

        InstalledEntry e{};

        // version
        {
            size_t p = chunk.find("\"version\"");
            if (p != std::string::npos)
            {
                p = chunk.find(':', p);

                if (p != std::string::npos)
                {
                    p = chunk.find('"', p);

                    if (p != std::string::npos)
                    {
                        size_t q = chunk.find('"', p + 1);

                        if (q != std::string::npos)
                            e.version =
                                chunk.substr(p + 1, q - p - 1);
                    }
                }
            }
        }

        // path
        {
            size_t p = chunk.find("\"path\"");
            if (p != std::string::npos)
            {
                p = chunk.find(':', p);

                if (p != std::string::npos)
                {
                    p = chunk.find('"', p);

                    if (p != std::string::npos)
                    {
                        size_t q = chunk.find('"', p + 1);

                        if (q != std::string::npos)
                            e.path =
                                chunk.substr(p + 1, q - p - 1);
                    }
                }
            }
        }

        // installed
        {
            size_t p = chunk.find("\"installed\"");

            if (p != std::string::npos)
            {
                p = chunk.find(':', p);

                if (p != std::string::npos)
                {
                    std::string value =
                        chunk.substr(p + 1);

                    e.installed =
                        value.find("true") != std::string::npos;
                }
            }
        }

        if (!e.version.empty())
        {
            std::cout
                << "[OK] Versao instalada encontrada: "
                << e.version
                << " | installed="
                << (e.installed ? "true" : "false")
                << " | path="
                << e.path
                << std::endl;

            out.push_back(e);
        }
    }

std::cout
    << "[OK] save.json carregado ("
    << out.size()
    << " entradas) de: "
    << path
    << std::endl;

std::cout << "[DEBUG] JSON LIDO:" << std::endl;
std::cout << json << std::endl;

    return out;
}


/*
 * Salva o estado da instalação.
 *
 * Exemplo:
 *
 * com.funkinmoon/
 * └── versions/
 *     └── v0.0.1/
 *         └── funkin-moon-windows-64bit/
 *             └── FunkinMoon.exe
 */
void saveInstalled(
    const std::string& version,
    const std::string& path
) {
    fs::create_directories(
        SAVES_PATH_()
    );

    std::ofstream f(
        INSTALLED_FILE_()
    );

    if (!f.is_open())
        return;

    f
        << "[\n"
        << "  {\n"
        << "    \"version\": \""
        << version
        << "\",\n"

        << "    \"installed\": true,\n"

        << "    \"path\": \""
        << path
        << "\"\n"

        << "  }\n"
        << "]\n";
}


struct ModEntry {

    std::string folder;

    bool enabled = false;
};


std::vector<ModEntry> loadMods() {

    std::vector<ModEntry> mods;

    std::string path =
        MODS_PATH_();

    fs::create_directories(path);

    std::vector<std::string> enabledList;

    if (fs::exists(
        MODS_STATE_FILE_()
    )) {

        std::ifstream f(
            MODS_STATE_FILE_()
        );

        /*
         * Use {} to avoid the most vexing parse.
         */
        std::string json{
            std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()
        };

        enabledList =
            jStrArray(
                json,
                "enabled"
            );
    }

    for (auto& entry :
         fs::directory_iterator(path)) {

        if (!entry.is_directory())
            continue;

        ModEntry m;

        m.folder =
            entry.path()
                .filename()
                .string();

        m.enabled =
            std::find(
                enabledList.begin(),
                enabledList.end(),
                m.folder
            ) != enabledList.end();

        mods.push_back(m);
    }

    return mods;
}


void saveModsState(
    const std::vector<ModEntry>& mods
) {
    fs::create_directories(
        SAVES_PATH_()
    );

    std::ofstream f(
        MODS_STATE_FILE_()
    );

    if (!f.is_open())
        return;

    f
        << "{\n"
        << "  \"enabled\":[";

    bool first = true;

    for (auto& m : mods) {

        if (!m.enabled)
            continue;

        if (!first)
            f << ",";

        f
            << "\n    \""
            << m.folder
            << "\"";

        first = false;
    }

    f
        << "\n  ]\n"
        << "}\n";
}


#if defined(_WIN32)

std::string resolveExePath(
    const std::vector<InstalledEntry>& installed,
    const std::string& number
) {
    /*
     * Primeiro tenta o caminho salvo no save.json.
     */

    std::string wantedVersion = number;

    // Remove o "v" para comparar com o save.json.
    if (!wantedVersion.empty() && wantedVersion[0] == 'v')
        wantedVersion = wantedVersion.substr(1);

    for (const auto& e : installed) {

        std::string installedVersion = e.version;

        // Remove o "v" do save.json, caso exista.
        if (!installedVersion.empty() && installedVersion[0] == 'v')
            installedVersion = installedVersion.substr(1);

        if (
            installedVersion == wantedVersion &&
            e.installed
        ) {
            std::string full =
                exeDir() +
                e.path +
                "/" +
                GAME_EXE_NAME;

            if (fs::exists(full))
                return full;
        }
    }

    /*
     * Fallback para a estrutura padrão.
     *
     * Se number = "v0.0.1", usa "v0.0.1".
     * Se number = "0.0.1", adiciona "v".
     */

    std::string versionFolder = number;

    if (versionFolder.empty() || versionFolder[0] != 'v')
        versionFolder = "v" + versionFolder;

    return
        VERSIONS_PATH_() +
        versionFolder +
        "/funkin-moon-windows-64bit/" +
        GAME_EXE_NAME;
}



void launchVersion(
    sf::RenderWindow& window,
    const std::vector<InstalledEntry>& installed,
    const std::string& number
) {
    std::string exePath =
        resolveExePath(
            installed,
            number
        );

    if (!fs::exists(exePath))
        return;

    window.setVisible(false);

    system(
        ("\"" + exePath + "\"").c_str()
    );

    window.setVisible(true);
}

bool isVersionInstalled(const std::vector<InstalledEntry>& installed, const std::string& number) {
    std::string path = resolveExePath(installed, number);

    std::cout << "VERSION: " << number << std::endl;
    std::cout << "EXE PATH: " << path << std::endl;
    std::cout << "EXISTS: " << fs::exists(path) << std::endl;

    return fs::exists(path);
}


#elif defined(__ANDROID__)

template <class F>
auto withActivity(F&& fn) {

    ANativeActivity* activity =
        sf::getNativeActivity();

    JNIEnv* env = nullptr;

    activity->vm->AttachCurrentThread(
        &env,
        nullptr
    );

    jclass cls =
        env->GetObjectClass(
            activity->clazz
        );

    auto result =
        fn(
            env,
            cls,
            activity->clazz
        );

    env->DeleteLocalRef(cls);

    return result;
}


bool androidPackageInstalled(
    const std::string& pkg
) {
    return withActivity(
        [&](JNIEnv* env,
            jclass cls,
            jobject self) {

            jmethodID mid =
                env->GetMethodID(
                    cls,
                    "isPackageInstalled",
                    "(Ljava/lang/String;)Z"
                );

            jstring jpkg =
                env->NewStringUTF(
                    pkg.c_str()
                );

            bool r =
                env->CallBooleanMethod(
                    self,
                    mid,
                    jpkg
                );

            env->DeleteLocalRef(jpkg);

            return r;
        }
    );
}


std::string androidInstalledVersion(
    const std::string& pkg
) {
    return withActivity(
        [&](JNIEnv* env,
            jclass cls,
            jobject self) {

            jmethodID mid =
                env->GetMethodID(
                    cls,
                    "getInstalledVersionName",
                    "(Ljava/lang/String;)Ljava/lang/String;"
                );

            jstring jpkg =
                env->NewStringUTF(
                    pkg.c_str()
                );

            jstring jres =
                (jstring)env->CallObjectMethod(
                    self,
                    mid,
                    jpkg
                );

            std::string result;

            if (jres) {

                const char* chars =
                    env->GetStringUTFChars(
                        jres,
                        nullptr
                    );

                result = chars;

                env->ReleaseStringUTFChars(
                    jres,
                    chars
                );

                env->DeleteLocalRef(jres);
            }

            env->DeleteLocalRef(jpkg);

            return result;
        }
    );
}


void androidLaunchPackage(
    const std::string& pkg
) {
    withActivity(
        [&](JNIEnv* env,
            jclass cls,
            jobject self) {

            jmethodID mid =
                env->GetMethodID(
                    cls,
                    "launchPackage",
                    "(Ljava/lang/String;)V"
                );

            jstring jpkg =
                env->NewStringUTF(
                    pkg.c_str()
                );

            env->CallVoidMethod(
                self,
                mid,
                jpkg
            );

            env->DeleteLocalRef(jpkg);

            return 0;
        }
    );
}


void androidInstallApk(
    const std::string& apkPath
) {
    withActivity(
        [&](JNIEnv* env,
            jclass cls,
            jobject self) {

            jmethodID mid =
                env->GetMethodID(
                    cls,
                    "installApk",
                    "(Ljava/lang/String;)V"
                );

            jstring jpath =
                env->NewStringUTF(
                    apkPath.c_str()
                );

            env->CallVoidMethod(
                self,
                mid,
                jpath
            );

            env->DeleteLocalRef(jpath);

            return 0;
        }
    );
}


bool isVersionInstalled(
    const std::vector<InstalledEntry>&,
    const std::string& number
) {
    return
        androidPackageInstalled(
            ANDROID_GAME_PACKAGE
        ) &&
        androidInstalledVersion(
            ANDROID_GAME_PACKAGE
        ) == number;
}


void launchVersion(
    sf::RenderWindow&,
    const std::vector<InstalledEntry>&,
    const std::string&
) {
    androidLaunchPackage(
        ANDROID_GAME_PACKAGE
    );
}

#endif


std::string fmtBytes(
    double b
) {
    std::ostringstream ss;

    if (b >= 1024 * 1024)
        ss
            << std::fixed
            << std::setprecision(1)
            << b / 1024 / 1024
            << " MB";

    else if (b >= 1024)
        ss
            << std::fixed
            << std::setprecision(1)
            << b / 1024
            << " KB";

    else
        ss
            << (int)b
            << " B";

    return ss.str();
}


sf::VertexArray roundRect(
    float x,
    float y,
    float w,
    float h,
    float r,
    sf::Color col
) {
    sf::VertexArray s(
        sf::PrimitiveType::TriangleFan
    );

    int n = 20;

    s.append({
        {x + w / 2, y + h / 2},
        col
    });

    float ang[] = {
        180,
        270,
        0,
        90
    };

    float ox[] = {
        x + r,
        x + w - r,
        x + w - r,
        x + r
    };

    float oy[] = {
        y + r,
        y + r,
        y + h - r,
        y + h - r
    };

    for (int c = 0; c < 4; c++) {

        for (int i = 0; i <= n; i++) {

            float a =
                (
                    ang[c] +
                    i * 90.f / n
                ) *
                3.14159f /
                180.f;

            s.append({
                {
                    ox[c] + r * cosf(a),
                    oy[c] + r * sinf(a)
                },
                col
            });
        }
    }

    float a =
        180 *
        3.14159f /
        180.f;

    s.append({
        {
            ox[0] + r * cosf(a),
            oy[0] + r * sinf(a)
        },
        col
    });

    return s;
}


sf::Texture makeCircleTexture(
    sf::Texture& src,
    unsigned int size
) {
    sf::RenderTexture rt;

    rt.resize({
        size,
        size
    });

    rt.clear(
        sf::Color::Transparent
    );

    sf::CircleShape mask(
        (float)size / 2
    );

    mask.setPosition({
        0,
        0
    });

    sf::Sprite spr(src);

    auto srcSz =
        src.getSize();

    float scale =
        (float)size /
        std::min(
            (float)srcSz.x,
            (float)srcSz.y
        );

    spr.setScale({
        scale,
        scale
    });

    spr.setPosition({
        (
            (float)size -
            srcSz.x * scale
        ) / 2.f,

        (
            (float)size -
            srcSz.y * scale
        ) / 2.f
    });

    sf::CircleShape circle(
        (float)size / 2
    );

    circle.setPosition({
        0,
        0
    });

    circle.setTexture(
        &src
    );

    circle.setTextureRect(
        sf::IntRect(
            {
                0,
                0
            },
            {
                (int)srcSz.x,
                (int)srcSz.y
            }
        )
    );

    rt.draw(circle);

    rt.display();

    return rt.getTexture();
}


void loadingScreen(
    sf::RenderWindow& window,
    sf::Font& fontTitle
) {
    sf::Texture bgTex;
    sf::Texture moonTex;

    bool hasBg =
        bgTex.loadFromFile(
            A_LOADING +
            "loadingBackCard.png"
        );

    bool hasMoon =
        moonTex.loadFromFile(
            A_LOADING +
            "moonLoading.png"
        );

    sf::Sprite bg(bgTex);
    sf::Sprite moon(moonTex);

    if (hasBg) {

        auto sz =
            bgTex.getSize();

        float sx =
            (float)WIN_W / sz.x;

        float sy =
            (float)WIN_H / sz.y;

        bg.setScale({
            sx,
            sy
        });
    }

    if (hasMoon) {

        auto sz =
            moonTex.getSize();

        moon.setOrigin({
            sz.x / 2.f,
            sz.y / 2.f
        });

        moon.setPosition({
            WIN_W / 2.f,
            WIN_H / 2.f + 30
        });

        float scale =
            150.f /
            std::max(
                (float)sz.x,
                (float)sz.y
            );

        moon.setScale({
            scale,
            scale
        });
    }

    sf::Text t(
        fontTitle,
        "",
        44
    );

    t.setFillColor(
        sf::Color::White
    );

    sf::FloatRect tb =
        t.getLocalBounds();

    t.setPosition({
        WIN_W / 2.f -
            tb.size.x / 2.f,

        WIN_H / 2.f -
            160
    });

    sf::Clock clk;

    float rot = 0.f;
    float dur = 2.5f;

    while (window.isOpen()) {

        float dt =
            clk.restart()
                .asSeconds();

        rot +=
            120.f * dt;

        if (hasMoon)
            moon.setRotation(
                sf::degrees(rot)
            );

        while (
            const std::optional ev =
                window.pollEvent()
        ) {
            if (
                ev->is<
                    sf::Event::Closed
                >()
            ) {
                window.close();
                return;
            }
        }

        window.clear(
            sf::Color(10, 15, 40)
        );

        if (hasBg)
            window.draw(bg);

        else {
            window.draw(
                roundRect(
                    WIN_W / 2.f - 280,
                    WIN_H / 2.f - 220,
                    560,
                    440,
                    14,
                    sf::Color(
                        0,
                        0,
                        0,
                        230
                    )
                )
            );
        }

        window.draw(t);

        if (hasMoon)
            window.draw(moon);

        window.display();

        if (
            clk.getElapsedTime()
                .asSeconds() + dt >= dur
        )
            break;
    }
}

#if defined(_WIN32)


#endif


bool loginScreen(
    sf::RenderWindow* window,
    sf::Font* fontTitle,
    sf::Font* fontText
) {
    sf::Texture bgTex;
    sf::Texture btnTex;

    bool hasBg =
        bgTex.loadFromFile(
            A_LOGIN +
            "loginBackCard.png"
        );

    bool hasBtn =
        btnTex.loadFromFile(
            A_LOGIN +
            "discordButton.png"
        );

    sf::Sprite bg(bgTex);
    sf::Sprite btn(btnTex);

    if (hasBg) {
        auto sz =
            bgTex.getSize();

        float sx =
            (float)WIN_W / sz.x;

        float sy =
            (float)WIN_H / sz.y;

        bg.setScale({
            sx,
            sy
        });
    }

    float cardW = 560;
    float cardH = 440;

    float cardX =
        WIN_W / 2.f -
        cardW / 2.f;

    float cardY =
        WIN_H / 2.f -
        cardH / 2.f;

    float btnW =
        hasBtn
            ? (float)btnTex.getSize().x
            : 400;

    float btnH =
        hasBtn
            ? (float)btnTex.getSize().y
            : 65;

    float btnX =
        WIN_W / 2.f -
        btnW / 2.f;

    float btnY =
        WIN_H / 2.f + 30;

    if (hasBtn)
        btn.setPosition({
            btnX,
            btnY
        });

    sf::Text title(
        *fontTitle,
        "Login",
        48
    );

    title.setFillColor(
        sf::Color::White
    );

    sf::FloatRect tb =
        title.getLocalBounds();

    title.setPosition({
        WIN_W / 2.f -
            tb.size.x / 2.f,

        WIN_H / 2.f -
            160
    });

    while (window->isOpen()) {

        /*
         * Discord terminou o OAuth?
         *
         * IMPORTANTE:
         * Isso fica FORA do pollEvent().
         */
        if (discordOAuthFinished)
        {
            discordOAuthFinished =
                false;

            if (completeDiscordLogin())
            {
                return true;
            }
        }

        sf::Vector2f mouse(
            sf::Mouse::getPosition(
                *window
            )
        );

        bool hBtn =
            mouse.x >= btnX &&
            mouse.x <= btnX + btnW &&
            mouse.y >= btnY &&
            mouse.y <= btnY + btnH;

        while (
            const std::optional ev =
                window->pollEvent()
        ) {

            if (
                ev->is<
                    sf::Event::Closed
                >()
            ) {
                window->close();

                return false;
            }

            if (
                ev->is<
                    sf::Event::MouseButtonPressed
                >() &&
                hBtn
            ) {
                openDiscordLogin();
            }
        }

        window->clear(
            sf::Color(
                10,
                15,
                40
            )
        );

        if (hasBg)
        {
            window->draw(bg);
        }
        else
        {
            window->draw(
                roundRect(
                    cardX,
                    cardY,
                    cardW,
                    cardH,
                    14,
                    sf::Color(
                        0,
                        0,
                        0,
                        230
                    )
                )
            );
        }

        window->draw(title);

        if (hasBtn)
        {
            btn.setColor(
                hBtn
                    ? sf::Color(
                        200,
                        200,
                        200
                    )
                    : sf::Color::White
            );

            window->draw(btn);
        }
        else
        {
            window->draw(
                roundRect(
                    btnX,
                    btnY,
                    btnW,
                    btnH,
                    10,
                    hBtn
                        ? sf::Color(
                            90,
                            70,
                            180
                        )
                        : sf::Color(
                            114,
                            137,
                            218
                        )
                )
            );

            sf::Text dt(
                *fontTitle,
                "Login with Discord",
                26
            );

            dt.setFillColor(
                sf::Color::White
            );

            sf::FloatRect db =
                dt.getLocalBounds();

            dt.setPosition({
                btnX +
                    btnW / 2 -
                    db.size.x / 2,

                btnY +
                    btnH / 2 -
                    db.size.y
            });

            window->draw(dt);
        }

        window->display();
    }

    return false;
}


void progressScreen(
    sf::RenderWindow& w,
    sf::Font& fontTitle,
    sf::Font& fontText,
    bool& downloading,
    bool& extracting,
    bool& completed,
    const std::string& title
) {
    while (
        w.isOpen() &&
        !completed
    ) {

        while (
            const std::optional ev =
                w.pollEvent()
        ) {
            if (
                ev->is<
                    sf::Event::Closed
                >()
            ) {
                w.close();
                return;
            }
        }

        w.clear(
            sf::Color(10, 15, 40)
        );

        sf::Text t(
            fontTitle,
            title,
            26
        );

        t.setFillColor(
            sf::Color::White
        );

        sf::FloatRect tb =
            t.getLocalBounds();

        t.setPosition({
            WIN_W / 2.f -
                tb.size.x / 2.f,
            120
        });

        w.draw(t);

        if (downloading) {

            double pct =
                progressData.total > 0
                    ? progressData.downloaded /
                        progressData.total *
                        100
                    : 0;

            static double lastDl = 0;
            static sf::Clock spCk;

            double el =
                spCk.getElapsedTime()
                    .asSeconds();

            if (el >= 0.5f) {

                progressData.speed =
                    (
                        progressData.downloaded -
                        lastDl
                    ) / el;

                lastDl =
                    progressData.downloaded;

                spCk.restart();
            }

            w.draw(
                roundRect(
                    100,
                    300,
                    WIN_W - 200,
                    28,
                    8,
                    sf::Color(
                        50,
                        50,
                        70
                    )
                )
            );

            if (pct > 0.1) {

                w.draw(
                    roundRect(
                        100,
                        300,
                        (float)(
                            (WIN_W - 200) *
                            pct / 100
                        ),
                        28,
                        8,
                        sf::Color(
                            80,
                            120,
                            255
                        )
                    )
                );
            }

            std::ostringstream ss;

            ss
                << std::fixed
                << std::setprecision(1)
                << pct
                << "%";

            sf::Text pt(
                fontTitle,
                ss.str(),
                20
            );

            pt.setFillColor(
                sf::Color::White
            );

            sf::FloatRect pb =
                pt.getLocalBounds();

            pt.setPosition({
                WIN_W / 2.f -
                    pb.size.x / 2,
                304
            });

            w.draw(pt);

            sf::Text mb(
                fontText,
                fmtBytes(
                    progressData.downloaded
                ) +
                " - " +
                fmtBytes(
                    progressData.total
                ),
                18
            );

            mb.setFillColor(
                sf::Color(
                    180,
                    180,
                    255
                )
            );

            mb.setPosition({
                100,
                340
            });

            w.draw(mb);

            sf::Text sp(
                fontText,
                "Speed: " +
                fmtBytes(
                    progressData.speed
                ) +
                " per second",
                18
            );

            sp.setFillColor(
                sf::Color(
                    180,
                    180,
                    255
                )
            );

            sp.setPosition({
                100,
                368
            });

            w.draw(sp);

        } else if (extracting) {

            sf::Text st(
                fontTitle,
                "Extracting... please wait",
                24
            );

            st.setFillColor(
                sf::Color::White
            );

            sf::FloatRect sb =
                st.getLocalBounds();

            st.setPosition({
                WIN_W / 2.f -
                    sb.size.x / 2,
                300
            });

            w.draw(st);
        }

        w.display();
    }
}


#if defined(_WIN32)

bool extractZip(
    const std::string& zipPath,
    const std::string& destDir
) {
    mz_zip_archive zip{};

    if (
        !mz_zip_reader_init_file(
            &zip,
            zipPath.c_str(),
            0
        )
    )
        return false;

    bool ok = true;

    int count =
        (int)mz_zip_reader_get_num_files(
            &zip
        );

    for (int i = 0; i < count; i++) {

        mz_zip_archive_file_stat st;

        if (
            !mz_zip_reader_file_stat(
                &zip,
                i,
                &st
            )
        ) {
            ok = false;
            continue;
        }

        std::string outPath =
            destDir +
            st.m_filename;

        if (
            mz_zip_reader_is_file_a_directory(
                &zip,
                i
            )
        ) {
            fs::create_directories(
                outPath
            );

            continue;
        }

        fs::create_directories(
            fs::path(
                outPath
            ).parent_path()
        );

        if (
            !mz_zip_reader_extract_to_file(
                &zip,
                i,
                outPath.c_str(),
                0
            )
        )
            ok = false;
    }

    mz_zip_reader_end(
        &zip
    );

    return ok;
}

#endif


void downloadVersion(
    sf::RenderWindow& window,
    sf::Font& fontTitle,
    sf::Font& fontText,
    const Version& ver
) {
    progressData = {};

    bool downloading = true;
    bool extracting = false;
    bool completed = false;

#if defined(_WIN32)

    std::string archivePath =
        TEMP_PATH_() +
        "game.zip";

    /*
     * O ZIP contém:
     *
     * funkin-moon-windows-64bit/
     *     FunkinMoon.exe
     *
     * Então extraímos para:
     *
     * versions/v0.0.1/
     *
     * e o ZIP cria:
     *
     * versions/v0.0.1/
     *     funkin-moon-windows-64bit/
     */

    std::string destPath =
        VERSIONS_PATH_() +
        "v" +
        ver.number +
        "/";

#elif defined(__ANDROID__)

    std::string archivePath =
        TEMP_PATH_() +
        "game.apk";

#endif

    std::thread t([&]() {

        fs::create_directories(
            TEMP_PATH_()
        );

        bool ok =
            downloadToFile(
                ver.zipUrl,
                archivePath,
                &progressData,
                1024 * 1024
            );

        downloading = false;
        extracting = true;

        if (ok) {

#if defined(_WIN32)

            fs::create_directories(
                destPath
            );

            bool extracted =
                extractZip(
                    archivePath,
                    destPath
                );

            /*
             * Só marca como instalada se:
             *
             * 1. O ZIP foi extraído.
             * 2. O executável realmente existe.
             */

            if (extracted) {

                std::string installedPath =
                    "com.funkinmoon/versions/v" +
                    ver.number +
                    "/funkin-moon-windows-64bit";

                std::string exePath =
                    exeDir() +
                    installedPath +
                    "/" +
                    GAME_EXE_NAME;

                if (fs::exists(exePath)) {

                    saveInstalled(
                        ver.number,
                        installedPath
                    );
                }

                if (fs::exists(archivePath))
                    fs::remove(
                        archivePath
                    );

            } else {

                if (fs::exists(archivePath))
                    fs::remove(
                        archivePath
                    );
            }

#elif defined(__ANDROID__)

            androidInstallApk(
                archivePath
            );

#endif
        }

        extracting = false;
        completed = true;
    });

    progressScreen(
        window,
        fontTitle,
        fontText,
        downloading,
        extracting,
        completed,
        "Downloading " +
        ver.number +
        "..."
    );

    t.join();
}


void modsScreen(
    sf::RenderWindow& window,
    sf::Font& fontTitle,
    sf::Font& fontText
) {
    auto versions =
        loadVersions();

    /*
     * Recarregado depois de cada instalação.
     */
    auto installed =
        loadInstalled();

    auto mods =
        loadMods();

    int selectedVersion = 0;

    float winW =
        (float)WIN_W;

    float winH =
        (float)WIN_H;

    float listX =
        SIDEBAR_W + 20;

    float listY = 60;

    float listW = 280;

    float listH =
        winH -
        BOTTOMBAR_H -
        80;

    float modsX =
        listX +
        listW +
        20;

    float modsY =
        listY;

    float modsW =
        winW -
        modsX -
        20;

    float modsH =
        listH;

    float modRowH = 44;

    float modToggleW = 64;

    float modToggleH = 28;

    float playButtonX =
        winW / 2 -
        100;

    float playButtonY =
        winH -
        BOTTOMBAR_H +
        25;

    float playButtonW = 200;

    float playButtonH = 60;

    float backButtonX =
        SIDEBAR_W + 20;

    float backButtonY = 15;

    float backButtonW = 100;

    float backButtonH = 34;

    while (window.isOpen()) {

        sf::Vector2f mouse(
            sf::Mouse::getPosition(
                window
            )
        );

        bool hoverBack =
            mouse.x >= backButtonX &&
            mouse.x <=
                backButtonX +
                backButtonW &&
            mouse.y >= backButtonY &&
            mouse.y <=
                backButtonY +
                backButtonH;

        bool hoverPlay =
            mouse.x >= playButtonX &&
            mouse.x <=
                playButtonX +
                playButtonW &&
            mouse.y >= playButtonY &&
            mouse.y <=
                playButtonY +
                playButtonH;

        while (
            const std::optional ev =
                window.pollEvent()
        ) {

            if (
                ev->is<
                    sf::Event::Closed
                >()
            ) {
                window.close();
                return;
            }

            if (
                ev->is<
                    sf::Event::MouseButtonPressed
                >()
            ) {

                if (hoverBack)
                    return;

                for (
                    int i = 0;
                    i < (int)versions.size();
                    i++
                ) {

                    float iy =
                        listY +
                        i * 52;

                    if (
                        mouse.x >= listX &&
                        mouse.x <=
                            listX + listW &&
                        mouse.y >= iy &&
                        mouse.y <=
                            iy + 46
                    ) {
                        selectedVersion = i;
                    }
                }

                if (
                    hoverPlay &&
                    !versions.empty()
                ) {

                    if (
                        isVersionInstalled(
                            installed,
                            versions[
                                selectedVersion
                            ].number
                        )
                    ) {

                        launchVersion(
                            window,
                            installed,
                            versions[
                                selectedVersion
                            ].number
                        );

                    } else {

                        downloadVersion(
                            window,
                            fontTitle,
                            fontText,
                            versions[
                                selectedVersion
                            ]
                        );

                        installed =
                            loadInstalled();
                    }
                }

                for (
                    int i = 0;
                    i < (int)mods.size();
                    i++
                ) {

                    float iy =
                        modsY +
                        i * modRowH;

                    float tx =
                        modsX +
                        modsW -
                        modToggleW -
                        16;

                    float ty =
                        iy +
                        (
                            modRowH -
                            modToggleH
                        ) / 2.f;

                    if (
                        mouse.x >= tx &&
                        mouse.x <=
                            tx + modToggleW &&
                        mouse.y >= ty &&
                        mouse.y <=
                            ty + modToggleH
                    ) {

                        mods[i].enabled =
                            !mods[i].enabled;

                        saveModsState(
                            mods
                        );
                    }
                }
            }
        }

        window.clear(
            sf::Color(
                10,
                15,
                40
            )
        );

        window.draw(
            roundRect(
                0,
                winH -
                    BOTTOMBAR_H,
                winW,
                BOTTOMBAR_H,
                0,
                sf::Color(
                    0,
                    0,
                    0,
                    220
                )
            )
        );

        window.draw(
            roundRect(
                backButtonX,
                backButtonY,
                backButtonW,
                backButtonH,
                6,
                hoverBack
                    ? sf::Color(
                        60,
                        60,
                        80
                    )
                    : sf::Color(
                        40,
                        40,
                        60
                    )
            )
        );

        sf::Text vt(
            fontText,
            "Back",
            18
        );

        vt.setFillColor(
            sf::Color::White
        );

        vt.setPosition({
            backButtonX + 10,
            backButtonY + 7
        });

        window.draw(vt);

        sf::Text title(
            fontTitle,
            "Mods",
            34
        );

        title.setFillColor(
            sf::Color::White
        );

        title.setPosition({
            SIDEBAR_W + 140,
            15
        });

        window.draw(title);

        window.draw(
            roundRect(
                listX - 4,
                listY - 4,
                listW + 8,
                listH + 8,
                8,
                sf::Color(
                    20,
                    20,
                    40
                )
            )
        );

        for (
            int i = 0;
            i < (int)versions.size();
            i++
        ) {

            float iy =
                listY +
                i * 52;

            bool sel =
                i == selectedVersion;

            bool hov =
                mouse.x >= listX &&
                mouse.x <=
                    listX + listW &&
                mouse.y >= iy &&
                mouse.y <=
                    iy + 46;

            sf::Color cardCol =
                sel
                    ? sf::Color(
                        60,
                        80,
                        160
                    )
                    : hov
                        ? sf::Color(
                            40,
                            40,
                            70
                        )
                        : sf::Color(
                            25,
                            25,
                            50
                        );

            window.draw(
                roundRect(
                    listX,
                    iy,
                    listW,
                    46,
                    6,
                    cardCol
                )
            );

            sf::Color badgeCol =
                versions[i].state == "final"
                    ? sf::Color(
                        40,
                        160,
                        80
                    )
                    : versions[i].state ==
                        "pre_release"
                        ? sf::Color(
                            200,
                            140,
                            0
                        )
                        : sf::Color(
                            100,
                            60,
                            180
                        );

            std::string badgeStr =
                versions[i].state == "final"
                    ? "Final"
                    : versions[i].state ==
                        "pre_release"
                        ? "Pre-Release"
                        : "Moon Phase";

            window.draw(
                roundRect(
                    listX +
                        listW -
                        120,
                    iy + 8,
                    110,
                    28,
                    5,
                    badgeCol
                )
            );

            sf::Text badge(
                fontText,
                badgeStr,
                12
            );

            badge.setFillColor(
                sf::Color::White
            );

            sf::FloatRect bb =
                badge.getLocalBounds();

            badge.setPosition({
                listX +
                    listW -
                    120 +
                    (
                        110 -
                        bb.size.x
                    ) / 2,
                iy + 13
            });

            window.draw(badge);

            sf::Text vnum(
                fontTitle,
                versions[i].number,
                18
            );

            vnum.setFillColor(
                sf::Color::White
            );

            vnum.setPosition({
                listX + 10,
                iy + 6
            });

            window.draw(vnum);

            bool inst =
                isVersionInstalled(
                    installed,
                    versions[i].number
                );

            sf::Text instTxt(
                fontText,
                inst
                    ? "Installed"
                    : "Not installed",
                13
            );

            instTxt.setFillColor(
                inst
                    ? sf::Color(
                        80,
                        200,
                        80
                    )
                    : sf::Color(
                        160,
                        160,
                        160
                    )
            );

            instTxt.setPosition({
                listX + 10,
                iy + 28
            });

            window.draw(instTxt);
        }

        if (versions.empty()) {

            sf::Text empty(
                fontText,
                "No versions here...",
                18
            );

            empty.setFillColor(
                sf::Color(
                    150,
                    150,
                    180
                )
            );

            empty.setPosition({
                listX + 10,
                listY + 20
            });

            window.draw(empty);
        }

        window.draw(
            roundRect(
                modsX - 4,
                modsY - 4,
                modsW + 8,
                modsH + 8,
                8,
                sf::Color(
                    20,
                    20,
                    40
                )
            )
        );

        for (
            int i = 0;
            i < (int)mods.size();
            i++
        ) {

            float iy =
                modsY +
                i * modRowH;

            float tx =
                modsX +
                modsW -
                modToggleW -
                16;

            float ty =
                iy +
                (
                    modRowH -
                    modToggleH
                ) / 2.f;

            bool hovToggle =
                mouse.x >= tx &&
                mouse.x <=
                    tx + modToggleW &&
                mouse.y >= ty &&
                mouse.y <=
                    ty + modToggleH;

            sf::Text mName(
                fontText,
                mods[i].folder,
                16
            );

            mName.setFillColor(
                sf::Color(
                    220,
                    220,
                    230
                )
            );

            mName.setPosition({
                modsX + 16,
                iy +
                    (
                        modRowH -
                        20
                    ) / 2.f
            });

            window.draw(mName);

            sf::Color toggleCol =
                mods[i].enabled
                    ? (
                        hovToggle
                            ? sf::Color(
                                30,
                                150,
                                60
                            )
                            : sf::Color(
                                40,
                                180,
                                70
                            )
                    )
                    : (
                        hovToggle
                            ? sf::Color(
                                80,
                                80,
                                90
                            )
                            : sf::Color(
                                60,
                                60,
                                70
                            )
                    );

            window.draw(
                roundRect(
                    tx,
                    ty,
                    modToggleW,
                    modToggleH,
                    8,
                    toggleCol
                )
            );

            sf::Text tt(
                fontText,
                mods[i].enabled
                    ? "ON"
                    : "OFF",
                14
            );

            tt.setFillColor(
                sf::Color::White
            );

            sf::FloatRect ttb =
                tt.getLocalBounds();

            tt.setPosition({
                tx +
                    modToggleW / 2 -
                    ttb.size.x / 2,
                ty +
                    modToggleH / 2 -
                    ttb.size.y
            });

            window.draw(tt);
        }

        if (mods.empty()) {

            sf::Text empty(
                fontText,
                "No mods found. Drop mod folders into the mods folder.",
                16
            );

            empty.setFillColor(
                sf::Color(
                    100,
                    100,
                    140
                )
            );

            empty.setPosition({
                modsX + 16,
                modsY + 20
            });

            window.draw(empty);
        }

        if (!versions.empty()) {

            bool inst =
                isVersionInstalled(
                    installed,
                    versions[
                        selectedVersion
                    ].number
                );

            sf::Color playCol =
                inst
                    ? (
                        hoverPlay
                            ? sf::Color(
                                30,
                                120,
                                30
                            )
                            : sf::Color(
                                40,
                                160,
                                40
                            )
                    )
                    : (
                        hoverPlay
                            ? sf::Color(
                                120,
                                80,
                                20
                            )
                            : sf::Color(
                                160,
                                110,
                                20
                            )
                    );

            window.draw(
                roundRect(
                    playButtonX,
                    playButtonY,
                    playButtonW,
                    playButtonH,
                    10,
                    playCol
                )
            );

            sf::Text pt(
                fontTitle,
                inst
                    ? "PLAY"
                    : "DOWNLOAD",
                26
            );

            pt.setFillColor(
                sf::Color::White
            );

            sf::FloatRect pb =
                pt.getLocalBounds();

            pt.setPosition({
                playButtonX +
                    playButtonW / 2 -
                    pb.size.x / 2,
                playButtonY +
                    playButtonH / 2 -
                    pb.size.y
            });

            window.draw(pt);
        }

        window.display();
    }
}



void changelogScreen(
    sf::RenderWindow& window,
    sf::Font& fontTitle,
    sf::Font& fontText
) {
    std::string content =
        readTextFile(
            A_NEWS +
            "changelog.md"
        );

    if (content.empty())
        content =
            "## Changelog\n"
            "No changelog found.";

    float scroll = 0.f;

    while (window.isOpen()) {

        while (
            const std::optional ev =
                window.pollEvent()
        ) {

            if (
                ev->is<
                    sf::Event::Closed
                >()
            ) {
                window.close();
                return;
            }

            if (
                const auto* k =
                    ev->getIf<
                        sf::Event::KeyPressed
                    >()
            ) {
                if (
                    k->code ==
                    sf::Keyboard::Key::Escape
                )
                    return;
            }

            if (
                const auto* we =
                    ev->getIf<
                        sf::Event::MouseWheelScrolled
                    >()
            ) {
                scroll =
                    std::max(
                        0.f,
                        scroll -
                            we->delta * 30.f
                    );
            }
        }

        window.clear(
            sf::Color(
                10,
                15,
                40
            )
        );

        float y =
            60.f -
            scroll;

        std::istringstream stream(
            content
        );

        std::string line;

        while (
            std::getline(
                stream,
                line
            )
        ) {

            if (
                y > -60 &&
                y < (float)WIN_H + 60
            ) {

                if (
                    line.size() >= 3 &&
                    line.substr(0, 3) == "## "
                ) {

                    sf::Text t(
                        fontTitle,
                        line.substr(3),
                        28
                    );

                    t.setFillColor(
                        sf::Color(
                            180,
                            180,
                            255
                        )
                    );

                    t.setPosition({
                        SIDEBAR_W + 20,
                        y
                    });

                    window.draw(t);

                    y += 42;

                } else if (
                    line.size() >= 2 &&
                    line.substr(0, 2) == "# "
                ) {

                    sf::Text t(
                        fontTitle,
                        line.substr(2),
                        36
                    );

                    t.setFillColor(
                        sf::Color::White
                    );

                    t.setPosition({
                        SIDEBAR_W + 20,
                        y
                    });

                    window.draw(t);

                    y += 52;

                } else if (
                    !line.empty()
                ) {

                    sf::Text t(
                        fontText,
                        line,
                        20
                    );

                    t.setFillColor(
                        sf::Color(
                            210,
                            210,
                            210
                        )
                    );

                    t.setPosition({
                        SIDEBAR_W + 20,
                        y
                    });

                    window.draw(t);

                    y += 28;

                } else {
                    y += 12;
                }

            } else {

                if (
                    line.size() >= 3 &&
                    line.substr(0, 3) == "## "
                )
                    y += 42;

                else if (
                    line.size() >= 2 &&
                    line.substr(0, 2) == "# "
                )
                    y += 52;

                else if (
                    !line.empty()
                )
                    y += 28;

                else
                    y += 12;
            }
        }

        sf::Text title(
            fontTitle,
            "Changelog - v0.1.0",
            32
        );

        title.setFillColor(
            sf::Color::White
        );

        title.setPosition({
            SIDEBAR_W + 20,
            15
        });

        window.draw(title);

        sf::Text back(
            fontText,
            "ESC to back",
            18
        );

        back.setFillColor(
            sf::Color(
                100,
                100,
                120
            )
        );

        back.setPosition({
            SIDEBAR_W + 20,
            (float)WIN_H - 30
        });

        window.draw(back);

        window.display();
    }
}


void optionsScreen(
    sf::RenderWindow& window,
    sf::Font& fontTitle,
    sf::Font& fontText,
    sf::Texture& avatarTex,
    bool hasAvatar
) {
    float winW =
        (float)WIN_W;

    float winH =
        (float)WIN_H;

    sf::Texture circTex;

    bool hasCirc = false;

    if (hasAvatar) {
        circTex =
            makeCircleTexture(
                avatarTex,
                120
            );

        hasCirc = true;
    }

    sf::Sprite avatarSpr(
        hasCirc
            ? circTex
            : avatarTex
    );

    float avatarX =
        SIDEBAR_W + 40;

    float avatarY = 80;

    float logoutButtonX =
        winW / 2 - 150;

    float logoutButtonY =
        winH -
        BOTTOMBAR_H -
        70;

    float logoutButtonW = 300;
    float logoutButtonH = 50;

    while (window.isOpen()) {

        sf::Vector2f mouse(
            sf::Mouse::getPosition(
                window
            )
        );

        bool hoverLogout =
            mouse.x >= logoutButtonX &&
            mouse.x <=
                logoutButtonX +
                logoutButtonW &&
            mouse.y >= logoutButtonY &&
            mouse.y <=
                logoutButtonY +
                logoutButtonH;

        while (
            const std::optional ev =
                window.pollEvent()
        ) {

            if (
                ev->is<
                    sf::Event::Closed
                >()
            ) {
                window.close();
                return;
            }

            if (
                const auto* k =
                    ev->getIf<
                        sf::Event::KeyPressed
                    >()
            ) {
                if (
                    k->code ==
                    sf::Keyboard::Key::Escape
                )
                    return;
            }

            if (
                ev->is<
                    sf::Event::MouseButtonPressed
                >() &&
                hoverLogout
            ) {

                currentUser =
                    UserData();

                if (
                    fs::exists(
                        SAVE_FILE_()
                    )
                )
                    fs::remove(
                        SAVE_FILE_()
                    );

                window.close();

                return;
            }
        }

        window.clear(
            sf::Color(
                10,
                15,
                40
            )
        );

        sf::Text title(
            fontTitle,
            "Settings",
            34
        );

        title.setFillColor(
            sf::Color::White
        );

        title.setPosition({
            SIDEBAR_W + 20,
            15
        });

        window.draw(title);

        if (hasCirc) {

            avatarSpr.setPosition({
                avatarX,
                avatarY
            });

            window.draw(
                avatarSpr
            );

        } else {

            window.draw(
                roundRect(
                    avatarX,
                    avatarY,
                    120,
                    120,
                    60,
                    sf::Color(
                        60,
                        60,
                        80
                    )
                )
            );
        }

        sf::Text nameText(
            fontTitle,
            currentUser.name,
            30
        );

        nameText.setFillColor(
            sf::Color::White
        );

        nameText.setPosition({
            avatarX + 140,
            avatarY + 10
        });

        window.draw(nameText);

        sf::Text nicknameText(
            fontText,
            "@" +
                currentUser.nickname,
            22
        );

        nicknameText.setFillColor(
            sf::Color(
                160,
                160,
                180
            )
        );

        nicknameText.setPosition({
            avatarX + 140,
            avatarY + 52
        });

        window.draw(
            nicknameText
        );

        sf::Text idText(
            fontText,
            "ID: " +
                currentUser.id,
            18
        );

        idText.setFillColor(
            sf::Color(
                100,
                100,
                120
            )
        );

        idText.setPosition({
            avatarX + 140,
            avatarY + 82
        });

        window.draw(idText);

        window.draw(
            roundRect(
                logoutButtonX,
                logoutButtonY,
                logoutButtonW,
                logoutButtonH,
                8,

                hoverLogout
                    ? sf::Color(
                        200,
                        40,
                        40
                    )
                    : sf::Color(
                        150,
                        30,
                        30
                    )
            )
        );

        sf::Text logout(
            fontTitle,
            "Exit to Account",
            22
        );

        logout.setFillColor(
            sf::Color::White
        );

        sf::FloatRect sb =
            logout.getLocalBounds();

        logout.setPosition({
            logoutButtonX +
                logoutButtonW / 2 -
                sb.size.x / 2,

            logoutButtonY +
                logoutButtonH / 2 -
                sb.size.y
        });

        window.draw(logout);

        sf::Text back(
            fontText,
            "ESC to Back",
            18
        );

        back.setFillColor(
            sf::Color(
                100,
                100,
                120
            )
        );

        back.setPosition({
            SIDEBAR_W + 20,
            winH - 30
        });

        window.draw(back);

        window.display();
    }
}


void homeScreen(
    sf::RenderWindow& window,
    sf::Font& fontTitle,
    sf::Font& fontText
) {
    float winW =
        (float)WIN_W;

    float winH =
        (float)WIN_H;

    const float SB_W = 185.f;
    const float BT_H = 120.f;

    const float SB_H =
        winH - BT_H;

    const float ICON_S = 110.f;
    const float OPT_S = 110.f;

    const float ICON_X =
        SB_W / 2.f -
        ICON_S / 2.f;

    (void)ICON_X;

    float iconY1 = 20.f;
    float iconY2 = 150.f;
    float iconY3 = 280.f;
    float iconY4 = 410.f;

    float AVT_S = 80.f;

    float AVT_X =
        SB_W / 2.f -
        AVT_S / 2.f;

    float AVT_Y =
        winH -
        BT_H +
        8.f;

    sf::Texture bgTex;
    sf::Texture playTex;
    sf::Texture leftBarTex;
    sf::Texture downBarTex;

    sf::Texture modTex;
    sf::Texture webTex;
    sf::Texture newsTex;
    sf::Texture optTex;

    sf::Texture avatarTex;
    sf::Texture fallbackTex;

    sf::Texture blackBarsTex;

    bool hasBg =
        bgTex.loadFromFile(
            A_HOME +
            "background.png"
        );

    if (!hasBg)
        hasBg =
            bgTex.loadFromFile(
                A_HOME +
                "bg.png"
            );

    bool hasPlay =
        playTex.loadFromFile(
            A_HOME +
            "playButton.png"
        );

    bool hasLeft =
        leftBarTex.loadFromFile(
            A_HOME +
            "leftblackBar.png"
        );

    bool hasDown =
        downBarTex.loadFromFile(
            A_HOME +
            "downblackBar.png"
        );

    bool hasMod =
        modTex.loadFromFile(
            A_HOME +
            "modsIconB.png"
        );

    bool hasWeb =
        webTex.loadFromFile(
            A_HOME +
            "websiteIcon.png"
        );

    bool hasNews =
        newsTex.loadFromFile(
            A_HOME +
            "newsIconB.png"
        );

    bool hasOpt =
        optTex.loadFromFile(
            A_HOME +
            "optionsIcon.png"
        );

    if (hasMod)
        modTex.setSmooth(true);

    if (hasWeb)
        webTex.setSmooth(true);

    if (hasNews)
        newsTex.setSmooth(true);

    if (hasOpt)
        optTex.setSmooth(true);

    bool hasFall =
        fallbackTex.loadFromFile(
            A_HOME +
            "fallback-user-icon.png"
        );

    bool hasBlackBars =
        blackBarsTex.loadFromFile(
            A_HOME +
            "blackBars.png"
        );

    bool hasAvImg = false;

    if (
        !currentUser.avatarUrl.empty()
    ) {

        std::string ap =
            BASE_PATH_() +
            "data/avatar_temp.png";

        if (
            downloadToFile(
                currentUser.avatarUrl,
                ap
            )
        )
            hasAvImg =
                avatarTex.loadFromFile(ap);
    }

    sf::Texture& userTex =
        hasAvImg
            ? avatarTex
            : fallbackTex;

    bool hasUser =
        hasAvImg ||
        hasFall;

    sf::Texture circTex;

    bool hasCirc = false;

    if (hasUser) {

        circTex =
            makeCircleTexture(
                userTex,
                (unsigned int)AVT_S
            );

        hasCirc = true;
    }

    sf::Sprite bgSpr(bgTex);
    sf::Sprite leftSpr(leftBarTex);
    sf::Sprite downSpr(downBarTex);
    sf::Sprite playSpr(playTex);

    sf::Sprite modSpr(modTex);
    sf::Sprite webSpr(webTex);
    sf::Sprite newsSpr(newsTex);
    sf::Sprite optSpr(optTex);

    sf::Sprite avatarSpr(
        hasCirc
            ? circTex
            : userTex
    );

    sf::Sprite blackBarsSpr(
        blackBarsTex
    );

    if (hasBg) {

        auto sz =
            bgTex.getSize();

        bgSpr.setScale({
            winW / (float)sz.x,
            winH / (float)sz.y
        });

        bgSpr.setPosition({
            0,
            0
        });
    }

    if (hasLeft) {

        auto sz =
            leftBarTex.getSize();

        leftSpr.setScale({
            SB_W / (float)sz.x,
            SB_H / (float)sz.y
        });

        leftSpr.setPosition({
            0,
            0
        });
    }

    if (hasDown) {

        auto sz =
            downBarTex.getSize();

        downSpr.setScale({
            winW / (float)sz.x,
            BT_H / (float)sz.y
        });

        downSpr.setPosition({
            0,
            winH - BT_H
        });
    }

    if (hasBlackBars)
        blackBarsSpr.setPosition({
            0,
            0
        });

    auto scaleToSize =
        [&](sf::Sprite& spr,
            sf::Texture& tex,
            float targetSize) {

            auto sz =
                tex.getSize();

            float sc =
                targetSize /
                std::max(
                    (float)sz.x,
                    (float)sz.y
                );

            spr.setScale({
                sc,
                sc
            });

            float w =
                (float)sz.x * sc;

            spr.setPosition({
                SB_W / 2.f -
                    w / 2.f,
                0
            });
        };

    if (hasMod) {

        scaleToSize(
            modSpr,
            modTex,
            ICON_S
        );

        modSpr.setPosition({
            SB_W / 2.f -
                modSpr.getGlobalBounds()
                    .size.x / 2.f,
            iconY1
        });
    }

    if (hasWeb) {

        scaleToSize(
            webSpr,
            webTex,
            ICON_S
        );

        webSpr.setPosition({
            SB_W / 2.f -
                webSpr.getGlobalBounds()
                    .size.x / 2.f,
            iconY2
        });
    }

    if (hasNews) {

        scaleToSize(
            newsSpr,
            newsTex,
            ICON_S
        );

        newsSpr.setPosition({
            SB_W / 2.f -
                newsSpr.getGlobalBounds()
                    .size.x / 2.f,
            iconY3
        });
    }

    if (hasOpt) {

        scaleToSize(
            optSpr,
            optTex,
            OPT_S
        );

        optSpr.setPosition({
            SB_W / 2.f -
                optSpr.getGlobalBounds()
                    .size.x / 2.f,
            iconY4
        });
    }

    avatarSpr.setPosition({
        AVT_X,
        AVT_Y
    });

    float playW = 0;
    float playH = 0;
    float playX = 0;
    float playY = 0;

    if (hasPlay) {

        auto sz =
            playTex.getSize();

        float maxH =
            BT_H - 16.f;

        float sc =
            maxH /
            (float)sz.y;

        if (
            (float)sz.x * sc >
            500.f
        )
            sc =
                500.f /
                (float)sz.x;

        playSpr.setScale({
            sc,
            sc
        });

        playW =
            (float)sz.x * sc;

        playH =
            (float)sz.y * sc;

        playX =
            winW / 2.f -
            playW / 2.f;

        playY =
            winH -
            BT_H +
            (BT_H - playH) /
                2.f;

        playSpr.setPosition({
            playX,
            playY
        });

    } else {

        playW = 260;
        playH = 70;

        playX =
            winW / 2.f -
            playW / 2.f;

        playY =
            winH -
            BT_H +
            (BT_H - playH) /
                2.f;
    }

    auto iconBounds =
        [&](float y,
            float size) -> bool {

            sf::Vector2f m(
                sf::Mouse::getPosition(
                    window
                )
            );

            return
                m.x >= 0 &&
                m.x <= SB_W &&
                m.y >= y &&
                m.y <= y + size;
        };

    while (window.isOpen()) {

        sf::Vector2f mouse(
            sf::Mouse::getPosition(
                window
            )
        );

        bool hMod =
            iconBounds(
                iconY1,
                ICON_S
            );

        bool hWeb =
            iconBounds(
                iconY2,
                ICON_S
            );

        bool hNews =
            iconBounds(
                iconY3,
                ICON_S
            );

        bool hOpt =
            iconBounds(
                iconY4,
                OPT_S
            );

        bool hPlay =
            mouse.x >= playX &&
            mouse.x <=
                playX + playW &&
            mouse.y >= playY &&
            mouse.y <=
                playY + playH;

        while (
            const std::optional ev =
                window.pollEvent()
        ) {

            if (
                ev->is<
                    sf::Event::Closed
                >()
            ) {
                window.close();
                return;
            }

            if (
                ev->is<
                    sf::Event::MouseButtonPressed
                >()
            ) {

                if (hMod)
                    modsScreen(
                        window,
                        fontTitle,
                        fontText
                    );

                if (hNews)
                    changelogScreen(
                        window,
                        fontTitle,
                        fontText
                    );

                if (hOpt)
                    optionsScreen(
                        window,
                        fontTitle,
                        fontText,
                        hasAvImg
                            ? avatarTex
                            : fallbackTex,
                        hasUser
                    );

                if (hPlay) {

                    auto versions =
                        loadVersions();

                    auto installed =
                        loadInstalled();

                    bool launched = false;

                    for (auto& v :
                         versions) {

                        if (
                            isVersionInstalled(
                                installed,
                                v.number
                            )
                        ) {

                            launchVersion(
                                window,
                                installed,
                                v.number
                            );

                            launched = true;

                            break;
                        }
                    }

                    if (!launched)
                        modsScreen(
                            window,
                            fontTitle,
                            fontText
                        );
                }
            }
        }

        window.clear(
            sf::Color(
                5,
                10,
                35
            )
        );

        if (hasBg)
            window.draw(bgSpr);

        if (hasLeft)
            window.draw(leftSpr);
        else
            window.draw(
                roundRect(
                    0,
                    0,
                    SB_W,
                    SB_H,
                    0,
                    sf::Color(
                        0,
                        0,
                        0,
                        220
                    )
                )
            );

        if (hasDown)
            window.draw(downSpr);
        else
            window.draw(
                roundRect(
                    0,
                    winH - BT_H,
                    winW,
                    BT_H,
                    0,
                    sf::Color(
                        0,
                        0,
                        0,
                        220
                    )
                )
            );

        if (hasBlackBars)
            window.draw(
                blackBarsSpr
            );

        auto drawIcon =
            [&](sf::Sprite& spr,
                bool hov,
                bool has) {

                if (!has)
                    return;

                spr.setColor(
                    hov
                        ? sf::Color(
                            160,
                            160,
                            160
                        )
                        : sf::Color::White
                );

                window.draw(spr);
            };

        drawIcon(
            modSpr,
            hMod,
            hasMod
        );

        drawIcon(
            webSpr,
            hWeb,
            hasWeb
        );

        drawIcon(
            newsSpr,
            hNews,
            hasNews
        );

        drawIcon(
            optSpr,
            hOpt,
            hasOpt
        );

        if (hasCirc)
            window.draw(
                avatarSpr
            );
        else if (hasUser)
            window.draw(
                avatarSpr
            );

        sf::Text nameText(
            fontText,
            currentUser.name,
            14
        );

        nameText.setFillColor(
            sf::Color(
                200,
                200,
                200
            )
        );

        sf::FloatRect nb =
            nameText.getLocalBounds();

        nameText.setPosition({
            SB_W / 2.f -
                nb.size.x / 2.f,

            AVT_Y +
                AVT_S +
                4
        });

        window.draw(nameText);

        if (hasPlay) {

            playSpr.setColor(
                hPlay
                    ? sf::Color(
                        180,
                        180,
                        180
                    )
                    : sf::Color::White
            );

            window.draw(playSpr);

        } else {

            window.draw(
                roundRect(
                    playX,
                    playY,
                    playW,
                    playH,
                    12,

                    hPlay
                        ? sf::Color(
                            40,
                            130,
                            40
                        )
                        : sf::Color(
                            50,
                            170,
                            50
                        )
                )
            );

            sf::Text pt(
                fontTitle,
                "PLAY",
                32
            );

            pt.setFillColor(
                sf::Color::White
            );

            sf::FloatRect pb =
                pt.getLocalBounds();

            pt.setPosition({
                playX +
                    playW / 2 -
                    pb.size.x / 2,

                playY +
                    playH / 2 -
                    pb.size.y
            });

            window.draw(pt);
        }

        window.display();
    }
}


int main() {

    sf::RenderWindow window(
        sf::VideoMode({
            WIN_W,
            WIN_H
        }),
        "Moon Launcher (Dev Build)",
        sf::Style::Titlebar |
        sf::Style::Close
    );

    window.setFramerateLimit(60);

    sf::Image icon;

    if (
        icon.loadFromFile(
            A_ICON
        )
    ) {

        auto size =
            icon.getSize();

        window.setIcon(
            size,
            icon.getPixelsPtr()
        );
    }

    sf::Font fontTitle;
    sf::Font fontText;
    sf::Font fontMono;

    fontTitle.openFromFile(
        A_FONTS +
        "FunkinOptions.otf"
    );

    fontText.openFromFile(
        A_FONTS +
        "FunkinLingLong.otf"
    );

    fontMono.openFromFile(
        A_FONTS +
        "VcrMono.ttf"
    );

    fs::create_directories(
        VERSIONS_PATH_()
    );

    fs::create_directories(
        SAVES_PATH_()
    );

    fs::create_directories(
        TEMP_PATH_()
    );

    bool loggedIn =
        loadUser();

    if (!loggedIn) {

        loggedIn =
            loginScreen(
                &window,
                &fontTitle,
                &fontText
            );

        if (!loggedIn)
            return 0;

        if (!window.isOpen())
            return 0;

        loadingScreen(
            window,
            fontTitle
        );
    }

    if (
        window.isOpen() &&
        loggedIn
    )
        homeScreen(
            window,
            fontTitle,
            fontText
        );

    return 0;
}