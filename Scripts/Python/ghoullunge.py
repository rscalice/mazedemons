"""Ghoul Lunge switch (Robert, 2026-10-28: the Lunge makes the ghouls jump, disabled for now).

DA_Archetype_Ghoul.Attacks holds only the Swipe while the Lunge is off. The AM_Ghoul_Lunge montage asset stays in the project.

  Disable (default):  ENABLE = False   -> removes the attack named "Lunge" from DA_Archetype_Ghoul.Attacks
  Re-enable:          ENABLE = True    -> appends the Lunge again with the shipped numbers below (idempotent)

Run with the editor closed:  Scripts\\RunPy.ps1 Scripts/Python/ghoullunge.py   (report: Saved/ClaudeDump/ghoullunge.txt)
After re-enabling, MazeDemons.AI.Brain.GhoulAttackRootMotion still passes (it re-adds the Lunge to a transient copy when the DA lacks it)
and its "shipped ghoul has one attack" check must be updated to expect two.
"""
import os
import traceback
import unreal

ENABLE = False

DA_PATH = "/Game/Data/Enemies/DA_Archetype_Ghoul"
MONTAGE = "/Game/Animations/Enemies/Ghoul/AM_Ghoul_Lunge"
# name, range, tell_time, cooldown, damage_mult, poise_damage, knockback, warp_range, warp_stop, sockets (ghoulassets.py numbers)
LUNGE = ("Lunge", 450.0, 0.9, 1.5, 1.3, 25.0, 350.0, 520.0, 90.0, ["hand_r", "hand_l"])

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT = os.path.join(PROJECT, "Saved", "ClaudeDump", "ghoullunge.txt")
L = []


def P(s=""):
    L.append(str(s))


def describe(da):
    return [(str(a.get_editor_property("name")), a.get_editor_property("montage").get_name() if a.get_editor_property("montage") else None,
             a.get_editor_property("range")) for a in da.get_editor_property("attacks")]


try:
    da = unreal.load_asset(DA_PATH)
    P("before: %s" % describe(da))
    attacks = list(da.get_editor_property("attacks"))
    kept = [a for a in attacks if str(a.get_editor_property("name")) != "Lunge"]
    if ENABLE:
        montage = unreal.load_asset(MONTAGE)
        a = unreal.MDEnemyAttack()
        nm, rng, tell, cd, dmg, poise, kb, warp, stop, sockets = LUNGE
        a.set_editor_property("name", nm)
        a.set_editor_property("montage", montage)
        a.set_editor_property("range", rng)
        a.set_editor_property("tell_time", tell)
        a.set_editor_property("cooldown", cd)
        a.set_editor_property("trace_sockets", sockets)
        a.set_editor_property("claw_length", 35.0)
        a.set_editor_property("claw_back", 10.0)
        step = a.get_editor_property("step")
        step.set_editor_property("damage_mult", dmg)
        step.set_editor_property("poise_damage", poise)
        step.set_editor_property("reaction", unreal.MDHitReaction.FLINCH)
        step.set_editor_property("knockback_strength", kb)
        step.set_editor_property("hit_stop_frames", 4)
        step.set_editor_property("warp_range", warp)
        step.set_editor_property("warp_stop_distance", stop)
        step.set_editor_property("root_motion_scale", 1.0)
        a.set_editor_property("step", step)
        kept.append(a)
    da.set_editor_property("attacks", kept)
    da.modify()
    P("saved=%s" % unreal.EditorAssetLibrary.save_asset(DA_PATH))
    P("after (re-read): %s" % describe(unreal.load_asset(DA_PATH)))
    P("montage asset still there: %s" % (unreal.load_asset(MONTAGE) is not None))
except Exception:
    P(traceback.format_exc())

with open(OUT, "w") as f:
    f.write("\n".join(L))
