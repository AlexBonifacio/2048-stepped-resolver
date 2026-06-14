@echo off
setlocal EnableExtensions

cd /d "%~dp0\.."

set "PY_CMD="

where g++ >nul 2>nul
if errorlevel 1 (
  echo g++ est introuvable.
  echo Installe MSYS2/MinGW ou compile 2048-ranks.exe autrement.
  pause
  exit /b 1
)

where py >nul 2>nul
if not errorlevel 1 set "PY_CMD=py -3"

if not defined PY_CMD (
  where python >nul 2>nul
  if not errorlevel 1 set "PY_CMD=python"
)

if not defined PY_CMD (
  echo Python est introuvable.
  pause
  exit /b 1
)

echo Compilation de 2048-ranks.exe...
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -o 2048-ranks.exe src/board.cpp src/evaluator.cpp src/main.cpp src/move.cpp src/solver.cpp src/stat.cpp
if errorlevel 1 (
  echo Compilation impossible.
  pause
  exit /b 1
)

echo Creation du zip Windows...
%PY_CMD% tools\package_windows.py --solver-exe 2048-ranks.exe
if errorlevel 1 (
  echo Package impossible.
  pause
  exit /b 1
)

echo.
echo Zip cree dans le dossier dist.
pause
