# Funciones comunes de las herramientas de actualizacion de Cafeteria ITCH (PowerShell 5.1 con .NET Framework 4.x).
# No instala nada. Esquema: ECDSA P-256 con SHA-256 (CNG), firma cruda de 64 bytes (r||s), clave publica de 64 bytes (X||Y).
#
# Formato del archivo de clave PRIVADA (siempre fuera del repositorio):
#   CAFEKEY1   <salt base64>  <iv base64>  <datos base64>   cifrado con una contrasena (PBKDF2 200000 + AES-256-CBC)
#   CAFEKEYT   <blob base64>                                 SIN cifrar: solo para la clave de PRUEBA de tests/

Add-Type -AssemblyName System.Core
Add-Type -AssemblyName System.Security

function ConvertTo-HexLower([byte[]]$Bytes) {
    $sb = New-Object System.Text.StringBuilder
    foreach ($b in $Bytes) { [void]$sb.Append($b.ToString('x2')) }
    return $sb.ToString()
}

function ConvertFrom-HexLower([string]$Hex) {
    $Hex = $Hex.Trim()
    if (($Hex.Length % 2) -ne 0) { throw 'Hexadecimal con longitud impar' }
    $out = New-Object byte[] ($Hex.Length / 2)
    for ($i = 0; $i -lt $out.Length; $i++) { $out[$i] = [Convert]::ToByte($Hex.Substring(2 * $i, 2), 16) }
    return $out
}

# Genera una pareja nueva. Devuelve @{ Privada = <blob CNG>; Publica = <64 bytes X||Y> }
function New-ParDeClavesActualizacion {
    $p = New-Object System.Security.Cryptography.CngKeyCreationParameters
    $p.ExportPolicy = [System.Security.Cryptography.CngExportPolicies]::AllowPlaintextExport
    $key = [System.Security.Cryptography.CngKey]::Create([System.Security.Cryptography.CngAlgorithm]::ECDsaP256, $null, $p)
    try {
        $priv = $key.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
        $pub = $key.Export([System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    } finally { $key.Dispose() }
    # EccPublicBlob = magic(4) cbKey(4) X(32) Y(32)
    $raw = New-Object byte[] 64
    [Array]::Copy($pub, 8, $raw, 0, 64)
    return @{ Privada = $priv; Publica = $raw }
}

function Protect-ClavePrivada([byte[]]$Blob, [string]$Contrasena, [string]$Ruta) {
    $salt = New-Object byte[] 16
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $rng.GetBytes($salt)
    $kdf = New-Object System.Security.Cryptography.Rfc2898DeriveBytes($Contrasena, $salt, 200000)
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.KeySize = 256
    $aes.Key = $kdf.GetBytes(32)
    $aes.IV = $kdf.GetBytes(16)
    $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
    $plain = [System.Text.Encoding]::ASCII.GetBytes('CAFEKEYOK') + $Blob
    $enc = $aes.CreateEncryptor().TransformFinalBlock($plain, 0, $plain.Length)
    $lines = @('CAFEKEY1', [Convert]::ToBase64String($salt), [Convert]::ToBase64String($aes.IV), [Convert]::ToBase64String($enc))
    [System.IO.File]::WriteAllText($Ruta, ($lines -join "`n") + "`n", (New-Object System.Text.UTF8Encoding($false)))
}

function Read-ClavePrivada([string]$Ruta, [string]$Contrasena) {
    if (-not (Test-Path -LiteralPath $Ruta)) { throw "No existe el archivo de clave: $Ruta" }
    $lines = [System.IO.File]::ReadAllText($Ruta).Split("`n") | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' }
    if ($lines[0] -eq 'CAFEKEYT') { return [Convert]::FromBase64String($lines[1]) }
    if ($lines[0] -ne 'CAFEKEY1') { throw 'El archivo no es una clave de actualizaciones de Cafeteria ITCH' }
    if (-not $Contrasena) { throw 'Falta la contrasena de la clave' }
    $salt = [Convert]::FromBase64String($lines[1])
    $iv = [Convert]::FromBase64String($lines[2])
    $enc = [Convert]::FromBase64String($lines[3])
    $kdf = New-Object System.Security.Cryptography.Rfc2898DeriveBytes($Contrasena, $salt, 200000)
    $aes = [System.Security.Cryptography.Aes]::Create()
    $aes.KeySize = 256
    $aes.Key = $kdf.GetBytes(32)
    $aes.IV = $iv
    $aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
    $aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
    try {
        $plain = $aes.CreateDecryptor().TransformFinalBlock($enc, 0, $enc.Length)
    } catch { throw 'Contrasena incorrecta o archivo de clave danado' }
    $magic = [System.Text.Encoding]::ASCII.GetString($plain, 0, 9)
    if ($magic -ne 'CAFEKEYOK') { throw 'Contrasena incorrecta o archivo de clave danado' }
    $blob = New-Object byte[] ($plain.Length - 9)
    [Array]::Copy($plain, 9, $blob, 0, $blob.Length)
    return $blob
}

function Get-PublicaDeBlobPrivado([byte[]]$Blob) {
    # EccPrivateBlob = magic(4) cbKey(4) X(32) Y(32) D(32)
    $raw = New-Object byte[] 64
    [Array]::Copy($Blob, 8, $raw, 0, 64)
    return $raw
}

# Firma los bytes con la clave privada (SHA-256). Devuelve 64 bytes (r||s).
function Get-FirmaActualizacion([byte[]]$Datos, [byte[]]$BlobPrivado) {
    $key = [System.Security.Cryptography.CngKey]::Import($BlobPrivado, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
    try {
        $ecdsa = New-Object System.Security.Cryptography.ECDsaCng($key)
        $ecdsa.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
        return $ecdsa.SignData($Datos)
    } finally { $key.Dispose() }
}

function Test-FirmaActualizacion([byte[]]$Datos, [byte[]]$Firma, [byte[]]$Publica64) {
    # Se reconstruye el blob publico: magic ECS1 (0x31534345) + 32 + X||Y
    $blob = New-Object byte[] 72
    [BitConverter]::GetBytes([int]0x31534345).CopyTo($blob, 0)
    [BitConverter]::GetBytes([int]32).CopyTo($blob, 4)
    [Array]::Copy($Publica64, 0, $blob, 8, 64)
    $key = [System.Security.Cryptography.CngKey]::Import($blob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    try {
        $ecdsa = New-Object System.Security.Cryptography.ECDsaCng($key)
        $ecdsa.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
        return $ecdsa.VerifyData($Datos, $Firma)
    } finally { $key.Dispose() }
}

function Read-Contrasena([string]$Mensaje) {
    $s = Read-Host -AsSecureString $Mensaje
    $b = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($s)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($b) } finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($b) }
}
