"""R1: the re-based gun numbers (BALANCE.md section "Ranged") and the cosmetic references, written into the DA_Ranged_* assets.

Idempotent. Run with the editor closed: Scripts\\RunPy.ps1 Scripts/Python/gundata.py
Report: Saved/ClaudeDump/gundata.txt (re-reads every asset after saving).

Scale (BALANCE.md): player AttackPower 10 -> a melee light hit = 10; reference melee DPS 17.5 (1.75 light hits/s); Ghoul 45 HP.
Ratios kept from ranged-design section 9: pistol shot = 0.7x a light hit, repeater DPS = 0.8x melee DPS, beams = 0.9x.
Fab assets are only referenced, never edited. Gameplay tags cannot be set from Python: DefaultDamageType stays as it is.
"""
import os
import traceback
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Saved", "ClaudeDump", "gundata.txt")
L = []


def P(s=""):
    L.append(str(s))


FX = "/Game/Fab/FreeStylizedLaserBeamVFX/"
MUZZLE = FX + "VFX/MuzzleFlash/NS_MuzzleFlash_0%d"
HIT = FX + "VFX/Hit/NS_Hit_0%d"
BEAM_MI = FX + "Materials/Instances/Beam/MI_Cylinder_%02d"

GUNS = {
    "DA_Ranged_HollowPistol": dict(
        damage_per_shot=7.0, rounds_per_second=3.0, poise_damage=8.0, headshot_mult=1.5, mag_size=8,
        falloff_start=1200.0, falloff_end=2000.0, falloff_min_mult=0.6,
        spread_hip=3.0, spread_aimed=1.0, spread_moving=1.4, spread_bloom=0.6, spread_bloom_decay=3.0, max_bloom=3.0,
        recoil_pitch=1.2, recoil_yaw=0.4, recoil_recovery=0.25, kick_back_cm=3.0, kick_pitch_deg=7.0,
        proj=dict(speed=20000.0, radius=10.0, max_range=4000.0, gravity=0.0, pierce=0),
        muzzle_fx=MUZZLE % 1, tracer=BEAM_MI % 1, tracer_length=60.0, tracer_thickness=3.0, wear=0.5),
    "DA_Ranged_CatacombRepeater": dict(
        damage_per_shot=1.75, rounds_per_second=8.0, poise_damage=4.0, headshot_mult=1.5, mag_size=24,
        falloff_start=1500.0, falloff_end=2500.0, falloff_min_mult=0.5,
        spread_hip=4.5, spread_aimed=1.6, spread_moving=1.4, spread_bloom=0.35, spread_bloom_decay=3.0, max_bloom=3.0,
        recoil_pitch=0.5, recoil_yaw=0.35, recoil_recovery=0.2, kick_back_cm=1.5, kick_pitch_deg=3.5,
        proj=dict(speed=22000.0, radius=10.0, max_range=4000.0, gravity=0.0, pierce=0),
        muzzle_fx=MUZZLE % 2, tracer=BEAM_MI % 5, tracer_length=45.0, tracer_thickness=2.2, wear=0.5),
    # Duskray (beam, R5 ticks it): 0.9 x 17.5 = 15.75 DPS at 10 Hz (1.575 per tick). Only the data changes here.
    "DA_Ranged_DuskrayPistol": dict(
        damage_per_shot=1.575, rounds_per_second=0.0, poise_damage=0.0, headshot_mult=1.5, mag_size=0,
        falloff_start=1000.0, falloff_end=1800.0, falloff_min_mult=0.4,
        recoil_pitch=0.0, recoil_yaw=0.0, kick_back_cm=0.5, kick_pitch_deg=1.5,
        beam=dict(damage_per_second=15.75, tick_hz=10.0, range=2000.0, heat_per_second=30.0, vent_per_second=8.0, max_heat=100.0, overheat_lock=2.5, beam_durability_per_second=0.5),
          beam_fx="/Game/Fab/FreeStylizedLaserBeamVFX/VFX/NS_Laser_01", beam_color=(0.8, 0.3, 1.0, 1.0), overheat_fx="/Game/Fab/Legendary_Forge/Fx/Smoke/NS_smoke", reaction="NONE", poise=0.5,
        muzzle_fx=MUZZLE % 3, impact_fx=HIT % 2, tracer=BEAM_MI % 9, tracer_length=60.0, tracer_thickness=3.0, wear=0.5),
}


