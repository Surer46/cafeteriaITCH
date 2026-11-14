# ficha_pc.ps1 - Ficha de la PC de caja (Cafeteria ITCH).
#
# Solo LEE informacion del equipo y escribe un archivo de texto. No instala, no cambia ninguna
# configuracion, no usa la red y no pide permisos de administrador.
# Compatible con PowerShell 2.0 (Windows 7 sin actualizar): sin cmdlets ni sintaxis posteriores.
# Archivo solo ASCII a proposito, para que cualquier pagina de codigos lo lea igual.
#
# NO recoge: nombres de personas ni de cuentas, nombre del equipo, numeros de serie, ni direcciones de red.
# Si junto a este guion esta el paquete de la aplicacion (CafeteriaITCH.exe), tambien ejecuta su
# autocomprobacion (--self-check) y anade el resultado.

$ErrorActionPreference = 'Continue'
$NA = 'no disponible'
$script:lines = New-Object System.Collections.ArrayList

# Ultima barrera de privacidad: identificadores de cuenta (SID) y nombre de usuario de Windows en rutas.
function Scrub-Text($text) {
    $t = [string]$text
    $t = [regex]::Replace($t, 'S-1-\d+(-\d+)+', '(identificador omitido)')
    $t = [regex]::Replace($t, '(?i)([A-Z]:\\Users\\)[^\\]+', '${1}(usuario)')
    return $t
}
function Add-Line($text) { [void]$script:lines.Add((Scrub-Text $text)) }
function Add-Section($title) { Add-Line ''; Add-Line ('=== ' + $title + ' ===') }

function Get-Wmi($class, $namespace) {
    try {
        if ($namespace) { return @(Get-WmiObject -Class $class -Namespace $namespace -ErrorAction Stop) }
        return @(Get-WmiObject -Class $class -ErrorAction Stop)
    } catch {
        return @()
    }
}

function Format-Gb($bytes) {
    if ($bytes -eq $null) { return $NA }
    try { return ('{0:N1} GB' -f ([double]$bytes / 1GB)) } catch { return $NA }
}

# Oculta direcciones de red y nombres de servidores en puertos e impresoras.
function Mask-Network($text) {
    if ($text -eq $null) { return $NA }
    $t = [string]$text
    if ($t -match '^\\\\') { return '(ruta de red omitida)' }
    if ($t -match '(\d{1,3}\.){3}\d{1,3}') { return '(direccion de red omitida)' }
    if ($t -match '^(http|https|ipp)' ) { return '(direccion de red omitida)' }
    if ($t -match '^WSD') { return 'WSD (identificador omitido)' }
    return $t
}

$here = $null
try { $here = Split-Path -Parent $MyInvocation.MyCommand.Path } catch { $here = $null }
if (-not $here) { $here = (Get-Location).Path }

Write-Host 'Reuniendo informacion de este equipo, un momento...'

# Resolucion real: el proceso se declara consciente de DPI antes de leer las pantallas.
$nativeOk = $false
try {
    $src = @'
using System;
using System.Runtime.InteropServices;
public static class FichaNativa {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] static extern IntPtr MonitorFromPoint(POINT pt, uint flags);
    [DllImport("shcore.dll")] static extern int GetDpiForMonitor(IntPtr hmon, int type, out uint dpiX, out uint dpiY);
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
    [DllImport("gdi32.dll")] static extern int GetDeviceCaps(IntPtr dc, int index);
    public static void DpiAware() { try { SetProcessDPIAware(); } catch (Exception) { } }
    public static int Metric(int index) { try { return GetSystemMetrics(index); } catch (Exception) { return -1; } }
    // DPI efectivo de la pantalla que contiene el punto; si no se puede, el DPI del sistema; si no, -1.
    public static int DpiAt(int x, int y) {
        try {
            POINT p = new POINT(); p.X = x; p.Y = y;
            IntPtr mon = MonitorFromPoint(p, 2);
            uint dx, dy;
            if (GetDpiForMonitor(mon, 0, out dx, out dy) == 0) return (int)dx;
        } catch (Exception) { }
        try {
            IntPtr dc = GetDC(IntPtr.Zero);
            int v = GetDeviceCaps(dc, 88);
            ReleaseDC(IntPtr.Zero, dc);
            return v;
        } catch (Exception) { return -1; }
    }
}
'@
    Add-Type -TypeDefinition $src
    [FichaNativa]::DpiAware()
    $nativeOk = $true
} catch { $nativeOk = $false }

