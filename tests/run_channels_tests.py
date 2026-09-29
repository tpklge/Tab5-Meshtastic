#!/usr/bin/env python3
"""Native protocol tests, AddressSanitizer + UndefinedBehaviorSanitizer. No device needed."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tab5-channels-') as temp:
    out = pathlib.Path(temp)
    includes = ['main/protocol', 'main/mesh', 'components/meshtastic_protos/nanopb', 'components/meshtastic_protos/proto', 'components/quirc']
    flags = ['-g', '-fsanitize=address,undefined'] + ['-I'+str(root/p) for p in includes]
    sources = list((root/'components/meshtastic_protos/nanopb').glob('*.c')) + list((root/'components/meshtastic_protos/proto/meshtastic').glob('*.c')) + list((root/'components/quirc').glob('*.c'))
    objects = []
    for i, source in enumerate(sources):
        obj = out/f'{i}.o'
        subprocess.run(['cc', *flags, '-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    binary = out/'channels_test'
    subprocess.run(['c++', '-std=c++17', *flags, str(root/'tests/channels_test.cpp'), str(root/'main/protocol/channel_url.cpp'), str(root/'main/mesh/mesh_proto.cpp'), *objects, '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(out/'channels.url')], check=True, cwd=root)
    try:
        import qrcode
    except ImportError:
        raise SystemExit('Install qrcode in the test Python environment to run the QR interoperability test.')
    qr = qrcode.QRCode(box_size=4, border=4)
    qr.add_data((out/'channels.url').read_text().strip()); qr.make(fit=True)
    matrix = qr.get_matrix()
    pixels = bytes(0 if cell else 255 for row in matrix for _ in range(4) for cell in row for _ in range(4))
    width = len(matrix)*4
    (out/'channels.pgm').write_bytes(f'P5\n{width} {width}\n255\n'.encode()+pixels)
    subprocess.run([str(binary), str(out/'channels.url'), str(out/'channels.pgm')], check=True, cwd=root)

    service = out/'channel_service_test'
    subprocess.run(['c++', '-std=c++17', '-I'+str(root/'tests/stubs'), '-I'+str(root/'main/app'), *flags,
                    str(root/'tests/channel_service_test.cpp'), str(root/'main/protocol/channel_service.cpp'),
                    str(root/'main/protocol/channel_url.cpp'), *objects, '-o', str(service)], check=True)
    subprocess.run([str(service)], check=True)