def soft(path):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        P("  MISSING asset " + path)
    return asset


def set_struct(da, prop, values):
    s = da.get_editor_property(prop)
    for k, v in values.items():
        s.set_editor_property(k, v)
    da.set_editor_property(prop, s)


try:
    for name, g in GUNS.items():
        path = "/Game/Data/Ranged/" + name
        da = unreal.EditorAssetLibrary.load_asset(path)
        if not da:
            P("missing " + path)
            continue
        plain = ["damage_per_shot", "rounds_per_second", "poise_damage", "headshot_mult", "mag_size", "falloff_start", "falloff_end",
                 "falloff_min_mult", "spread_hip", "spread_aimed", "spread_moving", "spread_bloom", "spread_bloom_decay",
                 "max_bloom", "recoil_pitch", "recoil_yaw", "recoil_recovery", "kick_back_cm", "kick_pitch_deg"]
        for p in plain:
            if p in g:
                da.set_editor_property(p, g[p])
        da.set_editor_property("reaction_cap", unreal.MDHitReaction.NONE if g.get("reaction") == "NONE" else unreal.MDHitReaction.FLINCH)
        if "poise" in g:
            da.set_editor_property("poise_damage", g["poise"])
        if "beam_fx" in g:
            da.set_editor_property("beam_fx", soft(g["beam_fx"]))
            da.set_editor_property("beam_color", unreal.LinearColor(*g["beam_color"]))
            da.set_editor_property("overheat_fx", soft(g["overheat_fx"]))
        da.set_editor_property("wear_per_hit", g["wear"])
        if "proj" in g:
            set_struct(da, "projectile", g["proj"])
        if "beam" in g:
            set_struct(da, "beam", g["beam"])
        da.set_editor_property("tracer_length", g["tracer_length"])
        da.set_editor_property("tracer_thickness", g["tracer_thickness"])
        for prop, key in (("muzzle_flash_fx", "muzzle_fx"), ("impact_fx", "impact_fx"), ("tracer_material", "tracer")):
            asset = soft(g[key]) if key in g else None
            if asset:
                da.set_editor_property(prop, asset)
        ok = unreal.EditorAssetLibrary.save_asset(path)
        P("%s saved=%s" % (name, ok))

    P("")
    P("---- re-read after save")
    for name in GUNS:
        da = unreal.EditorAssetLibrary.load_asset("/Game/Data/Ranged/" + name)
        pr = da.get_editor_property("projectile")
        bm = da.get_editor_property("beam")
        P("%s dmg=%s rps=%s poise=%s mag=%s falloff=%s/%s/%s cap=%s proj(speed=%s radius=%s range=%s) beam_dps=%s" % (
            name, da.get_editor_property("damage_per_shot"), da.get_editor_property("rounds_per_second"),
            da.get_editor_property("poise_damage"), da.get_editor_property("mag_size"),
            da.get_editor_property("falloff_start"), da.get_editor_property("falloff_end"), da.get_editor_property("falloff_min_mult"),
            da.get_editor_property("reaction_cap"), pr.get_editor_property("speed"), pr.get_editor_property("radius"),
            pr.get_editor_property("max_range"), bm.get_editor_property("damage_per_second")))
        P("   muzzle_fx=%s impact_fx=%s tracer=%s" % (
            da.get_editor_property("muzzle_flash_fx"), da.get_editor_property("impact_fx"), da.get_editor_property("tracer_material")))
except Exception:
    P(traceback.format_exc())

with open(OUT, "w") as f:
    f.write("\n".join(L))