Add-Line 'FICHA DE LA PC DE CAJA - Cafeteria ITCH'
try { Add-Line ('Fecha de la ficha: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')) } catch { Add-Line ('Fecha de la ficha: ' + $NA) }
Add-Line 'Solo lectura. No incluye nombres de personas ni cuentas, numeros de serie ni direcciones de red.'

# --- Windows ---
$os64 = $null
Add-Section 'Windows'
try {
    $os = @(Get-Wmi 'Win32_OperatingSystem' $null)
    if ($os.Length -gt 0) {
        $o = $os[0]
        Add-Line ('Edicion: ' + $o.Caption)
        Add-Line ('Version: ' + $o.Version + ' (compilacion ' + $o.BuildNumber + ')')
        if ($o.CSDVersion) { Add-Line ('Service Pack: ' + $o.CSDVersion) } else { Add-Line 'Service Pack: ninguno' }
        if ($o.OSArchitecture) {
            if ([string]$o.OSArchitecture -match '64') { $os64 = $true } elseif ([string]$o.OSArchitecture -match '32') { $os64 = $false }
        }
    } else {
        Add-Line ('Edicion y version: ' + $NA)
    }
} catch { Add-Line ('Edicion y version: ' + $NA) }
if ($os64 -eq $null) {
    # Respaldo si Windows no informa la arquitectura.
    if ($env:PROCESSOR_ARCHITEW6432) { $os64 = $true }
    elseif ($env:PROCESSOR_ARCHITECTURE -eq 'AMD64' -or $env:PROCESSOR_ARCHITECTURE -eq 'IA64') { $os64 = $true }
    elseif ($env:PROCESSOR_ARCHITECTURE -eq 'x86') { $os64 = $false }
}
if ($os64 -eq $true) { Add-Line 'Sistema de 32 o 64 bits: 64 bits' }
elseif ($os64 -eq $false) { Add-Line 'Sistema de 32 o 64 bits: 32 bits' }
else { Add-Line ('Sistema de 32 o 64 bits: ' + $NA) }
try { Add-Line ('PowerShell: ' + $PSVersionTable.PSVersion.ToString()) } catch { Add-Line 'PowerShell: 1.0 u otra version sin informacion' }

# --- Procesador ---
Add-Section 'Procesador'
try {
    $cpus = @(Get-Wmi 'Win32_Processor' $null)
    if ($cpus.Length -eq 0) { Add-Line ('Procesador: ' + $NA) }
    $i = 0
    foreach ($c in $cpus) {
        $i++
        $name = ([string]$c.Name).Trim()
        Add-Line ('Procesador ' + $i + ': ' + $name)
        if ($c.NumberOfCores) { Add-Line ('  Nucleos: ' + $c.NumberOfCores) } else { Add-Line ('  Nucleos: ' + $NA) }
        if ($c.NumberOfLogicalProcessors) { Add-Line ('  Procesadores logicos: ' + $c.NumberOfLogicalProcessors) } else { Add-Line ('  Procesadores logicos: ' + $NA) }
        if ($c.MaxClockSpeed) { Add-Line ('  Velocidad maxima: ' + $c.MaxClockSpeed + ' MHz') }
    }
} catch { Add-Line ('Procesador: ' + $NA) }

# --- Memoria ---
Add-Section 'Memoria RAM'
try {
    $cs = @(Get-Wmi 'Win32_ComputerSystem' $null)
    if ($cs.Length -gt 0 -and $cs[0].TotalPhysicalMemory) {
        Add-Line ('RAM total: ' + (Format-Gb $cs[0].TotalPhysicalMemory))
    } else { Add-Line ('RAM total: ' + $NA) }
} catch { Add-Line ('RAM total: ' + $NA) }

