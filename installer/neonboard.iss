#define MyAppName "NeonBoard"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "Lin"
#define MyAppExeName "NeonBoard.exe"

[Setup]
AppId={{8E3B6A2C-9F4D-4C1A-9B7E-2D5F6A8C1E30}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\NeonBoard
DefaultGroupName=NeonBoard
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=NeonBoard-Setup-{#MyAppVersion}
SetupIconFile=..\resources\icons\neonboard.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional tasks:"; Flags: unchecked

[InstallDelete]
Type: filesandordirs; Name: "{app}\plugins\hello"

[Files]
Source: "..\dist\NeonBoard\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\NeonBoard"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall NeonBoard"; Filename: "{uninstallexe}"
Name: "{userdesktop}\NeonBoard"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch NeonBoard"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
