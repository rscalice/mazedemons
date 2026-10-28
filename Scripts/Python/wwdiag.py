import unreal, traceback, math
# Read-only: Werewolf skeleton / attack1 analysis + collision responses of BP_Player / BP_Enemy_Ghoul components.
# Report: Saved/ClaudeDump/wwdiag.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/wwdiag.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
def sec(name, fn):
    try:
        P("== " + name); return fn()
    except Exception:
        P("EXC in %s: %s" % (name, traceback.format_exc()))

def werewolf():
    a = unreal.load_asset("/Game/Fab/Werewolf/Animations/Anim_Werewolf_attack1")
    P("  class %s skeleton %s len %.2f rate %s root_motion=%s" % (a.get_class().get_name(), a.get_editor_property('skeleton').get_path_name(), a.get_play_length(), a.get_editor_property('rate_scale'), a.get_editor_property('enable_root_motion')))
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property('extract_root_motion', False)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, opts)
    names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
    P("  %d bones: %s" % (len(names), ", ".join(names)))
    mesh = unreal.load_asset("/Game/Fab/Werewolf/Mesh/Full/SK_Werewolf_Full1")
    P("  mesh skeleton %s" % mesh.skeleton.get_path_name())
sec("Werewolf", werewolf)

def comps(path, label):
    bp = unreal.load_asset(path)
    cdo = unreal.get_default_object(bp.generated_class())
    P("  %s" % label)
    for c in cdo.get_components_by_class(unreal.PrimitiveComponent):
        try:
            bi = c.get_editor_property('body_instance')
            ch = {}
            for nm, cc in (("Camera", unreal.CollisionChannel.ECC_CAMERA), ("Visibility", unreal.CollisionChannel.ECC_VISIBILITY), ("Pawn", unreal.CollisionChannel.ECC_PAWN)):
                ch[nm] = str(c.get_collision_response_to_channel(cc)).split('.')[-1]
            P("    %s (%s) enabled=%s profile=%s %s" % (c.get_name(), c.get_class().get_name(), c.get_collision_enabled(), c.get_collision_profile_name(), ch))
        except Exception as ex:
            P("    %s ERR %s" % (c.get_name(), ex))
sec("Collision", lambda: (comps("/Game/Blueprints/Enemies/BP_Enemy_Ghoul", "BP_Enemy_Ghoul"), comps("/Game/Blueprints/BP_Player", "BP_Player")))

def ghoulda():
    da = unreal.load_asset("/Game/Data/Enemies/DA_Archetype_Ghoul")
    for a in da.get_editor_property('attacks'):
        st = a.get_editor_property('step')
        P("  %s range %s tell %s cd %s warp_range %s stop %s rms %s montage %s" % (a.name, a.range, a.tell_time, a.cooldown, st.warp_range, st.warp_stop_distance, st.root_motion_scale, a.montage.get_name()))
sec("Archetype", ghoulda)
open(out, "w").write("\n".join(L))
