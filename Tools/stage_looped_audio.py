#!/usr/bin/env python3
"""Download Kenney CC0 packs and stage renamed wav/ogg into Content/Audio/_import."""
from __future__ import annotations

import io
import shutil
import sys
import zipfile
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
STAGING = ROOT / "Tools" / "audio_staging"
IMPORT = ROOT / "Content" / "Audio" / "_import"

PACKS = {
    # key -> (zip filename under Tools/audio_staging, download URL)
    "impact": ("kenney_impact-sounds.zip", "https://kenney.nl/media/pages/assets/impact-sounds/0aadb5c0d3-1673012107/kenney_impact-sounds.zip"),
    "ui": ("kenney_ui-audio.zip", "https://kenney.nl/media/pages/assets/ui-audio/c1da0e977b-1673012098/kenney_ui-audio.zip"),
    "rpg": ("kenney_rpg-audio.zip", "https://kenney.nl/media/pages/assets/rpg-audio/2a3fcf7d69-1673012097/kenney_rpg-audio.zip"),
    "digital": ("kenney_digital-audio.zip", "https://kenney.nl/media/pages/assets/digital-audio/b7c74379df-1673012096/kenney_digital-audio.zip"),
}

# dest_stem -> (pack_key, preferred source basename without path, fallbacks...)
# Mood: dark/void indie FPS — dry impacts, short UI, void whoosh. No VO.
MAP = {
    # Existing SoftObject paths
    "portal": ("rpg", ["clothBelt2", "metalClick", "drawKnife3", "magic_spell_02", "drawKnife1"]),
    "melee_swoosh": ("rpg", ["knifeSlice", "knifeSlice2", "cloth1", "cloth2", "swish_2", "swish_3"]),
    "melee_impact": ("impact", ["impactPunch_heavy_000", "impactPunch_medium_000", "impactMetal_medium_000", "impactPlate_medium_000"]),
    "player_hurt": ("impact", ["impactPunch_medium_001", "impactPunch_heavy_001", "impactFlesh_medium_000", "impactGeneric_medium_000"]),
    "enemy_hurt": ("impact", ["impactPunch_heavy_002", "impactPunch_medium_002", "impactFlesh_heavy_000", "impactGeneric_heavy_000"]),
    "ranged_shot": ("digital", ["laserSmall_000", "laserSmall_001", "laser1", "phaserUp1", "tone1"]),
    "jump": ("impact", ["footstep_concrete_000", "footstep_concrete_001", "footstep_carpet_000"]),
    "button": ("ui", ["click_002", "click_001", "click_003", "switch_001", "switch1"]),
    # New cues
    "melee_windup": ("rpg", ["drawKnife2", "drawKnife3", "metalClick", "handleCoins"]),
    "ranged_telegraph": ("digital", ["phaserUp2", "phaserUp3", "phaserUp1", "laserLarge_000", "highUp"]),
    "hazard_fire": ("impact", ["impactFire_000", "impactFire_medium_000", "footstep_snow_000", "impactMining_000"]),
    "hazard_ice": ("impact", ["impactGlass_light_000", "impactGlass_medium_000", "footstep_snow_001", "impactPlate_light_000"]),
    "hazard_venom": ("impact", ["impactSoft_medium_000", "impactSoft_heavy_000", "footstep_water_000", "impactGeneric_light_000"]),
    "hazard_void": ("rpg", ["metalClick", "clothBelt", "drawKnife1", "magic_spell_03"]),
    "enemy_dodge": ("rpg", ["cloth3", "cloth4", "cloth1", "cloth2", "swish_1"]),
    "player_death": ("impact", ["impactPunch_heavy_003", "impactPunch_heavy_004", "impactGeneric_heavy_001", "impactPlate_heavy_000"]),
    "lever": ("ui", ["switch_002", "switch_001", "switch_003", "click_004", "switch2"]),
    "card_confirm": ("ui", ["click5", "click4", "click3", "switch3", "confirmation_001", "click_005"]),
}


def download(url: str, dest: Path) -> None:
    if dest.exists() and dest.stat().st_size > 1000:
        print(f"cached {dest.name}")
        return
    print(f"download {url}")
    req = Request(url, headers={"User-Agent": "LoopedAudioStaging/1.0"})
    with urlopen(req, timeout=120) as resp:
        data = resp.read()
    dest.write_bytes(data)
    print(f"  wrote {dest} ({len(data)} bytes)")


