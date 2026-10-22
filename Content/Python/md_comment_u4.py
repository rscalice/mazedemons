"""
md_comment_u4.py (milestone U4: quests, minimap, ProHUD guidance)
Comment boxes for the U4 graphs. Run: Tools > Execute Python Script > Content/Python/md_comment_u4.py,
then read the Output Log for [MD_Comment] lines. Idempotent: re-running skips boxes that already exist.
Graphs that don't exist under the guessed name are logged as "graph missing" and skipped.
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

HUD = "/Game/UI/HUD/W_MDHUD"

BOXES = {
    HUD: [
        # Event graph: entry points from the C++ HUD base
        ("EventGraph", "Quest state changed: refresh the ProHUD mission tracker, guidance and minimap markers", C_LOGIC, ["OnQuestUpdated"]),
        ("EventGraph", "Entered a new map zone: feed its captured map to the ProHUD minimap and rebind fog updates", C_SETUP, ["OnMapZoneChanged"]),
        ("EventGraph", "Fog changed: only mark dirty; the poll tick re-bakes at most once a second", C_LOGIC, ["OnFogUpdated"]),

        # Minimap
        ("SetupMinimapForZone", "Bake zone capture + shared fog into a runtime Texture2D (ProHUD needs a real Texture2D) and calibrate the minimap to the zone bounds", C_SETUP, ALL),
        ("PlaceMinimapCorners", "Move ProHUD's corner actors to the zone bounds so world positions map onto the map image", C_SETUP, ALL),
        ("EnsureMinimapBrush", "ProHUD brush check: re-apply the map if the minimap brush was reset", C_SETUP, ALL),
        ("BeginMinimapRefresh", "Start a marker refresh: remember which targets exist so stale ones can be removed afterwards", C_LOGIC, ALL),
        ("RefreshMinimapMarkers", "Objective, gate, station and party markers, updated in place (never remove and re-add: ProHUD's delayed removal would delete the new marker)", C_LOGIC, ALL),
        ("AddMinimapMarker", "Add or update one ProHUD minimap marker and record it as still alive", C_LOGIC, ALL),
        ("AddHuntMinimapMarker", "Kill quests have no marker actor: mark the zone centre as the hunt target", C_LOGIC, ALL),
        ("FinishMinimapRefresh", "Remove only the markers whose targets went away this refresh", C_LOGIC, ALL),
        ("ProHUDPollTick", "Every 0.5 s: party positions, throttled fog re-bake, tracker visibility and minimap rotation mode", C_LOGIC, ALL),

        # Mission tracker and notification
        ("RefreshTracker", "Keep the ProHUD mission in step with the active quest: update the count or move to the next zone", C_VIEW, ALL),
        ("RebuildTracker", "Recreate the mission for the active zone (or 'All zones cleansed' when every quest is done)", C_VIEW, ALL),
        ("ActivateTrackerTask", "Show the objective line under the mission title", C_VIEW, ALL),
        ("GetTrackerTaskText", "Objective text with its count, without doubling the count", C_VIEW, ALL),
        ("ShowZoneClearedNotification", "ProHUD notification replaces the old quest toast: 'Zone N cleansed - +1 perk point'", C_VIEW, ALL),

        # Compass and world markers
        ("RefreshGuidance", "Compass + world markers for the active objective and the gate only; gate red while closed, green once open", C_VIEW, ALL),
        ("AddGuidanceMarker", "Add or update one compass and world marker pair", C_VIEW, ALL),
        ("GetGateLabelAnchor", "Hidden anchor above the gate so its label doesn't overlap the objective label", C_VIEW, ALL),
    ],
    "/Game/Blueprints/BP_MDQuestGate": [
        ("EventGraph", "Gate opened by the Zone 3 quest: hide the cube and stop blocking the tunnel", C_LOGIC, ALL),
    ],
    "/Game/UI/Inventory/W_MDStatLine": [
        ("SetRowIcon", "Optional row icon (Respec perks); collapsed when there is no texture so other stat lines keep their layout", C_VIEW, ALL),
    ],
}

md_comment_lib.main(BOXES, "U4")
