#define MyAppName          "Phonometrica"
#define MyAppVersion       "0.9"
#define MyAppPublisher     "Julien Eychenne & Léa Courdès-Murphy"
#define MyAppURL           "http://www.phonometrica-ling.org"
#define MyAppExeName       "phonometrica.exe"
#define SourceRoot         "C:\Users\Julien\CLionProjects\phonometrica"
#define BuildDir           "C:\Devel\phon-deploy"

[Setup]
AppId={{BCFF6817-CA0C-4A62-890F-BB2BFA35E2D3}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DisableDirPage=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
LicenseFile={#SourceRoot}\LICENSE
OutputBaseFilename=phonometrica-{#MyAppVersion}-setup-x64
Compression=lzma2/ultra64
SolidCompression=yes
OutputDir={#SourceRoot}\dist
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile={#SourceRoot}\icons\phonometrica.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The main executable and everything windeployqt placed next to it (Qt DLLs, plugins, platforms/, etc.)
Source: "{#BuildDir}\*";      DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

; License
Source: "{#SourceRoot}\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent