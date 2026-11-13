#!/usr/bin/env python3
"""Native Wi-Fi scan and duplicate netif start regression tests."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
env = os.environ.copy()
sdk = Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
if sys.platform == 'darwin' and sdk.exists():
    env.setdefault('SDKROOT', str(sdk))
flags = ['-DSD_MOUNT_POINT="."', '-std=c++17', '-pthread', '-g', '-fsanitize=address,undefined']
flags += ['-I'+str(root/p) for p in ['tests/wifi_stubs', 'tests/ui_stubs', 'tests/stubs',
                                   'main/network', 'main/app', 'main/storage']]
with tempfile.TemporaryDirectory(prefix='tab5-wifi-tests-') as directory:
    for name, sources in [
        ('scan', ['tests/wifi_scan_test.cpp', 'main/storage/sd_kv.cpp']),
        ('netif', ['tests/wifi_netif_guard_test.cpp']),
        ('fat', ['tests/sd_kv_fat_test.cpp']),
    ]:
        binary = str(Path(directory)/name)
        subprocess.run(['c++', *flags, *[str(root/p) for p in sources], '-o', binary], check=True, env=env)
        subprocess.run([binary], check=True, env=env)
