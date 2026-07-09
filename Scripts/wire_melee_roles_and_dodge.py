# Editor Python — wire MeleeRole on DT_Enemies + Dodge anims on DT_EnemyVisuals
# Run via VibeUE execute_python_code AFTER C++ rebuild (new struct fields must exist).
# import unreal

import unreal

ROLE = {
    "Hound": "Flanker",
    "Rotclaw": "Swarm",
    "Voidling": "Swarm",
    "Grunt": "Regular",
    "Brute": "Tank",
    "Frostbrute": "Tank",
    "Spitter": "None",
    "Frostcaller": "None",
    "Emberling": "None",
    "Venomspitter": "None",
    "VoidAcolyte": "Regular",
    "EventChampion": "Regular",
    "Warden": "Regular",
}

def set_melee_roles():
    dt = unreal.EditorAssetLibrary.load_asset("/Game/Data/DT_Enemies")
    if not dt:
        print("DT_Enemies missing")
        return
    # DataTableService if available; else print guidance
    try:
        svc = unreal.DataTableService
    except Exception:
        svc = None
    for row_name, role in ROLE.items():
        print(f"SET {row_name} -> MeleeRole={role} (set in editor if API lacks enum write)")
    print("Done listing. Prefer editing MeleeRole column in DT_Enemies Details.")

def list_back_jump_anims():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = registry.get_assets_by_path("/Game/new_assets", recursive=True)
    hits = []
    for a in assets:
        n = str(a.asset_name)
        if "Back" in n or "back" in n or "Jump" in n or "Dodge" in n or "dodge" in n:
            hits.append(str(a.package_name) + "." + n)
    for h in hits[:80]:
        print(h)
    print("count", len(hits))

set_melee_roles()
list_back_jump_anims()
