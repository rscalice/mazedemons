"""
md_comment_u5.py (milestone U5 cleanup)
Fixes stale comment text and adds comment boxes for the U5 AC_Combat guards.
Run: Tools > Execute Python Script > Content/Python/md_comment_u5.py, then read the Output Log for [MD_Comment] lines.
Idempotent: boxes that already exist are skipped; replacements only touch boxes that still contain the old text.
"""
import importlib
import sys
import os

import unreal

_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else None
if _here and _here not in sys.path:
    sys.path.append(_here)

import md_comment_lib
importlib.reload(md_comment_lib)
from md_comment_lib import C_SETUP, C_INPUT, C_LOGIC, C_VIEW, ALL  # noqa: F401

COMBAT = "/Game/Components/Combat/AC_Combat"
HUD = "/Game/UI/HUD/W_MDHUD"

# (graph, text the old box contains, new text)
REPLACE = {
    "/Game/Components/Inventory/AC_Inventory": [
        ("ToggleInventoryWidget", "without freezing time",
         "Legacy inventory key: opens the game menu on the Inventory tab (never pauses)"),
    ],
    HUD: [
        ("EventGraph", "feed its captured map to the ProHUD minimap",
         "Entered a new zone: refresh markers, guidance and labels only (the level map does not change)"),
        ("EventGraph", "Fog changed: only mark dirty",
         "Fog changed: the minimap texture is updated in place from the shared level fog"),
    ],
}

BOXES = {
    COMBAT: [
        ("DoLightAttack", "Gate: attack only if input is enabled and CanPawnAttack (alive, no local menu); skip if the weapon has no montages", C_INPUT, ["CanPawnAttack", "Branch"]),
        ("CheckInputQueue", "Do not buffer an attack while dead or in a menu (CanPawnAttack)", C_INPUT, ALL),
        ("DoWeaponTrace", "Server only: trace and damage while the owner can attack; skip dead targets but keep them in the hit list so they are not re-checked", C_LOGIC, ALL),
        ("AdvancedPlayMontage", "Only play a montage when the owning character is valid; server RPC + multicast so a client's swing plays and hits", C_VIEW, ALL),
    ],
}

md_comment_lib.main(BOXES, "U5", REPLACE=REPLACE)
