; Inno Setup script for Pokemon Autochess
; Build the release bundle first:
;   .\tools\release_bundle.ps1

#define MyAppName "Pokemon Autochess"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Adam Wentworth"
#define MyAppExeName "PokemonAutochess.exe"
#define MyAppSourceDir "..\\dist\\Release"

[Setup]
AppId={{6A0D67C2-8F84-4C58-9B8C-7D7E2AE0C0C2}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppComments=Non-profit portfolio project. Not affiliated with Nintendo/Game Freak/The Pokemon Company.
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\\dist\\installer
OutputBaseFilename=PokemonAutochessSetup
; Optional: set a custom icon when you have a .ico file
; SetupIconFile=path\to\PokemonAutochess.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop icon"; Flags: unchecked
Name: "startmenuicon"; Description: "Create a &Start Menu shortcut"; Flags: checkedonce

[Files]
Source: "{#MyAppSourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startmenuicon
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"; Tasks: startmenuicon
