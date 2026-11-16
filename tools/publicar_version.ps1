# Prepara una version para publicarla en GitHub Releases: genera el manifiesto, lo firma, lo verifica y deja todo en
# una carpeta lista para subir. NO publica nada: imprime el comando exacto de gh y lo ejecuta usted.
#
# Uso:
#   1. Compile el instalador (Release):  cmake --build build_be_release --target installer
#   2. Escriba las novedades en un archivo de texto, una frase por linea, en lenguaje llano.
#   3. powershell -ExecutionPolicy Bypass -File tools\publicar_version.ps1 -Version 0.1.1 `
#          -Instalador build_be_release\installer_out\CafeteriaITCH-0.1.1-instalador-win64.exe `
#          -Notas notas-0.1.1.txt -Clave "$env:USERPROFILE\Cafeteria_ITCH_claves\actualizaciones.priv"
#      (pide la contrasena de la clave; la clave privada NUNCA debe estar dentro del repositorio)
#
# El programa solo acepta lo que verifique con su clave publica incrustada: firma, version mayor que la instalada,
# tamano y SHA-256 del instalador. Un archivo subido por otra persona no basta.
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Instalador,
    [Parameter(Mandatory = $true)][string]$Notas,
    [Parameter(Mandatory = $true)][string]$Clave,
    [string]$Salida = '',
    [string]$Repo = 'Surer46/cafeteriaITCH',
    [string]$Contrasena = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'actualizaciones_comun.ps1')

if ($Version -notmatch '^\d{1,4}\.\d{1,4}\.\d{1,4}$') { throw 'La version debe tener el formato a.b.c (por ejemplo 0.1.1).' }
if (-not (Test-Path -LiteralPath $Instalador)) { throw "No existe el instalador: $Instalador" }
$Instalador = (Resolve-Path -LiteralPath $Instalador).Path
$nombre = [System.IO.Path]::GetFileName($Instalador)
$esperado = "CafeteriaITCH-$Version-instalador-win64.exe"
if ($nombre -ne $esperado) { throw "El instalador debe llamarse $esperado (se llama $nombre)." }
if (-not (Test-Path -LiteralPath $Notas)) { throw "No existe el archivo de notas: $Notas" }
$repoDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$claveAbs = [System.IO.Path]::GetFullPath($Clave)
if ($claveAbs.StartsWith($repoDir, [StringComparison]::OrdinalIgnoreCase) -and ((Get-Content -LiteralPath $claveAbs -TotalCount 1) -ne 'CAFEKEYT')) {
    throw 'La clave privada no puede estar dentro del repositorio.'
}

$lineasNotas = @(Get-Content -LiteralPath $Notas -Encoding UTF8 | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
if ($lineasNotas.Count -eq 0) { throw 'El archivo de notas esta vacio: escriba al menos una frase.' }

# --- Manifiesto (JSON determinista, solo ASCII: los caracteres no ASCII se escriben como \uXXXX) ---
function ConvertTo-JsonString([string]$s) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')
    foreach ($ch in $s.ToCharArray()) {
        $code = [int]$ch
        if ($ch -eq '"') { [void]$sb.Append('\"') }
        elseif ($ch -eq '\') { [void]$sb.Append('\\') }
        elseif ($code -lt 32 -or $code -gt 126) { [void]$sb.Append('\u' + $code.ToString('x4')) }
        else { [void]$sb.Append($ch) }
    }
    [void]$sb.Append('"')
    return $sb.ToString()
}
$tam = (Get-Item -LiteralPath $Instalador).Length
$hash = (Get-FileHash -LiteralPath $Instalador -Algorithm SHA256).Hash.ToLowerInvariant()
$fecha = (Get-Date).ToString('yyyy-MM-dd')
$j = New-Object System.Text.StringBuilder
[void]$j.Append("{`n")
[void]$j.Append('  "schema": 1,' + "`n")
[void]$j.Append('  "product": "CafeteriaITCH",' + "`n")
[void]$j.Append('  "arch": "x64",' + "`n")
[void]$j.Append('  "version": ' + (ConvertTo-JsonString $Version) + ",`n")
[void]$j.Append('  "published": ' + (ConvertTo-JsonString $fecha) + ",`n")
[void]$j.Append('  "notes": [' + (($lineasNotas | ForEach-Object { ConvertTo-JsonString $_ }) -join ', ') + "],`n")
[void]$j.Append('  "installer": {"name": ' + (ConvertTo-JsonString $nombre) + ', "size": ' + $tam + ', "sha256": ' + (ConvertTo-JsonString $hash) + "}`n")
[void]$j.Append("}`n")
$manifiestoBytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($j.ToString())

# --- Firma ---
$pwd = $Contrasena
$cabecera = (Get-Content -LiteralPath $claveAbs -TotalCount 1)
if ($cabecera -eq 'CAFEKEY1' -and -not $pwd) { $pwd = Read-Contrasena 'Contrasena de la clave privada' }
$blob = Read-ClavePrivada $claveAbs $pwd
$firma = Get-FirmaActualizacion $manifiestoBytes $blob
$publica = Get-PublicaDeBlobPrivado $blob
if (-not (Test-FirmaActualizacion $manifiestoBytes $firma $publica)) { throw 'La firma generada no verifica con su propia clave publica.' }

# --- Salida ---
if (-not $Salida) { $Salida = Join-Path $repoDir ("dist\release-" + $Version) }
$Salida = [System.IO.Path]::GetFullPath($Salida)
if (Test-Path -LiteralPath $Salida) { throw "Ya existe $Salida. Borrela o elija otra carpeta." }
New-Item -ItemType Directory -Path $Salida | Out-Null
Copy-Item -LiteralPath $Instalador -Destination (Join-Path $Salida $nombre)
[System.IO.File]::WriteAllBytes((Join-Path $Salida 'manifiesto.json'), $manifiestoBytes)
[System.IO.File]::WriteAllText((Join-Path $Salida 'manifiesto.json.sig'), (ConvertTo-HexLower $firma) + "`n", (New-Object System.Text.UTF8Encoding($false)))
Copy-Item -LiteralPath $Notas -Destination (Join-Path $Salida 'notas.txt')

# Verificacion final de lo que se va a subir (el instalador copiado coincide con el manifiesto).
$hash2 = (Get-FileHash -LiteralPath (Join-Path $Salida $nombre) -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash2 -ne $hash) { throw 'El instalador copiado no coincide con el manifiesto.' }

Write-Host ''
Write-Host "Version $Version lista en: $Salida"
Write-Host "  $nombre ($tam bytes)"
Write-Host '  manifiesto.json, manifiesto.json.sig, notas.txt'
Write-Host ''
Write-Host 'Para publicarla (NO se publica sola; ejecute usted este comando):'
Write-Host ("  gh release create v{0} `"{1}`" `"{2}`" `"{3}`" --repo {4} --title `"Cafeteria ITCH {0}`" --notes-file `"{5}`"" -f `
    $Version, (Join-Path $Salida $nombre), (Join-Path $Salida 'manifiesto.json'), (Join-Path $Salida 'manifiesto.json.sig'), $Repo, (Join-Path $Salida 'notas.txt'))
Write-Host ''
Write-Host 'Los tres archivos deben subirse a la misma version; "latest" apunta a la ultima version publicada (no borrador ni preliminar).'