def index_zip(zip_path: Path) -> dict[str, str]:
    """basename(lower without ext) -> member path"""
    out: dict[str, str] = {}
    with zipfile.ZipFile(zip_path) as zf:
        for name in zf.namelist():
            if name.endswith("/"):
                continue
            low = name.lower()
            if not (low.endswith(".ogg") or low.endswith(".wav")):
                continue
            base = Path(name).stem.lower()
            out[base] = name
            # also index without trailing _000 style already in stem
    return out


def extract_member(zip_path: Path, member: str, dest_file: Path) -> None:
    with zipfile.ZipFile(zip_path) as zf:
        with zf.open(member) as src, open(dest_file, "wb") as dst:
            shutil.copyfileobj(src, dst)


def find_candidate(index: dict[str, str], prefs: list[str]) -> tuple[str, str] | None:
    for pref in prefs:
        key = pref.lower()
        if key in index:
            return key, index[key]
        # fuzzy: startswith
        for k, path in index.items():
            if k.startswith(key) or key in k:
                return k, path
    return None


def main() -> int:
    STAGING.mkdir(parents=True, exist_ok=True)
    IMPORT.mkdir(parents=True, exist_ok=True)

    zip_paths: dict[str, Path] = {}
    for key, (zip_name, url) in PACKS.items():
        zp = STAGING / zip_name
        # Prefer existing local zip (correct Kenney filenames) over download
        if zp.exists() and zp.stat().st_size > 1000:
            print(f"cached {zp.name}")
            zip_paths[key] = zp
            continue
        try:
            download(url, zp)
            zip_paths[key] = zp
        except Exception as e:
            print(f"FAIL download {key}: {e}", file=sys.stderr)
            # keep going with any existing zip under alternate names
            if zp.exists() and zp.stat().st_size > 1000:
                zip_paths[key] = zp
            else:
                alt = STAGING / f"kenney_{key}.zip"
                if alt.exists() and alt.stat().st_size > 1000:
                    print(f"using alternate {alt.name}")
                    zip_paths[key] = alt

    indexes = {k: index_zip(p) for k, p in zip_paths.items()}
    for k, idx in indexes.items():
        print(f"{k}: {len(idx)} audio files")

    mapping_lines = [
        "# Looped audio SOURCE MAP — all Kenney.nl packs, Creative Commons CC0 1.0",
        "# https://creativecommons.org/publicdomain/zero/1.0/",
        "",
    ]
    staged = []
    missing = []

    for dest_stem, (pack, prefs) in MAP.items():
        idx = indexes.get(pack, {})
        hit = find_candidate(idx, prefs)
        # cross-pack fallback
        if not hit:
            for other_pack, other_idx in indexes.items():
                hit = find_candidate(other_idx, prefs)
                if hit:
                    pack = other_pack
                    break
        if not hit:
            missing.append(dest_stem)
            print(f"MISSING {dest_stem}")
            continue
        src_key, member = hit
        ext = Path(member).suffix.lower()
        out = IMPORT / f"{dest_stem}{ext}"
        extract_member(zip_paths[pack], member, out)
        # Prefer .wav name for Unreal import script even if ogg — keep real ext
        staged.append(out.name)
        mapping_lines.append(f"{dest_stem}{ext}  <=  kenney_{pack} / {member}  (CC0 Kenney)")
        print(f"OK {out.name} <= {member}")

    (STAGING / "SOURCE_MAP.txt").write_text("\n".join(mapping_lines) + "\n", encoding="utf-8")
    attr = ROOT / "Content" / "Audio" / "ATTRIBUTION.md"
    attr.write_text(
        "\n".join(
            [
                "# Looped Audio Attribution",
                "",
                "All gameplay SFX staged for LOOPED are from **Kenney.nl** asset packs,",
                "licensed under **Creative Commons CC0 1.0** (public domain dedication).",
                "",
                "- Impact Sounds — https://kenney.nl/assets/impact-sounds",
                "- UI Audio — https://kenney.nl/assets/ui-audio",
                "- RPG Audio — https://kenney.nl/assets/rpg-audio",
                "- Digital Audio — https://kenney.nl/assets/digital-audio",
                "",
                "No voice-over / character VO assets. Dialogue remains text-only.",
                "",
                "See `Tools/audio_staging/SOURCE_MAP.txt` for per-file source mapping.",
                "",
            ]
        ),
        encoding="utf-8",
    )

    print(f"\nStaged {len(staged)} files into {IMPORT}")
    if missing:
        print("Missing:", ", ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
