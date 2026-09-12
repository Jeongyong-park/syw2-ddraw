"""Execute the original minimap alert timing with only final draw calls stubbed."""
import argparse
import json
from pathlib import Path
import struct


def verify(exe,wide=True):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x
    pe=pefile.PE(str(exe))
    cases=[]
    for active,frame,elapsed in ((0,0,0),(1,0,0),(1,0,3),(1,8,3),(2,8,3)):
        m=uc.Uc(uc.UC_ARCH_X86,uc.UC_MODE_32)
        m.mem_map(0x400000,pe.OPTIONAL_HEADER.SizeOfImage)
        m.mem_write(0x400000,pe.get_memory_mapped_image())
        stack,sp=0x3000000,0x3008000
        m.mem_map(stack,0x10000)
        put=lambda a,*v:m.mem_write(a,struct.pack('<'+'I'*len(v),*v))
        obj,engine=0x639350,0xe5bf18
        put(obj,17,530)
        m.mem_write(obj+0x2baac,struct.pack('<5hBBI',20,10,0,47,550,frame,active,100))
        m.mem_write(obj+0x1bd7a+(64+65*180)*2,bytes([40,30]))
        put(engine+4,1068 if wide else 800,600)
        clip=struct.pack('<4I',7,8,790,580)
        m.mem_write(engine+0x4d0,clip)
        put(0x8924b8,100+elapsed)
        put(sp,stack,64,65)
        m.reg_write(x.UC_X86_REG_ECX,obj)
        m.reg_write(x.UC_X86_REG_ESP,sp)
        draws=[]
        def draw(machine,address,size,data):
            if address not in (0x465b30,0x419de0): return
            esp=machine.reg_read(x.UC_X86_REG_ESP)
            count=5 if address==0x465b30 else 4
            values=struct.unpack('<'+'I'*(count+1),machine.mem_read(esp,4*(count+1)))
            draws.append((address,values[1:]))
            machine.reg_write(x.UC_X86_REG_ESP,esp+(24 if address==0x465b30 else 4))
            machine.reg_write(x.UC_X86_REG_EIP,values[0])
        m.hook_add(uc.UC_HOOK_CODE,draw)
        offset=struct.unpack('<i',pe.get_data(0x41c2d1-0x400000,4))[0]
        m.emu_start(0x41c2d5+offset,stack,count=5000)
        assert m.reg_read(x.UC_X86_REG_EIP)==stack
        assert m.reg_read(x.UC_X86_REG_ESP)==sp+12
        shift=134 if wide else 0
        assert draws[0]==(0x465b30,(47+shift,555,67+shift,565,255))
        if active:
            expected_sprite=struct.unpack('<h',pe.get_data(0x4f0af8+frame*2-0x400000,2))[0]&0xffffffff
            assert draws[1]==(0x419de0,(47+shift,550,0x49,expected_sprite))
        assert len(draws)==1+bool(active)
        assert bytes(m.mem_read(obj,8))==struct.pack('<2I',17,530)
        assert bytes(m.mem_read(obj+0x2bab2,4))==struct.pack('<2h',47,550)
        assert bytes(m.mem_read(engine+0x4d0,16))==clip
        expected_frame=frame+bool(active and elapsed>2)
        expected_active=active
        if active and expected_frame==9: expected_frame=0;expected_active-=1
        assert bytes(m.mem_read(obj+0x2bab6,2))==bytes([expected_frame,expected_active])
        cases.append({'active':active,'frame':frame,'elapsed':elapsed,'sprite_x':47+shift if active else None})
    return {'status':'original_alert_path_passed','wide':wide,'cases':cases,
            'stored_coordinates_and_clip_preserved':True,'animation_timing_preserved':True,
            'limitation':'Final sprite rasterization stubbed; live attack event must be checked'}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe',type=Path)
    ap.add_argument('--original',action='store_true')
    args=ap.parse_args()
    result=verify(args.exe,not args.original)
    args.exe.with_suffix('.alert-verification.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
