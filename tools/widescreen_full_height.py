"""Correctness-first full-height cache experiment; scrolling rebuilds all tiles.

This is intentionally opt-in until live rendering and frame cost are measured.
"""
import struct
from widescreen_cache_probe import NEW_CACHE, NEW_DIRTY


def append_full_height(code, entry, patch):
    for address, old, new in (
        (0x432912,512,608), (0x432955,511,607), (0x41bf45,512,600),
        (0x41b511,511,599), (0x41b547,511,599),
        (0x43248c,511,599), (0x4324ac,511,599),
    ):
        patch(address, struct.pack('<I',old), struct.pack('<I',new))
    for shift, stride in ((0x435358,0x435375),(0x4357f1,0x43580e),
                          (0x435db0,0x435dcd),(0x436060,0x43607d),(0x436308,0x436325)):
        patch(shift,bytes.fromhex('c1e204'),bytes.fromhex('c1e205'))
        patch(stride,bytes.fromhex('83c210'),bytes.fromhex('83c220'))
    # Expand all four camera-local passes together: fog, height reconciliation,
    # dirty tile detection and rasterization. Map boundary checks remain intact.
    for address in (0x432525,0x43253f,0x4325fd,0x432611,0x432863,0x43287b,
                    0x43303c,0x433050):
        patch(address,b'\xf0',b'\xe0')
    for address in (0x432528,0x432542,0x4325d0,0x4325e4,0x432600,0x432614,
                    0x432829,0x43283b,0x432866,0x43287e,0x4328ea,0x432904,
                    0x43303f,0x433053,0x433290,0x4332ac):
        patch(address,b'\x10',b'\x20')
    start=entry+len(code)
    # The original dirty/full-redraw gate precedes this hook; idle frames keep
    # their cache. Preserve registers, flags and the caller's direction flag.
    body=bytes.fromhex('9c60fc31c0bf')+struct.pack('<I',NEW_CACHE)
    body+=b'\xb9'+struct.pack('<I',1088*608)+bytes.fromhex('f3aab001bf')
    body+=struct.pack('<I',NEW_DIRTY)+b'\xb9'+struct.pack('<I',17*32)
    body+=bytes.fromhex('f3aa619d')
    body+=b'\xe9'+struct.pack('<i',0x433019-(start+len(body)+5))
    patch(0x432982,bytes.fromhex('80be7c59000001'),
          b'\xe9'+struct.pack('<i',start-0x432987)+b'\x90\x90')
    return code+body, {'entry':start,'cache_height':608,'visible_rows':600,
                       'dirty_stride':32,'redraw':'full on original dirty gate',
                       'status':'experimental_pending_live_rendering_and_performance'}
