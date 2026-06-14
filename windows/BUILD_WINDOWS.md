# Build Windows Package

Ce dossier contient le lanceur pour les utilisateurs non techniques. Le zip final doit contenir:

- `Lancer 2048 Helper.bat`
- `2048-ranks.exe`
- `web/`
- `data/`
- optionnel: `python/` avec un runtime Python Windows embarque

Les utilisateurs finaux n'ont pas besoin de `g++`. `g++` est necessaire uniquement pour fabriquer `2048-ranks.exe` avant de creer le zip.

## Sans g++ sur ton PC

Si le projet est sur GitHub, utilise le workflow:

```text
Actions -> Build Windows package -> Run workflow
```

GitHub compile `2048-ranks.exe` sur une machine Windows et publie l'artefact:

```text
2048-ranks-windows.zip
```

Le workflow ajoute aussi un runtime Python embarque dans `python/`, donc l'utilisateur final n'a pas besoin d'installer Python.

Tu peux ensuite envoyer ce zip aux utilisateurs.

## Depuis Ubuntu sans GitHub

Il faut installer un compilateur Windows une seule fois:

```bash
sudo apt update
sudo apt install -y mingw-w64 zip
```

Puis, depuis la racine du projet:

```bash
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ -o 2048-ranks.exe src/board.cpp src/evaluator.cpp src/main.cpp src/move.cpp src/solver.cpp src/stat.cpp
python3 tools/package_windows.py --solver-exe 2048-ranks.exe
```

Le fichier a envoyer sera:

```text
dist/2048-ranks-windows.zip
```

Pour une version sans installation Python cote utilisateur, ajoute un dossier `python/` contenant `python.exe` Windows avant de lancer `tools/package_windows.py`.

## Build rapide sur Windows

Option simple: double-clique sur:

```text
windows/Creer le zip Windows.bat
```

Il compile `2048-ranks.exe`, puis lance `tools/package_windows.py`.

Option manuelle:

Depuis la racine du projet, compile d'abord le solveur:

```powershell
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -o 2048-ranks.exe src/board.cpp src/evaluator.cpp src/main.cpp src/move.cpp src/solver.cpp src/stat.cpp
```

Puis cree le package:

```powershell
python tools/package_windows.py --solver-exe 2048-ranks.exe
```

Le zip sera cree dans `dist/`.

## Version sans installation Python

Pour eviter toute installation chez l'utilisateur final, ajoute un runtime Python Windows dans un dossier `python/` avant de lancer `tools/package_windows.py`.

Le script copiera automatiquement ce dossier dans le package si `python/python.exe` existe.

Structure attendue:

```text
python/
  python.exe
  python312.zip
  ...
```

Le lanceur utilisera ce Python local avant de chercher un Python installe sur Windows.
