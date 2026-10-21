"""Bind native names in GSC source, then invoke unmodified xensik/gsc-tool.

gsc-tool 1.5.1's object macros retain definition source locations, whereas its
method parser requires receiver/method tokens on the same line. Expanding only
registered identifier tokens before compilation avoids that compiler issue.
No bytecode is patched, and no game address is consumed by this tool.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

TOKEN = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|'
    r"'(?:\\[\s\S]|[^'\\])*'|"
    r'^[ \t]*\#[ \t]*(?:include|inline|using_animtree)\b[^\n]*|'
    r'\b[A-Za-z_][A-Za-z_0-9]*\b', re.MULTILINE)
INLINE_BINDINGS = re.compile(r'^[ \t]*#\s*inline\s+iw8x_native_bindings\s*;\s*$', re.IGNORECASE)


def load_bindings(path):
    def unique(pairs):
        out = {}
        for key, value in pairs:
            if key in out:
                raise ValueError(f'duplicate binding key: {key}')
            out[key] = value
        return out
    data = json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique)
    if data.get('api') != 1 or not isinstance(data.get('bindings'), list):
        raise ValueError('unsupported binding database')
    aliases, slots = {}, set()
    for item in data['bindings']:
        name, method, slot = item['name'], item['method'], item['slot']
        if (not isinstance(name, str) or not re.fullmatch(r'[a-z_][a-z_0-9]{0,126}', name)
                or type(method) is not bool or type(slot) is not int or not 0 <= slot < 256
                or name in aliases or (method, slot) in slots):
            raise ValueError('invalid or duplicate native binding')
        slots.add((method, slot))
        aliases[name] = f"{'_meth_' if method else '_func_'}{slot + (0x87CA if method else 0x438):04X}"
    return aliases


def expand(source, aliases):
    def replace(match):
        token = match.group()
        if INLINE_BINDINGS.fullmatch(token):
            return ' ' * len(token)  # Keep source line numbers unchanged.
        return aliases.get(token.lower(), token)
    return TOKEN.sub(replace, source)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bindings', required=True, type=Path)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('source', help='GSC path relative to source-root')
    args = parser.parse_args()
    source_root, output = args.source_root.resolve(strict=True), args.output.resolve()
    compiler = args.compiler.resolve(strict=True)
    source = (source_root / args.source).resolve(strict=True)
    relative = source.relative_to(source_root)
    if source.suffix.lower() != '.gsc':
        parser.error('source must be a .gsc file inside source-root')
    if output == source_root or source_root in output.parents:
        parser.error('output must be outside source-root to avoid recursively staging generated files')
    aliases = load_bindings(args.bindings)
    output.mkdir(parents=True, exist_ok=True)
    # Keep the unique staging directory as a reviewable build artifact.
    stage = Path(tempfile.mkdtemp(prefix='expanded-', dir=output)).resolve()
    assert stage.parent == output
    for path in source_root.rglob('*'):
        if not path.is_file() or path.suffix.lower() not in ('.gsc', '.gsh'):
            continue
        resolved = path.resolve(strict=True)
        resolved.relative_to(source_root)  # Reject symlinks escaping the source root.
        destination = stage / path.relative_to(source_root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(expand(path.read_text(encoding='utf-8-sig'), aliases), encoding='utf-8')
    command = [str(compiler), '-m', 'comp', '-g', 'iw8', '-s', 'pc', '-w', str(stage), str(stage / relative)]
    subprocess.run(command, cwd=output, check=True)
    print(f'Expanded sources retained at {stage}')


if __name__ == '__main__':
    main()
