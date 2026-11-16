; Guion de Inno Setup 7 para Cafeteria ITCH.
;
; Compilar (desde la raiz del repositorio, con una compilacion Release ya hecha):
;     cmake --build build_be_release --target installer
; Eso instala el componente "app" (SIN portable.flag) en build_be_release\installer_stage\CafeteriaITCH
; y llama a ISCC.exe con /DAppVersion=<version de project(VERSION)> y /DSourceDir=<esa carpeta>.
; El instalador queda en build_be_release\installer_out. Ver cmake\build_installer.cmake y docs\entorno.md.
;
; Reglas de este instalador:
;   - Solo Windows de 64 bits, minimo Windows 7 SP1. En 32 bits se niega con un mensaje claro.
;   - Los datos viven en {commonappdata}\CafeteriaITCH\data. La carpeta se crea solo si no existe, con permiso
;     de modificacion para Usuarios. Actualizar y desinstalar NUNCA la borran ni la mueven.
;   - No instala portable.flag: una copia instalada guarda en {commonappdata}, no junto al exe.
;   - Sin servicios, sin tareas programadas, sin red, sin cuentas ni contrasenas.
;   - Instalacion normal: no abre la aplicacion al terminar.
;   - Actualizacion silenciosa (/VERYSILENT /SUPPRESSMSGBOXES /NORESTART) sobre una version anterior: sin
;     preguntas, cierra CafeteriaITCH.exe si esta abierto, conserva los datos y, solo si se pasa /REABRIR, vuelve
;     a abrir el programa al terminar (con los permisos del usuario que lanzo el instalador, no elevado).
;   - El AppId es el mismo en todas las versiones: instalar otra version encima es una actualizacion.

#ifndef AppVersion
  #error Falta /DAppVersion=<version de project(VERSION)>
#endif
#ifndef SourceDir
  #error Falta /DSourceDir=<carpeta producida por cmake --install --component app>
#endif

