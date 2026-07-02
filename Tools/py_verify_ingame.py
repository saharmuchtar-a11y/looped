# In-PIE verification of the new content: relic aggregators, cursed bargain, curses, card equip.
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print("PIE WORLD:", world.get_name() if world else None)
gi = unreal.GameplayStatics.get_game_instance(world)
print("GI:", gi.get_class().get_name())

fails = []

def expect(desc, cond):
    print(("PASS " if cond else "FAIL ") + desc)
    if not cond:
        fails.append(desc)

# --- Run relics: data-only families ---
base_speed = gi.get_artifact_speed_bonus()
gi.grant_run_artifact("WindrunnerCharm")
expect("WindrunnerCharm speed bonus +0.12", abs(gi.get_artifact_speed_bonus() - base_speed - 0.12) < 0.001)

gi.grant_run_artifact("ResonantShard")
expect("ResonantShard echo mult 1.25", abs(gi.get_artifact_echo_mult() - 1.25) < 0.001)

# --- Cursed bargain: BloodPact grants +40 MaxHP AND injects Weakness ---
gi.grant_run_artifact("BloodPact")
expect("BloodPact flat MaxHP +40", abs(gi.get_artifact_flat_max_hp() - 40.0) < 0.001)
expect("BloodPact injected Weakness curse", gi.has_curse("Weakness"))

# --- Bespoke-hook relics are holdable ---
gi.grant_run_artifact("BerserkerFetish")
gi.grant_run_artifact("StaticCapacitor")
expect("BerserkerFetish held", gi.has_run_artifact("BerserkerFetish"))
expect("StaticCapacitor held", gi.has_run_artifact("StaticCapacitor"))

# --- New curses: add + describe ---
gi.add_curse("Volatile")
gi.add_curse("Static")
expect("Volatile active", gi.has_curse("Volatile"))
expect("Static active", gi.has_curse("Static"))
for c in ("Weakness", "Volatile", "Static"):
    d = str(gi.get_curse_description(c))
    print(f"  desc {c}: {d}")
    expect(f"{c} has description", len(d) > 0)

# --- New cards: eligible + equippable ---
eligible = [str(n) for n in gi.get_eligible_cards([])]
print("ELIGIBLE:", eligible)
for card in ("Frostbite", "Echo", "Deadeye"):
    expect(f"{card} eligible", card in eligible)
    lvl = gi.add_or_level_card(card)
    expect(f"{card} equipped at level {lvl}", gi.get_card_level(card) == lvl and lvl >= 1)

# Level captions render
print("Frostbite L1 desc:", str(gi.get_card_description_for_level("Frostbite", 1)))
print("Deadeye upgrade preview:", str(gi.get_card_upgrade_preview_text("Deadeye", 1)))

# Clean slate afterwards so the playthrough isn't polluted by the test grants
gi.reset_run_state()
expect("run state reset (no relics)", not gi.has_run_artifact("BloodPact"))
expect("run state reset (no curses)", not gi.has_curse("Weakness"))

print("INGAME_FAILURES:" if fails else "INGAME_ALL_PASSED")
for f in fails:
    print(" -", f)
