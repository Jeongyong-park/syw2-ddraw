"""Execute compiled production viewport math in Unicorn; no Windows API emulation."""
import argparse
from fractions import Fraction
from pathlib import Path
import random
import struct
import json
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP


def run(path, report=None):
    data = path.read_bytes()
    u16 = lambda p: struct.unpack_from("<H", data, p)[0]
    u32 = lambda p: struct.unpack_from("<I", data, p)[0]
    pe = u32(0x3c)
    if data[pe:pe+4] != b"PE\0\0" or u16(pe+4) != 0x14c or u16(pe+24) != 0x10b:
        raise ValueError("Expected x86 PE32 probe")
    opt = pe+24
    base, size = u32(opt+28), u32(opt+56)
    image = bytearray(size)
    image[:u32(opt+60)] = data[:u32(opt+60)]
    sections = opt+u16(pe+20)
    for i in range(u16(pe+6)):
        pos = sections+i*40
        va, raw_size, raw = u32(pos+12), u32(pos+16), u32(pos+20)
        image[va:va+raw_size] = data[raw:raw+raw_size]
    word = lambda p: struct.unpack_from("<I", image, p)[0]
    export = u32(opt+96)
    names, ordinals, functions = word(export+32), word(export+36), word(export+28)
    entry = None
    for i in range(word(export+24)):
        name = word(names+4*i)
        name = bytes(image[name:image.index(0, name)])
        if name in (b"ViewportProbe", b"_ViewportProbe"):
            ordinal = struct.unpack_from("<H", image, ordinals+2*i)[0]
            entry = base+word(functions+4*ordinal)
    if entry is None:
        raise ValueError("Probe export missing")
    cpu = Uc(UC_ARCH_X86, UC_MODE_32)
    cpu.mem_map(base, (size+4095)&~4095)
    cpu.mem_write(base, bytes(image))
    arena = 0x1000000
    cpu.mem_map(arena, 0x20000)
    inputs, outputs, stop, stack = arena, arena+0x1000, arena+0x2000, arena+0x1f000
    rng = random.Random(600)
    cases = [
        (1920,1080,800,600,0), (1920,1080,800,600,1),
        (640,480,800,600,1), (0,0,0,0,1),
        (3440,1440,800,600,1), (1200,900,800,600,0),
        (1,16384,8192,1,0), (16384,1,1,8192,1),
    ]
    cases += [(rng.randint(1,16384),rng.randint(1,16384),
               rng.randint(1,8192),rng.randint(1,8192),rng.randrange(2))
              for _ in range(1000)]
    def trunc(a,b):
        return (abs(a)//b)*(-1 if a<0 else 1)
    scenarios = [(case, [rng.randint(-8192,16384) for _ in range(4)], 'random') for case in cases]
    # Physical client dimensions representing 100/125/150/200% scaling.
    # Windows DPI API virtualization is deliberately outside this CPU-only test.
    for dpi in (96,120,144,192):
        for cw,ch in ((800*dpi//96,600*dpi//96),(3840,2160),(640,480)):
            for integer in (0,1):
                for step in list(range(-20,cw+21,37))+list(range(cw+20,-21,-37))+[cw//2]*8:
                    scenarios.append(((cw,ch,800,600,integer),(400,300,step,ch//2),f'move-stop-reverse-{dpi}-{integer}'))
                for cx,cy in ((0,0),(cw-1,0),(0,ch-1),(cw-1,ch-1),(-1,-1),(cw,ch)):
                    scenarios.append(((cw,ch,800,600,integer),(0,0,cx,cy),'boundary-click'))
    for case,coords,label in scenarios:
        cw,ch,gw,gh,integer = case
        cw,ch,gw,gh = [max(1,n) for n in (cw,ch,gw,gh)]
        scale = min(Fraction(cw,gw),Fraction(ch,gh))
        if integer and scale>=1:
            scale = Fraction(int(scale))
        w,h = max(1,int(gw*scale)),max(1,int(gh*scale))
        x,y = (cw-w)//2,(ch-h)//2
        lx,ly,cx,cy = coords
        expected = (x,y,w,h,x+trunc(lx*w,gw),y+trunc(ly*h,gh),
                    trunc((cx-x)*gw,w),trunc((cy-y)*gh,h))
        expected += (min(gw-1,max(0,expected[6])),min(gh-1,max(0,expected[7])))
        cpu.mem_write(inputs,struct.pack("<9i",*case,lx,ly,cx,cy))
        cpu.mem_write(outputs,b"\xcc"*40)
        cpu.mem_write(stack,struct.pack("<III",stop,inputs,outputs))
        cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.emu_start(entry,stop,timeout=1000000,count=100000)
        if cpu.reg_read(UC_X86_REG_EIP)!=stop:
            raise AssertionError("Probe did not return within instruction/time limit")
        actual = struct.unpack("<10i",cpu.mem_read(outputs,40))
        if actual!=expected:
            raise AssertionError(dict(seed=600,scenario=label,case=case,coords=coords,actual=actual,expected=expected))
    if report:
        report.parent.mkdir(parents=True,exist_ok=True)
        report.write_text(json.dumps(dict(seed=600,cases=len(scenarios),status='passed',
            scope='CPU coordinate logic; no FPS, DPI API, Windows or display latency measurement'),indent=2),encoding='utf-8')
    print(f"PASS: Unicorn x86 production viewport: {len(scenarios)} cases")
    print("Scope: CPU coordinate math only; not Windows/GDI/IME/GPU compatibility.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dll",type=Path,nargs="?",default=Path("build/Release/unicorn_probe.dll"))
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    try:
        run(args.dll,args.report)
    except Exception as error:
        if args.report:
            args.report.parent.mkdir(parents=True,exist_ok=True)
            detail=error.args[0] if error.args and isinstance(error.args[0],dict) else str(error)
            args.report.write_text(json.dumps(dict(seed=600,status='failed',
                error_type=type(error).__name__,detail=detail),indent=2),encoding='utf-8')
        raise
