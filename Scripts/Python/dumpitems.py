# Diagnostic (R0): dumps the new ranged rows. Run via Scripts\RunPy.ps1 (editor closed). Output: Saved\ClaudeDump\dumpitems.txt
import os
import unreal

NEW = ("HollowPistol", "CatacombRepeater", "DuskrayPistol", "AmmoLight", "AmmoHeavy", "UpgradeShard", "Enemy.Ghoul")
out = []
lib = unreal.MD_ItemAssetLibrary
for enum in ("E_ItemTypes", "E_ItemName"):
    out.append("ENUM %s: %s" % (enum, lib.list_enumerators("/Game/Blueprints/Items/" + enum)))
for t in ("DT_Items", "DT_ItemStats", "DT_ItemDisplay", "DT_ElementResist"):
    out.append("TABLE %s" % t)
    for line in lib.dump_table("/Game/Blueprints/Items/" + t, 0):
        if line.split(" | ")[0] in NEW:
            out.append(line)
for n in ("HollowPistol", "CatacombRepeater", "DuskrayPistol"):
    da = unreal.load_asset("/Game/Data/Ranged/DA_Ranged_" + n)
    out.append("DA %s stance=%s grip=%s mesh=%s dmgtype=%s" % (n, da.get_editor_property("stance"), da.get_editor_property("grip_type"),
               da.get_editor_property("static_mesh"), da.get_editor_property("default_damage_type")))
path = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "dumpitems.txt")
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
