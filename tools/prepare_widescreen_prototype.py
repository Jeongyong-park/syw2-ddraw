"""Build an isolated, deliberately experimental ESL widescreen EXE copy.

Requires an existing complete game directory under this repository's output/.
No original installation or running process is modified. Not a release installer.
"""
import argparse
import json
from pathlib import Path
import struct
from widescreen_probe import validate_image, BASE
from widescreen_cache_probe import operand_patches, render_patches, NEW_CACHE, NEW_DIRTY

# Screen-space fields loaded by 004B5960; sizes/margins/Y coordinates stay local.
HUD_X16 = (0x9e2b74, 0x9e2b84, 0x9e2b88, 0x9e2b8e, 0x9e2b92,
           0x9e2b96, 0x9e2b9c, 0x9e2ba4)
HUD_X32 = (0x9e2bac, 0x9e2bbc)


def prepare(source, target, hud_preview=False, full_height=False):
    import pefile
    if full_height and not hud_preview:
        raise ValueError('Full-height terrain requires the centered HUD preview')
    root = Path(__file__).resolve().parents[1] / 'output'
    source, target = Path(source).resolve(), Path(target).resolve()
    if not target.is_relative_to(root.resolve()) or target.parent != source.parent:
        raise ValueError('Prototype must be a sibling EXE in a repository output/ game copy')
    if target.exists() or source == target:
        raise ValueError('Refusing to overwrite an existing file')
    raw = source.read_bytes()
    digest = validate_image(raw)
    pe = pefile.PE(data=raw)
    if pe.OPTIONAL_HEADER.ImageBase != BASE:
        raise ValueError('Unexpected image base')
    patches = operand_patches(pe) + render_patches(pe)
    # Initial prototype expands the cache and camera rectangle. HUD remains unmodified.
    for address, old, new, size, purpose in [
        (0x41b4db,3,4,1,'battle mode selector'),
        (0x46452c,1024,1068,4,'battle display width'),
        (0x464533,768,600,4,'battle display height'),
        (0x41b516,831,1087,4,'initial terrain clip right'),
        (0x41b533,831,1087,4,'initial camera right'),
        (0x432491,831,1087,4,'cache build clip right'),
        (0x43249d,831,1087,4,'cache build camera right'),
    ]:
        before=old.to_bytes(size,'little')
        if pe.get_data(address-BASE,size)!=before:
            raise ValueError('Preimage mismatch: '+purpose)
        patches.append({'address':address,'before':before.hex(),
                        'after':new.to_bytes(size,'little').hex(),'instruction':purpose})
    modified = bytearray(raw)
    for p in patches:
        offset = pe.get_offset_from_rva(p['address']-BASE)
        before,after=bytes.fromhex(p['before']),bytes.fromhex(p['after'])
        if modified[offset:offset+len(before)]!=before:
            raise ValueError('Overlapping or mismatched patch')
        modified[offset:offset+len(after)]=after
    # Loader-owned zero-initialized memory; caches are never overlaid on original globals.
    # Image sections must follow the preceding aligned section. Include the
    # address gap in BSS rather than leaving an unmapped hole between sections.
    alignment=pe.OPTIONAL_HEADER.SectionAlignment
    original_end=max(s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData) for s in pe.sections)
    start = (original_end+alignment-1)//alignment*alignment
    end = NEW_DIRTY+8192-BASE
    last = pe.sections[-1]
    header = last.get_file_offset()+40
    first_data=min(s.PointerToRawData for s in pe.sections if s.SizeOfRawData)
    if header+40>first_data or any(modified[header:header+40]):
        raise ValueError('No empty section-header slot')
    if start < max(s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData) for s in pe.sections):
        raise ValueError('Cache section overlaps the original image')
    if start > NEW_CACHE-4096-BASE:
        raise ValueError('Original image reaches the prototype cache')
    if start%alignment or end%alignment:
        raise ValueError('Cache section is not aligned')
    modified[header:header+40]=struct.pack('<8sIIIIIIHHI',b'.hqwide',end-start,start,0,0,0,0,0,0,0xc0000080)
    struct.pack_into('<H',modified,pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'),len(pe.sections)+1)
    struct.pack_into('<I',modified,pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'),end)
    struct.pack_into('<I',modified,pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfUninitializedData'),
                     pe.OPTIONAL_HEADER.SizeOfUninitializedData+end-start)
    struct.pack_into('<I',modified,pe.OPTIONAL_HEADER.get_field_absolute_offset('CheckSum'),0)
    checked=pefile.PE(data=bytes(modified))
    if checked.sections[-1].Name.rstrip(b'\0')!=b'.hqwide':
        raise RuntimeError('Failed to verify prototype section')
    hud_report = None
    terrain_report = None
    if hud_preview:
        # Isolated HUD integration; not yet a production runtime patcher.
        # The game extracts/deletes its layout files; adjust in-memory coordinates
        # immediately after the original loader instead of changing those files.

        def patch(address, before, after):
            offset = pe.get_offset_from_rva(address-BASE)
            if len(before) != len(after) or modified[offset:offset+len(before)] != before:
                raise ValueError('HUD preimage mismatch at %x' % address)
            modified[offset:offset+len(after)] = after
            patches.append({'address': address, 'before': before.hex(), 'after': after.hex(),
                            'instruction': 'experimental HUD background/grid relocation'})

        # Existing BltFast sequence uses an imm8 zero for X. Emit an imm32 X in
        # a loader-owned executable section, retaining ESI's vtable and stack ABI.
        entry = BASE + end
        continuation = 0x41c60c
        code = bytes.fromhex('8b3250') + b'\x68' + struct.pack('<I', 134) + bytes.fromhex('52ff561c')
        code += b'\xe9' + struct.pack('<i', continuation-(entry+len(code)+5))
        patch(0x41c603, bytes.fromhex('8b32506a0052ff561c'),
              b'\xe9'+struct.pack('<i', entry-0x41c608)+b'\x90'*4)
        patch(0x41c5c3, bytes.fromhex('a11cbfe500'), b'\xb8'+struct.pack('<I', 800))
        layout_entry = entry+len(code)
        layout_code = b'\xe8'+struct.pack('<i', 0x4b5960-(layout_entry+5))
        layout_code += b'\x9c'  # preserve the loader's flags and registers
        for address in HUD_X16:
            layout_code += bytes.fromhex('668105')+struct.pack('<I', address)+struct.pack('<H', 134)
        for address in HUD_X32:
            layout_code += bytes.fromhex('8105')+struct.pack('<II', address, 134)
        layout_code += bytes.fromhex('9dc3')
        patch(0x41b555, b'\xe9'+struct.pack('<i', 0x4b5960-0x41b55a),
              b'\xe9'+struct.pack('<i', layout_entry-0x41b55a))
        code += layout_code
        argument_thunks = []
        def adjusted_x(original, sites, delta):
            nonlocal code
            address = entry+len(code)
            # Preserve flags; original function sees the same stack/ECX and an
            # adjusted first argument. Caller/callee cleanup remains unchanged.
            body = bytes.fromhex('9c81442408')+struct.pack('<i', delta)+b'\x9d'
            body += b'\xe9'+struct.pack('<i', original-(address+len(body)+5))
            code += body
            for site in sites:
                patch(site, b'\xe8'+struct.pack('<i', original-(site+5)),
                      b'\xe8'+struct.pack('<i', address-(site+5)))
            argument_thunks.append({'entry': address, 'original': original, 'sites': sites, 'delta': delta})
        adjusted_x(0x419d80, [0x41c68d, 0x41c715, 0x4b3834,
                              0x43f27c, 0x43f313, 0x43f3a1], 134)  # decorations/resource icons
        adjusted_x(0x41a370, [0x43f2e1, 0x43f36f, 0x43f3fd, 0x43f43a, 0x43f487], 134)
        adjusted_x(0x4516a0, [0x41e464], -134)  # minimap mouse -> local coordinates
        adjusted_x(0x417db0, [0x41e4d0], -134)  # HUD opacity hit mask -> local coordinates
        # Alarm coordinates are cached when the event arrives, independently of
        # the minimap origin temporarily shifted by the viewport-outline thunk.
        # Shift only the final sprite draw, preserving saved events and timing.
        adjusted_x(0x419de0, [0x451a09], 134)
        for site, old in ((0x4aa1c5,317),(0x4aa1ef,316),(0x4aa237,317),
                          (0x4aa259,316),(0x4aa341,317),(0x4aa363,316)):
            patch(site, b'\x68'+struct.pack('<I',old), b'\x68'+struct.pack('<I',old+134))

        # Minimap pixels remain in the local HUD surface. Its viewport outline
        # renders directly to the battle surface, so temporarily shift its X.
        minimap_entry = entry+len(code)
        marker = bytes.fromhex('568bf1810686000000ff74240cff74240c')
        marker += b'\xe8'+struct.pack('<i', 0x451920-(minimap_entry+len(marker)+5))
        marker += bytes.fromhex('812e860000005ec20800')
        code += marker
        patch(0x41c2d0, b'\xe8'+struct.pack('<i', 0x451920-0x41c2d5),
              b'\xe8'+struct.pack('<i', minimap_entry-0x41c2d5))
        if full_height:
            from widescreen_full_height import append_full_height
            code, terrain_report = append_full_height(code, entry, patch)
        # A versioned reversible patch table for the wrapper's startup-only
        # 4:3 / 16:9 selector. Original EXE and on-disk prototype remain unchanged.
        table_offset = len(code)
        for p in patches:
            before, after = bytes.fromhex(p['before']), bytes.fromhex(p['after'])
            if len(before) != len(after) or not 0 < len(before) <= 16:
                raise ValueError('Unsupported aspect patch size')
            code += struct.pack('<II16s16s', p['address'], len(before), before, after)
        code += struct.pack('<8sII', b'HQASPECT', table_offset, len(patches))
        code_header = header+40
        if code_header+40 > first_data or any(modified[code_header:code_header+40]):
            raise ValueError('No executable section header slot')
        file_alignment = pe.OPTIONAL_HEADER.FileAlignment
        raw_offset = (len(modified)+file_alignment-1)//file_alignment*file_alignment
        raw_size = (len(code)+file_alignment-1)//file_alignment*file_alignment
        modified.extend(bytes(raw_offset-len(modified)))
        modified.extend(code.ljust(raw_size, b'\0'))
        modified[code_header:code_header+40] = struct.pack(
            '<8sIIIIIIHHI', b'.hqcode', len(code), end, raw_size, raw_offset, 0, 0, 0, 0, 0x60000020)
        struct.pack_into('<H', modified, pe.FILE_HEADER.get_field_absolute_offset('NumberOfSections'), len(pe.sections)+2)
        struct.pack_into('<I', modified, pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfImage'),
                         end+(len(code)+alignment-1)//alignment*alignment)
        struct.pack_into('<I', modified, pe.OPTIONAL_HEADER.get_field_absolute_offset('SizeOfCode'), pe.OPTIONAL_HEADER.SizeOfCode+raw_size)
        hud_report = {'status': 'lower_hud_integration_pending_live_validation', 'offset_x': 134,
                      'layout_thunk_address': layout_entry, 'thunk_address': entry, 'thunk_bytes': code.hex(),
                      'argument_thunks': argument_thunks, 'minimap_thunk': minimap_entry,
                      'remaining': ['Minimap alert markers', 'Bottom side terrain',
                                    'Live input and rendering verification']}

    report={'source_sha256':digest,'status':'experimental_pending_full_battle_validation',
            'display':[1068,600],'cache':[1088,608 if full_height else 512],'cache_address':hex(NEW_CACHE),
            'full_height':terrain_report,
            'dirty_address':hex(NEW_DIRTY),'patches':patches, 'hud_preview': hud_report}
    # Exclusive creation prevents a concurrent invocation from replacing another file.
    with target.open('xb') as stream:
        stream.write(modified)
    target.with_suffix('.prototype.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return target


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source',type=Path)
    ap.add_argument('target',type=Path)
    ap.add_argument('--hud-preview', action='store_true', help='Experimental centered HUD and restart-only aspect selector')
    ap.add_argument('--full-height', action='store_true', help='Experimental full-redraw 608-row terrain cache')
    args=ap.parse_args()
    print(prepare(args.source,args.target,args.hud_preview,args.full_height))
