"""Content expansion (2026-07-02): adds the 3 new cards + 5 new run relics.

Runs automatically ONCE at next editor startup (registered under
[/Script/PythonScriptPlugin.PythonScriptPluginSettings] +StartupScripts in
Config/DefaultEngine.ini) and then removes that hook itself. Can also be run
headless: UnrealEditor-Cmd.exe Looped.uproject -ExecutePythonScript="...".

Idempotent: rows that already exist are skipped. Prints CREATED:/SKIPPED:/ERROR: lines
so a failed run can be rolled back by hand.
"""
import json
import os
import unreal

DT_CARDS = "/Game/Data/DT_PassiveCards"
DT_ARTS = "/Game/Data/DT_Artifacts"
DTS = unreal.DataTableService


def tag(name):
    return {"GameplayTags": [{"TagName": name}]}


def get_existing_field(table, row, field):
    """Copy a field (e.g. the placeholder icon path) from an existing row."""
    raw = DTS.get_row(table, row)
    if not raw:
        return None
    try:
        return json.loads(raw).get(field)
    except Exception:
        return None


def add(table, row_name, data):
    if DTS.row_exists(table, row_name):
        print(f"SKIPPED: {table} row '{row_name}' already exists")
        return
    if DTS.add_row(table, row_name, json.dumps(data)):
        print(f"CREATED: {table} row '{row_name}'")
    else:
        print(f"ERROR: failed to add {table} row '{row_name}'")


print("existing card rows:", list(DTS.list_rows(DT_CARDS)))
print("existing artifact rows:", list(DTS.list_rows(DT_ARTS)))

# Reuse existing art as placeholders until Sahar supplies real frames/icons.
card_icon = get_existing_field(DT_CARDS, "BurnShot", "CardIcon")
art_icon = get_existing_field(DT_ARTS, "Bloodstone", "Icon")

# ---------------- Cards (DT_PassiveCards / FPassiveCardData) ----------------

frostbite_levels = []
frostbite_descs = []
for freeze in (1.2, 1.4, 1.6, 1.8, 2.0):
    frostbite_levels.append({"FreezeDuration": freeze})
    frostbite_descs.append(f"3 chill stacks freeze the target for {freeze:.1f}s")

echo_levels = []
echo_descs = []
for interval in (5, 4, 3):
    echo_levels.append({"EchoInterval": interval})
    echo_descs.append(f"every {interval}th hit triggers your card effects twice")

deadeye_levels = []
deadeye_descs = []
for pct in (5, 10, 15, 20, 25):
    deadeye_levels.append({"CritChance": pct / 100.0, "CritMultiplier": 2.0})
    deadeye_descs.append(f"{pct}% chance to crit for double damage")

cards = {
    "Frostbite": {
        "DisplayName": "Frostbite",
        "Description": "Hits chill. Three stacks freeze the target solid.",
        "Rarity": "Rare",
        "CardColor": {"R": 0.3, "G": 0.8, "B": 1.0, "A": 1.0},
        "EffectTags": tag("Effect.Cryo"),
        "MaxLevel": 5,
        "bRequiresUnlock": False,
        "Levels": frostbite_levels,
        "LevelDescriptions": frostbite_descs,
    },
    "Echo": {
        "DisplayName": "Echo",
        "Description": "Every few hits, your card effects trigger twice.",
        "Rarity": "Epic",
        "CardColor": {"R": 0.7, "G": 0.4, "B": 1.0, "A": 1.0},
        "EffectTags": tag("Effect.Echo"),
        "MaxLevel": 3,
        "bRequiresUnlock": False,
        "Levels": echo_levels,
        "LevelDescriptions": echo_descs,
    },
    "Deadeye": {
        "DisplayName": "Deadeye",
        "Description": "A chance for your hits to strike critically for double damage.",
        "Rarity": "Common",
        "CardColor": {"R": 1.0, "G": 0.85, "B": 0.3, "A": 1.0},
        "EffectTags": tag("Effect.Crit"),
        "MaxLevel": 5,
        "bRequiresUnlock": False,
        "Levels": deadeye_levels,
        "LevelDescriptions": deadeye_descs,
    },
}

