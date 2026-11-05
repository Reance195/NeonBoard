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

[Files]
Source: "..\dist\NeonBoard\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
; 弃用文件清单随包带到 {tmp}，由 [Code] 在 ssPostInstall 读它做清理（见 CleanObsoleteFiles）。
; 唯一事实源是 installer/obsolete-files.txt —— 增量侧读同一份（scripts/build-delta.ps1 打进增量包）。
Source: "obsolete-files.txt"; DestDir: "{tmp}"; Flags: ignoreversion deleteafterinstall

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
  没有这份物证，安装器静默失败与成功在日志里长得一模一样。

  Inno Setup 6 只有 Unicode 版：LoadStringFromFile / SaveStringToFile 的字符串参数
  是 AnsiString，不是 String。用 String 变量传进去会在**编译期**报 Type mismatch ——
  v0.1.5 第一次发布就栽在这里（iss:117），而 iss 的 [Code] 段只有 ISCC 会编译，
  verify.ps1 不跑 ISCC、单测只 grep 文本，本地全绿也拦不住。
  所以这里一律走 TArrayOfString 的 LoadStringsFromFile / SaveStringsToFile。 }
{ 弃用文件清理：清单是 installer/obsolete-files.txt（随包落到临时目录），与增量侧同一份。
  为什么不能只靠 [InstallDelete] 写死在脚本里 —— 两处各写一遍必然漂，而增量路径
  （自更新 bat）根本不跑安装器，0.1.3 一路增量升上来的用户会永久背着 1136 个文件。
  语义与 C++ 侧 cleanupObsoleteFiles 一致：缺席即已清理，删不动（被占用）只跳过，
  绝不因为清理失败而让一次成功的安装变成失败。 }
procedure CleanObsoleteFiles;
var
  Lines: TArrayOfString;
  ListPath, P, Full: String;
  Deleted, Skipped, I: Integer;
begin
  ListPath := ExpandConstant('{tmp}\obsolete-files.txt');
  if not LoadStringsFromFile(ListPath, Lines) then begin
    Log('NeonBoard obsolete cleanup: list not readable, nothing to do');
    Exit;
  end;
  Deleted := 0;
  Skipped := 0;
  for I := 0 to GetArrayLength(Lines) - 1 do begin
    P := Trim(Lines[I]);
    if (P = '') or (Copy(P, 1, 1) = '#') or (Copy(P, 1, 1) = ';') then
      Continue;
    { 清单只允许安装目录内的相对路径：绝对路径、盘符、含 .. 的一律丢掉 }
    if (Pos(':', P) > 0) or (Pos('..', P) > 0) then begin
      Log('NeonBoard obsolete cleanup: rejected unsafe path ' + P);
      Continue;
    end;
    StringChange(P, '/', '\');
    Full := ExpandConstant('{app}\' + P);
    if DirExists(Full) then begin
      if DelTree(Full, False, True, True) and RemoveDir(Full) then
        Deleted := Deleted + 1
      else begin
        Skipped := Skipped + 1;
        Log('NeonBoard obsolete cleanup: could not remove dir ' + Full);
      end;
    end else if FileExists(Full) then begin
      if DeleteFile(Full) then
        Deleted := Deleted + 1
      else begin
        Skipped := Skipped + 1;
        Log('NeonBoard obsolete cleanup: could not remove file ' + Full);
      end;
    end;
  end;
  Log('NeonBoard obsolete cleanup: deleted=' + IntToStr(Deleted) +
      ' skipped=' + IntToStr(Skipped));
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Lines: TArrayOfString;
  Receipt: TArrayOfString;
  Commit: String;
  Line: String;
  P: Integer;
  I: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;
  CleanObsoleteFiles;
  Commit := 'unknown';
  { BUILD-INFO.txt 由发布闸门第 5 条产出，行形如「commit    : <sha>」；文件缺席
    （CI 目前不把它打进安装目录）就留 unknown，回执照样能判版本。
    注意：Pascal 注释到第一个右花括号就结束，注释里千万别写 Inno 常量（形如
    左花括号 app 右花括号），否则后半句会被当代码解析 —— v0.1.5 第二次发布栽在这里。 }
  if LoadStringsFromFile(ExpandConstant('{app}\BUILD-INFO.txt'), Lines) then begin
    for I := 0 to GetArrayLength(Lines) - 1 do begin
      Line := Trim(Lines[I]);
      if Pos('commit', Lowercase(Line)) = 1 then begin
        P := Pos(':', Line);
        if P > 0 then
          Commit := Trim(Copy(Line, P + 1, Length(Line)));
        Break;
      end;
    end;
  end;
  SetArrayLength(Receipt, 1);
  Receipt[0] := '{"version": "{#MyAppVersion}"' +
    ', "writtenAt": "' + GetDateTimeString('yyyy-mm-dd hh:nn:ss', '-', ':') + '"' +
    ', "commit": "' + Commit + '"' +
    ', "source": "inno"}';
  SaveStringsToFile(ExpandConstant('{app}\install-receipt.json'), Receipt, False);
end;
