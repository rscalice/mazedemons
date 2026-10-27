import unreal
# Read-only data check of the Ghoul animation setup (ABP graph links, blend space evaluation, retargeted anims,
# BP_Enemy_Ghoul mesh anim class). Report: Saved/ClaudeDump/ghouldiag.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/ghouldiag.txt"
L = []
def P(s): L.append(str(s)); print(s)

ANIM_DIR = "/Game/Animations/Enemies/Ghoul"
bs = unreal.load_asset(ANIM_DIR + "/BS_Ghoul_Locomotion")
for l in unreal.MD_AILibrary.describe_enemy_animation(ANIM_DIR + "/ABP_Ghoul", bs): P(l)

P("== retargeted anims")
opts = unreal.AnimPoseEvaluationOptions()
for n in ("A_Ghoul_Idle", "A_Ghoul_Walk", "A_Ghoul_Run"):
    a = unreal.load_asset(ANIM_DIR + "/" + n)
    if a is None: P("  %s MISSING" % n); continue
    length = a.get_play_length()
    frames = a.get_editor_property('number_of_sampled_frames') if hasattr(a, 'number_of_sampled_frames') else -1
    p0 = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, opts)
    p1 = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, min(0.4, length * 0.5), opts)
    t0 = unreal.AnimPoseExtensions.get_bone_pose(p0, 'thigh_l', unreal.AnimPoseSpaces.LOCAL)
    t1 = unreal.AnimPoseExtensions.get_bone_pose(p1, 'thigh_l', unreal.AnimPoseSpaces.LOCAL)
    r0 = unreal.AnimPoseExtensions.get_bone_pose(p0, 'root', unreal.AnimPoseSpaces.WORLD).translation
    r1 = unreal.AnimPoseExtensions.get_bone_pose(p1, 'root', unreal.AnimPoseSpaces.WORLD).translation
    d = t0.rotation.angular_distance(t1.rotation)
    P("  %s: len %.2f frames %s skeleton %s rootmotion=%s thigh_l rotation change %.3f rad root travel (%.0f,%.0f,%.0f)" % (
        n, length, frames, a.get_editor_property('skeleton').get_name(), a.get_editor_property('enable_root_motion'), d,
        (r1 - r0).x, (r1 - r0).y, (r1 - r0).z))

P("== BP_Enemy_Ghoul")
bp = unreal.load_asset("/Game/Blueprints/Enemies/BP_Enemy_Ghoul")
cdo = unreal.get_default_object(bp.generated_class())
mesh = cdo.get_editor_property('mesh')
P("  mesh %s anim mode %s anim class %s" % (mesh.skeletal_mesh_asset.get_name() if mesh.skeletal_mesh_asset else None,
    mesh.get_editor_property('animation_mode'), mesh.get_editor_property('anim_class')))
try:
    P("  anim_class parent: %s" % unreal.SystemLibrary.get_class_display_name(mesh.get_editor_property('anim_class')))
except Exception as ex:
    P("  (%s)" % ex)
open(out, "w").write("\n".join(L))
