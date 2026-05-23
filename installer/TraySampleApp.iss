#define MyAppName "Tray Sample App"
#define MyAppPublisher "ZIVPO"
#define MyServiceName "TraySampleService"
#define MyServiceExe "TraySampleService.exe"
#define MyAppExe "TraySampleApp.exe"
#ifndef AppVersion
#define AppVersion "0.0.1"
#endif

[Setup]
AppId={{B5C9D7A0-2F9B-4F5E-9D2C-7E1F4B8A6D11}
AppName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppVersion={#AppVersion}
DefaultDirName={autopf}\TraySampleApp
DisableProgramGroupPage=yes
OutputDir=..\build\installer\output
OutputBaseFilename=TraySampleApp-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\{#MyAppExe}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\installer\package\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Run]
Filename: "sc.exe"; Parameters: "stop {#MyServiceName}"; Flags: runhidden waituntilterminated; StatusMsg: "Stopping existing service (if any)..."; RunOnceId: "StopBeforeInstall"
Filename: "{app}\{#MyServiceExe}"; Parameters: "/uninstall"; Flags: runhidden waituntilterminated; StatusMsg: "Removing existing service registration..."; RunOnceId: "UninstallBeforeInstall"
Filename: "{app}\{#MyServiceExe}"; Parameters: "/install"; Flags: runhidden waituntilterminated; StatusMsg: "Registering Windows service..."; RunOnceId: "InstallService"
Filename: "sc.exe"; Parameters: "start {#MyServiceName}"; Flags: runhidden waituntilterminated; StatusMsg: "Starting Windows service..."; RunOnceId: "StartService"

[UninstallRun]
Filename: "sc.exe"; Parameters: "stop {#MyServiceName}"; Flags: runhidden waituntilterminated; RunOnceId: "StopOnUninstall"
Filename: "{app}\{#MyServiceExe}"; Parameters: "/uninstall"; Flags: runhidden waituntilterminated; RunOnceId: "UninstallService"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
