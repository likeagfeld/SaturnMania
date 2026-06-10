# Ghidra headless post-script: dump disassembly around the slInitSystem fault.
# Faulting PC (measured from exception stack) ~= 0x060116cc.
# Dumps a window of instructions + the containing function + referenced symbols.
from ghidra.program.model.address import Address

fault = 0x060116cc
win_lo = fault - 0x80
win_hi = fault + 0x40

fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
af = currentProgram.getAddressFactory().getDefaultAddressSpace()


def A(x):
    return af.getAddress(x)


print("=== CONTAINING FUNCTION ===")
fn = fm.getFunctionContaining(A(fault))
if fn is not None:
    print("func: %s  entry=0x%08x" % (fn.getName(), fn.getEntryPoint().getOffset()))
else:
    print("(no function defined at 0x%08x)" % fault)

print("=== DISASM 0x%08x .. 0x%08x ===" % (win_lo, win_hi))
addr = A(win_lo)
end = A(win_hi)
while addr.compareTo(end) <= 0:
    inst = listing.getInstructionAt(addr)
    if inst is None:
        cu = listing.getCodeUnitAt(addr)
        b = ""
        try:
            b = "%02x" % (currentProgram.getMemory().getByte(addr) & 0xff)
        except:
            b = "??"
        print("0x%08x:   .byte 0x%s" % (addr.getOffset(), b))
        addr = addr.add(1)
        continue
    mark = " <==FAULT" if inst.getAddress().getOffset() == fault else ""
    # show referenced addresses (memory operands)
    refs = ""
    for r in inst.getReferencesFrom():
        refs += " ->0x%08x" % r.getToAddress().getOffset()
    print("0x%08x:   %-28s%s%s" % (inst.getAddress().getOffset(), inst.toString(), refs, mark))
    addr = addr.add(inst.getLength())

print("=== SYMBOLS NEAR FAULT ===")
st = currentProgram.getSymbolTable()
for off in [0x06011600, 0x06011680, 0x060116c0, 0x06011700]:
    syms = st.getSymbols(A(off))
    for s in syms:
        print("0x%08x: %s" % (off, s.getName()))
# also: nearest function entry below fault
print("=== nearest function entries around fault ===")
it = fm.getFunctions(True)
prev = None
for f in it:
    e = f.getEntryPoint().getOffset()
    if e <= fault:
        prev = f
    else:
        print("next-> %s @0x%08x" % (f.getName(), e))
        break
if prev is not None:
    print("prev<- %s @0x%08x" % (prev.getName(), prev.getEntryPoint().getOffset()))
