@echo off
rem Lanzador de la ficha de la PC de caja: doble clic.
rem Ejecuta ficha_pc.ps1 con -ExecutionPolicy Bypass SOLO para ese proceso; no cambia la politica del equipo.
rem Solo lee informacion, no instala nada y no usa la red.
setlocal
set "AQUI=%~dp0"
set "SCRIPT=%AQUI%ficha_pc.ps1"

if not exist "%SCRIPT%" goto sinscript

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%"
set "CODIGO=%ERRORLEVEL%"
if "%CODIGO%"=="0" goto fin
goto falla

:sinscript
echo.
echo No se encontro ficha_pc.ps1 junto a este archivo.
echo Copie los dos archivos juntos (ficha_pc.cmd y ficha_pc.ps1) a la misma carpeta.
goto fin

:falla
echo.
echo ============================================================
echo La ficha no se pudo completar (codigo %CODIGO%).
echo Puede que una directiva de este equipo bloquee PowerShell, o que
echo PowerShell no este disponible. Se pudo haber mostrado un mensaje
echo de Windows arriba: anotelo o tome una foto de esta pantalla y
echo avise a quien instalo el sistema.
echo ============================================================

:fin
echo.
pause
endlocal
