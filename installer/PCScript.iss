; PCScript installer - built with Inno Setup 6 (https://jrsoftware.org/isinfo.php)
;   ISCC.exe /DBinDir="<folder containing pcscript.exe>" installer\PCScript.iss
; Output: out\PCScript-Setup.exe

#define AppName "PCScript"
#define AppVersion "2.0.0"
#define AppPublisher "PCScript"
#ifndef BinDir
  #define BinDir "..\build\bin"
#endif

[Setup]
AppId={{AA9BF083-3E58-4C74-854F-8866B32ABA2E}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\PCScript
DefaultGroupName=PCScript
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName} {#AppVersion}
UninstallDisplayIcon={app}\assets\app.ico
SetupIconFile=..\assets\app.ico
WizardStyle=modern
WizardImageFile=..\assets\installer_wizard.bmp
WizardSmallImageFile=..\assets\installer_small.bmp
LicenseFile=..\LICENSE
OutputDir=..\out
OutputBaseFilename=PCScript-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
ChangesEnvironment=yes
VersionInfoVersion=2.0.0.0
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
VersionInfoCompany={#AppPublisher}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "addtopath"; Description: "Add PCScript to the &PATH (type ""pcscript"" in any Command Prompt)"; GroupDescription: "System integration:"
Name: "associate"; Description: "Associate &.pcs files with PCScript (double-click runs the program, right-click > Edit opens PCScript Studio)"; GroupDescription: "System integration:"
Name: "desktopicon"; Description: "Create a &desktop shortcut for PCScript Studio"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#BinDir}\pcscript.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\pcscript-studio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\extensions\*"; DestDir: "{app}\extensions"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "pcscript-prompt.cmd"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\assets\logo_original.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\app.ico"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\assets\pcs_file.ico"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "..\docs\*.md"; DestDir: "{app}\docs"; Flags: ignoreversion
; Examples go to the user's Documents so they are writable (the file-handling example creates files).
Source: "..\examples\*.pcs"; DestDir: "{userdocs}\PCScript\Examples"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{autoprograms}\PCScript\PCScript Studio"; Filename: "{app}\pcscript-studio.exe"; IconFilename: "{app}\assets\app.ico"
Name: "{autodesktop}\PCScript Studio"; Filename: "{app}\pcscript-studio.exe"; IconFilename: "{app}\assets\app.ico"; Tasks: desktopicon
Name: "{autoprograms}\PCScript\PCScript Command Prompt"; Filename: "{app}\pcscript-prompt.cmd"; WorkingDir: "{userdocs}\PCScript\Examples"; IconFilename: "{app}\assets\app.ico"
Name: "{autoprograms}\PCScript\PCScript Examples"; Filename: "{userdocs}\PCScript\Examples"
Name: "{autoprograms}\PCScript\PCScript Documentation"; Filename: "{app}\docs"
Name: "{autoprograms}\PCScript\Uninstall PCScript"; Filename: "{uninstallexe}"

[Registry]
; .pcs file type (per-user or all-users, matching the install mode)
Root: HKA; Subkey: "Software\Classes\.pcs"; ValueType: string; ValueName: ""; ValueData: "PCScript.Source"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source"; ValueType: string; ValueName: ""; ValueData: "PCScript Program"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\assets\pcs_file.ico"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\shell"; ValueType: string; ValueName: ""; ValueData: "open"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\shell\open"; ValueType: string; ValueName: ""; ValueData: "Run with PCScript"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\pcscript.exe"" ""%1"""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\shell\runterminal"; ValueType: string; ValueName: ""; ValueData: "Run in PCScript terminal (keep open)"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\shell\runterminal\command"; ValueType: string; ValueName: ""; ValueData: "cmd.exe /k """"{app}\pcscript.exe"" ""%1"""""; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\shell\edit"; ValueType: string; ValueName: ""; ValueData: "Edit in PCScript Studio"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\PCScript.Source\shell\edit\command"; ValueType: string; ValueName: ""; ValueData: """{app}\pcscript-studio.exe"" ""%1"""; Tasks: associate
; PATH
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: IsAdminInstallMode and NeedsAddPath; Tasks: addtopath
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: (not IsAdminInstallMode) and NeedsAddPath; Tasks: addtopath

[Run]
Filename: "{app}\pcscript-prompt.cmd"; Description: "Open a PCScript command prompt in the examples folder"; WorkingDir: "{userdocs}\PCScript\Examples"; Flags: postinstall nowait skipifsilent shellexec

[Code]
function EnvRoot: Integer;
begin
  if IsAdminInstallMode then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function EnvKey: String;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

function NeedsAddPath: Boolean;
var
  Existing: String;
begin
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Existing) then
  begin
    Result := True;
    Exit;
  end;
  Result := Pos(';' + Uppercase(ExpandConstant('{app}')) + ';', ';' + Uppercase(Existing) + ';') = 0;
end;

procedure RemoveFromPath;
var
  Existing, Dir: String;
  P: Integer;
begin
  Dir := ExpandConstant('{app}');
  if not RegQueryStringValue(EnvRoot, EnvKey, 'Path', Existing) then Exit;
  P := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Existing) + ';');
  if P = 0 then Exit;
  if P > 1 then
    Delete(Existing, P - 1, Length(Dir) + 1)   { remove ";<dir>" }
  else
    Delete(Existing, 1, Length(Dir) + 1);      { <dir> was first: remove "<dir>;" }
  RegWriteExpandStringValue(EnvRoot, EnvKey, 'Path', Existing);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then RemoveFromPath;
end;
