#include <SFML/Graphics.hpp>
#include <curl/curl.h>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <atomic>
#include <windows.h>

namespace fs = std::filesystem;

// ======= CONSTANTES =======
const unsigned int WIN_W = 1280;
const unsigned int WIN_H = 720;
const float SIDEBAR_W    = 185.f;
const float BOTTOMBAR_H  = 120.f;

// ======= CAMINHOS (baseados na pasta do .exe, não no diretório atual) =======
// Isso evita que o launcher perca o login/salvamento quando é aberto
// de um jeito diferente (duplo clique vs terminal vs atalho), pois antes
// os caminhos eram relativos ao diretório de trabalho (cwd), que pode mudar.
std::string exeDir() {
    static std::string dir = [](){
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string path(buffer);
        size_t pos = path.find_last_of("\\/");
        return (pos==std::string::npos) ? std::string("") : path.substr(0, pos+1);
    }();
    return dir;
}

std::string BASE_PATH_()     { return exeDir() + "com.funkinmoon\\"; }
std::string VERSIONS_PATH_() { return BASE_PATH_() + "versions\\"; }
std::string SAVES_PATH_()    { return BASE_PATH_() + "data\\saves\\"; }
std::string TEMP_PATH_()     { return BASE_PATH_() + "temp_extract\\"; }
std::string SAVE_FILE_()     { return SAVES_PATH_() + "user.json"; }

const std::string VERSIONS_JSON = "versions.json";
const std::string GAME_EXE_NAME = "FunkinMoon.exe";

const std::string A_UI      = "assets\\ui\\";
const std::string A_HOME    = A_UI + "home\\";
const std::string A_LOGIN   = A_UI + "login\\";
const std::string A_LOADING = A_UI + "loading\\";
const std::string A_NEWS    = A_UI + "news\\docs\\";
const std::string A_FONTS   = A_UI + "fonts\\";
const std::string A_ICON    = "assets\\app\\icons\\icon-moon.png";

// ======= STRUCTS =======
struct ProgressData { double downloaded=0, total=0, speed=0; };
ProgressData progressData;

struct Version {
    std::string numero;
    std::string zipUrl;
    std::string estado; // moon_phase, pre_release, final
};

struct UserData {
    std::string nome, apelido, id, avatarUrl;
    bool logado = false;
};
UserData currentUser;

// ======= CURL =======
size_t writeFileCb(void* p, size_t s, size_t n, FILE* f) { return fwrite(p,s,n,f); }
size_t writeStrCb(void* p, size_t s, size_t n, std::string* str) {
    str->append((char*)p,s*n); return s*n;
}
int progressCb(void* p, curl_off_t dlt, curl_off_t dln, curl_off_t, curl_off_t) {
    auto* d=(ProgressData*)p; d->downloaded=(double)dln; d->total=(double)dlt; return 0;
}
std::string curlGet(const std::string& url) {
    std::string res;
    CURL* c=curl_easy_init();
    if(c){
        curl_easy_setopt(c,CURLOPT_URL,url.c_str());
        curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,writeStrCb);
        curl_easy_setopt(c,CURLOPT_WRITEDATA,&res);
        curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,0L);
        curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,0L);
        curl_easy_setopt(c,CURLOPT_USERAGENT,"Mozilla/5.0");
        curl_easy_setopt(c,CURLOPT_TIMEOUT,15L);
        curl_easy_perform(c);
        curl_easy_cleanup(c);
    }
    return res;
}

// ======= JSON =======
std::string jStr(const std::string& json, const std::string& key) {
    std::string s="\""+key+"\":\"";
    size_t p=json.find(s); if(p==std::string::npos) return "";
    p+=s.size(); size_t e=json.find("\"",p); if(e==std::string::npos) return "";
    std::string v=json.substr(p,e-p),out;
    for(size_t i=0;i<v.size();i++){
        if(v[i]=='\\'&&i+1<v.size()){
            if(v[i+1]=='/'){out+='/';i++;}
            else if(v[i+1]=='n'){out+='\n';i++;}
            else if(v[i+1]=='"'){out+='"';i++;}
            else out+=v[i];
        } else out+=v[i];
    }
    return out;
}

