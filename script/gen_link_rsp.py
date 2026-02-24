from pathlib import Path

# Generate a response file for the linker to avoid Windows command length limits.
# Run from the Debug directory.

objs = sorted(Path('.').rglob('*.o'))
with open('link.rsp', 'w', encoding='utf-8') as f:
    f.write(' '.join(p.as_posix() for p in objs))
print(f'link.rsp generated with {len(objs)} objects')
