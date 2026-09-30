#!/usr/bin/env python3
"""Exercise original and patched RAK SafeFile/renameFile with STM32 append semantics.
Usage: python3 tests/run_rak_persistence_tests.py /path/to/RAK3172h-firmware
No hardware access. The provided checkout must have the patch applied.
"""
import os, pathlib, subprocess, sys, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
rak=pathlib.Path(sys.argv[1]).resolve()
env=os.environ.copy()
if sys.platform=='darwin':env.setdefault('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
def run(args):subprocess.run(args,check=True,env=env)
with tempfile.TemporaryDirectory(prefix='rak-persistence-') as tmp:
    out=pathlib.Path(tmp)
    (out/'stubs.h').write_text((root/'tests/rak_persistence/stubs.h').read_text())
    for name in ['FSCommon.h','SPILock.h','configuration.h']:(out/name).write_text('#include "stubs.h"\n')
    (out/'SafeFile.h').write_text((rak/'src/SafeFile.h').read_text())
    includes=['-I'+str(out),'-I'+str(rak/'src/mesh/generated'),'-I'+str(root/'components/meshtastic_protos/nanopb')]
    flags=['-std=c++17','-g','-fsanitize=address,undefined','-DARCH_STM32WL',*includes]
    for fixed in [False,True]:
        def source(name):
            return (rak/name).read_text() if fixed else subprocess.check_output(['git','-C',str(rak),'show','HEAD:'+name],text=True)
        (out/'SafeFile.cpp').write_text(source('src/SafeFile.cpp'))
        text=source('src/FSCommon.cpp')
        # Compile the unmodified production copyFile/renameFile bodies only;
        # unrelated directory traversal requires the full Arduino runtime.
        start=text.index('bool copyFile(');end=text.index('#include <cstring>',start)
        (out/'rename.cpp').write_text('#include "stubs.h"\n'+text[start:end])
        binary=out/('fixed' if fixed else 'original')
        run(['c++',*flags,str(root/'tests/rak_persistence/test.cpp'),str(out/'SafeFile.cpp'),str(out/'rename.cpp'),
             str(rak/'src/mesh/generated/meshtastic/channel.pb.cpp'),
             *[str(root/'components/meshtastic_protos/nanopb'/n) for n in ['pb_common.c','pb_encode.c','pb_decode.c']],'-o',str(binary)])
        run([str(binary),*(['fixed'] if fixed else [])])
