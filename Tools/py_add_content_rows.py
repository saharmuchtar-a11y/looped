# Content expansion: add 3 new cards to DT_PassiveCards and 5 new relics to DT_Artifacts.
# Idempotent (row_exists checks). Logs CREATED/SKIPPED per row, saves both assets at the end.
import json
import unreal

CARDS = "/Game/Data/DT_PassiveCards"
ARTIFACTS = "/Game/Data/DT_Artifacts"
PLACEHOLDER_ICON = "/Game/Textures/cards/curse.curse"  # same placeholder frame BurnShot uses


def tags(*names):
    parents = sorted({n.rsplit(".", 1)[0] for n in names if "." in n})
    return {
        "GameplayTags": [{"TagName": n} for n in names],
        "ParentTags": [{"TagName": p} for p in parents],
    }


def card_level(damage=0, ticks=0, slow=1.0, radius=0, heal=0, speed=0, gravity=1.0,
               maxhp=0, crit_chance=0.0, crit_mult=2.0, freeze=0.0, echo=0):
    return {
        "Damage": damage, "Ticks": ticks, "SlowMultiplier": slow, "Radius": radius,
        "HealAmount": heal, "MoveSpeedBonus": speed, "GravityScale": gravity,
        "FlatMaxHP": maxhp, "CritChance": crit_chance, "CritMultiplier": crit_mult,
        "FreezeDuration": freeze, "EchoInterval": echo,
    }


NEW_CARDS = {
    "Frostbite": {
        "DisplayName": "Frostbite",
        "Description": "Each hit chills. 3 chill stacks freeze the enemy solid.",
        "Rarity": "Rare",
        "CardColor": {"R": 0.25, "G": 0.75, "B": 1.0, "A": 1.0},
        "CardIcon": PLACEHOLDER_ICON,
        "EffectTags": tags("Effect.Cryo"),
        "MaxLevel": 5,
        "bRequiresUnlock": False,
        "Levels": [card_level(freeze=f) for f in (1.2, 1.4, 1.6, 1.8, 2.0)],
        "LevelDescriptions": [f"Freeze for {f:.1f}s at 3 chill stacks" for f in (1.2, 1.4, 1.6, 1.8, 2.0)],
    },
    "Echo": {
        "DisplayName": "Echo",
        "Description": "Every few hits, your whole effect chain triggers twice.",
        "Rarity": "Epic",
        "CardColor": {"R": 0.7, "G": 0.3, "B": 1.0, "A": 1.0},
        "CardIcon": PLACEHOLDER_ICON,
        "EffectTags": tags("Effect.Echo"),
        "MaxLevel": 3,
        "bRequiresUnlock": False,
        "Levels": [card_level(echo=n) for n in (5, 4, 3)],
        "LevelDescriptions": [f"Every {n}th hit echoes the chain" for n in (5, 4, 3)],
    },
    "Deadeye": {
        "DisplayName": "Deadeye",
        "Description": "A chance for your strikes to land twice as hard.",
        "Rarity": "Common",
        "CardColor": {"R": 1.0, "G": 0.85, "B": 0.2, "A": 1.0},
        "CardIcon": PLACEHOLDER_ICON,
        "EffectTags": tags("Effect.Crit"),
        "MaxLevel": 5,
        "bRequiresUnlock": False,
        "Levels": [card_level(crit_chance=c) for c in (0.05, 0.10, 0.15, 0.20, 0.25)],
        "LevelDescriptions": [f"{int(c * 100)}% chance to crit (x2 damage)" for c in (0.05, 0.10, 0.15, 0.20, 0.25)],
    },
}

NEW_ARTIFACTS = {
    # Bespoke code hooks keyed on the row NAME (HasRunArtifact) — EffectTags left empty on
    # purpose so the generic magnitude aggregators never double-apply them.
    "BerserkerFetish": {
        "DisplayName": "Berserker Fetish",
        "Description": "Hit 50% harder while below 30% HP. Pain is a weapon.",
        "Rarity": "Rare", "Scope": "Run",
        "EffectTags": {"GameplayTags": [], "ParentTags": []},
        "Magnitude": 0, "bRequiresUnlock": False, "bIsCursed": False, "AssociatedCurseId": "None",
    },
    "StaticCapacitor": {
        "DisplayName": "Static Capacitor",
        "Description": "Every 8th strike discharges a lightning pulse around your target.",
        "Rarity": "Rare", "Scope": "Run",
        "EffectTags": {"GameplayTags": [], "ParentTags": []},
        "Magnitude": 0, "bRequiresUnlock": False, "bIsCursed": False, "AssociatedCurseId": "None",
    },
    "WindrunnerCharm": {
        "DisplayName": "Windrunner Charm",
        "Description": "A charm of stolen wind. +12% move speed for the rest of the run.",
        "Rarity": "Common", "Scope": "Run",
        "EffectTags": tags("Artifact.MoveSpeed"),
        "Magnitude": 0.12, "bRequiresUnlock": False, "bIsCursed": False, "AssociatedCurseId": "None",
    },
    "ResonantShard": {
        "DisplayName": "Resonant Shard",
        "Description": "It hums with the loop's memory. Echo gains x1.25 this run.",
        "Rarity": "Common", "Scope": "Run",
        "EffectTags": tags("Artifact.EchoGain"),
        "Magnitude": 1.25, "bRequiresUnlock": False, "bIsCursed": False, "AssociatedCurseId": "None",
    },
    "BloodPact": {
        "DisplayName": "Blood Pact",
        "Description": "+40 Max HP... but your strikes weaken. The pact always collects.",
        "Rarity": "Rare", "Scope": "Run",
        "EffectTags": tags("Artifact.MaxHP"),
        "Magnitude": 40, "bRequiresUnlock": False, "bIsCursed": True, "AssociatedCurseId": "Weakness",
    },
}


def add_rows(table, rows):
    for name, data in rows.items():
        if unreal.DataTableService.row_exists(table, name):
            print(f"SKIPPED (exists): {table} / {name}")
            continue
        ok = unreal.DataTableService.add_row(table, name, json.dumps(data))
        print(f"{'CREATED' if ok else 'FAILED'}: {table} / {name}")


add_rows(CARDS, NEW_CARDS)
add_rows(ARTIFACTS, NEW_ARTIFACTS)

for path in (CARDS, ARTIFACTS):
    saved = unreal.EditorAssetLibrary.save_asset(path)
    print(f"SAVED: {path} -> {saved}")

print("CARD ROWS NOW:", [str(r) for r in unreal.DataTableService.list_rows(CARDS)])
print("ARTIFACT ROWS NOW:", [str(r) for r in unreal.DataTableService.list_rows(ARTIFACTS)])
print("ADD_DONE")
