"""Check the generated full-height cache gate, bounds and original copy routine."""
import argparse
import json
from pathlib import Path
import struct


def verify(exe):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x
    from .widescreen_cache_probe import NEW_CACHE, NEW_DIRTY, CAMERA, ENGINE
    pe=pefile.PE(str(exe))
    m=uc.Uc(uc.UC_ARCH_X86,uc.UC_MODE_32)
    m.mem_map(0x400000,pe.OPTIONAL_HEADER.SizeOfImage)
    m.mem_write(0x400000,pe.get_memory_mapped_image())
    stack=0x3000000; sp=stack+0x8000
    m.mem_map(stack,0x10000)
    put=lambda a,*v:m.mem_write(a,struct.pack('<'+'I'*len(v),*v))
    size=1088*608
    for dirty in (0,1):
        m.mem_write(NEW_CACHE-16,b'G'*16+b'X'*size+b'G'*16)
        m.mem_write(NEW_DIRTY-16,b'G'*16+b'X'*(17*32)+b'G'*16)
        m.mem_write(CAMERA+0x597c,bytes([0,dirty]))
        m.reg_write(x.UC_X86_REG_ESI,CAMERA)
        m.reg_write(x.UC_X86_REG_ESP,sp)
        stop=0x433019 if dirty else 0x4332b5
        m.emu_start(0x432911,stop,count=1500000)
        assert m.reg_read(x.UC_X86_REG_EIP)==stop
        assert bytes(m.mem_read(NEW_CACHE,size))==(b'\0' if dirty else b'X')*size
        assert bytes(m.mem_read(NEW_DIRTY,17*32))==(b'\1' if dirty else b'X')*(17*32)
        for a in (NEW_CACHE-16,NEW_CACHE+size,NEW_DIRTY-16,NEW_DIRTY+17*32):
            assert bytes(m.mem_read(a,16))==b'G'*16
        assert struct.unpack('<I',m.mem_read(ENGINE+0x14,4))[0]==608
        assert struct.unpack('<I',m.mem_read(ENGINE+0x4dc,4))[0]==607
    for address in (0x435358,0x4357f1,0x435db0,0x436060,0x436308):
        m.reg_write(x.UC_X86_REG_EDX,16)
        m.reg_write(x.UC_X86_REG_EAX,18)
        m.emu_start(address,address+10,count=2)
        assert m.reg_read(x.UC_X86_REG_EDX)==NEW_DIRTY+16*32+18
    source=bytes((i+i//1088)%251+1 for i in range(size))
    m.mem_write(NEW_CACHE,source)
    dest=0x3101000; visible=1088*600
    m.mem_map(dest-4096,0xa2000)
    m.mem_write(dest-16,b'G'*16+b'X'*visible+b'G'*16)
    put(ENGINE+0xc,8,1088)
    put(ENGINE+0x1514,dest)
    put(sp,stack)
    m.reg_write(x.UC_X86_REG_ESP,sp)
    def surface_calls(machine,address,length,data):
        if address not in (0x438840,0x464ec0,0x465080): return
        esp=machine.reg_read(x.UC_X86_REG_ESP)
        ret=struct.unpack('<I',machine.mem_read(esp,4))[0]
        machine.reg_write(x.UC_X86_REG_EAX,1)
        machine.reg_write(x.UC_X86_REG_ESP,esp+(4 if address==0x438840 else 8))
        machine.reg_write(x.UC_X86_REG_EIP,ret)
    m.hook_add(uc.UC_HOOK_CODE,surface_calls)
    m.emu_start(0x41bf20,stack,count=2000000)
    assert m.reg_read(x.UC_X86_REG_EIP)==stack
    assert bytes(m.mem_read(dest,visible))==source[:visible]
    assert bytes(m.mem_read(dest-16,16))==bytes(m.mem_read(dest+visible,16))==b'G'*16
    return {'status':'emulated_full_height_bounds_passed','cache':[1088,608],
            'copied_rows':600,'dirty_lookup_sites':5,'idle_gate_preserves_cache':True,
            'limitations':['Terrain assets not rasterized','Live rendering and frame cost not measured']}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe',type=Path)
    args=ap.parse_args()
    result=verify(args.exe)
    args.exe.with_suffix('.terrain-verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
