# Crea la pareja de claves con la que se firman las actualizaciones de Cafeteria ITCH.
#
# Uso (una sola vez, en la PC desde la que se publicaran las versiones):
#   powershell -ExecutionPolicy Bypass -File tools\crear_claves_actualizacion.ps1 -RutaPrivada "$env:USERPROFILE\Cafeteria_ITCH_claves\actualizaciones.priv"
#
# - La clave PRIVADA se guarda cifrada con una contrasena que usted elige (no se guarda en ningun lado).
#   NUNCA debe estar dentro del repositorio ni en la caja. Guarde una copia en una USB en un lugar seguro.
# - La clave PUBLICA se imprime en hexadecimal y se guarda junto a la privada (.pub). Se pega en
#   src\services\update_public_keys.inc y se recompila el programa.
# - Si pierde la privada o la contrasena: las cajas instaladas dejan de aceptar actualizaciones nuevas (no se danan);
#   hay que crear otra pareja y distribuir UNA VEZ un instalador por USB con la nueva clave publica.
#
# Opcion -Prueba: crea la clave de PRUEBA (sin cifrar) que usan las pruebas automaticas. No usar para publicar.
param(
    [Parameter(Mandatory = $true)][string]$RutaPrivada,
    [switch]$Prueba
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'actualizaciones_comun.ps1')

if (Test-Path -LiteralPath $RutaPrivada) { throw "Ya existe $RutaPrivada. No se sobrescribe una clave." }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$destino = [System.IO.Path]::GetFullPath($RutaPrivada)
if (-not $Prueba -and $destino.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'La clave privada no puede guardarse dentro del repositorio.'
}
$dir = [System.IO.Path]::GetDirectoryName($destino)
if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }

$par = New-ParDeClavesActualizacion
if ($Prueba) {
    $text = "CAFEKEYT`n" + [Convert]::ToBase64String($par.Privada) + "`n"
    [System.IO.File]::WriteAllText($destino, $text, (New-Object System.Text.UTF8Encoding($false)))
} else {
    $c1 = Read-Contrasena 'Elija una contrasena para la clave privada'
    $c2 = Read-Contrasena 'Repita la contrasena'
    if ($c1 -ne $c2 -or $c1.Length -lt 10) { throw 'Las contrasenas no coinciden o tienen menos de 10 caracteres.' }
    Protect-ClavePrivada $par.Privada $c1 $destino
}
$hex = ConvertTo-HexLower $par.Publica
[System.IO.File]::WriteAllText(($destino + '.pub'), $hex + "`n", (New-Object System.Text.UTF8Encoding($false)))

Write-Host ''
Write-Host "Clave privada guardada en: $destino"
Write-Host 'Clave publica (128 caracteres hexadecimales):'
Write-Host $hex
Write-Host ''
Write-Host 'Siguiente paso: pegue la clave publica en una ranura libre de src\services\update_public_keys.inc y recompile.'
