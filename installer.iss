[Setup]
AppName=USBIP Client
AppVersion=1.0.1
DefaultDirName={autopf}\USBIP Client
DefaultGroupName=USBIP Client
OutputBaseFilename=USBIPClient_Installer
OutputDir=dist
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Main executable and root DLLs
Source: "build\Release\USBIPClient.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion

; CRITICAL: Copy the Qt platforms plugin folder (this fixes the error)
Source: "build\Release\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs

; Drivers and Database
Source: "build\Release\Drivers\*"; DestDir: "{app}\Drivers"; Flags: ignoreversion recursesubdirs
Source: "usb.ids"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\USBIP Client"; Filename: "{app}\USBIPClient.exe"
Name: "{autodesktop}\USBIP Client"; Filename: "{app}\USBIPClient.exe"; Tasks: desktopicon

[Run]
; Optional: Force launch after install
Filename: "{app}\USBIPClient.exe"; Description: "{cm:LaunchProgram,USBIP Client}"; Flags: nowait postinstall skipifsilent