"""Wire the three ranged weapon data assets to Robert's gun meshes (idempotent, re-runnable).

Run in the editor (no PIE): py "<project>/Scripts/Python/gunsetup.py"

Hand socket convention (verified in PIE, see the GUNS report): on MainHand, +X points toward the ELBOW (fingers are -X),
+Z is the thumb / "up" side, -Z is the grip side. HandOffset is the weapon mesh transform relative to MainHand, so:
  * barrel direction must map to -X (toward the fingers), mesh up must stay +Z (yaw-only rotation), and
  * the mesh origin of all three guns sits at (or near) the pistol grip, so Location is just the palm offset.
SupportHandOffset is in the weapon mesh's local space (multiplied by the component transform).
Nothing is ever added to the Fab meshes; everything lives in the data assets.
"""
import unreal

REVOLVER = "/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver"
ION_RIFLE = "/Game/Fab/ION_RIFLE_-__EC-11_/ion_rifle_ec_11/SkeletalMeshes/ion_rifle_ec_11"
BEAM_RIFLE = "/Game/Fab/Beam_Rifle/model/StaticMeshes/model"


def T(loc, yaw, scale):
    return unreal.Transform(unreal.Vector(*loc), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(scale, scale, scale))
    # Rotator(roll, pitch, yaw) argument order in the Python API


GUNS = {
    # Revolver: static, barrel on mesh -X, grip down -Z, pivot at the grip. yaw 0 => barrel toward the fingers.
    "DA_Ranged_HollowPistol": dict(
        static=REVOLVER, skeletal=None, stance="PISTOL", grip="ONE_HANDED_SUPPORT_WHEN_AIMED",
        hand=T((-1.0, 0.0, 2.4), 0.0, 0.6), support=T((0.0, 0.0, -8.0), 0.0, 1.0)),
    # ION rifle EC-11: skeletal, muzzle on mesh -Y, pivot at the pistol grip. yaw -90 => muzzle toward the fingers.
    "DA_Ranged_CatacombRepeater": dict(
        static=None, skeletal=ION_RIFLE, stance="RIFLE", grip="TWO_HANDED",
        hand=T((-8.0, 0.0, 0.0), -90.0, 0.2), support=T((0.0, -150.0, 0.0), 0.0, 1.0)),
    # Beam rifle: static, muzzle on mesh -Y, pivot far from the mesh (X 18..81, Z 28..209), so Location re-centres it.
    "DA_Ranged_DuskrayPistol": dict(
        static=BEAM_RIFLE, skeletal=None, stance="RIFLE", grip="TWO_HANDED",
        hand=T((-6.7, 7.5, -11.0), -90.0, 0.15), support=T((50.0, -90.0, 80.0), 0.0, 1.0)),
}


def apply():
    for name, g in GUNS.items():
        path = "/Game/Data/Ranged/" + name
        da = unreal.EditorAssetLibrary.load_asset(path)
        if not da:
            unreal.log_error("gunsetup: missing " + path)
            continue
        da.set_editor_property("static_mesh", unreal.EditorAssetLibrary.load_asset(g["static"]) if g["static"] else None)
        da.set_editor_property("skeletal_mesh", unreal.EditorAssetLibrary.load_asset(g["skeletal"]) if g["skeletal"] else None)
        da.set_editor_property("stance", getattr(unreal.MDWeaponStance, g["stance"]))
        da.set_editor_property("grip_type", getattr(unreal.MDGripType, g["grip"]))
        da.set_editor_property("hand_offset", g["hand"])
        da.set_editor_property("has_right_grip_offset", True)
        da.set_editor_property("support_hand_offset", g["support"])
        da.set_editor_property("has_left_grip_offset", True)
        ok = unreal.EditorAssetLibrary.save_asset(path)
        unreal.log("gunsetup: %s saved=%s" % (name, ok))


apply()
