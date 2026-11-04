#define MyAppName "NeonBoard"
; 版本号唯一来源是 CMakeLists.txt 的 project(NeonBoard VERSION x.y.z)：scripts/release.ps1
; 与 .github/workflows/release.yml 解析后以 /DMyAppVersion=<x.y.z> 传入。本文件不写版本字面量 ——
; 写死一处就多一个漏改点，而漏改的后果是「装上了新版却仍自报旧版本」，更新检查会判「已是最新」。
#ifndef MyAppVersion
  #error MyAppVersion is not defined: build via scripts/release.ps1 or pass /DMyAppVersion=<x.y.z>
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
; 2026-11-04 真机实测：Restart Manager 关不掉带看门狗的宿主 NeonBoardHost.exe
; （界面被关后又被宿主的 checkIdle 复活），静默模式整单悄悄失败、交互模式逐文件
; DeleteFile code 5，留下新旧混装树。占用清理改由 PrepareToInstall 的 taskkill
; 负责（见 [Code]），关不掉就响亮地中止，不留半装。
CloseApplications=no
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional tasks:"; Flags: unchecked

[InstallDelete]
; 历史遗留：早期版本自带过一个 hello 示例插件，早已从仓库移除；升级时顺手清掉，
; 免得老用户的 plugins/ 里一直躺着一个加载失败的目录。
; 正常情况下不需要在这里逐项列插件 —— [Files] 是整目录覆盖，残留只会来自
; 「新版本不再包含」的旧目录（如本条），新增插件不必改这里。
Type: filesandordirs; Name: "{app}\plugins\hello"

; 0.1.4 起（8e52812：windeployqt 只留 Basic 样式）不再打包 Qt 自带的其它控件样式，
; 但 Inno 覆盖安装只写新包里的文件、不删新包里没有的文件 —— 2026-11-04 真机取证：
; 覆盖装完 0.1.4 后，按已发布 delta-0.1.3-0.1.4 清单逐文件复核，Modified 2/2 与
; Added 15/15 全对，Deleted 的 1136 条**一个都没少**（六套样式目录共 1126 个
; qml/dll + root 级 10 个样式 dll）。功能上无害（应用只用 Basic），但每个从 0.1.3
; 升上来的用户都会永久背着它们。下面这份清单就是那 1136 条的收敛形式：
; 目录级 6 条覆盖 1126 个文件，文件级 10 条覆盖 root 的样式 dll。
; **不要改成 Qt6QuickControls2*Style*.dll 这类通配** —— 0.1.4 仍在用的
; Qt6QuickControls2BasicStyleImpl.dll 会被一并删掉，样式直接加载失败。
Type: filesandordirs; Name: "{app}\qml\QtQuick\Controls\FluentWinUI3"
Type: filesandordirs; Name: "{app}\qml\QtQuick\Controls\Material"
Type: filesandordirs; Name: "{app}\qml\QtQuick\Controls\Fusion"
Type: filesandordirs; Name: "{app}\qml\QtQuick\Controls\Universal"
Type: filesandordirs; Name: "{app}\qml\QtQuick\Controls\Imagine"
Type: filesandordirs; Name: "{app}\qml\QtQuick\Controls\Windows"
Type: files; Name: "{app}\Qt6QuickControls2FluentWinUI3StyleImpl.dll"
Type: files; Name: "{app}\Qt6QuickControls2Fusion.dll"
Type: files; Name: "{app}\Qt6QuickControls2FusionStyleImpl.dll"
Type: files; Name: "{app}\Qt6QuickControls2Imagine.dll"
Type: files; Name: "{app}\Qt6QuickControls2ImagineStyleImpl.dll"
Type: files; Name: "{app}\Qt6QuickControls2Material.dll"
Type: files; Name: "{app}\Qt6QuickControls2MaterialStyleImpl.dll"
Type: files; Name: "{app}\Qt6QuickControls2Universal.dll"
Type: files; Name: "{app}\Qt6QuickControls2UniversalStyleImpl.dll"
Type: files; Name: "{app}\Qt6QuickControls2WindowsStyleImpl.dll"

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

[Code]
{ 2026-11-04 真机实测：带看门狗的宿主会让 Restart Manager 永远关不净，
  占用清理只能自己动手。客户端拉起安装包前已杀过一次宿主，这里兜底再杀，
  ewWaitUntilTerminated 保证返回时进程已死、文件锁已释放。 }
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  R: Integer;
begin
  Result := '';
  NeedsRestart := False;
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM NeonBoardHost.exe', '',
       SW_HIDE, ewWaitUntilTerminated, R);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM NeonBoard.exe', '',
       SW_HIDE, ewWaitUntilTerminated, R);
end;

{ 安装回执：启动时读它，版本与自身编译版本不符即报「上次更新未完成」。
  没有这份物证，安装器静默失败与成功在日志里长得一模一样。 }
procedure CurStepChanged(CurStep: TSetupStep);
var
  Info: String;
  Commit: String;
  P: Integer;
  Q: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;
  Commit := 'unknown';
  if LoadStringFromFile(ExpandConstant('{app}\BUILD-INFO.txt'), Info) then begin
    P := Pos('commit', Info);
    if P > 0 then begin
      Q := Pos(':', Copy(Info, P, Length(Info)));
      if Q > 0 then begin
        Commit := Trim(Copy(Info, P + Q, 80));
        Q := Pos(#13, Commit);
        if Q > 0 then
          Commit := Copy(Commit, 1, Q - 1);
        Q := Pos(#10, Commit);
        if Q > 0 then
          Commit := Copy(Commit, 1, Q - 1);
        Commit := Trim(Commit);
      end;
    end;
  end;
  SaveStringToFile(ExpandConstant('{app}\install-receipt.json'),
    '{"version": "{#MyAppVersion}"' +
    ', "writtenAt": "' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', #0, #0) + '"' +
    ', "commit": "' + Commit + '"' +
    ', "source": "inno"}', False);
end;
