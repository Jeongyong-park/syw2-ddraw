"""Execute the generated HUD background and layout thunks in Unicorn."""
import argparse
import json
from pathlib import Path
import struct


def verify(exe):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x
    pe = pefile.PE(str(exe))
    m = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
    m.mem_map(0x400000, pe.OPTIONAL_HEADER.SizeOfImage)
    m.mem_write(0x400000, pe.get_memory_mapped_image())
    region, sp, stop = 0x3000000, 0x3010000, 0x3001000
    m.mem_map(region, 0x20000)
    put = lambda a, *v: m.mem_write(a, struct.pack('<'+'I'*len(v), *v))
    read = lambda a, n: struct.unpack('<'+'I'*n, m.mem_read(a, n*4))
    put(0xe5bf1c, 1068, 600)
    put(0xb92ce8, 203)
    put(0x104fb2c, region)
    put(0x104f814, region+0x200)
    put(region, region+0x100)
    put(region+0x11c, stop)
    put(sp, region+0x1000)
    m.reg_write(x.UC_X86_REG_ESP, sp)
    m.emu_start(0x41c5c0, stop, count=500)
    if m.reg_read(x.UC_X86_REG_EIP) != stop:
        raise RuntimeError('BltFast not reached')
    call_sp = m.reg_read(x.UC_X86_REG_ESP)
    args = read(call_sp+4, 6)
    rect = read(args[4], 4)
    if args[:4] != (region, 134, 397, region+0x200) or args[5] != 0x11 or rect != (0, 0, 800, 203):
        raise RuntimeError(f'Wrong background arguments: {args}, {rect}')
    m.mem_write(stop, bytes.fromhex('c21800'))  # COM: this + five arguments
    m.emu_start(stop, 0x41c60c, count=100)
    if m.reg_read(x.UC_X86_REG_EIP) != 0x41c60c or m.reg_read(x.UC_X86_REG_ESP) != sp-24:
        raise RuntimeError('Background thunk stack imbalance')

    layout_stop = stop+0x100
    fields = {0x9e2b74: (2,652), 0x9e2b84: (2,271), 0x9e2b88: (2,350),
              0x9e2b8e: (2,122), 0x9e2b92: (2,540), 0x9e2b96: (2,330),
              0x9e2b9c: (2,199), 0x9e2ba4: (2,445),
              0x9e2bac: (4,450), 0x9e2bbc: (4,197)}
    initial_layout = bytearray([0x53])*112
    for address, (size,value) in fields.items():
        offset = address-0x9e2b68
        initial_layout[offset:offset+size] = value.to_bytes(size, 'little')
    initial_layout[14:16] = (486).to_bytes(2,'little')
    initial_layout[30:32] = (493).to_bytes(2,'little')
    expected_layout = bytearray(initial_layout)
    for address, (size,value) in fields.items():
        offset = address-0x9e2b68
        expected_layout[offset:offset+size] = (value+134).to_bytes(size,'little')
    def loader(machine, address, size, data):
        if address == layout_stop:
            machine.emu_stop()
            return
        if address != 0x4b5960:
            return
        if machine.reg_read(x.UC_X86_REG_ECX) != 0x9e2b68:
            raise RuntimeError('Wrong layout object')
        machine.mem_write(0x9e2b68, bytes(initial_layout))
        esp = machine.reg_read(x.UC_X86_REG_ESP)
        machine.reg_write(x.UC_X86_REG_ESP, esp+4)
        machine.reg_write(x.UC_X86_REG_EIP, read(esp, 1)[0])

    m.hook_add(uc.UC_HOOK_CODE, loader)
    # Twice proves original reload + correction does not accumulate the offset.
    for _ in range(2):
        put(sp, layout_stop)
        m.reg_write(x.UC_X86_REG_ESP, sp)
        try:
            m.emu_start(0x41b550, layout_stop, count=100)
        except uc.UcError as error:
            raise RuntimeError('Layout execution at %x, stack %x' %
                               (m.reg_read(x.UC_X86_REG_EIP), m.reg_read(x.UC_X86_REG_ESP))) from error
        if m.reg_read(x.UC_X86_REG_EIP) != layout_stop or m.reg_read(x.UC_X86_REG_ESP) != sp+4:
            raise RuntimeError('Layout thunk stack imbalance')
        if struct.unpack('<hh', m.mem_read(0x9e2b74, 4)) != (786, 486):
            raise RuntimeError('Command grid origin mismatch')
        if struct.unpack('<hh', m.mem_read(0x9e2b84, 4)) != (405, 493):
            raise RuntimeError('Shortcut origin mismatch')
        if m.mem_read(0x9e2b68, len(expected_layout)) != expected_layout:
            raise RuntimeError('HUD field or neighboring byte changed unexpectedly')

    argument_checks = []
    for site, original, delta in ((0x41c68d,0x419d80,134), (0x41c715,0x419d80,134),
                                  (0x4b3834,0x419d80,134), (0x43f27c,0x419d80,134),
                                  (0x43f313,0x419d80,134), (0x43f3a1,0x419d80,134),
                                  (0x43f2e1,0x41a370,134), (0x43f36f,0x41a370,134),
                                  (0x43f3fd,0x41a370,134), (0x43f43a,0x41a370,134),
                                  (0x43f487,0x41a370,134),
                                  (0x41e464,0x4516a0,-134), (0x41e4d0,0x417db0,-134)):
        for px in (0,133,134,500,1067):
            put(sp,px,500,region+0x500,region+0x504)
            m.reg_write(x.UC_X86_REG_ESP,sp)
            m.reg_write(x.UC_X86_REG_ECX,region+0x400)
            m.reg_write(x.UC_X86_REG_EFLAGS,0x246)
            m.emu_start(site,original,count=100)
            if m.reg_read(x.UC_X86_REG_EIP) != original or m.reg_read(x.UC_X86_REG_ESP) != sp-4:
                raise RuntimeError('Argument adapter stack mismatch')
            if read(sp,4) != ((px+delta)&0xffffffff,500,region+0x500,region+0x504):
                raise RuntimeError('Argument adapter changed the wrong parameter')
            if m.reg_read(x.UC_X86_REG_ECX) != region+0x400 or m.reg_read(x.UC_X86_REG_EFLAGS) != 0x246:
                raise RuntimeError('Argument adapter clobbered this/flags')
            argument_checks.append({'site':hex(site),'x':px,'adjusted_x':px+delta})

    minimap_entry = 0x41c2d5+struct.unpack('<i',m.mem_read(0x41c2d1,4))[0]
    marker_stop = stop+0x200
    put(region+0x400,17,530)
    put(sp,marker_stop,64,65)
    m.reg_write(x.UC_X86_REG_ESP,sp)
    m.reg_write(x.UC_X86_REG_ECX,region+0x400)
    m.reg_write(x.UC_X86_REG_ESI,0x12345678)
    m.emu_start(minimap_entry,0x451920,count=100)
    marker_sp = m.reg_read(x.UC_X86_REG_ESP)
    if read(region+0x400,2) != (151,530) or read(marker_sp+4,2) != (64,65):
        raise RuntimeError('Minimap origin/argument mismatch')
    m.mem_write(0x451920,bytes.fromhex('c20800'))
    m.emu_start(0x451920,marker_stop,count=100)
    if (m.reg_read(x.UC_X86_REG_EIP) != marker_stop or
        m.reg_read(x.UC_X86_REG_ESP) != sp+12 or
        m.reg_read(x.UC_X86_REG_ESI) != 0x12345678 or read(region+0x400,2) != (17,530)):
        raise RuntimeError('Minimap temporary origin or stack not restored')
    # Run the actual screen-to-minimap function, not only its argument adapter.
    minimap_input = 0x41e469+struct.unpack('<i',m.mem_read(0x41e465,4))[0]
    input_stop = stop+0x300
    put(region+0x400,17,530)
    table = region+0x400+0x13eea
    m.mem_write(table,b'\xff'*(180*90*2+2))
    m.mem_write(table+(20*180+20)*2,bytes([64,65]))
    for px,expected in ((171,True),(37,False),(133,False)):
        put(sp,input_stop,px,550,region+0x500,region+0x504)
        put(region+0x500,0xcccccccc,0xcccccccc)
        m.reg_write(x.UC_X86_REG_ESP,sp)
        m.reg_write(x.UC_X86_REG_ECX,region+0x400)
        m.emu_start(minimap_input,input_stop,count=1000)
        if m.reg_read(x.UC_X86_REG_EIP) != input_stop or m.reg_read(x.UC_X86_REG_ESP) != sp+20:
            raise RuntimeError('Original minimap input did not return')
        if bool(m.reg_read(x.UC_X86_REG_EAX)) != expected:
            raise RuntimeError('Shifted minimap input hit mismatch')
        if expected and read(region+0x500,2) != (0xcccc0040,0xcccc0041):
            raise RuntimeError('Shifted minimap selected the wrong world tile')
    return {'status': 'generated_hud_thunks_passed', 'background_destination': [134, 397],
            'background_source': list(rect), 'command_origin': [786, 486],
            'shortcut_origin': [405, 493], 'reloads': 2,
            'layout_fields': len(fields), 'argument_checks': len(argument_checks),
            'minimap_origin_restored': True,
            'original_minimap_input_checks': 3,
            'limitations': ['COM call captured, not rendered', 'Layout file loader stubbed',
                            'Partial HUD only; not live-game validation']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    args = parser.parse_args()
    result = verify(args.exe)
    args.exe.with_suffix('.hud-verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