for name, data in cards.items():
    if card_icon:
        data["CardIcon"] = card_icon
    add(DT_CARDS, name, data)

# ---------------- Run relics (DT_Artifacts / FArtifactData) ----------------

artifacts = {
    # Bespoke code hooks keyed on the ROW NAME — do not rename without a C++ change.
    "BerserkerFetish": {
        "DisplayName": "Berserker Fetish",
        "Description": "Below 30% HP your damage rises 50%.",
        "Rarity": "Rare",
        "Scope": "Run",
        "Magnitude": 0.0,
    },
    "StaticCapacitor": {
        "DisplayName": "Static Capacitor",
        "Description": "Every 8th hit discharges a spark pulse around the target.",
        "Rarity": "Rare",
        "Scope": "Run",
        "Magnitude": 0.0,
    },
    # Generic magnitude families — pure data.
    "WindrunnerCharm": {
        "DisplayName": "Windrunner Charm",
        "Description": "Move 12% faster this run.",
        "Rarity": "Common",
        "Scope": "Run",
        "EffectTags": tag("Artifact.MoveSpeed"),
        "Magnitude": 0.12,
    },
    "ResonantShard": {
        "DisplayName": "Resonant Shard",
        "Description": "Echo gains are 25% richer this run.",
        "Rarity": "Common",
        "Scope": "Run",
        "EffectTags": tag("Artifact.EchoGain"),
        "Magnitude": 1.25,
    },
    "BloodPact": {
        "DisplayName": "Blood Pact",
        "Description": "+40 max HP... but your blows land weaker.",
        "Rarity": "Epic",
        "Scope": "Run",
        "EffectTags": tag("Artifact.MaxHP"),
        "Magnitude": 40.0,
        "bIsCursed": True,
        "AssociatedCurseId": "Weakness",
    },
}

for name, data in artifacts.items():
    if art_icon:
        data["Icon"] = art_icon
    add(DT_ARTS, name, data)

# ---------------- Save + verify ----------------

for asset in (DT_CARDS, DT_ARTS):
    ok = unreal.EditorAssetLibrary.save_asset(asset)
    print(f"SAVED: {asset} -> {ok}")

print("final card rows:", list(DTS.list_rows(DT_CARDS)))
print("final artifact rows:", list(DTS.list_rows(DT_ARTS)))
for verify_row in ("Frostbite", "Echo", "Deadeye"):
    print(f"VERIFY {verify_row}:", DTS.get_row(DT_CARDS, verify_row))
for verify_row in ("BerserkerFetish", "StaticCapacitor", "WindrunnerCharm", "ResonantShard", "BloodPact"):
    print(f"VERIFY {verify_row}:", DTS.get_row(DT_ARTS, verify_row))

# ---------------- One-shot: remove the startup hook so this never runs again ----------------

HOOK_LINES = (
    "[/Script/PythonScriptPlugin.PythonScriptPluginSettings]",
    "; One-shot content-expansion data import (2026-07-02): adds the 3 new cards + 5 new relics,",
    "; saves the tables, then removes this hook itself. Safe to delete if it already ran.",
    "+StartupScripts=C:/Users/sahar/Desktop/Sahar_playground/Looped/Scripts/add_content_rows.py",
)

try:
    ini_path = os.path.join(
        unreal.Paths.project_config_dir(), "DefaultEngine.ini"
    )
    with open(ini_path, "r", encoding="utf-8") as f:
        lines = f.readlines()
    kept = [ln for ln in lines if ln.strip() not in HOOK_LINES]
    if len(kept) != len(lines):
        with open(ini_path, "w", encoding="utf-8") as f:
            f.writelines(kept)
        print("CLEANED: removed add_content_rows.py startup hook block from DefaultEngine.ini")
except Exception as e:  # never let cleanup kill the editor session
    print(f"WARNING: could not remove startup hook: {e}")

