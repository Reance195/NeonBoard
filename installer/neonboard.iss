#define MyAppName "NeonBoard"
; 版本号的唯一来源是 CMakeLists 的 project(VERSION)：构建脚本解析后以
; /DMyAppVersion=<版本> 传入。下面的默认值只在手工编译安装包时兜底，
; 发版请改 CMakeLists，不要只改这里（否则安装包版本与程序自报版本会分叉）。
#ifndef MyAppVersion
#define MyAppVersion "0.1.1"
#endif
#define MyAppPublisher "Lin"
#define MyAppExeName "NeonBoard.exe"
#define MyAppHostName "NeonBoardHost.exe"

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
Name: "{group}\NeonBoard"; Filename: "{app}\{#MyAppHostName}"; Parameters: "--open"
Name: "{group}\Uninstall NeonBoard"; Filename: "{uninstallexe}"
Name: "{userdesktop}\NeonBoard"; Filename: "{app}\{#MyAppHostName}"; Parameters: "--open"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppHostName}"; Parameters: "--open"; Description: "Launch NeonBoard"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
