# R0: puts the support-hand IK node into ABP_GenericRetarget (idempotent). Run via Scripts\RunPy.ps1 Scripts\Python\mkik.py (editor closed).
# Graph afterwards: RetargetPoseFromMesh -> LocalToComponent -> MD Support Hand IK -> ComponentToLocal -> Output Pose.
import os
import unreal

ABP = "/Game/Blueprints/GASP/RetargetedCharacters/ABP_GenericRetarget"
lib = unreal.MD_AnimGraphLibrary
res = lib.insert_support_hand_ik(ABP)
out = ["insert result: %r" % (res,)]
out.append("IK nodes in graph now: %d" % lib.count_support_hand_ik_nodes(ABP))
path = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "mkik.txt")
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
