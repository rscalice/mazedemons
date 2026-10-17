"""
md_comment_wp4.py (milestone U1 / WP4 HUD)
Comment boxes for the rewritten HUD widget graphs in /Game/UI/HUD. Replaces the stale W_MDHUD entries in md_comment_u1.py
(that script still refers to the removed ToastText; do not run its W_MDHUD block again).
Run: Tools > Execute Python Script > Content/Python/md_comment_wp4.py, then read the Output Log for [MD_Comment] lines.
Idempotent: a box whose text already exists in the graph is not added again.
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

HUD = "/Game/UI/HUD/"

BOXES = {
    HUD + "W_MDHUD": [
        ("EventGraph", "Health bar follows the health value pushed from C++, and the vignette reacts to the change",
         C_VIEW, ["OnHealthUpdated", "HealthBar", "ApplyHealthChange"]),
        ("EventGraph", "Stamina bar fill and colour switch to the exhausted look while stamina is locked out",
         C_VIEW, ["OnStaminaUpdated", "StaminaBar", "SetFillToken"]),
        ("EventGraph", "Show the current gold amount", C_VIEW, ["OnGoldUpdated", "GoldText"]),
        ("EventGraph", "Show the death overlay only while the player is dead", C_VIEW, ["OnDeathStateChanged", "DeathOverlay"]),
        ("EventGraph", "Hand each pickup notification to the toast stack", C_VIEW, ["OnPickupToast", "PushToast"]),
        ("EventGraph", "Once the HUD is bound to the player, let the quick slot bar find the owner's inventory",
         C_SETUP, ["OnBound", "BindtoOwnerInventory"]),
        ("EventGraph", "While dead, count down the seconds until respawn", C_VIEW, ["Tick", "RespawnText", "bDead", "RespawnSecondsRemaining"]),
    ],
    HUD + "W_MDInteractPrompt": [
        ("EventGraph", "On construct, bind this prompt to the owning player controller", C_SETUP, ALL),
        ("BindToController", "Listen for focus and input-device changes, and show the right glyph straight away", C_SETUP, ALL),
        ("HandleFocusChanged", "Show the prompt row and its text only while an interactable is in focus", C_VIEW, ALL),
        ("HandleInputDeviceChanged", "Glyph is F on keyboard and X on gamepad", C_VIEW, ALL),
    ],
    HUD + "W_MDQuickSlotBar": [
        ("BindToOwnerInventory", "Subscribe to the owner's inventory so the slots refresh when they change", C_SETUP, ALL),
        ("FindOwnerInventory", "Find the inventory on the pawn, controller or player state", C_LOGIC, ALL),
        ("HandleQuickSlotsChanged", "Refresh the slots whenever the inventory reports a quick slot change", C_LOGIC, ALL),
        ("RefreshSlots", "Push each quick slot's item into its slot widget", C_VIEW, ALL),
    ],
    HUD + "W_MDQuickSlot": [
        ("Refresh", "Show the slot's item icon and quantity, or an empty slot", C_VIEW, ALL),
        ("ResolveItemIcon", "Look up the item icon from the item row name", C_LOGIC, ALL),
    ],
    HUD + "W_MDToastStack": [
        ("PushToast", "Stack a pickup toast, or add to the quantity if the same item already has one", C_LOGIC, ALL),
        ("FindToast", "Find the live toast for an item, if any", C_LOGIC, ALL),
    ],
    HUD + "W_MDToast": [
        ("ShowItem", "Start a toast for an item", C_VIEW, ALL),
        ("AddQuantity", "Add to an existing toast and restart its timer", C_LOGIC, ALL),
        ("Advance", "Animate the toast and hide it when its time is up", C_VIEW, ALL),
        ("RefreshView", "Write the item name, icon and quantity into the toast", C_VIEW, ALL),
        ("ResolveItemIcon", "Look up the item icon from the item row name", C_LOGIC, ALL),
    ],
    HUD + "W_MDVignette": [
        ("InitMaterial", "Create the dynamic vignette material once", C_SETUP, ALL),
        ("ApplyHealthChange", "Flash on damage and set the low health look", C_LOGIC, ALL),
        ("StartFlash", "Begin a damage flash", C_VIEW, ALL),
        ("Advance", "Fade the flash out over time", C_VIEW, ALL),
        ("ApplyLook", "Write the current look values into the material", C_VIEW, ALL),
    ],
}

md_comment_lib.main(BOXES, "WP4")
