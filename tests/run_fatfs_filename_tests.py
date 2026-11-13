#!/usr/bin/env python3
"""Reproduce filename rejection using the ESP-IDF FatFs implementation."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parents[1]
idf = Path(os.environ.get('IDF_PATH', str(Path.home()/'esp/esp-idf')))
source = idf/'components/fatfs/src'
if not (source/'ff.c').exists():
    raise SystemExit('Set IDF_PATH to an ESP-IDF checkout.')
env = os.environ.copy()
sdk = Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
if sys.platform == 'darwin' and sdk.exists():
    env.setdefault('SDKROOT', str(sdk))
config = (root/'build/config/sdkconfig.h').read_text()
config = re.sub(r'^#define CONFIG_FATFS_LFN_.*$', '', config, flags=re.M)
with tempfile.TemporaryDirectory(prefix='tab5-fatfs-names-') as tmp:
    directory = Path(tmp)
    (directory/'freertos').mkdir()
    (directory/'freertos/FreeRTOS.h').write_text('#pragma once\n#define portTICK_PERIOD_MS 1\n')
    (directory/'freertos/semphr.h').write_text('#pragma once\ntypedef void* SemaphoreHandle_t;\n')
    for mode in ['NONE', 'HEAP']:
        (directory/'sdkconfig.h').write_text(config+'\n#define CONFIG_FATFS_LFN_'+mode+' 1\n')
        binary = directory/mode
        gc = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
        subprocess.run(['cc', '-std=c11', '-ffunction-sections', '-fdata-sections', gc,
                        '-I'+str(directory), '-I'+str(source), str(root/'tests/fatfs_filename_test.c'),
                        str(source/'ffunicode.c'), '-o', str(binary)], check=True, env=env)
        subprocess.run([str(binary)], check=True, env=env)
