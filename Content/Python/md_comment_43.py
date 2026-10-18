"""
md_comment_43.py (issue #43, inventory polish)
Comment boxes for the inventory stat panel, rarity frames and quick slot changes.
Run: Tools > Execute Python Script > Content/Python/md_comment_43.py, then read the Output Log for [MD_Comment] lines.
Safe to run twice: a box whose text already exists in the graph is skipped.
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
    "/Game/Blueprints/UI/Inventory/W_Inventory": [
        ("ApplyItemDetailsCommon", "Fill the item details panel with the icon, quantity, slot, rarity and armour text, then colour the name by rarity",
         C_VIEW, ALL),
        ("ColorDetailsName", "Tint the details name with the colour of the item's rarity", C_VIEW, ALL),
        ("RefreshStats", "Rebuild the stat lines from the player's current stats", C_VIEW, ALL),
        ("BindStatEvents", "Listen for stat changes so the stat lines update while the inventory is open", C_SETUP, ALL),
        ("UnbindStatEvents", "Stop listening for stat changes when the inventory closes", C_SETUP, ALL),
        ("SetCompareRow", "Remember which equipped row the hovered item is compared against", C_LOGIC, ALL),
        ("EventGraph", "On open: show the stats and start listening for stat changes",
         C_SETUP, ["BindStatEvents", "RefreshStats"]),
        ("EventGraph", "On close: stop listening for stat changes", C_SETUP, ["UnbindStatEvents"]),
    ],
    "/Game/UI/Inventory/W_MDStatLine": [
        ("SetStatLine", "Show one stat line with its name, value and how it compares to the hovered item", C_VIEW, ALL),
    ],
    "/Game/Blueprints/UI/Inventory/W_InventorySlot": [
        ("ApplyRarityFrame", "Colour the slot frame by the rarity of the item in it", C_VIEW, ALL),
        ("EventGraph", "Hovering a slot shows its item details and sets the compare row", C_VIEW,
         ["OnHovered", "ShowItemDetails"]),
        ("EventGraph", "Leaving a slot clears the hover details and the compare row", C_VIEW,
         ["OnUnhovered", "ClearHoverDetails"]),
    ],
    "/Game/Blueprints/UI/Inventory/W_EquipmentSlot": [
        ("ApplyRarityFrame", "Colour the slot frame by the rarity of the equipped item", C_VIEW, ALL),
        ("NotifyHovered", "Hovering an equipment slot shows its item details", C_VIEW, ALL),
        ("NotifyUnhovered", "Leaving an equipment slot clears the hover details", C_VIEW, ALL),
        ("NotifyFocused", "Gamepad focus on an equipment slot highlights it and shows its details", C_VIEW, ALL),
        ("NotifyBlurred", "Losing gamepad focus removes the highlight and the details", C_VIEW, ALL),
    ],
    "/Game/UI/HUD/W_MDQuickSlot": [
        ("Refresh", "Keep the inventory in a local variable so both rarity frame calls further along can read it without a long wire",
         C_SETUP, ["SetInventoryRef"]),
    ],
}

md_comment_lib.main(BOXES, "43")
