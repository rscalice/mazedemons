"""Wire the three ranged weapon data assets to Robert's gun meshes (idempotent, re-runnable).

Run in the editor (no PIE): py "<project>/Scripts/Python/gunsetup.py"

All guns attach to the VISIBLE Manny's hand_r bone (the MainHand socket on that mesh sits ~33 cm from the palm, which
made the guns float). hand_r frame in the raised aim pose: +X points toward the ELBOW (fingers are -X), +Z is the
thumb / "up" side, +Y is the palm side. HandOffset is the weapon mesh transform relative to hand_r:
    Location = PalmPoint - R * (Scale * GripPointInMeshSpace)
where R maps the barrel to -X and mesh up to +Z (yaw only), PalmPoint = (-?, +1.5, ?) in hand_r space.
SupportHandOffset is the left-hand target in the weapon mesh's local space (multiplied by the component transform).
It must stay within reach of the left arm (see FMDRangedRules::SupportHandReachAlpha); farther points fade the IK out.
Nothing is ever added to the Fab meshes; everything lives in the data assets.
Sizes: HollowPistol ~29 cm, CatacombRepeater ~84 cm, DuskrayPistol ~90 cm.
"""
import unreal

REVOLVER = "/Game/Fab/Free_Revolver___Handcanon/tesseract_revolver/StaticMeshes/tesseract_revolver"
ION_RIFLE = "/Game/Fab/ION_RIFLE_-__EC-11_/ion_rifle_ec_11/SkeletalMeshes/ion_rifle_ec_11"
BEAM_RIFLE = "/Game/Fab/Beam_Rifle/model/StaticMeshes/model"
HAND_SOCKET = "hand_r"


def T(loc, yaw, scale):
    return unreal.Transform(unreal.Vector(*loc), unreal.Rotator(0.0, 0.0, yaw), unreal.Vector(scale, scale, scale))
    # Rotator(roll, pitch, yaw) argument order in the Python API


GUNS = {
    # Revolver: static, barrel on mesh -X, grip down -Z. yaw 0 => barrel toward the fingers.
    "DA_Ranged_HollowPistol": dict(
        static=REVOLVER, skeletal=None, stance="PISTOL", grip="ONE_HANDED_SUPPORT_WHEN_AIMED",
        # R1: scale 0.6 -> 0.65 (the mesh is 48.9 cm long: 31.8 cm, a handcannon rather than a toy). The grip point is about
        # (0,0,-5) in mesh space, so Location.z moves by -(0.65-0.6)*(-5) = +0.25 to keep the grip in the palm.
        muzzle="-X",
        hand=T((-5.3, 1.5, -2.35), 0.0, 0.65), support=T((0.0, 0.0, -6.0), 0.0, 1.0)),
    # ION rifle EC-11: skeletal, muzzle on mesh -Y, pistol grip ~(0,18,-25). yaw -90 => muzzle toward the fingers.
    # Support point is on the handguard (-65 along the barrel) so the left hand is within arm reach.
    "DA_Ranged_CatacombRepeater": dict(
        static=None, skeletal=ION_RIFLE, stance="RIFLE", grip="TWO_HANDED",
        muzzle="-Y",
        hand=T((-9.14, 1.5, 0.75), -90.0, 0.23), support=T((0.0, -65.0, 12.0), 0.0, 1.0)),
    # Beam rifle: static, muzzle on mesh -Y, grip ~(50,5,70), pivot far from the mesh, so Location re-centres it.
    "DA_Ranged_DuskrayPistol": dict(
        static=BEAM_RIFLE, skeletal=None, stance="RIFLE", grip="TWO_HANDED",
        # R1: lowered 6 cm (z -13.3 -> -19.3) so it rides at chest height like the repeater. Tunable live with md.Ranged.Tune.
        muzzle="-Y",
        hand=T((-6.95, 11.0, -19.3), -90.0, 0.19), support=T((50.0, -80.0, 108.0), 0.0, 1.0)),
}


def muzzle_local(mesh, axis):
    """Muzzle in mesh space from the bounds: the tip of the barrel axis, centred on the other axes (Fab meshes have no Muzzle socket)."""
    b = mesh.get_bounds()
    o, e = b.origin, b.box_extent
    if axis == "-X":
        return unreal.Vector(o.x - e.x, o.y, o.z + 0.4 * e.z)  # the barrel rides in the upper part of a revolver
    return unreal.Vector(o.x, o.y - e.y, o.z)


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
        da.set_editor_property("hand_socket", HAND_SOCKET)
        da.set_editor_property("hand_offset", g["hand"])
        da.set_editor_property("has_right_grip_offset", True)
        da.set_editor_property("support_hand_offset", g["support"])
        da.set_editor_property("has_left_grip_offset", True)
        mesh = unreal.EditorAssetLibrary.load_asset(g["static"] or g["skeletal"])
        if mesh and g.get("muzzle"):
            da.set_editor_property("muzzle_local", muzzle_local(mesh, g["muzzle"]))
            da.set_editor_property("has_muzzle_local", True)
        ok = unreal.EditorAssetLibrary.save_asset(path)
        back = unreal.EditorAssetLibrary.load_asset(path)
        unreal.log("gunsetup: %s saved=%s hand_socket=%s hand_loc=%s" % (
            name, ok, back.get_editor_property("hand_socket"), back.get_editor_property("hand_offset").translation))


apply()