// ======= SAVES =======
void salvarUser() {
    fs::create_directories(SAVES_PATH_());
    std::ofstream f(SAVE_FILE_());
    if(!f.is_open()){
        // Se não abriu, avisa no console (útil pra debugar em terminal)
        printf("[ERRO] Nao foi possivel escrever em: %s\n", SAVE_FILE_().c_str());
        return;
    }
    f<<"{\n  \"nome\":\""<<currentUser.nome<<"\",\n  \"apelido\":\""
     <<currentUser.apelido<<"\",\n  \"id\":\""<<currentUser.id
     <<"\",\n  \"avatarUrl\":\""<<currentUser.avatarUrl<<"\"\n}\n";
    f.close();
    printf("[OK] Save escrito em: %s\n", SAVE_FILE_().c_str());
}
bool carregarUser() {
    std::string savePath = SAVE_FILE_();
    if(!fs::exists(savePath)){
        printf("[INFO] Nenhum save encontrado em: %s\n", savePath.c_str());
        return false;
    }
    std::ifstream f(savePath);
    std::string json((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    currentUser.nome=jStr(json,"nome"); currentUser.apelido=jStr(json,"apelido");
    currentUser.id=jStr(json,"id");     currentUser.avatarUrl=jStr(json,"avatarUrl");
    currentUser.logado=!currentUser.nome.empty();
    printf("[OK] Save carregado de: %s (nome='%s')\n", savePath.c_str(), currentUser.nome.c_str());
    return currentUser.logado;
}

// ======= VERSOES =======
std::vector<Version> carregarVersions() {
    std::vector<Version> vers;
    std::ifstream f(VERSIONS_JSON);
    if(!f.is_open()) return vers;
    std::string json((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    std::string token="{\"numero\":";
    size_t pos=0;
    while((pos=json.find(token,pos))!=std::string::npos){
        size_t end=json.find(token,pos+token.size());
        if(end==std::string::npos) end=json.size();
        std::string chunk=json.substr(pos,end-pos);
        pos+=token.size();
        Version v;
        v.numero=jStr(chunk,"numero"); v.zipUrl=jStr(chunk,"zipUrl"); v.estado=jStr(chunk,"estado");
        if(!v.numero.empty()) vers.push_back(v);
    }
    return vers;
}

// ======= INSTALLED.JSON (versões instaladas localmente) =======
struct InstalledEntry {
    std::string version;
    std::string path;      // caminho relativo à pasta do .exe do launcher
    bool installed = false;
};

std::string INSTALLED_FILE_() { return BASE_PATH_() + "save.json"; }

bool jBool(const std::string& json, const std::string& key) {
    std::string s = "\"" + key + "\":";
    size_t p = json.find(s);
    if (p == std::string::npos) return false;
    p += s.size();
    while (p < json.size() && (json[p]==' ' || json[p]=='\t')) p++;
    return json.compare(p, 4, "true") == 0;
}

// Lê um array de objetos: [{ "version":"...", "installed":true, "path":"..." }, ...]
std::vector<InstalledEntry> carregarInstalled() {
    std::vector<InstalledEntry> out;
    std::string path = INSTALLED_FILE_();
    if (!fs::exists(path)) {
        printf("[INFO] Nenhum installed.json encontrado em: %s\n", path.c_str());
        return out;
    }
    std::ifstream f(path);
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    size_t pos = 0;
    while ((pos = json.find('{', pos)) != std::string::npos) {
        size_t end = json.find('}', pos);
        if (end == std::string::npos) break;
        std::string chunk = json.substr(pos, end - pos + 1);
        pos = end + 1;

        InstalledEntry e;
        e.version   = jStr(chunk, "version");
        e.path      = jStr(chunk, "path");
        e.installed = jBool(chunk, "installed");
        if (!e.version.empty()) out.push_back(e);
    }
    printf("[OK] installed.json carregado (%d entradas) de: %s\n", (int)out.size(), path.c_str());
    return out;
}

// Resolve o .exe de uma versão usando o installed.json; se não achar (ou não
// estiver marcada como instalada), cai no caminho antigo baseado em VERSIONS_PATH_().
std::string resolveExePath(const std::vector<InstalledEntry>& installed, const std::string& numero) {
    for (auto& e : installed) {
        if (e.version == numero && e.installed) {
            std::string full = exeDir() + e.path + "\\" + GAME_EXE_NAME;
            if (fs::exists(full)) return full;
        }
    }
    return VERSIONS_PATH_() + numero + "\\" + GAME_EXE_NAME;
}

// ======= HELPERS =======
std::string fmtBytes(double b) {
    std::ostringstream ss;
    if(b>=1024*1024) ss<<std::fixed<<std::setprecision(1)<<b/1024/1024<<" MB";
    else if(b>=1024) ss<<std::fixed<<std::setprecision(1)<<b/1024<<" KB";
    else ss<<(int)b<<" B";
    return ss.str();
}

// ======= DRAW =======
sf::VertexArray roundRect(float x,float y,float w,float h,float r,sf::Color col){
    sf::VertexArray s(sf::PrimitiveType::TriangleFan);
    int n=20;
    s.append({{x+w/2,y+h/2},col});
    float ang[]={180,270,0,90};
    float ox[]={x+r,x+w-r,x+w-r,x+r};
    float oy[]={y+r,y+r,y+h-r,y+h-r};
    for(int c=0;c<4;c++)
        for(int i=0;i<=n;i++){
            float a=(ang[c]+i*90.f/n)*3.14159f/180.f;
            s.append({{ox[c]+r*cosf(a),oy[c]+r*sinf(a)},col});
        }
    float a=180*3.14159f/180.f;
    s.append({{ox[0]+r*cosf(a),oy[0]+r*sinf(a)},col});
    return s;
}

// Foto redonda usando RenderTexture com máscara circular
sf::Texture makeCircleTexture(sf::Texture& src, unsigned int size) {
    sf::RenderTexture rt;
    rt.resize({size,size});
    rt.clear(sf::Color::Transparent);

    // Círculo de máscara
    sf::CircleShape mask((float)size/2);
    mask.setPosition({0,0});

    // Sprite da textura original escalado
    sf::Sprite spr(src);
    auto srcSz = src.getSize();
    float scale = (float)size / std::min((float)srcSz.x,(float)srcSz.y);
    spr.setScale({scale,scale});
    // Centralizar
    spr.setPosition({((float)size - srcSz.x*scale)/2.f, ((float)size - srcSz.y*scale)/2.f});

    // Usar stencil via sf::BlendMode não é direto no SFML 3
    // Solução: desenhar círculo branco, depois sprite com multiply
    // Alternativa simples: desenhar sprite dentro do círculo usando sf::View recortada

    // Desenhar sprite diretamente (sem clipar) e depois overlay do círculo vazado
    // Para SFML 3 a forma mais simples é usar um CircleShape com texture
    sf::CircleShape circle((float)size/2);
    circle.setPosition({0,0});
    circle.setTexture(&src);
    // Mapear a textura para o círculo
    circle.setTextureRect(sf::IntRect({0,0},{(int)srcSz.x,(int)srcSz.y}));

    rt.draw(circle);
    rt.display();
    return rt.getTexture();
}

// ======= TELA LOADING =======
void telaLoading(sf::RenderWindow& window, sf::Font& fontTitle) {
    sf::Texture bgTex, moonTex;
    bool hasBg   = bgTex.loadFromFile(A_LOADING+"loadingBackCard.png");
    bool hasMoon = moonTex.loadFromFile(A_LOADING+"moonLoading.png");

    sf::Sprite bg(bgTex), moon(moonTex);
    if(hasBg){
        auto sz=bgTex.getSize();
        float sx=(float)WIN_W/sz.x, sy=(float)WIN_H/sz.y;
        bg.setScale({sx,sy});
    }
    if(hasMoon){
        auto sz=moonTex.getSize();
        moon.setOrigin({sz.x/2.f,sz.y/2.f});
        moon.setPosition({WIN_W/2.f,WIN_H/2.f+30});
        float scale=150.f/std::max((float)sz.x,(float)sz.y);
        moon.setScale({scale,scale});
    }

    sf::Text t(fontTitle,"",44);
    t.setFillColor(sf::Color::White);
    sf::FloatRect tb=t.getLocalBounds();
    t.setPosition({WIN_W/2.f-tb.size.x/2.f, WIN_H/2.f-160});

    sf::Clock clk;
    float rot=0.f, dur=2.5f;

    while(window.isOpen()){
        float dt=clk.restart().asSeconds();
        rot+=120.f*dt;
        if(hasMoon) moon.setRotation(sf::degrees(rot));
        while(const std::optional ev=window.pollEvent())
            if(ev->is<sf::Event::Closed>()){window.close();return;}
        window.clear(sf::Color(10,15,40));
        if(hasBg) window.draw(bg);
        else {
            // Card preto centralizado
            window.draw(roundRect(WIN_W/2.f-280,WIN_H/2.f-220,560,440,14,sf::Color(0,0,0,230)));
        }
        window.draw(t);
        if(hasMoon) window.draw(moon);
        window.display();
        if(clk.getElapsedTime().asSeconds()+dt>=dur) break;
    }
}

// ======= TELA LOGIN =======
bool telaLogin(sf::RenderWindow& window, sf::Font& fontTitle, sf::Font& fontText) {
    sf::Texture bgTex, btnTex;
    bool hasBg  = bgTex.loadFromFile(A_LOGIN+"loginBackCard.png");
    bool hasBtn = btnTex.loadFromFile(A_LOGIN+"discordButton.png");

    sf::Sprite bg(bgTex), btn(btnTex);
    if(hasBg){
        auto sz=bgTex.getSize();
        float sx=(float)WIN_W/sz.x, sy=(float)WIN_H/sz.y;
        bg.setScale({sx,sy});
    }

    float cardW=560,cardH=440,cardX=WIN_W/2.f-cardW/2.f,cardY=WIN_H/2.f-cardH/2.f;

    float btnW = hasBtn?(float)btnTex.getSize().x:400;
    float btnH = hasBtn?(float)btnTex.getSize().y:65;
    float btnX = WIN_W/2.f-btnW/2.f;
    float btnY = WIN_H/2.f+30;
    if(hasBtn) btn.setPosition({btnX,btnY});

    sf::Text titulo(fontTitle,"Login",48);
    titulo.setFillColor(sf::Color::White);
    sf::FloatRect tb=titulo.getLocalBounds();
    titulo.setPosition({WIN_W/2.f-tb.size.x/2.f,WIN_H/2.f-160});

    while(window.isOpen()){
        sf::Vector2f mouse(sf::Mouse::getPosition(window));
        bool hBtn=mouse.x>=btnX&&mouse.x<=btnX+btnW&&mouse.y>=btnY&&mouse.y<=btnY+btnH;

        while(const std::optional ev=window.pollEvent()){
            if(ev->is<sf::Event::Closed>()){window.close();return false;}
            if(ev->is<sf::Event::MouseButtonPressed>()&&hBtn){
                // Placeholder OAuth - simula login
                currentUser.nome="Convidado-Guloso";
                currentUser.apelido="convidado_guloso";
                currentUser.id="000000000000";
                currentUser.avatarUrl="";
                currentUser.logado=true;
                salvarUser();
                return true;
            }
        }

        window.clear(sf::Color(10,15,40));
        if(hasBg) window.draw(bg);
        else window.draw(roundRect(cardX,cardY,cardW,cardH,14,sf::Color(0,0,0,230)));

        window.draw(titulo);

        if(hasBtn){
            btn.setColor(hBtn?sf::Color(200,200,200):sf::Color::White);
            window.draw(btn);
        } else {
            window.draw(roundRect(btnX,btnY,btnW,btnH,10,hBtn?sf::Color(90,70,180):sf::Color(114,137,218)));
            sf::Text dt(fontTitle,"Login com Discord",26);
            dt.setFillColor(sf::Color::White);
            sf::FloatRect db=dt.getLocalBounds();
            dt.setPosition({btnX+btnW/2-db.size.x/2,btnY+btnH/2-db.size.y});
            window.draw(dt);
        }
        window.display();
    }
    return false;
}

// ======= TELA PROGRESSO (download) =======
void telaProgresso(sf::RenderWindow& w, sf::Font& fontTitle, sf::Font& fontText,
                   bool& baixando, bool& extraindo, bool& concluido, const std::string& titulo) {
    while(w.isOpen()&&!concluido){
        while(const std::optional ev=w.pollEvent())
            if(ev->is<sf::Event::Closed>()){w.close();return;}
        w.clear(sf::Color(10,15,40));
        sf::Text t(fontTitle,titulo,26); t.setFillColor(sf::Color::White);
        sf::FloatRect tb=t.getLocalBounds(); t.setPosition({WIN_W/2.f-tb.size.x/2.f,120});
        w.draw(t);
        if(baixando){
            double pct=progressData.total>0?progressData.downloaded/progressData.total*100:0;
            static double lastDl=0; static sf::Clock spCk;
            double el=spCk.getElapsedTime().asSeconds();
            if(el>=0.5f){progressData.speed=(progressData.downloaded-lastDl)/el;lastDl=progressData.downloaded;spCk.restart();}
            w.draw(roundRect(100,300,WIN_W-200,28,8,sf::Color(50,50,70)));
            if(pct>0.1) w.draw(roundRect(100,300,(float)((WIN_W-200)*pct/100),28,8,sf::Color(80,120,255)));
            std::ostringstream ss; ss<<std::fixed<<std::setprecision(1)<<pct<<"%";
            sf::Text pt(fontTitle,ss.str(),20); pt.setFillColor(sf::Color::White);
            sf::FloatRect pb=pt.getLocalBounds(); pt.setPosition({WIN_W/2.f-pb.size.x/2,304}); w.draw(pt);
            sf::Text mb(fontText,fmtBytes(progressData.downloaded)+" - "+fmtBytes(progressData.total),18);
            mb.setFillColor(sf::Color(180,180,255)); mb.setPosition({100,340}); w.draw(mb);
            sf::Text sp(fontText,"Speed: "+fmtBytes(progressData.speed)+"per second",18);
            sp.setFillColor(sf::Color(180,180,255)); sp.setPosition({100,368}); w.draw(sp);
        } else if(extraindo){
            sf::Text st(fontTitle,"Extracting... please wait",24); st.setFillColor(sf::Color::White);
            sf::FloatRect sb=st.getLocalBounds(); st.setPosition({WIN_W/2.f-sb.size.x/2,300}); w.draw(st);
        }
        w.display();
    }
}

// ======= DOWNLOAD VERSAO =======
void baixarVersao(sf::RenderWindow& window, sf::Font& fontTitle, sf::Font& fontText, const Version& ver) {
    progressData={};
    bool baixando=true, extraindo=false, concluido=false;
    std::string zipPath=TEMP_PATH_()+"game.zip";
    std::string destPath=VERSIONS_PATH_()+ver.numero+"\\";

    std::thread t([&](){
        fs::create_directories(TEMP_PATH_());
        bool ok=false;
        CURL* curl=curl_easy_init();
        if(curl){
            FILE* fp=fopen(zipPath.c_str(),"wb");
            if(fp){
                curl_easy_setopt(curl,CURLOPT_URL,ver.zipUrl.c_str());
                curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writeFileCb);
                curl_easy_setopt(curl,CURLOPT_WRITEDATA,fp);
                curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progressCb);
                curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&progressData);
                curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
                curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
                curl_easy_setopt(curl,CURLOPT_MAXREDIRS,10L);
                curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,0L);
                curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,0L);
                curl_easy_setopt(curl,CURLOPT_USERAGENT,"Mozilla/5.0");
                CURLcode res=curl_easy_perform(curl);
                fclose(fp);
                if(res==CURLE_OK&&fs::exists(zipPath)&&fs::file_size(zipPath)>1024*1024) ok=true;
            }
            curl_easy_cleanup(curl);
        }
        baixando=false; extraindo=true;
        if(ok){
            fs::create_directories(destPath);
            system(("powershell -Command \"Expand-Archive -Path '"+zipPath+"' -DestinationPath '"+destPath+"' -Force\"").c_str());
            fs::remove(zipPath);
        }
        extraindo=false; concluido=true;
    });
    telaProgresso(window,fontTitle,fontText,baixando,extraindo,concluido,"Baixando "+ver.numero+"...");
    t.join();
}

