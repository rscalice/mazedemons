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
BOXES = {}

md_comment_lib.main(BOXES, "U1")
