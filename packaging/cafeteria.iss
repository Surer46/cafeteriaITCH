; Guion de Inno Setup para Cafeteria ITCH.
;
; ESTADO: NO VERIFICADO. Inno Setup no esta instalado en la PC de desarrollo; este guion no se ha
; compilado ni ejecutado. Un instalador generado con el solo debe probarse en una PC de prueba
; (con Windows 7 SP1 si esa es la caja) antes de usarse.
;
; Compilar (cuando se apruebe instalar Inno Setup 6.x), desde la raiz del repositorio:
;
;   1. Preparar el paquete (solo componente "app": SIN portable.flag):
;        cmake --install build_be_release --config Release --component app --prefix dist\CafeteriaITCH
;   2. Compilar el instalador:
;        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=0.1.0 /DSourceDir=dist\CafeteriaITCH /O dist packaging\cafeteria.iss
;
;   AppVersion debe ser la de project(VERSION) en CMakeLists.txt. SourceDir es relativo a este
;   archivo o absoluto; ISCC resuelve las rutas relativas respecto a la carpeta del .iss, asi que
;   conviene pasar SourceDir con ruta absoluta.
;
; Reglas de este instalador:
;   - Solo Windows de 64 bits, minimo Windows 7 SP1. En 32 bits se niega con un mensaje claro.
;   - Los datos viven en {commonappdata}\CafeteriaITCH\data. La carpeta se crea solo si no existe,
;     con permiso de modificacion para Usuarios. Actualizar y desinstalar NUNCA la borran ni la mueven.
;   - No instala portable.flag: una copia instalada guarda en {commonappdata}, no junto al exe.
;   - Sin servicios, sin tareas programadas, sin red, sin cuentas ni contrasenas. No ejecuta la
;     aplicacion al terminar.

#ifndef AppVersion
  #error Falta /DAppVersion=<version de project(VERSION)>
#endif
#ifndef SourceDir
  #error Falta /DSourceDir=<carpeta producida por cmake --install --component app>
#endif

[Setup]
; GUID fijo: identifica la aplicacion para actualizar/desinstalar. No cambiarlo entre versiones.
AppId={{6F0B1D3E-5C2A-4B7E-9A41-CAFE7E41C401}
AppName=Cafeteria ITCH
AppVersion={#AppVersion}
AppPublisher=Cafeteria ITCH
DefaultDirName={autopf}\CafeteriaITCH
DefaultGroupName=Cafeteria ITCH
DisableProgramGroupPage=yes
OutputBaseFilename=CafeteriaITCH-{#AppVersion}-instalador-win64
Compression=lzma2
SolidCompression=yes
; Solo 64 bits: el ejecutable es x64 y no arrancaria en un Windows de 32 bits.
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Windows 7 SP1 (6.1.7601) como minimo.
MinVersion=6.1sp1
; Instala para todo el equipo (necesario para {commonappdata} y los accesos de todos los usuarios).
PrivilegesRequired=admin
UninstallDisplayName=Cafeteria ITCH
WizardStyle=classic
; Sin icono aprobado todavia: no se define SetupIconFile ni UninstallDisplayIcon (pendiente).

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Dirs]
; Carpeta de datos: se crea solo si no existe y uninsneveruninstall impide que el desinstalador la
; borre. Permiso de modificacion para Usuarios.
Name: "{commonappdata}\CafeteriaITCH\data"; Permissions: users-modify; Flags: uninsneveruninstall

[Files]
; Todo el contenido del paquete (CafeteriaITCH.exe, DLL, migrations\, assets\, licencias\, paquete.manifest).
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Cafeteria ITCH"; Filename: "{app}\CafeteriaITCH.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Cafeteria ITCH"; Filename: "{app}\CafeteriaITCH.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el Escritorio"; Flags: unchecked

; No hay seccion [Run]: no se ejecuta la aplicacion al terminar.
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