# --- Discos ---
Add-Section 'Discos'
try {
    $storage = @(Get-Wmi 'MSFT_PhysicalDisk' 'root\Microsoft\Windows\Storage')
    $physical = @(Get-Wmi 'Win32_DiskDrive' $null)
    if ($physical.Length -eq 0) { Add-Line ('Discos fisicos: ' + $NA) }
    foreach ($d in $physical) {
        $kind = $null
        foreach ($s in $storage) {
            if ([string]$s.DeviceId -eq [string]$d.Index) {
                if ($s.MediaType -eq 4) { $kind = 'SSD' }
                elseif ($s.MediaType -eq 3) { $kind = 'disco duro (HDD)' }
            }
        }
        if (-not $kind) {
            if ([string]$d.Model -match 'SSD|Solid State|NVMe') { $kind = 'SSD (deducido del modelo)' }
            elseif ($storage.Length -eq 0) { $kind = 'no se puede saber en este Windows' }
            else { $kind = $NA }
        }
        $model = ([string]$d.Model).Trim()
        Add-Line ('Disco fisico ' + $d.Index + ': ' + $model + ', ' + (Format-Gb $d.Size) + ', tipo: ' + $kind)
    }
    $logical = @(Get-Wmi 'Win32_LogicalDisk' $null | Where-Object { $_.DriveType -eq 3 })
    if ($logical.Length -eq 0) { Add-Line ('Unidades: ' + $NA) }
    foreach ($l in $logical) {
        Add-Line ('Unidad ' + $l.DeviceID + ' ' + $l.FileSystem + ': tamano ' + (Format-Gb $l.Size) + ', libre ' + (Format-Gb $l.FreeSpace))
    }
} catch { Add-Line ('Discos: ' + $NA) }

