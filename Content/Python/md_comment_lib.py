"""
md_comment_lib.py - shared logic for MazeDemons Blueprint comment scripts.
Per-phase scripts (md_comment_u1.py, ...) only hold a BOXES dict and call main(BOXES).

BOXES = {
    "/Game/Path/To/Blueprint": [
        (graph_name, comment_text, colour, patterns),   # patterns: ALL or a list of node-title / member substrings
    ],
}

REPLACE = {
    "/Game/Path/To/Blueprint": [
        (graph_name, old_contains, new_text),   # rewrites an existing comment box whose text contains old_contains
    ],
}
Replacements run before the boxes for that Blueprint, so a replaced box is not duplicated by BOXES. Pass them with
main(BOXES, REPLACE=REPLACE). A Blueprint may appear only in REPLACE.

Defensive and idempotent: a box whose text already exists in the graph is not added again.
Run via Tools > Execute Python Script, then read the Output Log for lines tagged [MD_Comment].
"""
import unreal

TAG = "[MD_Comment]"
LIB = unreal.MD_BlueprintCommentLibrary

# blue = setup, amber = input, green = logic, purple = visuals
C_SETUP = unreal.LinearColor(0.10, 0.25, 0.50, 0.45)
C_INPUT = unreal.LinearColor(0.50, 0.35, 0.05, 0.45)
C_LOGIC = unreal.LinearColor(0.12, 0.35, 0.18, 0.45)
C_VIEW = unreal.LinearColor(0.35, 0.12, 0.40, 0.45)
ALL = ["*"]


def norm(s):
    return "".join(str(s).lower().split())


def parse(lines):
    """'Name | Title | X,Y | Member' -> [(name, title, member)]"""
    out = []
    for line in lines:
        parts = [p.strip() for p in str(line).split("|")]
        if len(parts) >= 3:
            out.append((parts[0], parts[1], parts[3] if len(parts) > 3 else ""))
    return out


def apply_replacements(bp, short, replacements):
    """Applies (graph, old_contains, new_text) tuples; returns how many boxes were rewritten."""
    done = 0
    for graph, old_contains, new_text in replacements:
        try:
            n = LIB.set_comment_box_text(bp, graph, old_contains, new_text)
        except Exception as e:
            unreal.log_warning("%s %s/%s: set_comment_box_text failed (%s)" % (TAG, short, graph, e))
            continue
        if n > 0:
            done += n
            unreal.log("%s %s/%s: REPLACED %d box(es) containing '%s'" % (TAG, short, graph, n, old_contains[:40]))
        else:
            unreal.log_warning("%s %s/%s: no comment box contains '%s' (already replaced?)" % (TAG, short, graph, old_contains[:40]))
    return done


def process(path, boxes, replacements=()):
    added = 0
    bp = unreal.load_asset(path)
    if not bp:
        unreal.log_warning("%s MISSING asset %s (skipped %d boxes, %d replacements)" % (TAG, path, len(boxes), len(replacements)))
        return 0
    short = path.split("/")[-1]
    added += apply_replacements(bp, short, replacements)
    for graph, text, colour, patterns in boxes:
        try:
            nodes = parse(LIB.list_graph_nodes_ex(bp, graph))
        except Exception as e:
            unreal.log_warning("%s %s/%s: cannot list nodes (%s)" % (TAG, short, graph, e))
            continue
        if not nodes:
            unreal.log_warning("%s %s/%s: graph missing or empty, skipped '%s'" % (TAG, short, graph, text[:40]))
            continue
        if any(n.startswith("EdGraphNode_Comment") and norm(text) in norm(t) for n, t, _ in nodes):
            unreal.log("%s %s/%s: already exists '%s'" % (TAG, short, graph, text[:40]))
            continue
        real = [(n, t, m) for n, t, m in nodes if not n.startswith("EdGraphNode_Comment")]
        if patterns == ALL:
            hits = [n for n, t, m in real]
        else:
            pats = [norm(p) for p in patterns]
            hits = [n for n, t, m in real if any(p in norm(t) or (m and p in norm(m)) for p in pats)]
        if not hits:
            unreal.log_warning("%s %s/%s: no nodes matched for '%s'" % (TAG, short, graph, text[:40]))
            continue
        try:
            ok = LIB.add_comment_box(bp, graph, hits, text, colour, 40)
        except Exception as e:
            unreal.log_warning("%s %s/%s: add_comment_box failed (%s)" % (TAG, short, graph, e))
            continue
        if ok:
            added += 1
            unreal.log("%s %s/%s: ADDED box over %d nodes: %s" % (TAG, short, graph, len(hits), text[:60]))
        else:
            unreal.log_warning("%s %s/%s: add_comment_box returned empty for '%s'" % (TAG, short, graph, text[:40]))
    try:
        LIB.compile_blueprint(bp)
        LIB.save_blueprint(bp)
    except Exception as e:
        unreal.log_warning("%s %s: compile or save failed (%s)" % (TAG, short, e))
    return added


def main(boxes_by_path, label="run", REPLACE=None):
    total = 0
    replace_by_path = REPLACE or {}
    paths = list(boxes_by_path.keys()) + [p for p in replace_by_path if p not in boxes_by_path]
    for path in paths:
        try:
            total += process(path, boxes_by_path.get(path, []), replace_by_path.get(path, []))
        except Exception as e:
            unreal.log_warning("%s %s: unexpected error (%s)" % (TAG, path, e))
    unreal.log("%s %s DONE: %d comment boxes added or replaced" % (TAG, label, total))
    return total
