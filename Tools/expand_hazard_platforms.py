# Expand lava/venom in Crossing + LavaField so platforms are the main path,
# AND keep fight pockets / ferry routes reachable without camping on burn.
# Run via VibeUE execute_python_code while editor is open.
#
# Softlock fix 2026-07-09:
# - Crossing platforms must ferry bank→bank (old route ended mid-river).
# - LavaField keeps dense carpet under overlook but west approach + SE pocket stay clear.
# - Enemies stay on safe ground the player can reach via platforms / rim.
import unreal

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
HazardCls = unreal.load_class(None, "/Script/Looped.ElementalHazard")


def _set_hazard_props(a, row, dmg, pulse_on=0.0, pulse_off=0.0):
    try:
        a.set_editor_property("element_row", row)
        a.set_editor_property("damage_per_tick", float(dmg))
        a.set_editor_property("light_intensity", 0.0)
        a.set_editor_property("pulse_on_seconds", float(pulse_on))
        a.set_editor_property("pulse_off_seconds", float(pulse_off))
    except Exception as e:
        print("prop", e)


def _apply_specs(hazards, specs, row, dmg):
    for i, (lab, loc, sc) in enumerate(specs):
        a = hazards[i] if i < len(hazards) else eas.spawn_actor_from_class(HazardCls, loc)
        a.set_actor_label(lab)
        a.set_actor_location(loc, False, True)
        a.set_actor_scale3d(sc)
        _set_hazard_props(a, row, dmg)
        print("set", lab, "halfXY", 250.0 * sc.x, 250.0 * sc.y)
    for j, a in enumerate(hazards):
        if j >= len(specs):
            print("destroy extra", a.get_actor_label())
            eas.destroy_actor(a)


def _move_enemy(label, x, y, z):
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == label:
            a.set_actor_location(unreal.Vector(x, y, z), False, True)
            print("moved", label, "->", (x, y, z))
            return
    print("MISSING", label)


def expand_crossing():
    les.load_level("/Game/L_F2_Crossing")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    print("world", world.get_name())
    # Widen mid-room venom river (~x±750) so banks are separated; platforms ferry above.
    hazards = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "ElementalHazard"]
    print("hazards before", len(hazards), [a.get_actor_label() for a in hazards])
    specs = [
        ("VenomRiver_S", unreal.Vector(0.0, -1000.0, 0.0), unreal.Vector(3.0, 3.2, 1.0)),
        ("VenomRiver_M", unreal.Vector(0.0, 0.0, 0.0), unreal.Vector(3.0, 3.2, 1.0)),
        ("VenomRiver_N", unreal.Vector(0.0, 1000.0, 0.0), unreal.Vector(3.0, 3.2, 1.0)),
        ("VenomFinger_W", unreal.Vector(-550.0, 0.0, 0.0), unreal.Vector(1.4, 2.0, 1.0)),
        ("VenomFinger_E", unreal.Vector(550.0, 0.0, 0.0), unreal.Vector(1.4, 2.0, 1.0)),
    ]
    _apply_specs(hazards, specs, "Venom", 7.0)

    # Ferry platforms: west bank (-1200) → east bank (+1200). Old layout ended mid-river.
    for lab, y, delay in (("CrossingPlatform_1", -600.0, 0.0), ("CrossingPlatform_2", 600.0, 3.0)):
        for a in eas.get_all_level_actors():
            if a.get_actor_label() != lab:
                continue
            a.set_actor_location(unreal.Vector(-1200.0, y, 100.0), False, True)
            a.set_editor_property("stops", [unreal.Vector(2400.0, 0.0, 0.0)])
            a.set_editor_property("move_speed", 240.0)
            a.set_editor_property("pause_seconds", 1.5)
            a.set_editor_property("start_delay_seconds", delay)
            print("platform", lab, "bank ferry")

    for a in eas.get_all_level_actors():
        if a.get_actor_label() == "R1_Platform":
            a.set_actor_location(unreal.Vector(-1200.0, 0.0, 100.0), False, True)
            a.set_actor_scale3d(unreal.Vector(3.0, 3.0, 0.40))
            print("R1_Platform west boarding pad")

    # Bank fight pockets clear of river half-width 750 (+ pad)
    _move_enemy("L_Room5_Enemy_2", -1400.0, -1000.0, 100.0)
    _move_enemy("L_Room5_Enemy_4", -1400.0, 1000.0, 100.0)
    _move_enemy("L_Room5_Enemy_6", 1400.0, -1000.0, 100.0)
    _move_enemy("L_Room5_Enemy_1", 1400.0, 1000.0, 90.0)
    _move_enemy("L_Room5_Enemy_3", 1400.0, -200.0, 100.0)
    _move_enemy("L_Room5_Enemy_5", 1400.0, 1400.0, 100.0)
    print("save Crossing", les.save_current_level())