// ======= TELA MODS =======
void telaMods(sf::RenderWindow& window, sf::Font& fontTitle, sf::Font& fontText) {
    auto vers = carregarVersions();
    auto installed = carregarInstalled();
    int verSel = 0; // índice selecionado
    float winW=(float)WIN_W, winH=(float)WIN_H;

    // Layout: lista versões à esquerda, área mods à direita
    float listX=SIDEBAR_W+20, listY=60, listW=280, listH=winH-BOTTOMBAR_H-80;
    float modsX=listX+listW+20, modsY=listY, modsW=winW-modsX-20, modsH=listH;
    float btnPlayX=winW/2-100, btnPlayY=winH-BOTTOMBAR_H+25, btnPlayW=200, btnPlayH=60;
    float btnVoltarX=SIDEBAR_W+20, btnVoltarY=15, btnVoltarW=100, btnVoltarH=34;

    while(window.isOpen()){
        sf::Vector2f mouse(sf::Mouse::getPosition(window));
        bool hVoltar=mouse.x>=btnVoltarX&&mouse.x<=btnVoltarX+btnVoltarW&&mouse.y>=btnVoltarY&&mouse.y<=btnVoltarY+btnVoltarH;
        bool hPlay=mouse.x>=btnPlayX&&mouse.x<=btnPlayX+btnPlayW&&mouse.y>=btnPlayY&&mouse.y<=btnPlayY+btnPlayH;

        while(const std::optional ev=window.pollEvent()){
            if(ev->is<sf::Event::Closed>()){window.close();return;}
            if(ev->is<sf::Event::MouseButtonPressed>()){
                if(hVoltar) return;

                // Clique na lista de versões
                for(int i=0;i<(int)vers.size();i++){
                    float iy=listY+i*52;
                    if(mouse.x>=listX&&mouse.x<=listX+listW&&mouse.y>=iy&&mouse.y<=iy+46)
                        verSel=i;
                }

                // Botão play - abre a versão selecionada
                if(hPlay&&!vers.empty()){
                    std::string exePath=resolveExePath(installed, vers[verSel].numero);
                    if(fs::exists(exePath)){
                        window.setVisible(false);
                        system(("\""+exePath+"\"").c_str());
                        window.setVisible(true);
                    } else {
                        // Versão não instalada, baixar
                        baixarVersao(window,fontTitle,fontText,vers[verSel]);
                    }
                }
            }
        }

        window.clear(sf::Color(10,15,40));

        // Barra de baixo
        window.draw(roundRect(0,winH-BOTTOMBAR_H,winW,BOTTOMBAR_H,0,sf::Color(0,0,0,220)));

        // Botão voltar
        window.draw(roundRect(btnVoltarX,btnVoltarY,btnVoltarW,btnVoltarH,6,hVoltar?sf::Color(60,60,80):sf::Color(40,40,60)));
        sf::Text vt(fontText,"Back",18); vt.setFillColor(sf::Color::White); vt.setPosition({btnVoltarX+10,btnVoltarY+7}); window.draw(vt);

        // Título
        sf::Text titulo(fontTitle,"Mods",34); titulo.setFillColor(sf::Color::White);
        titulo.setPosition({SIDEBAR_W+140,15}); window.draw(titulo);

        // Lista de versões
        window.draw(roundRect(listX-4,listY-4,listW+8,listH+8,8,sf::Color(20,20,40)));
        sf::Text ltitle(fontText,"",18); ltitle.setFillColor(sf::Color(150,150,200));
        ltitle.setPosition({listX+8,listY-30}); window.draw(ltitle);

        for(int i=0;i<(int)vers.size();i++){
            float iy=listY+i*52;
            bool sel=(i==verSel);
            bool hov=mouse.x>=listX&&mouse.x<=listX+listW&&mouse.y>=iy&&mouse.y<=iy+46;
            sf::Color cardCol = sel?sf::Color(60,80,160): hov?sf::Color(40,40,70):sf::Color(25,25,50);
            window.draw(roundRect(listX,iy,listW,46,6,cardCol));

            // Badge do estado
            sf::Color badgeCol = vers[i].estado=="final"?sf::Color(40,160,80):
                                 vers[i].estado=="pre_release"?sf::Color(200,140,0):sf::Color(100,60,180);
            std::string badgeStr = vers[i].estado=="final"?"Final":
                                   vers[i].estado=="pre_release"?"Pre-Release":"Moon Phase";
            window.draw(roundRect(listX+listW-120,iy+8,110,28,5,badgeCol));
            sf::Text badge(fontText,badgeStr,12); badge.setFillColor(sf::Color::White);
            sf::FloatRect bb=badge.getLocalBounds();
            badge.setPosition({listX+listW-120+(110-bb.size.x)/2,iy+13}); window.draw(badge);

            // Número da versão
            sf::Text vnum(fontTitle,vers[i].numero,18); vnum.setFillColor(sf::Color::White);
            vnum.setPosition({listX+10,iy+6}); window.draw(vnum);

            // Verificar se instalada (installed.json primeiro, depois fallback)
            std::string exePath=resolveExePath(installed, vers[i].numero);
            bool inst=fs::exists(exePath);
            sf::Text instTxt(fontText,inst?"Install":"Not install",13);
            instTxt.setFillColor(inst?sf::Color(80,200,80):sf::Color(160,160,160));
            instTxt.setPosition({listX+10,iy+28}); window.draw(instTxt);
        }

        if(vers.empty()){
            sf::Text empty(fontText,"No versions here...",18); empty.setFillColor(sf::Color(150,150,180));
            empty.setPosition({listX+10,listY+20}); window.draw(empty);
        }

        // Área de mods (placeholder por enquanto)
        window.draw(roundRect(modsX-4,modsY-4,modsW+8,modsH+8,8,sf::Color(20,20,40)));
        sf::Text mPlaceholder(fontText,"Mods soon...",20);
        mPlaceholder.setFillColor(sf::Color(100,100,140));
        mPlaceholder.setPosition({modsX+20,modsY+modsH/2}); window.draw(mPlaceholder);

        // Botão PLAY
        if(!vers.empty()){
            std::string exePath=resolveExePath(installed, vers[verSel].numero);
            bool inst=fs::exists(exePath);
            sf::Color playCol=inst?(hPlay?sf::Color(30,120,30):sf::Color(40,160,40)):
                                   (hPlay?sf::Color(120,80,20):sf::Color(160,110,20));
            window.draw(roundRect(btnPlayX,btnPlayY,btnPlayW,btnPlayH,10,playCol));
            sf::Text pt(fontTitle,inst?"PLAY":"DOWNLOAD",26); pt.setFillColor(sf::Color::White);
            sf::FloatRect pb=pt.getLocalBounds();
            pt.setPosition({btnPlayX+btnPlayW/2-pb.size.x/2,btnPlayY+btnPlayH/2-pb.size.y});
            window.draw(pt);
        }

        window.display();
    }
}

