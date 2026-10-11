; Installer for ComicsDisplayExp (Inno Setup 6).
; Build it with installer\build-setup.ps1, which passes the defines below.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.1.0.0"
#endif
#ifndef Platform
  #define Platform "x64"
#endif
#ifndef SourceDir
  #define SourceDir "..\" + Platform + "\Release\ComicsDisplayExp"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#define AppName "ComicsDisplayExp"
#define AppExe "ComicsDisplayExp.exe"

[Setup]
; Never change AppId: it is how upgrades find the existing installation.
AppId={{8FA77095-279A-47C1-8A3E-F0DEC75A4582}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
AppPublisherURL=https://github.com/fkorak/ComicsDisplayExp
AppSupportURL=https://github.com/fkorak/ComicsDisplayExp/issues
VersionInfoVersion={#NumericVersion}
; Per-user install by default (no admin prompt); the user may choose "all users".
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
#if Platform == "ARM64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
MinVersion=10.0.19041
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-Setup-{#AppVersion}-{#Platform}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Icons are drawn by ComicsDisplayExp\Assets\make-icons.ps1.
SetupIconFile=..\ComicsDisplayExp\Assets\AppIcon.ico
WizardSmallImageFile=WizardSmall-55.bmp,WizardSmall-64.bmp,WizardSmall-69.bmp,WizardSmall-83.bmp,WizardSmall-92.bmp,WizardSmall-110.bmp,WizardSmall-119.bmp,WizardSmall-138.bmp
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
; Close a running viewer before replacing its files (upgrades).
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "defaultviewer"; Description: "Make {#AppName} the default viewer for comic books (CBZ, CBR, CB7, CBT)"; GroupDescription: "File associations:"
Name: "openwith"; Description: "Add {#AppName} to ""Open with"" for ZIP, RAR and 7Z (their default app stays the same)"; GroupDescription: "File associations:"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The self-contained app folder (Windows App SDK runtime included); no debug/build leftovers.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "*.pdb,*.lib,*.exp"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; File associations are written by the app itself (per user), so installer, menu and
; uninstaller all share one implementation.
Filename: "{app}\{#AppExe}"; Parameters: "--register-comics"; Tasks: defaultviewer; Flags: waituntilterminated runasoriginaluser; StatusMsg: "Registering comic book file types..."
Filename: "{app}\{#AppExe}"; Parameters: "--register-archives"; Tasks: openwith; Flags: waituntilterminated runasoriginaluser; StatusMsg: "Adding to ""Open with""..."
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--unregister"; Flags: waituntilterminated; RunOnceId: "RemoveFileAssociations"