def expand_lavafield():
    les.load_level("/Game/L_F3_LavaField")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    print("world", world.get_name())
    hazards = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == "ElementalHazard"]
    pools, blowers = [], []
    for a in hazards:
        try:
            pon = a.get_editor_property("pulse_on_seconds")
        except Exception:
            pon = 0.0
        if pon and float(pon) > 0:
            blowers.append(a)
        else:
            pools.append(a)
    print("pools", len(pools), "blowers", len(blowers))
    # Dense Fire carpet under overlook; west approach corridor + SE pocket stay walkable.
    lava_specs = [
        ("Lava_C", unreal.Vector(200.0, 0.0, 0.0), unreal.Vector(3.2, 3.2, 1.0)),
        ("Lava_N", unreal.Vector(250.0, 800.0, 0.0), unreal.Vector(3.0, 2.4, 1.0)),
        ("Lava_S", unreal.Vector(250.0, -800.0, 0.0), unreal.Vector(3.0, 2.4, 1.0)),
        ("Lava_E", unreal.Vector(950.0, 0.0, 0.0), unreal.Vector(2.4, 3.2, 1.0)),
        ("Lava_NE", unreal.Vector(800.0, 700.0, 0.0), unreal.Vector(2.2, 2.2, 1.0)),
        ("Lava_SE", unreal.Vector(800.0, -700.0, 0.0), unreal.Vector(2.2, 2.2, 1.0)),
        ("Lava_NW", unreal.Vector(-100.0, 600.0, 0.0), unreal.Vector(2.0, 2.0, 1.0)),
        ("Lava_SW", unreal.Vector(-100.0, -600.0, 0.0), unreal.Vector(2.0, 2.0, 1.0)),
        ("Lava_E2", unreal.Vector(1200.0, 250.0, 0.0), unreal.Vector(1.8, 2.4, 1.0)),
        ("Lava_N2", unreal.Vector(450.0, 1150.0, 0.0), unreal.Vector(2.4, 1.6, 1.0)),
        ("Lava_S2", unreal.Vector(450.0, -1150.0, 0.0), unreal.Vector(2.4, 1.6, 1.0)),
    ]
    _apply_specs(pools, lava_specs, "Fire", 10.0)
    _move_enemy("R6_Enemy_RampW", -1400.0, 0.0, 100.0)
    _move_enemy("R6_Enemy_RampE", 1550.0, -1450.0, 100.0)
    _move_enemy("R6_Enemy_Roam1", -1400.0, 1200.0, 100.0)
    _move_enemy("R6_Enemy_Roam2", -1400.0, -1200.0, 100.0)
    _move_enemy("R6_Enemy_TopAnchor", 150.0, 150.0, 300.0)
    _move_enemy("R6_Enemy_TopRoll", -150.0, -150.0, 300.0)
    print("blowers kept", [b.get_actor_label() for b in blowers])
    print("save LavaField", les.save_current_level())


expand_crossing()
expand_lavafield()
print("DONE")