// ======= TELA CHANGELOG =======
void telaChangelog(sf::RenderWindow& window, sf::Font& fontTitle, sf::Font& fontText) {
    std::string conteudo="";
    std::ifstream f(A_NEWS+"changelog.md");
    if(f.is_open()){std::string l;while(std::getline(f,l))conteudo+=l+"\n";}
    else conteudo="## Changelog\nNenhum changelog encontrado.";

    float scroll=0.f;
    float maxScroll=9999.f;

    while(window.isOpen()){
        while(const std::optional ev=window.pollEvent()){
            if(ev->is<sf::Event::Closed>()){window.close();return;}
            if(const auto* k=ev->getIf<sf::Event::KeyPressed>())
                if(k->code==sf::Keyboard::Key::Escape) return;
            if(const auto* we=ev->getIf<sf::Event::MouseWheelScrolled>())
                scroll=std::max(0.f,scroll-we->delta*30.f);
        }

        window.clear(sf::Color(10,15,40));

        float y=60.f-scroll;
        std::istringstream stream(conteudo);
        std::string linha;
        while(std::getline(stream,linha)){
            if(y>-60&&y<(float)WIN_H+60){
                if(linha.size()>=3&&linha.substr(0,3)=="## "){
                    sf::Text t(fontTitle,linha.substr(3),28);
                    t.setFillColor(sf::Color(180,180,255)); t.setPosition({SIDEBAR_W+20,y}); window.draw(t); y+=42;
                } else if(linha.size()>=2&&linha.substr(0,2)=="# "){
                    sf::Text t(fontTitle,linha.substr(2),36);
                    t.setFillColor(sf::Color::White); t.setPosition({SIDEBAR_W+20,y}); window.draw(t); y+=52;
                } else if(!linha.empty()){
                    sf::Text t(fontText,linha,20);
                    t.setFillColor(sf::Color(210,210,210)); t.setPosition({SIDEBAR_W+20,y}); window.draw(t); y+=28;
                } else y+=12;
            } else {
                if(linha.size()>=3&&linha.substr(0,3)=="## ") y+=42;
                else if(linha.size()>=2&&linha.substr(0,2)=="# ") y+=52;
                else if(!linha.empty()) y+=28;
                else y+=12;
            }
        }

        sf::Text titulo(fontTitle,"Changelog - v0.1.0",32); titulo.setFillColor(sf::Color::White);
        titulo.setPosition({SIDEBAR_W+20,15}); window.draw(titulo);
        sf::Text back(fontText,"ESC to back",18); back.setFillColor(sf::Color(100,100,120));
        back.setPosition({SIDEBAR_W+20,(float)WIN_H-30}); window.draw(back);
        window.display();
    }
}

