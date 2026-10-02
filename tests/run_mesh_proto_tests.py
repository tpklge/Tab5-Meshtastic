#!/usr/bin/env python3
"""Native mesh_proto encode/decode tests, AddressSanitizer + UndefinedBehaviorSanitizer. No device needed."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tab5-mesh-proto-') as temp:
    out = pathlib.Path(temp)
    includes = ['main/mesh', 'components/meshtastic_protos/nanopb', 'components/meshtastic_protos/proto']
    flags = ['-g', '-fsanitize=address,undefined'] + ['-I'+str(root/p) for p in includes]
    sources = list((root/'components/meshtastic_protos/nanopb').glob('*.c')) + list((root/'components/meshtastic_protos/proto/meshtastic').glob('*.c'))
    objects = []
    for i, source in enumerate(sources):
        obj = out/f'{i}.o'
        subprocess.run(['cc', *flags, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = out/'mesh_proto_test'
    subprocess.run(['c++', '-std=c++17', *flags,
                    str(root/'tests/mesh_proto_test.cpp'),
                    str(root/'main/mesh/mesh_proto.cpp'),
                    *objects, '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, cwd=root)
