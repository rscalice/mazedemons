"""Give the R0 guns and ammo real inventory icons (idempotent). Run via Scripts\\RunPy.ps1 with the editor closed.

There is no gun art yet, so PLACEHOLDERS are copies of the rasterloom icon sampler (white/128px): `gun` for the three guns
and `pouch` for both ammo rows. The copies live in Blueprints/Items/ItemIcons so nothing under Content/Fab is modified.
Replace the two textures (keep the asset names) when real art exists.
"""
import os
import unreal

SRC = "/Game/Fab/rasterloom_free_icon_sampler_v10/white/128px/"
DST = "/Game/Blueprints/Items/ItemIcons"
COPIES = {"gun": "T_Gun_Icon", "pouch": "T_AmmoPouch_Icon"}
ROWS = {
    "HollowPistol": "T_Gun_Icon", "CatacombRepeater": "T_Gun_Icon", "DuskrayPistol": "T_Gun_Icon",
    "AmmoLight": "T_AmmoPouch_Icon", "AmmoHeavy": "T_AmmoPouch_Icon",
}
out = []
tools = unreal.AssetToolsHelpers.get_asset_tools()
for src, name in COPIES.items():
    target = "%s/%s" % (DST, name)
    if not unreal.EditorAssetLibrary.does_asset_exist(target):
        copy = tools.duplicate_asset(name, DST, unreal.load_asset(SRC + src))
        out.append("duplicated %s -> %s (%s)" % (src, target, copy))
    unreal.EditorAssetLibrary.save_asset(target, only_if_is_dirty=False)

lib = unreal.MD_ItemAssetLibrary
for row, tex in ROWS.items():
    value = "/Script/Engine.Texture2D'%s/%s.%s'" % (DST, tex, tex)
    ok, log = lib.set_row_from_template("/Game/Blueprints/Items/DT_Items", row, row, {"ItemIcon": value})
    out.append("%s -> %s ok=%s %s" % (row, tex, ok, log))
unreal.EditorAssetLibrary.save_asset("/Game/Blueprints/Items/DT_Items", only_if_is_dirty=False)

# Re-read: saves can silently fail to persist.
for line in lib.dump_table("/Game/Blueprints/Items/DT_Items", 0):
    if line.split(" | ")[0] in ROWS:
        icon = [p for p in line.split(" | ") if p.startswith("ItemIcon")]
        out.append("VERIFY %s %s" % (line.split(" | ")[0], icon))
path = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Saved", "ClaudeDump", "gicons.txt")
with open(path, "w", encoding="utf-8") as f:
    f.write("\n".join(out))
