#!/usr/bin/env python3

import argparse
import json
import shutil
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / "dist"
PACKAGE_NAME = "2048-ranks-windows"


def ignore_build_artifacts(_directory, names):
    ignored = set()
    for name in names:
        if name == "__pycache__" or name.endswith(".pyc") or name.endswith(".pyo"):
            ignored.add(name)
    return ignored


def empty_session():
    return {
        "moves": 0,
        "score": 0,
        "cells": [0] * 16,
        "map": [[0, 0, 0, 0] for _ in range(4)],
        "observations": [],
        "spawns": {},
        "solved": [],
        "outcome": {"status": "in_progress", "target": 13},
    }


def copy_file_if_exists(source, target):
    if source.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        return True
    return False


def zip_directory(source_dir, zip_path):
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source_dir.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(source_dir.parent))


def main():
    parser = argparse.ArgumentParser(description="Prepare un zip Windows pour 2048 ranks.")
    parser.add_argument("--solver-exe", default="", help="Chemin vers 2048-ranks.exe deja compile.")
    parser.add_argument("--name", default=PACKAGE_NAME, help="Nom du dossier/zip genere.")
    parser.add_argument("--no-zip", action="store_true", help="Cree seulement le dossier, sans zip.")
    args = parser.parse_args()

    package_dir = DIST / args.name
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True)

    shutil.copytree(ROOT / "web", package_dir / "web", ignore=ignore_build_artifacts)
    shutil.copy2(ROOT / "windows" / "Lancer 2048 Helper.bat", package_dir / "Lancer 2048 Helper.bat")
    shutil.copy2(ROOT / "windows" / "README_UTILISATEUR.txt", package_dir / "README_UTILISATEUR.txt")

    data_dir = package_dir / "data"
    (data_dir / "sessions").mkdir(parents=True)
    (data_dir / "simulation_reports").mkdir(parents=True)

    copy_file_if_exists(ROOT / "data" / "observed_spawns.json", data_dir / "observed_spawns.json")
    copy_file_if_exists(
        ROOT / "data" / "sessions" / "youtube_max01_VozVoz.json",
        data_dir / "sessions" / "youtube_max01_VozVoz.json",
    )
    (data_dir / "sessions" / "ma_partie.json").write_text(
        json.dumps(empty_session(), indent=2) + "\n",
        encoding="utf-8",
    )

    checkpoints = ROOT / "data" / "checkpoints"
    if checkpoints.exists():
        shutil.copytree(checkpoints, data_dir / "checkpoints", ignore=ignore_build_artifacts)

    solver_source = Path(args.solver_exe) if args.solver_exe else ROOT / "2048-ranks.exe"
    if not solver_source.is_absolute():
        solver_source = ROOT / solver_source
    if solver_source.exists():
        shutil.copy2(solver_source, package_dir / "2048-ranks.exe")
    else:
        (package_dir / "IL_MANQUE_2048-ranks.exe.txt").write_text(
            "Compile 2048-ranks.exe puis relance tools/package_windows.py --solver-exe CHEMIN\\2048-ranks.exe\n",
            encoding="utf-8",
        )
        print("Attention: 2048-ranks.exe introuvable. Le dossier genere n'est pas directement utilisable.")

    local_python = ROOT / "python"
    if (local_python / "python.exe").exists():
        shutil.copytree(local_python, package_dir / "python", ignore=ignore_build_artifacts)
    else:
        print("Info: aucun runtime Python embarque trouve dans python/. Le launcher utilisera Python installe.")

    if not args.no_zip:
        zip_path = DIST / f"{args.name}.zip"
        zip_directory(package_dir, zip_path)
        print(f"Package cree: {zip_path}")
    print(f"Dossier cree: {package_dir}")


if __name__ == "__main__":
    main()
