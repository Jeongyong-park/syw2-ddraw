"""Relocate the original ESL terrain-scroll instructions inside Unicorn only.

No game process or EXE file is changed. Exports reviewed operand preimages and
checks the original scroll branches against an independent pixel-coordinate model.
"""
import argparse
import json
from pathlib import Path
import struct
from .widescreen_probe import validate_image, BASE, ENGINE, CAMERA

OLD_CACHE, OLD_DIRTY = 0xba3d30, 0x105796c
NEW_CACHE, NEW_DIRTY = 0x2201000, 0x2301000
OLD_PITCH, PITCH, HEIGHT, COLUMNS = 832, 1088, 512, 17
BEGIN, END = 0x432911, 0x433019


def operand_patches(pe):
    import capstone as cs
    md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_32)
    md.detail = True
    patches = []
    constants = {832: PITCH, 831: PITCH-1, 768: PITCH-64,
                 0x61800: PITCH*480, 13: COLUMNS}

    def convert(value, memory):
        if OLD_CACHE <= value <= OLD_CACHE+OLD_PITCH*HEIGHT+64:
            row, col = divmod(value-OLD_CACHE, OLD_PITCH)
            return NEW_CACHE+row*PITCH+col
        if OLD_DIRTY <= value <= OLD_DIRTY+13*16+15:
            offset = value-OLD_DIRTY
            # Last-column flags and one-past-end loop sentinels move by four columns.
            return NEW_DIRTY+offset+(64 if offset >= 12*16 else 0)
        if memory and value in (-0x6840, -0x67c0):
            return -(PITCH*32+(64 if value == -0x6840 else -64))
        if not memory:
            return constants.get(value)
        return None

    for ins in md.disasm(pe.get_data(BEGIN-BASE, END-BEGIN), BEGIN):
        for op in ins.operands:
            memory = op.type == cs.x86.X86_OP_MEM
            if not memory and op.type != cs.x86.X86_OP_IMM:
                continue
            value = op.mem.disp if memory else op.imm
            changed = convert(value, memory)
            if changed is None or changed == value:
                continue
            offset = ins.disp_offset if memory else ins.imm_offset
            size = ins.disp_size if memory else ins.imm_size
            if size != 4:
                raise ValueError('Relocation needs a new instruction at %x' % ins.address)
            patches.append({'address': ins.address+offset,
                            'instruction': '%08x: %s %s' % (ins.address, ins.mnemonic, ins.op_str),
                            'before': bytes(ins.bytes[offset:offset+size]).hex(),
                            'after': struct.pack('<I', changed & 0xffffffff).hex()})
    return patches


def render_patches(pe):
    """Explicitly reviewed sites outside the scroll function; no global replacement."""
    sites = [(0x41bf40, OLD_CACHE, NEW_CACHE, 'terrain cache source'),
             (0x41bf4a, OLD_PITCH, PITCH, 'terrain cache copy width'),
             (0x492317, OLD_PITCH, PITCH, 'battle surface minimum width comparison'),
             (0x49231e, OLD_PITCH, PITCH, 'battle surface minimum width value')]
    sites += [(a+3, OLD_DIRTY, NEW_DIRTY, 'terrain dirty-block lookup')
              for a in (0x43535b, 0x4357f4, 0x435db3, 0x436063, 0x43630b)]
    patches = []
    for address, old, new, purpose in sites:
        before = struct.pack('<I',old)
        if pe.get_data(address-BASE,4) != before:
            raise ValueError('Render preimage mismatch at %08x' % address)
        patches.append({'address':address,'instruction':purpose,
                        'before':before.hex(),'after':struct.pack('<I',new).hex()})
    return patches


