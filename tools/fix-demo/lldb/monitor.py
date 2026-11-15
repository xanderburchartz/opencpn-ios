# Attach to an app started with --wait-for-debugger, read what wxDisplay::GetFromWindow
# returns in MyFrame's constructor and what g_current_monitor becomes, then watch for a crash.
# Usage: monitor.py <pid> <seconds-after>
import sys, time, lldb
pid, after = int(sys.argv[1]), float(sys.argv[2])
dbg = lldb.SBDebugger.Create(); dbg.SetAsync(False)
target = dbg.CreateTarget(""); err = lldb.SBError()
proc = target.AttachToProcessWithID(dbg.GetListener(), pid, err)
if not err.Success(): print("attach mislukt:", err); sys.exit(1)
def gcm():
    for sym in target.FindSymbols("g_current_monitor"):
        addr = sym.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if addr != lldb.LLDB_INVALID_ADDRESS:
            e = lldb.SBError(); v = proc.ReadUnsignedFromMemory(addr, 8, e)
            if e.Success(): return v
    return None
bp = target.BreakpointCreateByName("wxDisplay::GetFromWindow")
proc.Continue()
for _ in range(50):
    if proc.GetState() != lldb.eStateStopped: break
    th = proc.GetSelectedThread()
    caller = th.GetFrameAtIndex(1).GetFunctionName() or ""
    if "MyFrame::MyFrame" in caller:
        print("g_current_monitor voor de aanroep:", gcm())
        th.StepOut()
        ret = th.GetFrameAtIndex(0).FindRegister("w0").GetValueAsSigned()
        print("GetFromWindow in", caller.split("(")[0], "geeft", ret)
        # run to the end of the constructor's next GetFromWindow-free stretch: step a few lines
        for i in range(12): th.StepInstruction(False)
        print("g_current_monitor na de toewijzing:", gcm(), "(SIZE_MAX = %d)" % (2**64-1))
        break
    proc.Continue()
target.BreakpointDelete(bp.GetID())
dbg.SetAsync(True); proc.Continue()
end = time.time() + after
while time.time() < end and proc.GetState() not in (lldb.eStateExited, lldb.eStateCrashed): time.sleep(0.5)
st = proc.GetState()
if st == lldb.eStateStopped or st == lldb.eStateCrashed:
    th = proc.GetSelectedThread()
    print("app gestopt:", th.GetStopDescription(200))
    for i in range(min(8, th.GetNumFrames())): print("  ", th.GetFrameAtIndex(i).GetFunctionName())
elif st == lldb.eStateExited:
    print("app beëindigd, code", proc.GetExitStatus())
else:
    print("app draait na", after, "s; g_current_monitor =", gcm())
    proc.Detach()
