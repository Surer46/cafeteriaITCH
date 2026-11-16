// Claves públicas de actualizaciones (ECDSA P-256), 128 caracteres hexadecimales cada una (X||Y, 64 bytes).
// Hay DOS ranuras: la vigente y una de relevo, para poder rotar la clave sin dejar cajas varadas.
// Mientras las dos estén vacías el programa NO hace ninguna petición de red de actualizaciones y rechaza todo.
// Las crea el usuario con tools\crear_claves_actualizacion.ps1 (la clave PRIVADA nunca entra al repositorio).
// Pegue aquí la clave pública que imprime ese guion y recompile.
"",
""
