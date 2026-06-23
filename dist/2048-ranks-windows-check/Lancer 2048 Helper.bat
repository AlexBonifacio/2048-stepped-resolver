@echo off
setlocal EnableExtensions

cd /d "%~dp0"

set "SESSION=ma_partie"
set "PORT=8765"
set "PY_CMD="

if not exist "web\server.py" (
  echo web\server.py was not found.
  echo Make sure you extracted the whole zip folder.
  pause
  exit /b 1
)

if not exist "2048-ranks.exe" (
  echo 2048-ranks.exe was not found.
  echo This folder is not a complete Windows release.
  echo Ask for the complete Windows zip, or compile 2048-ranks.exe before launching.
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
  echo Python was not found.
  echo Use a zip that contains the python\ folder, or install Python 3 from python.org.
  echo Then launch this file again.
  pause
  exit /b 1
)

echo.
echo Starting 2048 Helper...
echo A web page will open automatically.
echo Keep this window open while you play.
echo To stop: close this window.
echo.

%PY_CMD% web\server.py --host 127.0.0.1 --port %PORT% --session "%SESSION%" --open

echo.
echo The server stopped.
pause
