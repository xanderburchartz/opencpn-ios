# Attach to a running process, count calls of given functions for N seconds, detach.
# Usage: tel_aanroepen.py <pid> <seconds> <function> [<function> ...]
import sys, time, lldb
pid, secs, funcs = int(sys.argv[1]), float(sys.argv[2]), sys.argv[3:]
dbg = lldb.SBDebugger.Create(); dbg.SetAsync(True)
target = dbg.CreateTarget("")
err = lldb.SBError(); proc = target.AttachToProcessWithID(dbg.GetListener(), pid, err)
if not err.Success(): print("attach mislukt:", err); sys.exit(1)
listener = dbg.GetListener(); ev = lldb.SBEvent()
def wait_state(states, t=20):
    end = time.time() + t
    while time.time() < end:
        if listener.WaitForEvent(1, ev) and lldb.SBProcess.EventIsProcessEvent(ev):
            if lldb.SBProcess.GetStateFromEvent(ev) in states: return True
        if proc.GetState() in states: return True
    return False
wait_state([lldb.eStateStopped])
bps = {f: target.BreakpointCreateByName(f) for f in funcs}
for b in bps.values(): b.SetAutoContinue(True)
proc.Continue(); print("teltijd", secs, "s", flush=True)
end = time.time() + secs
while time.time() < end: listener.WaitForEvent(1, ev)
proc.Stop(); wait_state([lldb.eStateStopped])
for f, b in bps.items(): print(f"{f}: {b.GetHitCount()} aanroepen")
for b in bps.values(): target.BreakpointDelete(b.GetID())
proc.Detach(); print("losgekoppeld")