// ======= TELA OPCOES =======
void telaOpcoes(sf::RenderWindow& window, sf::Font& fontTitle, sf::Font& fontText, sf::Texture& avatarTex, bool hasAvatar) {
    float winW=(float)WIN_W, winH=(float)WIN_H;

    // Avatar circular grande
    sf::Texture circTex;
    bool hasCirc=false;
    if(hasAvatar){
        circTex=makeCircleTexture(avatarTex,120);
        hasCirc=true;
    }
    sf::Sprite avatarSpr(hasCirc?circTex:(hasAvatar?avatarTex:circTex));

    float avatarX=SIDEBAR_W+40, avatarY=80;
    float btnSairX=winW/2-150, btnSairY=winH-BOTTOMBAR_H-70, btnSairW=300, btnSairH=50;

    while(window.isOpen()){
        sf::Vector2f mouse(sf::Mouse::getPosition(window));
        bool hSair=mouse.x>=btnSairX&&mouse.x<=btnSairX+btnSairW&&mouse.y>=btnSairY&&mouse.y<=btnSairY+btnSairH;

        while(const std::optional ev=window.pollEvent()){
            if(ev->is<sf::Event::Closed>()){window.close();return;}
            if(const auto* k=ev->getIf<sf::Event::KeyPressed>())
                if(k->code==sf::Keyboard::Key::Escape) return;
            if(ev->is<sf::Event::MouseButtonPressed>()&&hSair){
                currentUser=UserData();
                if(fs::exists(SAVE_FILE_())) fs::remove(SAVE_FILE_());
                window.close(); return;
            }
        }

        window.clear(sf::Color(10,15,40));

        sf::Text titulo(fontTitle,"Config's",34); titulo.setFillColor(sf::Color::White);
        titulo.setPosition({SIDEBAR_W+20,15}); window.draw(titulo);

        // Avatar
        if(hasCirc){
            avatarSpr.setPosition({avatarX,avatarY});
            window.draw(avatarSpr);
        } else {
            window.draw(roundRect(avatarX,avatarY,120,120,60,sf::Color(60,60,80)));
        }

        // Info usuário
        sf::Text nome(fontTitle,currentUser.nome,30); nome.setFillColor(sf::Color::White);
        nome.setPosition({avatarX+140,avatarY+10}); window.draw(nome);
        sf::Text apelido(fontText,"@"+currentUser.apelido,22); apelido.setFillColor(sf::Color(160,160,180));
        apelido.setPosition({avatarX+140,avatarY+52}); window.draw(apelido);
        sf::Text id(fontText,"ID: "+currentUser.id,18); id.setFillColor(sf::Color(100,100,120));
        id.setPosition({avatarX+140,avatarY+82}); window.draw(id);

        // Botão sair
        window.draw(roundRect(btnSairX,btnSairY,btnSairW,btnSairH,8,hSair?sf::Color(200,40,40):sf::Color(150,30,30)));
        sf::Text sair(fontTitle,"Exit to Account",22); sair.setFillColor(sf::Color::White);
        sf::FloatRect sb=sair.getLocalBounds();
        sair.setPosition({btnSairX+btnSairW/2-sb.size.x/2,btnSairY+btnSairH/2-sb.size.y});
        window.draw(sair);

        sf::Text back(fontText,"ESC to Back",18); back.setFillColor(sf::Color(100,100,120));
        back.setPosition({SIDEBAR_W+20,winH-30}); window.draw(back);
        window.display();
    }
}

