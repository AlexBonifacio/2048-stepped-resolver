@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "SESSION=ma_partie"
set "PORT=8765"
set "PY_CMD="

if not exist "web\server.py" (
  echo Fichier web\server.py introuvable.
  echo Assure-toi d'avoir extrait tout le dossier du zip.
  pause
  exit /b 1
)

if not exist "2048-ranks.exe" (
  echo Fichier 2048-ranks.exe introuvable.
  echo Ce dossier n'est pas une version Windows complete.
  echo Demande le zip Windows complet, ou compile 2048-ranks.exe avant de relancer.
  pause
  exit /b 1
)

if exist "python\python.exe" (
  set "PY_CMD=python\python.exe"
) else (
  where py >nul 2>nul
  if not errorlevel 1 set "PY_CMD=py -3"
)

if not defined PY_CMD (
  where python >nul 2>nul
  if not errorlevel 1 set "PY_CMD=python"
)

if not defined PY_CMD (
  echo Python est introuvable.
  echo Utilise un zip qui contient le dossier python\, ou installe Python 3 depuis python.org.
  echo Ensuite relance ce fichier.
  pause
  exit /b 1
)

echo.
echo 2048 Helper demarre...
echo Une page web va s'ouvrir automatiquement.
echo Garde cette fenetre ouverte pendant la partie.
echo Pour arreter: ferme cette fenetre.
echo.

%PY_CMD% web\server.py --host 127.0.0.1 --port %PORT% --session "%SESSION%" --open

echo.
echo Le serveur s'est arrete.
pause
