#!/usr/bin/env python3
"""Native calendar/state tests and headless LVGL chat/Wi-Fi editor integration."""
import os
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
env = os.environ.copy()
mac_sdk = pathlib.Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
if sys.platform == 'darwin' and mac_sdk.exists():
    env.setdefault('SDKROOT', str(mac_sdk))
with tempfile.TemporaryDirectory(prefix='tab5-chat-tests-') as temp:
    out = pathlib.Path(temp)
    includes = ['tests/ui_stubs', 'tests/stubs', 'main/app', 'main/mesh', 'main/storage', 'main/ble',
                'main/board', 'main/protocol', 'main/network', 'managed_components/lvgl__lvgl',
                'components/meshtastic_protos/nanopb', 'components/meshtastic_protos/proto']
    flags = ['-std=c++17', '-g', '-fsanitize=address,undefined'] + ['-I'+str(root/p) for p in includes]
    def run(args):
        subprocess.run(args, check=True, cwd=root, env=env)
    clock = out/'clock-test'
    run(['c++', *flags, str(root/'tests/chat_clock_test.cpp'), str(root/'main/app/app_state.cpp'),
         str(root/'main/app/clock_calendar.cpp'), '-o', str(clock)])
    run([str(clock)])
    config = out/'lv_conf.h'
    config.write_text('''#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (4 * 1024 * 1024)
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_USE_OS LV_OS_NONE
#define LV_USE_THORVG_INTERNAL 0
#define LV_USE_LOG 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
''')
    cmake = ['cmake', '-S', str(root/'managed_components/lvgl__lvgl'), '-B', str(out/'build'),
             '-DLV_BUILD_CONF_PATH='+str(config), '-DCONFIG_LV_BUILD_DEMOS=OFF',
             '-DCONFIG_LV_BUILD_EXAMPLES=OFF', '-DCONFIG_LV_USE_THORVG_INTERNAL=OFF']
    if sys.platform == 'darwin' and env.get('SDKROOT'):
        cmake.append('-DCMAKE_OSX_SYSROOT='+env['SDKROOT'])
    # Save verbose library build output for failures without flooding successful runs.
    for command in [cmake, ['cmake', '--build', str(out/'build'), '-j', '8']]:
        result = subprocess.run(command, cwd=root, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
    ui = out/'ui-test'
    gc = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
    run(['c++', *flags, '-ffunction-sections', '-fdata-sections', gc, f'-DLV_CONF_PATH="{config}"',
         str(root/'tests/chat_ui_test.cpp'), str(root/'main/app/app_state.cpp'),
         str(out/'build/lib/liblvgl.a'), '-o', str(ui)])
    run([str(ui)])