// ======= HOME =======
void telaHome(sf::RenderWindow& window, sf::Font& fontTitle, sf::Font& fontText, sf::Font& fontMono) {
    float winW=(float)WIN_W, winH=(float)WIN_H;

    // Layout baseado no concept:
    // Sidebar: 185px largura, altura total MENOS a barra de baixo
    // Barra baixo: 120px altura, começa em x=0, vai até winW (em cima da sidebar visualmente)
    // Ícones: 130px, centralizados na sidebar
    // Avatar: dentro da barra de baixo, canto esquerdo
    // PLAY: centralizado na barra de baixo, após a sidebar

    const float SB_W   = 185.f;  // sidebar largura
    const float BT_H   = 120.f;  // bottom bar altura
    const float SB_H   = winH - BT_H; // sidebar altura (não cobre a bottom bar)
    const float ICON_S = 110.f;  // tamanho dos ícones principais (mods/site/news)
    const float OPT_S  = 110.f;  // tamanho do ícone de opções (pontinhos), igual aos outros
    const float ICON_X = SB_W/2.f - ICON_S/2.f; // x centralizado na sidebar

    // Posições Y dos ícones — colados perto do topo, como no concept
    float iconY1 = 20.f;
    float iconY2 = 150.f;
    float iconY3 = 280.f;
    float iconY4 = 410.f; // pontinhos (opções), logo abaixo do 3º ícone

    // Avatar: dentro da barra de baixo, lado esquerdo da sidebar
    float AVT_S  = 80.f;
    float AVT_X  = SB_W/2.f - AVT_S/2.f;
    float AVT_Y  = winH - BT_H + 8.f; // só posição, mais pra cima dentro da barra de baixo

    // Carregar texturas
    sf::Texture bgTex, playTex, leftBarTex, downBarTex;
    sf::Texture modTex, webTex, newsTex, optTex;
    sf::Texture avatarTex, fallbackTex;
    sf::Texture blackBarsTex;

    bool hasBg   = bgTex.loadFromFile(A_HOME+"background.png");
    if(!hasBg)     hasBg = bgTex.loadFromFile(A_HOME+"bg.png");
    bool hasPlay = playTex.loadFromFile(A_HOME+"playButton.png");
    bool hasLeft = leftBarTex.loadFromFile(A_HOME+"leftblackBar.png");
    bool hasDown = downBarTex.loadFromFile(A_HOME+"downblackBar.png");
    bool hasMod  = modTex.loadFromFile(A_HOME+"modsIconB.png");
    bool hasWeb  = webTex.loadFromFile(A_HOME+"websiteIcon.png");
    bool hasNews = newsTex.loadFromFile(A_HOME+"newsIconB.png");
    bool hasOpt  = optTex.loadFromFile(A_HOME+"optionsIcon.png");
    if(hasMod)  modTex.setSmooth(true);
    if(hasWeb)  webTex.setSmooth(true);
    if(hasNews) newsTex.setSmooth(true);
    if(hasOpt)  optTex.setSmooth(true);
    bool hasFall = fallbackTex.loadFromFile(A_HOME+"fallback-user-icon.png");
    bool hasBlackBars = blackBarsTex.loadFromFile(A_HOME+"blackBars.png");

    bool hasAvImg=false;
    if(!currentUser.avatarUrl.empty()){
        std::string ap=BASE_PATH_()+"data\\avatar_temp.png";
        system(("powershell -Command \"Invoke-WebRequest -Uri '"+currentUser.avatarUrl+"' -OutFile '"+ap+"'\" 2>nul").c_str());
        hasAvImg=avatarTex.loadFromFile(ap);
    }
    sf::Texture& userTex = hasAvImg ? avatarTex : fallbackTex;
    bool hasUser = hasAvImg || hasFall;

    sf::Texture circTex;
    bool hasCirc=false;
    if(hasUser){ circTex=makeCircleTexture(userTex,(unsigned int)AVT_S); hasCirc=true; }

    // ---- Sprites ----
    sf::Sprite bgSpr(bgTex);
    sf::Sprite leftSpr(leftBarTex);
    sf::Sprite downSpr(downBarTex);
    sf::Sprite playSpr(playTex);
    sf::Sprite modSpr(modTex), webSpr(webTex), newsSpr(newsTex), optSpr(optTex);
    sf::Sprite avatarSpr(hasCirc ? circTex : userTex);
    sf::Sprite blackBarsSpr(blackBarsTex);

    // Background — preenche tudo
    if(hasBg){
        auto sz=bgTex.getSize();
        bgSpr.setScale({winW/(float)sz.x, winH/(float)sz.y});
        bgSpr.setPosition({0,0});
    }

    // Sidebar — x=0, y=0, w=SB_W, h=SB_H (não vai até a barra de baixo)
    if(hasLeft){
        auto sz=leftBarTex.getSize();
        leftSpr.setScale({SB_W/(float)sz.x, SB_H/(float)sz.y});
        leftSpr.setPosition({0,0});
    }

    // Barra de baixo — x=0, y=winH-BT_H, w=winW, h=BT_H (cobre a sidebar embaixo)
    if(hasDown){
        auto sz=downBarTex.getSize();
        downSpr.setScale({winW/(float)sz.x, BT_H/(float)sz.y});
        downSpr.setPosition({0, winH-BT_H});
    }

    // blackBars — mesma resolução da janela, encaixa na posição zero
    if(hasBlackBars){
        blackBarsSpr.setPosition({0,0});
    }

    // Escalar ícone para um tamanho-alvo mantendo proporção
    auto scaleToSize=[&](sf::Sprite& spr, sf::Texture& tex, float targetSize){
        auto sz=tex.getSize();
        float sc=targetSize/std::max((float)sz.x,(float)sz.y);
        spr.setScale({sc,sc});
        // Centralizar horizontalmente na sidebar
        float w=(float)sz.x*sc;
        spr.setPosition({SB_W/2.f-w/2.f, 0}); // y será setado depois
    };

    if(hasMod) { scaleToSize(modSpr, modTex, ICON_S);   modSpr.setPosition( {SB_W/2.f-modSpr.getGlobalBounds().size.x/2.f,  iconY1}); }
    if(hasWeb) { scaleToSize(webSpr, webTex, ICON_S);   webSpr.setPosition( {SB_W/2.f-webSpr.getGlobalBounds().size.x/2.f,  iconY2}); }
    if(hasNews){ scaleToSize(newsSpr,newsTex,ICON_S);   newsSpr.setPosition({SB_W/2.f-newsSpr.getGlobalBounds().size.x/2.f, iconY3}); }
    if(hasOpt) { scaleToSize(optSpr, optTex, OPT_S);    optSpr.setPosition( {SB_W/2.f-optSpr.getGlobalBounds().size.x/2.f,  iconY4}); }

    // Avatar circular na barra de baixo
    avatarSpr.setPosition({AVT_X, AVT_Y});

    // PLAY — centralizado na área após a sidebar, dentro da barra de baixo
    float playW=0,playH=0,playX=0,playY=0;
    if(hasPlay){
        auto sz=playTex.getSize();
        float maxH=BT_H-16.f;
        float sc=maxH/(float)sz.y;
        if((float)sz.x*sc>500.f) sc=500.f/(float)sz.x;
        playSpr.setScale({sc,sc});
        playW=(float)sz.x*sc; playH=(float)sz.y*sc;
        // Centralizado na tela inteira (largura total da janela)
        playX=winW/2.f-playW/2.f;
        playY=winH-BT_H+(BT_H-playH)/2.f;
        playSpr.setPosition({playX,playY});
    } else {
        playW=260; playH=70;
        playX=winW/2.f-playW/2.f;
        playY=winH-BT_H+(BT_H-playH)/2.f;
    }
    auto iconBounds=[&](float y, float size)->bool{
        sf::Vector2f m(sf::Mouse::getPosition(window));
        return m.x>=0&&m.x<=SB_W&&m.y>=y&&m.y<=y+size;
    };

    while(window.isOpen()){
        sf::Vector2f mouse(sf::Mouse::getPosition(window));
        bool hMod  = iconBounds(iconY1, ICON_S);
        bool hWeb  = iconBounds(iconY2, ICON_S);
        bool hNews = iconBounds(iconY3, ICON_S);
        bool hOpt  = iconBounds(iconY4, OPT_S);
        bool hPlay = mouse.x>=playX&&mouse.x<=playX+playW&&mouse.y>=playY&&mouse.y<=playY+playH;

        while(const std::optional ev=window.pollEvent()){
            if(ev->is<sf::Event::Closed>()){window.close();return;}
            if(ev->is<sf::Event::MouseButtonPressed>()){
                if(hMod)  telaMods(window,fontTitle,fontText);
                if(hNews) telaChangelog(window,fontTitle,fontText);
                if(hOpt)  telaOpcoes(window,fontTitle,fontText,hasAvImg?avatarTex:fallbackTex,hasUser);
                if(hPlay){
                    auto vers=carregarVersions();
                    auto installed=carregarInstalled();
                    bool abriu=false;
                    for(auto& v:vers){
                        std::string exe=resolveExePath(installed, v.numero);
                        if(fs::exists(exe)){
                            window.setVisible(false);
                            system(("\""+exe+"\"").c_str());
                            window.setVisible(true);
                            abriu=true; break;
                        }
                    }
                    if(!abriu) telaMods(window,fontTitle,fontText);
                }
            }
        }

        // ===== DESENHO =====
        window.clear(sf::Color(5,10,35));

        // 1. Background
        if(hasBg) window.draw(bgSpr);

        // 2. Sidebar (atrás dos ícones, não cobre a barra de baixo)
        if(hasLeft) window.draw(leftSpr);
        else window.draw(roundRect(0,0,SB_W,SB_H,0,sf::Color(0,0,0,220)));

        // 3. Barra de baixo (cobre a parte de baixo da sidebar)
        if(hasDown) window.draw(downSpr);
        else window.draw(roundRect(0,winH-BT_H,winW,BT_H,0,sf::Color(0,0,0,220)));

        // 3.5 blackBars — em cima das barras, debaixo dos ícones/opções
        if(hasBlackBars) window.draw(blackBarsSpr);

        // 4. Ícones
        auto drawIcon=[&](sf::Sprite& spr, bool hov, bool has){
            if(!has) return;
            spr.setColor(hov?sf::Color(160,160,160):sf::Color::White);
            window.draw(spr);
        };
        drawIcon(modSpr, hMod, hasMod);
        drawIcon(webSpr, hWeb, hasWeb);
        drawIcon(newsSpr,hNews,hasNews);
        drawIcon(optSpr, hOpt, hasOpt);

        // 5. Avatar redondo na barra de baixo
        if(hasCirc) window.draw(avatarSpr);
        else if(hasUser) window.draw(avatarSpr);

        // 6. Nome do usuário abaixo do avatar
        sf::Text nomeT(fontText,currentUser.nome,14);
        nomeT.setFillColor(sf::Color(200,200,200));
        sf::FloatRect nb=nomeT.getLocalBounds();
        nomeT.setPosition({SB_W/2.f-nb.size.x/2.f, AVT_Y+AVT_S+4});
        window.draw(nomeT);

        // 7. PLAY
        if(hasPlay){
            playSpr.setColor(hPlay?sf::Color(180,180,180):sf::Color::White);
            window.draw(playSpr);
        } else {
            window.draw(roundRect(playX,playY,playW,playH,12,hPlay?sf::Color(40,130,40):sf::Color(50,170,50)));
            sf::Text pt(fontTitle,"PLAY",32); pt.setFillColor(sf::Color::White);
            sf::FloatRect pb=pt.getLocalBounds();
            pt.setPosition({playX+playW/2-pb.size.x/2,playY+playH/2-pb.size.y});
            window.draw(pt);
        }

        window.display();
    }
}

