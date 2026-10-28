import unreal
# V1 WP2: rebuild /Game/AI/ST_Enemy with the Retreat and Regroup states (UMD_AILibrary.build_enemy_state_tree is idempotent).
# Commandlet, editor closed. Report: Saved/ClaudeDump/wp2st.txt
out = r"C:/Users/Robert Scalice/Documents/Unreal Projects/mazedemons/Saved/ClaudeDump/wp2st.txt"
L = []
def P(s): L.append(str(s)); print(s)

ok, log = unreal.MD_AILibrary.build_enemy_state_tree("/Game/AI/ST_Enemy")
P("build ok = %s" % ok)
for l in log: P("  " + str(l))
for l in unreal.MD_AILibrary.describe_state_tree("/Game/AI/ST_Enemy"): P("  " + str(l))
open(out, "w").write("\n".join(L))
