import unreal, traceback
# Ghoul locomotion = motion matching. Run with the editor closed (commandlet; Scripts\RunPy.ps1 Scripts/Python/ghoulmm.py). Idempotent.
#   1. PSD_Ghoul (Animations/Enemies/Ghoul/MM): the 16 MM loops (Run + Walk x F FL FR L R B BL BR) + A_Ghoul_Idle, schema PSS_Ghoul
#   2. ABP_Ghoul: MotionMatching(PSD_Ghoul) -> PoseHistory (trajectory generated from the character movement) -> DefaultSlot ->
#      Inertialization -> output (C++ UMD_AILibrary.build_enemy_anim_blueprint)
#   3. BP_Enemy_Ghoul uses ABP_Ghoul (anim class), as before
# Report: Saved/ClaudeDump/ghoulmm.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghoulmm.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
def sec(name, fn):
    try:
        P("== " + name); return fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc()))

EAL = unreal.EditorAssetLibrary
ANIM = "/Game/Animations/Enemies/Ghoul"
MM = ANIM + "/MM"
SK_PATH = "/Game/Characters/UE5_Mannequins/Meshes/SK_Mannequin"
DIRS = ["F", "FL", "FR", "L", "R", "B", "BL", "BR"]

def unpack(res):
    if isinstance(res, tuple): return res[0], list(res[1])
    return res, []

def s_psd():
    schema = unreal.load_asset(MM + "/PSS_Ghoul")
    P("  schema %s" % schema)
    seqs = [unreal.load_asset(ANIM + "/A_Ghoul_Idle")]
    for gait in ("Run", "Walk"):
        for d in DIRS:
            seqs.append(unreal.load_asset("%s/A_Ghoul_MM_%s_%s" % (MM, gait, d)))
    missing = [i for i, s in enumerate(seqs) if s is None]
    if missing:
        raise RuntimeError("missing clips at %s" % missing)
    n, log = unpack(unreal.MD_PoseSearchLibrary.build_database_from_anims(MM + "/PSD_Ghoul", schema, seqs))
    for l in log: P("  " + str(l))
    P("  built entries: %s" % n)
    psd = unreal.load_asset(MM + "/PSD_Ghoul")
    try:
        P("  database now has %d entries" % len(psd.get_editor_property('animation_assets')))
    except Exception as ex:
        P("  (could not read entries: %s)" % ex)
    return psd
psd = sec("PSD_Ghoul", s_psd)

def s_abp():
    if psd is None:
        P("  no database"); return None
    sk = unreal.load_asset(SK_PATH)
    ok, log = unpack(unreal.MD_AILibrary.build_enemy_anim_blueprint(ANIM + "/ABP_Ghoul", sk, psd))
    for l in log: P("  " + str(l))
    P("  build ok = %s" % ok)
    return ok
sec("ABP_Ghoul", s_abp)

def s_bp():
    abp = unreal.load_asset(ANIM + "/ABP_Ghoul")
    bp = unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
    cdo = unreal.get_default_object(bp.generated_class())
    mesh = cdo.get_editor_property('mesh')
    mesh.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT)
    mesh.set_anim_instance_class(abp.generated_class())
    bp.modify()
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    EAL.save_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul", only_if_is_dirty=False)
    P("  BP_Enemy_Ghoul anim class = %s" % mesh.get_editor_property('anim_class'))
sec("BP_Enemy_Ghoul", s_bp)
open(out, "w").write("\n".join(L))
