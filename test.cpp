#include <curl/curl.h>
#include <string>
#include <iostream>
#include <fstream>

size_t writeStringCallback(void* ptr, size_t size, size_t nmemb, std::string* str) {
    str->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

int main() {
    std::string result;
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://gamebanana.com/apiv11/Mod/Index?_nPage=1&_aCategoryRowIds[]=29202&_nPerpage=2");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }

    std::ofstream f("api_response.txt");
    f << result;
    f.close();

    std::cout << "Salvo em api_response.txt" << std::endl;
    std::cout << "Tamanho: " << result.size() << " bytes" << std::endl;
    return 0;
}