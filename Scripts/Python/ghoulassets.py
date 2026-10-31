import unreal, traceback
# V1 WP1 Ghoul base assets (run with the editor closed as a commandlet; idempotent):
#   1. ST_Enemy StateTree built from C++ (UMD_AILibrary::BuildEnemyStateTree)
#   2. SK_Ghoul: a copy of the Fab ghoul mesh outside Content/Fab, on the project's SK_Mannequin skeleton
#      (its 79 bones are a strict subset of the 161 of SK_Mannequin), physics asset PA_Ghoul_UE5
#   3. BP_Enemy_Ghoul: mesh, anim class ABP_Ghoul, camera / visibility collision
# The animation side lives elsewhere: ghoulmm.py (PSD_Ghoul + ABP_Ghoul, motion matching) and ghoulcombat.py (the three
# attack montages + DA_Archetype_Ghoul attacks and nimble numbers). The old Swipe / Lunge / blend space placeholders are gone.
# Report: Saved/ClaudeDump/ghoulassets.txtout = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulassets.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
def sec(name, fn):
    try:
        P("== " + name); return fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc())); return None

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.AnimationLibrary
SK_PATH = "/Game/Characters/UE5_Mannequins/Meshes/SK_Mannequin"
ANIM_DIR = "/Game/Animations/Enemies/Ghoul"
CHAR_DIR = "/Game/Characters/Enemies/Ghoul"
MESH_PATH = CHAR_DIR + "/SK_Ghoul"

def save(path):
    ok = EAL.save_asset(path, only_if_is_dirty=False)
    P("  saved %s -> %s" % (path, ok)); return ok

def unpack(res):
    """(ok, log) from a C++ helper returning bool + TArray<FString>: python gives a tuple or just the array."""
    if isinstance(res, tuple):
        return bool(res[0]), list(res[1])
    log = [res] if isinstance(res, str) else list(res)
    return not any(str(l).startswith("Error") for l in log), log

# ---------------------------------------------------------------- 1. StateTree
def s_statetree():
    ok, log = unpack(unreal.MD_AILibrary.build_enemy_state_tree("/Game/AI/ST_Enemy"))
    for l in log: P("  " + str(l))
    P("  build ok = %s" % ok)
    for l in unreal.MD_AILibrary.describe_state_tree("/Game/AI/ST_Enemy"): P("  " + str(l))
sec("StateTree", s_statetree)

# ---------------------------------------------------------------- 2. Mesh
def s_mesh():
    sk = unreal.load_asset(SK_PATH)
    if not EAL.does_asset_exist(MESH_PATH):
        EAL.make_directory(CHAR_DIR)
        dup = EAL.duplicate_asset("/Game/Fab/Ghoul/Mesh/SK_Ghoul_UE5", MESH_PATH)
        P("  duplicated -> %s" % (dup.get_path_name() if dup else None))
    mesh = unreal.load_asset(MESH_PATH)
    if mesh.skeleton != sk:
        r = unreal.MD_AILibrary.assign_skeleton(mesh, sk)
        ok, err = (r[0], r[1]) if isinstance(r, tuple) else ((r == "" or r is True), r)
        P("  assign_skeleton -> %s %s" % (ok, err))
        if not ok:
            raise RuntimeError("skeleton assignment failed: " + str(err))
    pa = unreal.load_asset("/Game/Data/Enemies/Physics/PA_Ghoul_UE5")
    if pa and mesh.physics_asset != pa:
        mesh.set_editor_property('physics_asset', pa)
    mesh.modify(); save(MESH_PATH)
    mesh = unreal.load_asset(MESH_PATH)
    P("  SK_Ghoul skeleton now: %s, physics: %s" % (mesh.skeleton.get_path_name(), mesh.physics_asset.get_path_name() if mesh.physics_asset else None))
    return mesh
ghoul_mesh = sec("Mesh", s_mesh)

# ECollisionResponse is exposed as unreal.CollisionResponseType
CR_IGNORE = unreal.CollisionResponseType.ECR_IGNORE
CR_BLOCK = unreal.CollisionResponseType.ECR_BLOCK

def s_blueprint():
    bp = unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
    cdo = unreal.get_default_object(bp.generated_class())
    mesh = cdo.get_editor_property('mesh')
    if ghoul_mesh is None:
        raise RuntimeError("no SK_Ghoul: leaving the Blueprint mesh alone")
    mesh.set_skeletal_mesh_asset(ghoul_mesh)
    abp = unreal.load_asset(ANIM_DIR + "/ABP_Ghoul")
    if abp:
        mesh.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT)
        mesh.set_anim_instance_class(abp.generated_class())
        P("  anim class ABP_Ghoul")
    else:
        idle = unreal.load_asset(ANIM_DIR + "/A_Ghoul_Idle")
        if idle:
            ad = mesh.get_editor_property('animation_data')
            ad.set_editor_property('anim_to_play', idle)
            mesh.set_editor_property('animation_data', ad)
        P("  ABP_Ghoul not built yet: single-node A_Ghoul_Idle for now")
    # The BP's serialized collision (profile Custom, everything blocking) overrides the C++ constructor, so the camera
    # channel is cleared on the BP's components as well: the player's camera probe must pass through enemies. Visibility
    # stays blocking for the weapon sweeps. (AMDEnemyCharacter::PostInitializeComponents enforces the same at runtime.)
    for comp in (cdo.get_editor_property('capsule_component'), cdo.get_editor_property('mesh')):
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA, CR_IGNORE)
        comp.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY, CR_BLOCK)
    bp.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
    cdo = unreal.get_default_object(unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul").generated_class())
    P("  BP mesh now: %s" % cdo.get_editor_property('mesh').skeletal_mesh_asset.get_path_name())
    for comp in (cdo.get_editor_property('capsule_component'), cdo.get_editor_property('mesh')):
        P("  %s: Camera %s, Visibility %s" % (comp.get_name(), comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_CAMERA), comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)))
sec("Blueprint", s_blueprint)

open(out, "w").write("\n".join(L))
