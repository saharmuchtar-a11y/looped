# Looped Audio — Unreal Editor import (run via VibeUE execute_python_code or Output Log Python)
# Imports Content/Audio/_import/* into /Game/Audio with matching asset names.
# Safe to re-run: replaces existing SoundWaves at the same paths.

import unreal
import os

IMPORT_DIR = r"c:\Users\sahar\Desktop\Sahar_playground\Looped\Content\Audio\_import"
DEST = "/Game/Audio"

STEMS = [
    "portal", "melee_swoosh", "melee_impact", "player_hurt", "enemy_hurt",
    "ranged_shot", "jump", "button",
    "melee_windup", "ranged_telegraph",
    "hazard_fire", "hazard_ice", "hazard_venom", "hazard_void",
    "enemy_dodge", "player_death", "lever", "card_confirm",
]


def find_source(stem: str):
    for ext in (".wav", ".ogg", ".WAV", ".OGG"):
        p = os.path.join(IMPORT_DIR, stem + ext)
        if os.path.isfile(p):
            return p
    return None


def import_one(stem: str) -> str:
    src = find_source(stem)
    if not src:
        return f"MISSING source for {stem}"

    task = unreal.AssetImportTask()
    task.filename = src
    task.destination_path = DEST
    task.destination_name = stem
    task.replace_existing = True
    task.automated = True
    task.save = True

    # Sound factory
    factory = unreal.SoundFactory()
    task.factory = factory

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = f"{DEST}/{stem}.{stem}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return f"OK {path}"
    # Some imports land without extension in does_asset_exist check
    if unreal.EditorAssetLibrary.does_asset_exist(f"{DEST}/{stem}"):
        return f"OK {DEST}/{stem}"
    return f"FAIL import {stem} from {src}"


results = [import_one(s) for s in STEMS]
unreal.EditorAssetLibrary.save_directory(DEST, only_if_is_dirty=True, recursive=True)
print("\n".join(results))
print("DONE")