# --- Pantallas ---
Add-Section 'Pantallas'
try {
    [void][System.Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms')
    $screens = @([System.Windows.Forms.Screen]::AllScreens)
    if ($screens.Length -eq 0) { Add-Line ('Pantallas: ' + $NA) }
    $n = 0
    foreach ($sc in $screens) {
        $n++
        $b = $sc.Bounds
        $dpiText = $NA
        if ($nativeOk) {
            $dpi = [FichaNativa]::DpiAt([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2))
            if ($dpi -gt 0) { $dpiText = ('{0}% ({1} DPI)' -f [int][math]::Round($dpi * 100 / 96), $dpi) }
        }
        $main = ''
        if ($sc.Primary) { $main = ' (principal)' }
        Add-Line ('Pantalla ' + $n + $main + ': ' + $b.Width + ' x ' + $b.Height + ' pixeles, escalado ' + $dpiText)
    }
    if (-not $nativeOk) { Add-Line 'Nota: no se pudo preparar la lectura de escalado; las resoluciones pueden verse reducidas por el escalado.' }
} catch { Add-Line ('Pantallas: ' + $NA) }

# --- Pantalla tactil ---
Add-Section 'Pantalla tactil'
if ($nativeOk) {
    $dig = [FichaNativa]::Metric(94)
    $maxT = [FichaNativa]::Metric(95)
    if ($dig -lt 0) {
        Add-Line ('Pantalla tactil: ' + $NA)
    } else {
        $touch = (($dig -band 3) -ne 0)
        if ($touch) {
            $ready = 'no lista'
            if (($dig -band 128) -ne 0) { $ready = 'lista para usar' }
            Add-Line ('Pantalla tactil: si (' + $ready + ', puntos de contacto simultaneos: ' + $maxT + ')')
        } else {
            Add-Line 'Pantalla tactil: no detectada por Windows (si el monitor es tactil, puede faltar su controlador)'
        }
        if (($dig -band 12) -ne 0) { Add-Line 'Lapiz digital: detectado' }
    }
} else { Add-Line ('Pantalla tactil: ' + $NA) }

# --- Impresoras ---
Add-Section 'Impresoras instaladas'
try {
    $printers = @(Get-Wmi 'Win32_Printer' $null)
    if ($printers.Length -eq 0) { Add-Line ('Impresoras: ninguna instalada o ' + $NA) }
    foreach ($p in $printers) {
        $def = 'no'
        if ($p.Default) { $def = 'SI' }
        Add-Line ('Impresora: ' + (Mask-Network $p.Name))
        Add-Line ('  Controlador: ' + $p.DriverName)
        Add-Line ('  Puerto: ' + (Mask-Network $p.PortName))
        Add-Line ('  Predeterminada: ' + $def)
    }
} catch { Add-Line ('Impresoras: ' + $NA) }

# --- Autocomprobacion del paquete ---
Add-Section 'Autocomprobacion del paquete (CafeteriaITCH.exe --self-check)'
$exe = $null
try {
    if (Test-Path -LiteralPath (Join-Path $here 'CafeteriaITCH.exe')) { $exe = Join-Path $here 'CafeteriaITCH.exe' }
    else {
        foreach ($sub in @(Get-ChildItem -LiteralPath $here | Where-Object { $_.PSIsContainer })) {
            if (-not $exe -and (Test-Path -LiteralPath (Join-Path $sub.FullName 'CafeteriaITCH.exe'))) {
                $exe = Join-Path $sub.FullName 'CafeteriaITCH.exe'
            }
        }
    }
} catch { $exe = $null }

if (-not $exe) {
    Add-Line 'No hay un paquete (CafeteriaITCH.exe) junto a este guion: no se ejecuto la autocomprobacion.'
} elseif ($os64 -ne $true) {
    # Se decide antes de intentar nada: el paquete es de 64 bits.
    if ($os64 -eq $false) {
        Add-Line 'No se intento la autocomprobacion: el sistema es de 32 bits y el paquete es de 64 bits.'
    } else {
        Add-Line 'No se intento la autocomprobacion: no se pudo saber si el sistema es de 32 o 64 bits.'
    }
} else {
    $report = Join-Path ([System.IO.Path]::GetTempPath()) ('ficha_autocomprobacion_' + [System.Diagnostics.Process]::GetCurrentProcess().Id + '.txt')
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $exe
        $psi.Arguments = '--self-check "--self-check-report=' + $report + '"'
        $psi.UseShellExecute = $false
        $psi.WorkingDirectory = (Split-Path -Parent $exe)
        $proc = [System.Diagnostics.Process]::Start($psi)
        if ($proc.WaitForExit(60000)) {
            Add-Line ('Codigo de salida: ' + $proc.ExitCode + ' (0 = correcto)')
        } else {
            try { $proc.Kill() } catch { }
            Add-Line 'El programa no termino en 60 segundos y se cerro. Probablemente Windows mostro un mensaje de error (por ejemplo, falta un componente del sistema).'
        }
        if (Test-Path -LiteralPath $report) {
            Add-Line 'Informe de la autocomprobacion:'
            foreach ($ln in @(Get-Content -LiteralPath $report -Encoding UTF8)) { Add-Line ('  ' + $ln) }
        } else {
            Add-Line 'El programa no escribio informe.'
        }
    } catch {
        Add-Line ('El ejecutable no pudo arrancar: ' + $_.Exception.Message)
    }
    try { if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Force } } catch { }
}

# --- Escritura del resultado ---
$text = [string]::Join("`r`n", @($script:lines.ToArray())) + "`r`n"
$utf8bom = New-Object System.Text.UTF8Encoding($true)
$outName = 'ficha_pc_resultado.txt'
$written = $null
$where = ''
try {
    $target = Join-Path $here $outName
    [System.IO.File]::WriteAllText($target, $text, $utf8bom)
    $written = $target
    $where = 'junto al guion'
} catch {
    try {
        $desktop = [Environment]::GetFolderPath('Desktop')
        $target = Join-Path $desktop $outName
        [System.IO.File]::WriteAllText($target, $text, $utf8bom)
        $written = $target
        $where = 'en el Escritorio (no se pudo escribir junto al guion, quiza la USB esta protegida)'
    } catch {
        $written = $null
    }
}

Write-Host ''
Write-Host $text
Write-Host '----------------------------------------'
if ($written) {
    Write-Host ('Listo. El archivo se guardo ' + $where + ':')
    Write-Host $written
    exit 0
} else {
    Write-Host 'ATENCION: no se pudo guardar el archivo ni junto al guion ni en el Escritorio.'
    Write-Host 'Copie el texto de arriba y envielo a quien instalo el sistema.'
    exit 2
}