def run(exe):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x
    raw = Path(exe).read_bytes()
    digest = validate_image(raw)
    pe = pefile.PE(data=raw)
    patches = operand_patches(pe) + render_patches(pe)
    if not patches:
        raise RuntimeError('No cache references found')
    original = bytes((i*17+i//PITCH*13) % 251+1 for i in range(PITCH*HEIGHT))
    results = []
    # Isometric camera deltas and the corresponding screen-space cache shift.
    moves = [(1,1,0,-32), (-1,-1,0,32), (-1,1,64,0), (1,-1,-64,0),
             (0,2,64,-32), (-2,0,64,32), (0,-2,-64,32), (2,0,-64,-32)]
    for cx, cy, dx, dy in moves:
        m = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
        m.mem_map(BASE, (pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095)
        m.mem_write(BASE, pe.get_memory_mapped_image())
        for patch in patches:
            before = bytes.fromhex(patch['before'])
            if bytes(m.mem_read(patch['address'], len(before))) != before:
                raise ValueError('Patch preimage mismatch')
        for patch in patches:
            m.mem_write(patch['address'], bytes.fromhex(patch['after']))
        stack = 0x2000000
        m.mem_map(stack, 0x10000)
        m.mem_map(NEW_CACHE-4096, 0x8a000)
        m.mem_map(NEW_DIRTY-4096, 0x3000)
        m.mem_write(NEW_CACHE, original)
        m.mem_write(NEW_CACHE-32, b'G'*32)
        m.mem_write(NEW_CACHE+len(original), b'G'*32)
        m.mem_write(NEW_DIRTY-32, b'G'*32)
        m.mem_write(NEW_DIRTY+COLUMNS*16, b'G'*32)
        old_bytes = bytes(m.mem_read(OLD_CACHE, OLD_PITCH*HEIGHT))
        old_dirty = bytes(m.mem_read(OLD_DIRTY, 13*16))
        m.mem_write(CAMERA+0x4fd4, struct.pack('<2I', 64+cx, 64+cy))
        m.mem_write(CAMERA+0x8c, struct.pack('<2h', 180, 180))
        m.mem_write(CAMERA+0x597d, b'\1')
        m.mem_write(0x1057968, struct.pack('<2h', 64, 64))
        m.reg_write(x.UC_X86_REG_ESP, stack+0x8000)
        m.reg_write(x.UC_X86_REG_ESI, CAMERA)
        copies = []

        def external(machine, address, size, data):
            # Execute the original small clip/pointer functions, emulate only CRT memmove.
            if address != 0x4db1d0:
                return
            sp = machine.reg_read(x.UC_X86_REG_ESP)
            ret, dest, source, count = struct.unpack('<4I', machine.mem_read(sp, 16))
            if not all(NEW_CACHE <= p and p+count <= NEW_CACHE+len(original) for p in (source,dest)):
                raise RuntimeError('Out-of-cache copy: %x <- %x length %x' % (dest,source,count))
            machine.mem_write(dest, bytes(machine.mem_read(source,count)))
            copies.append(count)
            machine.reg_write(x.UC_X86_REG_EAX, dest)
            machine.reg_write(x.UC_X86_REG_ESP, sp+4)
            machine.reg_write(x.UC_X86_REG_EIP, ret)

        m.hook_add(uc.UC_HOOK_CODE, external)
        m.emu_start(BEGIN, END, count=100000)
        if m.reg_read(x.UC_X86_REG_EIP) != END:
            raise RuntimeError('Scroll did not reach the terrain redraw boundary')
        actual = bytes(m.mem_read(NEW_CACHE,len(original)))
        flags = bytes(m.mem_read(NEW_DIRTY,COLUMNS*16))
        checked = 0
        for y in range(max(0,dy),min(HEIGHT,HEIGHT+dy)):
            left, right = max(0,dx), min(PITCH,PITCH+dx)
            expected = original[(y-dy)*PITCH+left-dx:(y-dy)*PITCH+right-dx]
            if actual[y*PITCH+left:y*PITCH+right] != expected:
                raise RuntimeError('Retained pixels mismatch at delta %s row %d' % ((cx,cy),y))
            checked += right-left
        # Every exposed block must be marked for the subsequent original terrain redraw.
        for col in range(COLUMNS):
            for row in range(16):
                exposed = (dx>0 and col==0 or dx<0 and col==COLUMNS-1 or
                           dy>0 and row==0 or dy<0 and row==15)
                if exposed and flags[col*16+row] != 1:
                    raise RuntimeError('Missing dirty block at %s' % ((col,row),))
        guards = [(NEW_CACHE-32,b'G'*32), (NEW_CACHE+len(original),b'G'*32),
                  (NEW_DIRTY-32,b'G'*32), (NEW_DIRTY+COLUMNS*16,b'G'*32),
                  (OLD_CACHE,old_bytes), (OLD_DIRTY,old_dirty)]
        if any(bytes(m.mem_read(a,len(b))) != b for a,b in guards):
            raise RuntimeError('Relocation changed a guard or the original cache')
        results.append({'camera_delta':[cx,cy], 'pixel_shift':[dx,dy],
                        'retained_pixels_checked':checked, 'memmove_calls':len(copies),
                        'exposed_blocks_marked':True, 'guards_and_old_cache_intact':True})
    # Exercise the original cache-to-surface function with only surface lock/unlock
    # and the unrelated update call stubbed. The pixel-copy instructions are original.
    dest = 0x2401000
    m.mem_map(dest-4096,0x8a000)
    m.mem_write(NEW_CACHE,original)
    m.mem_write(dest-32,b'G'*32)
    m.mem_write(dest+len(original),b'G'*32)
    m.mem_write(ENGINE+0xc,struct.pack('<II',8,PITCH))
    m.mem_write(ENGINE+0x1514,struct.pack('<I',dest))
    sp = stack+0x8000
    m.mem_write(sp,struct.pack('<I',stack))
    m.reg_write(x.UC_X86_REG_ESP,sp)

    def surface_calls(machine,address,size,data):
        if address not in (0x438840,0x464ec0,0x465080):
            return
        esp = machine.reg_read(x.UC_X86_REG_ESP)
        ret = struct.unpack('<I',machine.mem_read(esp,4))[0]
        machine.reg_write(x.UC_X86_REG_EAX,1)
        machine.reg_write(x.UC_X86_REG_ESP,esp+(4 if address==0x438840 else 8))
        machine.reg_write(x.UC_X86_REG_EIP,ret)

    m.hook_add(uc.UC_HOOK_CODE,surface_calls)
    m.emu_start(0x41bf20,stack,count=2000000)
    if m.reg_read(x.UC_X86_REG_EIP)!=stack:
        raise RuntimeError('Cache presentation did not return')
    if bytes(m.mem_read(dest,len(original)))!=original:
        raise RuntimeError('Original cache presentation lost or displaced pixels')
    if any(bytes(m.mem_read(a,32))!=b'G'*32 for a in (dest-32,dest+len(original))):
        raise RuntimeError('Presentation damaged destination guards')
    # Verify all five terrain-type lookup instructions reach the relocated final block.
    for address in (0x43535b,0x4357f4,0x435db3,0x436063,0x43630b):
        m.reg_write(x.UC_X86_REG_EDX,16*16)
        m.reg_write(x.UC_X86_REG_EAX,15)
        m.emu_start(address,address+7,count=1)
        if m.reg_read(x.UC_X86_REG_EDX)!=NEW_DIRTY+271:
            raise RuntimeError('Terrain lookup missed the relocated final block')
    return {'exe_sha256':digest, 'status':'emulated_scroll_and_cache_presentation',
            'cache_pitch':PITCH, 'cache_height':HEIGHT, 'patches':patches, 'cases':results,
            'presentation':{'pixels_checked':len(original),'guards_intact':True},
            'terrain_lookup_sites_checked':5}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe',type=Path)
    ap.add_argument('--report',type=Path,required=True)
    args = ap.parse_args()
    report = run(args.exe)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='patches'},indent=2))
    print('Validated operand relocations:',len(report['patches']))
