"""
md_comment_wp5.py (milestone U1 / WP5 inventory, context menu, pause)
Comment boxes for the WP5 graphs. Run: Tools > Execute Python Script > Content/Python/md_comment_wp5.py,
then read the Output Log for [MD_Comment] lines. Idempotent.
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

INV = "/Game/Blueprints/UI/Inventory/"
MENUS = "/Game/UI/Menus/"

BOXES = {
    INV + "W_Inventory": [
        ("EventGraph", "On construct, take the owning player's inventory and build the grid and gold text", C_SETUP, ALL),
        ("OnPreviewKeyDown", "Tab, Esc, gamepad Special Left or B closes the inventory and returns control to the game", C_INPUT, ALL),
        ("OnFocusReceived", "Put focus on the first inventory slot", C_INPUT, ALL),
    ],
    INV + "W_InventorySlot": [
        ("OpenContextMenu", "Open the item context menu for this slot, next to the cursor", C_VIEW, ALL),
        ("OnKeyDown", "Gamepad Y (face button top) opens the context menu", C_INPUT, ALL),
        ("OnMouseButtonDown", "Right click opens the context menu", C_INPUT, ALL),
    ],
    MENUS + "W_MDContextMenu": [
        ("SetupMenu", "Show only the buttons that apply to this item and focus the first one", C_VIEW, ALL),
        ("OnPreviewKeyDown", "Esc or gamepad B closes the menu", C_INPUT, ALL),
    ],
    MENUS + "W_Pause": [
        ("ResumeGame", "Resume closes the pause menu through the player controller (the game is never paused)", C_LOGIC, ALL),
        ("OnPreviewKeyDown", "Esc closes the pause menu", C_INPUT, ALL),
        ("OnFocusReceived", "Focus Resume when the menu opens", C_INPUT, ALL),
    ],
    "/Game/Components/Inventory/AC_Inventory": [
        ("ToggleInventoryWidget", "Legacy inventory key: opens the game menu on the Inventory tab (never pauses)", C_LOGIC, ALL),
    ],
}

md_comment_lib.main(BOXES, "WP5")
