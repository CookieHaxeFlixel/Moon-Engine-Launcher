@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "MSYS_ROOT=C:\msys64\ucrt64"
set "MSYS_USR=C:\msys64\usr\bin"
set "PATH=%MSYS_ROOT%\bin;%MSYS_USR%;%PATH%"
set "MSYSTEM=UCRT64"
set "GXX=%MSYS_ROOT%\bin\g++.exe"
set "GCC=%MSYS_ROOT%\bin\gcc.exe"
set "OUT=%ROOT%export\windows\release\bin"
set "EXE_TMP=%OUT%\Moon Launcher.building.exe"
set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"

if not exist "%MSYS_ROOT%\include\lua.h" (
    echo [ERRO] Lua nao esta instalado no MSYS2 UCRT64.
    echo Instale com: C:\msys64\usr\bin\pacman.exe -S mingw-w64-ucrt-x86_64-lua
    pause
    exit /b 1
)

cd /d "%ROOT%"

set "APP_VERSION="
for /f "delims=" %%V in ('python3 get_version.py') do set "APP_VERSION=%%V"
if not defined APP_VERSION (
    echo [ERRO] nao foi possivel ler Project::VERSION de project.hpp.
    pause
    exit /b 1
)
echo Versao: %APP_VERSION%

"%MSYS_ROOT%\bin\python3.exe" "%ROOT%generate_app_icon.py"
if errorlevel 1 (
    echo [ERRO] falha ao gerar app.ico.
    pause
    exit /b 1
)

if not exist "%ROOT%assets\app\icons\app.ico" (
    echo [ERRO] app.ico nao foi gerado.
    pause
    exit /b 1
)

"%MSYS_ROOT%\bin\windres.exe" ".\app.rc" -O coff -o ".\app.res"
if errorlevel 1 (
    echo [ERRO] windres falhou ao compilar app.rc
    pause
    exit /b 1
)

echo ========================================
echo        Moon Launcher Build System
echo ========================================
echo.

if not exist "%GXX%" (
    echo [ERRO] g++.exe nao foi encontrado em %MSYS_ROOT%\bin
    echo Instale o MSYS2 ucrt64 e confirme que o compilador MinGW esta presente.
    pause
    exit /b 1
)

if not exist "%OUT%" mkdir "%OUT%"

echo [1/3] Compilando Miniz...
echo.

"%GCC%" -c ".\third_party\miniz\miniz.c" -o ".\miniz.o"
if errorlevel 1 goto ERROR

"%GCC%" -c ".\third_party\miniz\miniz_tdef.c" -o ".\miniz_tdef.o"
if errorlevel 1 goto ERROR

"%GCC%" -c ".\third_party\miniz\miniz_tinfl.c" -o ".\miniz_tinfl.o"
if errorlevel 1 goto ERROR

"%GCC%" -c ".\third_party\miniz\miniz_zip.c" -o ".\miniz_zip.o"
if errorlevel 1 goto ERROR


echo.
echo [2/3] Compilando Moon Launcher...
echo.

"%GXX%" -std=c++17 -municode -static ".\src\LauncherUpdater.cpp" -o "%OUT%\LauncherUpdater.exe"
if errorlevel 1 goto ERROR

"%GXX%" -std=c++17 ^
    -I"." ^
    -I".\third_party\miniz" ^
    ".\app.res" ^
    ".\src\main.cpp" ^
    ".\src\ModRuntime.cpp" ^
    ".\src\LoadingLauncher.cpp" ^
    ".\project.cpp" ^
    ".\miniz.o" ^
    ".\miniz_tdef.o" ^
    ".\miniz_tinfl.o" ^
    ".\miniz_zip.o" ^
    -o "%EXE_TMP%" ^
    -mwindows ^
    -L"%MSYS_ROOT%\lib" ^
    -lsfml-graphics ^
    -lsfml-window ^
    -lsfml-system ^
    -lsfml-audio ^
    -lcurl ^
    -llua ^
    -lshell32 ^
    -lws2_32 ^
    -lbcrypt

if errorlevel 1 goto ERROR

if not exist "%EXE_TMP%" goto ERROR

move /Y "%EXE_TMP%" "%OUT%\Moon Launcher.exe" >nul
if errorlevel 1 (
    echo [ERRO] O EXE antigo ainda esta bloqueado. Feche o launcher e tente novamente.
    pause
    exit /b 1
)

if not exist "%OUT%\Moon Launcher.exe" goto ERROR


echo.
echo [3/3] Copiando DLLs e arquivos de runtime...
echo.

for %%F in ("%MSYS_ROOT%\bin\*.dll") do (
    copy /Y "%%~F" "%OUT%\" >nul
)

if exist "%ROOT%versions.json" (
    copy /Y "%ROOT%versions.json" "%OUT%\versions.json" >nul
)

if not exist "%OUT%\launcher-settings.json" (
    copy /Y "%ROOT%launcher-settings.json" "%OUT%\launcher-settings.json" >nul
)

if exist "%ROOT%assets" (
    xcopy /E /I /Y "%ROOT%assets" "%OUT%\assets" >nul
)

if exist "%ROOT%com.funkinmoon" (
    xcopy /E /I /Y "%ROOT%com.funkinmoon" "%OUT%\com.funkinmoon" >nul
)

if exist "%ROOT%mods" (
    xcopy /E /I /Y "%ROOT%mods" "%OUT%\mods" >nul
)

echo.
echo [4/5] Criando pacote de atualizacao (moon-launcher-v%APP_VERSION%.zip com bin\)...
echo.

set "PKG=%ROOT%export\windows\release\package"
set "ZIP=%ROOT%export\windows\release\moon-launcher-v%APP_VERSION%.zip"

if exist "%PKG%" rmdir /s /q "%PKG%"
if exist "%ZIP%" del /f /q "%ZIP%"

robocopy "%OUT%" "%PKG%\bin" /E /XD com.funkinmoon mods /XF launcher-update.log "Moon Launcher.building.exe" /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto ERROR

"%SystemRoot%\System32\tar.exe" -a -c -f "%ZIP%" -C "%PKG%" bin
if errorlevel 1 goto ERROR

rmdir /s /q "%PKG%"

if exist "%ISCC%" (
    echo.
    echo [5/5] Criando instalador Setup...
    "%ISCC%" /DMyAppVersion=%APP_VERSION% ".\setup.iss"
    if errorlevel 1 goto ERROR
) else (
    echo [AVISO] Inno Setup nao encontrado; Setup nao foi gerado.
)

if exist "%ROOT%\app.res" del /f /q "%ROOT%\app.res"


echo.
echo ========================================
echo          BUILD CONCLUIDA!
echo ========================================
echo.
echo Executavel: "%OUT%\Moon Launcher.exe"
echo Pacote de update: "%ZIP%"
echo Pasta de runtime: "%OUT%"
echo.
pause
exit /b 0


:ERROR

echo.
echo ========================================
echo             BUILD FALHOU
echo ========================================
echo.
echo O erro do compilador esta acima.
echo.
pause
exit /b 1