// ======= MAIN =======
int main(){
    // Console de debug: mostra no terminal se o save.json foi lido/escrito e onde.
    // Se preferir sem essa janelinha de console, pode remover as 2 linhas abaixo.
    AllocConsole();
    freopen("CONOUT$","w",stdout);

    sf::RenderWindow window(
        sf::VideoMode({WIN_W,WIN_H}),
        "Moon' Launcher (Dev Build)",
        sf::Style::Titlebar|sf::Style::Close
    );
    window.setFramerateLimit(60);

    sf::Image icon;
    if(icon.loadFromFile(A_ICON)){
        auto sz=icon.getSize(); window.setIcon(sz,icon.getPixelsPtr());
    }

    sf::Font fontTitle, fontText, fontMono;
    fontTitle.openFromFile(A_FONTS+"FunkinOptions.otf");
    fontText.openFromFile(A_FONTS+"FunkinLingLong.otf");
    fontMono.openFromFile(A_FONTS+"VcrMono.ttf");

    printf("[INFO] Pasta de dados: %s\n", BASE_PATH_().c_str());

    fs::create_directories(VERSIONS_PATH_());
    fs::create_directories(SAVES_PATH_());
    fs::create_directories(TEMP_PATH_());

    bool logado=carregarUser();
    if(!logado){
        logado=telaLogin(window,fontTitle,fontText);
        if(!window.isOpen()) return 0;
        if(logado) telaLoading(window,fontTitle);
    }
    if(window.isOpen()&&logado)
        telaHome(window,fontTitle,fontText,fontMono);

    return 0;
}