[Setup]
; GUID fijo: identifica la aplicacion para actualizar/desinstalar. NO cambiarlo entre versiones.
AppId={{6F0B1D3E-5C2A-4B7E-9A41-CAFE7E41C401}
AppName=Cafeteria ITCH
AppVersion={#AppVersion}
AppPublisher=Cafeteria ITCH
VersionInfoVersion={#AppVersion}
VersionInfoDescription=Instalador de Cafeteria ITCH
DefaultDirName={autopf}\CafeteriaITCH
DefaultGroupName=Cafeteria ITCH
DisableProgramGroupPage=yes
OutputBaseFilename=CafeteriaITCH-{#AppVersion}-instalador-win64
Compression=lzma2
SolidCompression=yes
; Inno Setup 7: "x64" ya no es el identificador recomendado; x64compatible incluye x64 y Windows 11 Arm64 (que
; ejecuta x64 por emulacion). El asistente sigue siendo de 32 bits (SetupArchitecture por omision) para poder
; mostrar un mensaje claro en un Windows de 32 bits en lugar de fallar al cargar.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 7 SP1 (6.1.7601) como minimo (es tambien el valor por omision de Inno Setup 7).
MinVersion=6.1sp1
; Instala para todo el equipo (necesario para {commonappdata} y los accesos de todos los usuarios).
PrivilegesRequired=admin
; Un solo instalador a la vez.
SetupMutex=CafeteriaITCHSetup
; Cierra CafeteriaITCH.exe si esta abierto (Restart Manager de Windows). Con /VERYSILENT lo hace siempre, sin
; preguntar. "yes" y no "force": primero pide el cierre normal, para no perder trabajo a medias.
CloseApplications=yes
CloseApplicationsFilter=CafeteriaITCH.exe
; El programa no usa RegisterApplicationRestart, asi que Inno no puede reabrirlo solo: la reapertura la hace
; la entrada [Run] de abajo, y solo con /REABRIR.
RestartApplications=no
UninstallDisplayName=Cafeteria ITCH
WizardStyle=classic
; Sin icono aprobado todavia: no se define SetupIconFile ni UninstallDisplayIcon (pendiente).

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Dirs]
; Carpeta de datos: se crea solo si no existe y uninsneveruninstall impide que el desinstalador la borre.
Name: "{commonappdata}\CafeteriaITCH\data"; Permissions: users-modify; Flags: uninsneveruninstall

[Files]
; Todo el contenido del paquete (CafeteriaITCH.exe, DLL, migrations\, assets\, licencias\, paquete.manifest).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Cafeteria ITCH"; Filename: "{app}\CafeteriaITCH.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Cafeteria ITCH"; Filename: "{app}\CafeteriaITCH.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el Escritorio"; Flags: unchecked

[Run]
; Solo en una instalacion silenciosa con /REABRIR (la usa la actualizacion automatica). En una instalacion normal
; la condicion es falsa: no hay casilla ni se abre nada. runasoriginaluser: el programa se abre con los permisos
; del usuario que lanzo el instalador, no con los del administrador que elevo.
Filename: "{app}\CafeteriaITCH.exe"; WorkingDir: "{app}"; Flags: nowait postinstall runasoriginaluser; Check: ReopenAfterSilentInstall

; No hay [UninstallDelete] sobre {commonappdata}: desinstalar nunca borra los datos.

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  // Segunda barrera para un Windows de 32 bits: mensaje claro en vez de instalar algo que no arranca.
  if not Is64BitInstallMode then
  begin
    MsgBox('Este programa necesita Windows de 64 bits. Este equipo es de 32 bits, asi que no se instalara nada.',
           mbError, MB_OK);
    Result := False;
  end;
end;

// Antes de copiar nada se pide el cierre NORMAL de CafeteriaITCH.exe (taskkill sin /F envia WM_CLOSE a sus
// ventanas, igual que cerrarlo con la X). Se repite hasta 20 segundos; taskkill devuelve 128 cuando ya no hay
// ningun proceso con ese nombre. Si algo lo impidiera, queda el Restart Manager de CloseApplications=yes.
// Motivo: en la prueba B10, el cierre por Restart Manager (WM_QUERYENDSESSION/WM_ENDSESSION) hizo caer al
// programa con 0xC0000374 (corrupcion del heap) mientras estaba en el alta del Dueno; el cierre con WM_CLOSE no.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode, Attempt: Integer;
begin
  Result := '';
  for Attempt := 1 to 40 do
  begin
    if not Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM CafeteriaITCH.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      Break;                       // taskkill no disponible: que siga el Restart Manager
    if ResultCode = 128 then
      Break;                       // ya no hay ningun CafeteriaITCH.exe abierto
    Sleep(500);
  end;
end;

// Desinstalar con el programa abierto: mismo cierre NORMAL (taskkill sin /F, nunca forzado) hasta 20 segundos. Si
// despues sigue abierto, el desinstalador se niega ANTES de borrar nada, asi que no queda nada a medias.
function InitializeUninstall(): Boolean;
var
  ResultCode, Attempt: Integer;
  StillOpen: Boolean;
begin
  Result := True;
  StillOpen := False;
  for Attempt := 1 to 40 do
  begin
    if not Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM CafeteriaITCH.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    begin
      StillOpen := False;          // taskkill no disponible: no se puede comprobar, se continua
      Break;
    end;
    if ResultCode = 128 then
    begin
      StillOpen := False;          // ya no hay ningun CafeteriaITCH.exe abierto
      Break;
    end;
    StillOpen := True;
    Sleep(500);
  end;
  if StillOpen then
  begin
    MsgBox('Cierre Cafeteria ITCH y vuelva a intentarlo. No se desinstalo nada.', mbError, MB_OK);
    Result := False;
  end;
end;

// Verdadero solo si el instalador corre en modo silencioso Y se le paso /REABRIR.
function ReopenAfterSilentInstall(): Boolean;
var
  I: Integer;
begin
  Result := False;
  if not WizardSilent then Exit;
  for I := 1 to ParamCount do
    if CompareText(ParamStr(I), '/REABRIR') = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

// Rechaza instalar sobre una copia portable (portable.flag) en la misma carpeta: mezclaria
// los datos de las dos ubicaciones.
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = wpSelectDir) and FileExists(AddBackslash(WizardDirValue) + 'portable.flag') then
  begin
    MsgBox('La carpeta elegida contiene una copia portable (portable.flag). ' +
           'Elija otra carpeta para no mezclar los datos de las dos copias.', mbError, MB_OK);
    Result := False;
  end;
end;
