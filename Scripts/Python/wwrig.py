import unreal, traceback
# Read-only: chain layout of the project's UE5 mannequin IK rig, to mirror on the Werewolf skeleton.
# Report: Saved/ClaudeDump/wwrig.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/wwrig.txt"
L = []
def P(s):
    L.append(str(s)); print(s)
try:
    rig = unreal.load_asset("/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget")
    c = unreal.IKRigController.get_controller(rig)
    P("retarget root: %s" % c.get_retarget_root())
    for ch in c.get_retarget_chains():
        P("  chain %s" % [(k, str(ch.get_editor_property(k))) for k in ("chain_name","start_bone","end_bone","ik_goal_name")] if False else "  chain " + str(ch.get_editor_property("chain_name")) + " | " + str(ch.get_editor_property("start_bone")) + " | " + str(ch.get_editor_property("end_bone")) + " | " + str(ch.get_editor_property("ik_goal_name")))
    P("goals: %s" % [str(g) for g in c.get_all_goals()] if hasattr(c, 'get_all_goals') else "")
    P("solvers: %d" % c.get_num_solvers())
    P("methods: %s" % [m for m in dir(c) if not m.startswith('_')])
except Exception:
    P(traceback.format_exc())
try:
    rtg = unreal.load_asset("/Game/Animations/Enemies/Ghoul/Retarget/RTG_FabManny_to_Ghoul")
    rc = unreal.IKRetargeterController.get_controller(rtg)
    P("retargeter methods: %s" % [m for m in dir(rc) if not m.startswith('_')])
except Exception:
    P(traceback.format_exc())
try:
    a = unreal.load_asset("/Game/Animations/Enemies/Ghoul/A_Ghoul_Run")
    P("anim methods: %s" % [m for m in dir(a) if 'control' in m.lower() or 'model' in m.lower() or 'root' in m.lower()])
except Exception:
    P(traceback.format_exc())
open(out, "w").write("\n".join(L))
