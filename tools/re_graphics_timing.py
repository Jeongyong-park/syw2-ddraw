"""Read-only x86 timing import/call-site report. Requires pefile and capstone."""
import argparse
import hashlib
from pathlib import Path
import capstone
import pefile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('exe', type=Path)
parser.add_argument('--start', type=lambda s:int(s,0))
parser.add_argument('--size', type=lambda s:int(s,0), default=256)
args = parser.parse_args()
raw = args.exe.read_bytes()
pe = pefile.PE(data=raw)
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
md.skipdata = True
imports = {i.address:i.name.decode(errors='replace') for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
def dump(insns):
    for i in insns:
        if not i.id: continue
        name = ''
        for op in i.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.disp in imports:
                name = ' ; '+imports[op.mem.disp]
        print(f'{i.address:08x}: {i.mnemonic:8} {i.op_str}{name}')
print('SHA256', hashlib.sha256(raw).hexdigest())
if args.start:
    dump(md.disasm(pe.get_data(args.start-base,args.size),args.start))
else:
    timing = {a:n for a,n in imports.items() if any(s.lower() in n.lower() for s in ['time','tick','sleep','performance','wait'])}
    print(timing)
    for s in pe.sections:
        if not s.Characteristics & 0x20000000: continue
        insns = list(md.disasm(s.get_data(),base+s.VirtualAddress))
        for k,i in enumerate(insns):
            if not i.id: continue
            if any(o.type==capstone.x86.X86_OP_MEM and o.mem.disp in timing for o in i.operands):
                print('\nTiming reference:')
                dump(insns[max(0,k-10):k+14])
