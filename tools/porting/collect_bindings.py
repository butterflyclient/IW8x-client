"""Inventory explicit Replay RVAs for IDA signature export (no source edits)."""
import argparse
import json
import re
from pathlib import Path


def collect(root, build='Replay TU19 / 1.20.4.7623265', image_size=0x1324b000):
    if not (root/'src').is_dir():
        raise ValueError('Use --root to select a project directory containing src/')
    entries = {}
    def add(rva, name, path, expected=None):
        value = int(rva, 16)
        if not 0x1000 <= value < image_size:
            return
        entry = entries.setdefault(value, {'name': name, 'rva': hex(value), 'aliases': [], 'sources': []})
        if name not in entry['aliases']:
            entry['aliases'].append(name)
        location = path.relative_to(root).as_posix()
        if location not in entry['sources']:
            entry['sources'].append(location)
        if expected:
            entry['expected_prefix'] = expected
    for path in sorted((root/'src').rglob('*')):
        if path.suffix not in {'.h', '.hpp', '.cpp', '.inl'}:
            continue
        source = path.read_text(encoding='utf-8-sig')
        for name, rva in re.findall(r'\b(?:uintptr_t|uint64_t|uint32_t)\s+(\w*(?:RVA|Rva))\s*=\s*(0x[0-9A-Fa-f]+)', source):
            add(rva, name, path)
        if path.name.endswith('_bindings.h'):
            for rva, values in re.findall(r'\{\s*(0x[0-9A-Fa-f]+)\s*,\s*\{([^}]+)\}', source):
                expected = ' '.join(f'{int(x,16):02X}' for x in re.findall(r'0x[0-9A-Fa-f]+', values))
                add(rva, path.stem+'_'+rva[2:].lower(), path, expected)
        for rva in re.findall(r'\bNative<[^\n;]+?>\s*\(\s*(0x[0-9A-Fa-f]+)\s*\)', source):
            add(rva, 'native_'+rva[2:].lower(), path)
    if not entries:
        raise ValueError('No supported RVA declarations found; provide an explicit inventory for this source tree')
    return {'schema': 1, 'build': build, 'image_size': hex(image_size), 'symbols': [entries[k] for k in sorted(entries)]}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--build', default='Replay TU19 / 1.20.4.7623265')
    parser.add_argument('--image-size', type=lambda value: int(value, 0), default=0x1324b000)
    args = parser.parse_args()
    document = collect(args.root, args.build, args.image_size)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2)+'\n', encoding='utf-8')
    print(f"Collected {len(document['symbols'])} distinct RVAs into {args.output}")
