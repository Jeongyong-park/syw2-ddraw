"""Differential scope check of original vision commands and widened fog rendering.

Not a lockstep/network test: executes the original fog preprocessing and vision
writers, with synthetic map data. No combat, AI, network or simulation tick runs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from .widescreen_probe import validate_image, BASE, CAMERA

VISION=0x89d58e
TILES=180*180
VISION_SIZE=8*TILES
FOG=0x8dca0e
DIRTY=0x8ec72e
HISTORY=0xb94010
ENTRY,END=0x4324e0,0x432911
STACK=0x3000000
SP=STACK+0x8000
MASK=STACK+0x1000


def verify(original,prototype):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x
    digest=validate_image(original.read_bytes())
    images=[pefile.PE(str(original)),pefile.PE(str(prototype))]
    initial=bytes((player+(tx//7)+(ty//11))%3 for player in range(8)
                  for tx in range(180) for ty in range(180))
    machines=[]
    # Only these derived render buffers may be written by the prepass.
    allowed=((FOG,TILES*2,'fog_interpolation'),(DIRTY,TILES,'render_dirty'),
             (HISTORY,TILES*2,'fog_history'),(CAMERA+0x597c,1,'redraw_flag'),
             (STACK,0x10000,'stack'))
    for pe in images:
        m=uc.Uc(uc.UC_ARCH_X86,uc.UC_MODE_32)
        m.mem_map(BASE,pe.OPTIONAL_HEADER.SizeOfImage)
        m.mem_write(BASE,pe.get_memory_mapped_image())
        m.mem_map(STACK,0x10000)
        m.mem_write(VISION,initial)
        m.mem_write(CAMERA+0x8c,struct.pack('<hh',180,180))
        m.mem_write(FOG,b'\x11\x11'*TILES)
        m.mem_write(HISTORY,b'\xff\xff'*TILES)
        # A bounded diamond stencil for original vision writer routines.
        m.mem_write(MASK,struct.pack('<7i',2,0,1,2,1,0,100))
        m.mem_write(0x8f45be,bytes((tx//13+ty//17)%4 for tx in range(180) for ty in range(180)))
        machines.append(m)
    results=[]
    for step,(camera,player) in enumerate((((64,64),0),((5,5),3),((176,176),7),
                                          ((80,55),0),((30,130),3),((100,90),7))):
        snapshots=[]
        for mode,m in zip(('4:3','16:9'),machines):
            # Identical original-game vision operations in both independent VMs.
            # Includes 37 emitters, all-player updates and map-edge clipping.
            for emitter in range(37):
                px=(emitter*7+step*3)%180
                py=(emitter*11+step*5)%180
                function=(0x417fe0,0x418160,0x418080)[(emitter+step)%3]
                m.mem_write(SP,struct.pack('<5I',STACK,MASK,px,py,player))
                m.reg_write(x.UC_X86_REG_ESP,SP)
                m.emu_start(function,STACK,count=10000)
                if m.reg_read(x.UC_X86_REG_EIP)!=STACK: raise RuntimeError('Vision command did not return')
            function=0x418200 if step%2 else 0x4182a0
            m.mem_write(SP,struct.pack('<4I',STACK,MASK,*camera))
            m.reg_write(x.UC_X86_REG_ESP,SP)
            m.emu_start(function,STACK,count=10000)
            if m.reg_read(x.UC_X86_REG_EIP)!=STACK: raise RuntimeError('All-player vision command did not return')
            before=bytes(m.mem_read(VISION,VISION_SIZE))
            m.mem_write(CAMERA+0x4fd4,struct.pack('<2I',*camera))
            m.mem_write(0xb63fc4,struct.pack('<I',player))
            m.mem_write(0x956775+player*0x3abc,bytes([player]))
            m.reg_write(x.UC_X86_REG_ECX,CAMERA)
            m.reg_write(x.UC_X86_REG_ESP,SP)
            writes={name:0 for _,_,name in allowed}
            def check_write(machine,access,address,size,value,data):
                for start,length,name in allowed:
                    if start<=address and address+size<=start+length:
                        writes[name]+=1; return
                raise RuntimeError(f'Unexpected preprocessing write: {mode} 0x{address:x}+{size}')
            hook=m.hook_add(uc.UC_HOOK_MEM_WRITE,check_write)
            m.emu_start(ENTRY,END,count=10000000)
            m.hook_del(hook)
            if m.reg_read(x.UC_X86_REG_EIP)!=END: raise RuntimeError('Fog prepass did not reach renderer boundary')
            after=bytes(m.mem_read(VISION,VISION_SIZE))
            if before!=after: raise RuntimeError('Rendering changed source vision')
            snapshots.append(after)
            results.append({'step':step,'mode':mode,'camera':camera,'player':player,
                            'vision_sha256':hashlib.sha256(after).hexdigest(),
                            'fog_sha256':hashlib.sha256(bytes(m.mem_read(FOG,TILES*2))).hexdigest(),
                            'writes':writes})
        if snapshots[0]!=snapshots[1]: raise RuntimeError('Vision commands diverged across aspect modes')
    if not any(results[i]['fog_sha256']!=results[i+1]['fog_sha256'] for i in range(0,len(results),2)):
        raise RuntimeError('Fixture failed to exercise different fog extents')
    return {'status':'visibility_scope_passed_not_lockstep_certification','source_sha256':digest,
            'paired_steps':6,'original_vision_commands_per_mode':6*38,
            'source_vision_bytes_compared_per_step':VISION_SIZE,'cases':results,
            'limitations':['Synthetic visibility fixture; no unit AI/combat/network',
                           'Only fog preprocessing ends at 00432911; other render paths not covered',
                           'No simulation-state checksum or full mixed-client multiplayer test']}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('original',type=Path)
    ap.add_argument('prototype',type=Path)
    ap.add_argument('--report',required=True,type=Path)
    args=ap.parse_args()
    report=verify(args.original,args.prototype)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2))
