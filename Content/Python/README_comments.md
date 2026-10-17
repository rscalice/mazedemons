# Blueprint comments from scripts

MCP cannot set `NodeComment` or create comment boxes, so agents use `UMD_BlueprintCommentLibrary`
(editor-only module `MazeDemonsEditor`) from a Python script that Robert runs once.

## Run
1. Build once with the editor closed (the module is part of the Editor target).
2. In the editor: Tools > Execute Python Script > `Content/Python/md_comment_u1.py` (or the script for the phase).
3. Check the Output Log for `[MD_Comment]` lines. Re-running is safe: a box whose text already exists in the graph is not added again.

`Content/Python` is on the editor's Python path automatically; the phase scripts also append their own folder so `import md_comment_lib` works.

## API (`unreal.MD_BlueprintCommentLibrary`, all static)
| Call | Purpose |
|---|---|
| `list_graph_nodes(bp, "Graph")` | `"Name \| Title \| X,Y"` per node. Pass `"None"`/empty name for all graphs (lines prefixed `[Graph]`). |
| `list_graph_nodes_ex(bp, "Graph")` | Same plus a 4th field: member name (called function, variable, event). |
| `get_graph_names(bp)` | Every graph the Blueprint owns. |
| `set_node_comment(bp, "Graph", "K2Node_CallFunction_3", "why", True)` | Pinned bubble on one node. |
| `add_comment_box(bp, "Graph", ["NodeA", "NodeB"], "Section title", unreal.LinearColor(r,g,b,a), 40)` | Box around the nodes; returns the comment node name. |
| `compile_blueprint(bp)` / `save_blueprint(bp)` | Compile (widget BPs too) and save without prompts. |

Graph search covers event graphs, functions, macros and delegate signatures, so widget Blueprints work the same way.

## Pattern for a new phase
1. Agent builds graphs with MCP as usual (logic in functions, left-to-right).
2. Copy `md_comment_u1.py`, fill the `BOXES` dict: per Blueprint path, a list of `(graph, text, colour, patterns)`. Patterns match node titles or member names (case-insensitive substring); use `ALL` for every node in the graph. No K2Node names needed.
3. Comments say *why*, not what; keep them to a sentence.
4. Robert runs it; the agent commits the resulting `.uasset` changes.

Colours: blue = setup, amber = input, green = logic, purple = visuals (`C_SETUP`, `C_INPUT`, `C_LOGIC`, `C_VIEW` in `md_comment_lib.py`).

Shared logic lives in `md_comment_lib.py` (`process`, `main`); phase scripts only hold their `BOXES`.
