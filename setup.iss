#define MyAppName "Moon Launcher"
; Passed by build.bat (/DMyAppVersion=<Project::VERSION>); fallback when compiling by hand.
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#define MyAppPublisher "The Moon Crew"
#define MyAppExeName "Moon Launcher.exe"

[Setup]
AppId={{1B8C9F35-5FC8-4A4E-949A-28DB63B6D9A1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\Moon Launcher
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
CloseApplications=yes
RestartApplications=no
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\{#MyAppExeName}
SetupIconFile=assets\app\icons\app.ico
OutputDir=export\windows\release
OutputBaseFilename=Moon-Launcher-Setup-v{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Dirs]
Name: "{app}\mods"; Flags: uninsneveruninstall
Name: "{app}\com.funkinmoon\data\saves"; Flags: uninsneveruninstall
Name: "{app}\com.funkinmoon\versions"; Flags: uninsneveruninstall

[Files]
Source: "export\windows\release\bin\*"; DestDir: "{app}"; Excludes: "launcher-settings.json,launcher-update.log,Moon Launcher.building.exe,mods\*,com.funkinmoon\data\saves\*,com.funkinmoon\versions\*"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "launcher-settings.json"; DestDir: "{app}"; Flags: onlyifdoesntexist

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
