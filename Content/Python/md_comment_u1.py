"""
md_comment_u1.py (milestone U1)
Comment boxes for the U1 Blueprint changes. BOXES is filled in once the editor and MCP are back up.
Run: Tools > Execute Python Script > Content/Python/md_comment_u1.py, then read the Output Log for [MD_Comment] lines.
"""
import importlib
import sys
import os

import unreal

# Make sure the sibling module is importable however the script is launched.
_here = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else None
if _here and _here not in sys.path:
    sys.path.append(_here)

import md_comment_lib
importlib.reload(md_comment_lib)
from md_comment_lib import C_SETUP, C_INPUT, C_LOGIC, C_VIEW, ALL  # noqa: F401

# "/Game/Path/BP": [(graph, "why, one sentence", colour, ALL or ["title or member pattern", ...]), ...]
BOXES = {
    "/Game/Blueprints/BP_Player": [
        ("EventGraph", "Inventory key or gamepad button toggles the inventory through one input action, so rebinding needs no graph change",
         C_INPUT, ["IA_ToggleInventory", "AC_Inventory", "ToggleInventory", "GetInventory"]),
        ("EventGraph", "Mark the spawned weapon as equipped so nobody, on any machine, can pick it up while it is worn",
         C_LOGIC, ["MDPickupBase", "SetEquipped"]),
    ],
    "/Game/Blueprints/BP_MDPlayerController": [
        ("EventGraph", "After a respawn, reset the pawn's input state so sprint is not stuck on from the previous life",
         C_LOGIC, ["RespawnResetMovement", "CharacterInputState", "BP_Player", "UpdateInputState"]),
    ],
    "/Game/Blueprints/Items/BP_BaseItem": [
        ("EventGraph", "Pickup is validated on the server only; the item refuses if it is not interactable",
         C_INPUT, ["OnInteracted", "TryPickup", "HasAuthority"]),
        ("EventGraph", "Equipped items stop being interactable on every machine (this fires on server and clients)",
         C_LOGIC, ["OnEquippedChanged", "Not_PreBool", "Set Is Interactable"]),
    ],
    "/Game/Components/Combat/AC_Combat": [
        ("DoWeaponTrace", "Keep a list of actors already hit this swing (starting with the attacker) so each target is damaged only once per swing",
         C_LOGIC, ["AddUnique"]),
    ],
    "/Game/UI/HUD/W_MDHUD": [
        ("EventGraph", "Health bar follows the health value pushed by the HUD component", C_VIEW, ["OnHealthUpdated", "HealthBar"]),
        ("EventGraph", "Stamina bar fill and colour switch to the exhausted look while stamina is locked out",
         C_VIEW, ["OnStaminaUpdated", "StaminaBar", "SetFillToken"]),
        ("EventGraph", "Show the current gold amount", C_VIEW, ["OnGoldUpdated", "GoldText"]),
        ("EventGraph", "Show the death overlay only while the player is dead", C_VIEW, ["OnDeathStateChanged", "DeathOverlay"]),
        ("EventGraph", "Briefly toast what was picked up, then hide it again", C_VIEW, ["OnPickupToast", "ToastText", "RetriggerableDelay"]),
        ("EventGraph", "While dead, count down the seconds until respawn", C_VIEW, ["Tick", "RespawnText", "bDead", "RespawnSecondsRemaining"]),
    ],
}

md_comment_lib.main(BOXES, "U1")
