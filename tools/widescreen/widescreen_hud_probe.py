"""Validate original ESL HUD hit testing after moving its menu origin.

Read-only: original instructions run in Unicorn, never in a live game.
This covers the 4x3 command grid, not the whole HUD or its background.
"""
import argparse
import json
from pathlib import Path
import struct
from .widescreen_probe import BASE, validate_image


def inspect(exe):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x
    raw = Path(exe).read_bytes()
    digest = validate_image(raw)
    pe = pefile.PE(data=raw)
    m = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
    m.mem_map(BASE, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    m.mem_write(BASE, pe.get_memory_mapped_image())
    stack = 0x2000000
    m.mem_map(stack, 0x20000)
    sp = stack + 0x10000

    def external(machine, address, size, data):
        # Stub only unit-command availability and a metadata lookup. The
        # original index arithmetic and strict rectangle comparisons execute.
        cleanup = {0x4a3a40: 8, 0x496360: 0}.get(address)
        if cleanup is None:
            return
        esp = machine.reg_read(x.UC_X86_REG_ESP)
        ret, = struct.unpack('<I', machine.mem_read(esp, 4))
        machine.reg_write(x.UC_X86_REG_EAX, 1)
        machine.reg_write(x.UC_X86_REG_ESP, esp + 4 + cleanup)
        machine.reg_write(x.UC_X86_REG_EIP, ret)

    m.hook_add(uc.UC_HOOK_CODE, external)
    # Dimensions deliberately differ: catches width/height or row/column swaps.
    columns, rows, width, height, gap_x, gap_y = 4, 3, 34, 31, 2, 3
    m.mem_write(0x9e2b68, struct.pack('<6h', columns, rows, width, height, gap_x, gap_y))
    m.mem_write(0xb939b0, struct.pack('<I', 0))

    def hit(index, origin, px, py):
        m.mem_write(0x9e2b74, struct.pack('<hh', origin, 486))
        m.mem_write(0x4ed814, struct.pack('<hh', px, py))
        m.mem_write(sp, struct.pack('<II', stack, index))
        m.reg_write(x.UC_X86_REG_ESP, sp)
        m.emu_start(0x498c20, stack, count=1000)
        if m.reg_read(x.UC_X86_REG_EIP) != stack or m.reg_read(x.UC_X86_REG_ESP) != sp + 4:
            raise RuntimeError('Original hit test failed to return with its calling convention')
        return m.reg_read(x.UC_X86_REG_EAX) != 0

    checks = 0
    for shift in (0, 134):
        origin = 652 + shift
        for index in range(columns * rows):
            left = origin + (index % columns) * (width + gap_x)
            top = 486 + (index // columns) * (height + gap_y)
            for dx, dy, expected in (
                (1, 1, True), (width-1, height-1, True),
                (0, height//2, False), (width, height//2, False),
                (width//2, 0, False), (width//2, height, False),
                (-1, 1, False), (width+1, 1, False),
            ):
                if hit(index, origin, left+dx, top+dy) != expected:
                    raise RuntimeError(f'Hit mismatch: shift={shift}, index={index}, point={(dx, dy)}')
                checks += 1
            if shift and hit(index, origin, left-shift+width//2, top+height//2):
                raise RuntimeError('Old button center still activates shifted command')
            if shift:
                checks += 1
    cursor_cases = []
    source, dest = 0x2100000, 0x2200000
    m.mem_map(source, 4096)
    m.mem_map(dest, 0x100000)
    engine = 0xe5bf18
    # Original cursor RLE: FE,count skips transparent pixels. Opaque values
    # deliberately vary by row. Includes drawing against the expanded right edge.
    sprite = bytes([0xfe, 2, 41, 42, 43, 44, 0xfe, 2,
                    51, 52, 53, 54, 55, 56, 57, 58])
    m.mem_write(source, sprite)
    for pitch in (1068, 1088):
        for px, py in ((400, 250), (900, 250), (1064, 599)):
            size = pitch * 600
            m.mem_write(dest, bytes([17]) * (size + 32))
            m.mem_write(engine+4, struct.pack('<5I', 1068, 600, 8, pitch, 600))
            m.mem_write(engine+0x1514, struct.pack('<I', dest))
            m.mem_write(engine+0x4d0, struct.pack('<4I', 0, 0, 1067, 599))
            m.mem_write(sp, struct.pack('<6I', stack, px, py, 8, 2, source))
            m.reg_write(x.UC_X86_REG_ESP, sp)
            m.reg_write(x.UC_X86_REG_ECX, engine)
            m.emu_start(0x466d50, stack, count=10000)
            if m.reg_read(x.UC_X86_REG_EIP) != stack or m.reg_read(x.UC_X86_REG_ESP) != sp+24:
                raise RuntimeError('Cursor renderer did not return correctly')
            expected = bytearray([17]) * (size+32)
            for row, values in enumerate(([None, None, 41, 42, 43, 44, None, None],
                                           [51, 52, 53, 54, 55, 56, 57, 58])):
                for col, value in enumerate(values):
                    if value is not None and px+col < 1068 and py+row < 600:
                        expected[(py+row)*pitch+px+col] = value
            if m.mem_read(dest, size+32) != expected:
                raise RuntimeError(f'Cursor pixel/guard mismatch: {pitch}, {px}, {py}')
            cursor_cases.append({'pitch': pitch, 'position': [px, py], 'exact_pixels': True})
    return {'source_sha256': digest, 'status': 'original_hud_hit_and_cursor_render_passed',
            'checks': checks, 'buttons': columns*rows, 'hud_offset_x': 134,
            'cursor_render_cases': cursor_cases,
            'limitations': ['Command availability stubbed', 'Other HUD hit regions not tested',
                            'Synthetic cursor sprite, no real asset or presentation test',
                            'No live game or rendering changes']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.exe)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
