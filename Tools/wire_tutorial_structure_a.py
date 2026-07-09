# Post-build wiring for L_Tutorial Structure A.
# Run via VibeUE execute_python_code AFTER the C++ rebuild (new TutorialDirector props).
import unreal

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

# Ensure L_Tutorial is loaded
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not world or world.get_name() != "L_Tutorial":
    les.load_level("/Game/L_Tutorial")

by_label = {a.get_actor_label(): a for a in eas.get_all_level_actors()}

def get(lab):
    a = by_label.get(lab)
    if not a:
        raise RuntimeError("missing actor label: " + lab)
    return a

dir_actor = get("TutorialDirector")
dlg = get("Dialogue_Orin")
cage = get("Cage_Orin")
lever = get("Lever_Training")
goal = get("HazardGoal")
gate_move = get("Gate_AfterMove")
gate_haz = get("Gate_AfterHazard")
gate_arena = get("Gate_ToArena")
spawns = [get("TutorialSpawn_1"), get("TutorialSpawn_2"), get("TutorialSpawn_3")]

portal = None
for a in eas.get_all_level_actors():
    if a.get_class().get_name() == "PortalActor":
        portal = a
        break

dir_actor.set_editor_property("cage", cage)
dir_actor.set_editor_property("orin_dialogue", dlg)
dir_actor.set_editor_property("exit_portal", portal)
dir_actor.set_editor_property("gate_after_move", gate_move)
dir_actor.set_editor_property("gate_after_hazard", gate_haz)
dir_actor.set_editor_property("gate_to_arena", gate_arena)
dir_actor.set_editor_property("training_lever", lever)
dir_actor.set_editor_property("hazard_goal", goal)
dir_actor.set_editor_property("hazard_goal_radius", 250.0)
dir_actor.set_editor_property("spawn_points", spawns)
dir_actor.set_editor_property("move_distance_required", 900.0)

# Confirm
for p in ["orin_dialogue", "gate_after_move", "gate_after_hazard", "gate_to_arena", "training_lever", "hazard_goal"]:
    print(p, "=", dir_actor.get_editor_property(p))

les.save_current_level()
print("WIRED + SAVED")
