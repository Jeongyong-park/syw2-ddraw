"""Compare original outbound message dispatch; not a multiplayer certification.

Connection/host queries and the transport boundary are stubbed. Original packet
header updates, routing and the 0x103 local queue insertion execute in Unicorn.
No input translation, receive handling, combat or simulation ticks run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from .widescreen_probe import validate_image


def verify(original, prototype):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x

    digest = validate_image(original.read_bytes())
    images = [pefile.PE(str(path)) for path in (original, prototype)]
    results = []
    base, scratch = 0x400000, 0x3000000
    obj, packet, sp, stop = scratch, scratch + 0x6000, scratch + 0xf000, scratch + 0xff00
    for host in (0, 1):
        for kind in (0x103, 0x107, 0x10d, 0x112):
            for length in (0, 4, 120):
                snapshots = []
                for mode, pe in zip(('4:3', '16:9'), images):
                    m = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
                    m.mem_map(base, pe.OPTIONAL_HEADER.SizeOfImage)
                    m.mem_write(base, pe.get_memory_mapped_image())
                    m.mem_map(scratch, 0x10000)
                    m.mem_write(0x632c08, bytes(0x4b00))
                    put = lambda address, *values: m.mem_write(address, struct.pack('<' + 'I' * len(values), *values))
                    put(0xe5bf1c, 800 if mode == '4:3' else 1068, 600)
                    put(0xb63fc4, 3)
                    put(obj + 0x48b8, 0x1234)
                    put(packet, kind, (length << 24) | 123)
                    m.mem_write(packet + 8, bytes((i * 17 + 3) % 256 for i in range(length)))
                    put(sp, stop, packet, 0)
                    m.reg_write(x.UC_X86_REG_ECX, obj)
                    m.reg_write(x.UC_X86_REG_ESP, sp)
                    sent = []

                    def boundary(machine, address, size, data):
                        if address not in (0x439b80, 0x439c50, 0x439e80):
                            return
                        esp = machine.reg_read(x.UC_X86_REG_ESP)
                        ret = struct.unpack('<I', machine.mem_read(esp, 4))[0]
                        if address == 0x439e80:
                            target, flags, ptr, count = struct.unpack('<4I', machine.mem_read(esp + 4, 16))
                            if count != length + 8 or ptr != packet:
                                raise RuntimeError('Unexpected outbound packet boundary')
                            sent.append((target, flags, bytes(machine.mem_read(ptr, count)).hex()))
                        machine.reg_write(x.UC_X86_REG_EAX, host if address == 0x439c50 else 1)
                        machine.reg_write(x.UC_X86_REG_ESP, esp + (20 if address == 0x439e80 else 4))
                        machine.reg_write(x.UC_X86_REG_EIP, ret)

                    m.hook_add(uc.UC_HOOK_CODE, boundary)
                    m.emu_start(0x439fa0, stop, count=100000)
                    if m.reg_read(x.UC_X86_REG_EIP) != stop or m.reg_read(x.UC_X86_REG_ESP) != sp + 12:
                        raise RuntimeError('Dispatch failed to return correctly')
                    if len(sent) != 1 or m.reg_read(x.UC_X86_REG_EAX) != 1:
                        raise RuntimeError('Dispatch failed to send exactly one packet')
                    queue = bytes(m.mem_read(0x632c08, 0x4b00))
                    expected_count = 1 if kind == 0x103 else 0
                    if struct.unpack_from('<I', queue, 0x91c)[0] != expected_count:
                        raise RuntimeError('Expected queue insertion was not exercised')
                    snapshots.append((sent, queue))
                if snapshots[0] != snapshots[1]:
                    raise RuntimeError(f'Outbound dispatch differs: {kind:x}, {host=}, {length=}')
                results.append({'host': bool(host), 'kind': hex(kind), 'payload_bytes': length,
                                'target': snapshots[0][0][0][0],
                                'queue_sha256': hashlib.sha256(snapshots[0][1]).hexdigest()})
    return {'status': 'outbound_dispatch_scope_passed_not_lockstep_certification',
            'source_sha256': digest, 'paired_cases': len(results), 'cases': results,
            'limitations': ['Synthetic prebuilt commands; input conversion not exercised',
                            'Connection/host queries and transport stubbed; no real networking',
                            'No receive handling, simulation tick, AI/combat or state checksum',
                            'Queue tested with one insertion, not overflow/retransmission']}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('original', type=Path)
    ap.add_argument('prototype', type=Path)
    ap.add_argument('--report', required=True, type=Path)
    args = ap.parse_args()
    report = verify(args.original, args.prototype)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'cases'}, indent=2))
