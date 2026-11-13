// Exercise the actual ESP-IDF FatFs name parser with and without LFN support.
#include <assert.h>
#include <stdio.h>
#include "ff.c"
int main(void) {
    FATFS fs = {0};
#if FF_USE_LFN
    WCHAR long_name[FF_MAX_LFN + 1];
    fs.lfnbuf = long_name;
#endif
    const char* names[] = {"t5wifi_credentials.bin", "t5wifi_auto.bin.tmp", "t5app_brightness.bin", "tab5mesh_active.bin.bak", "chat_export.txt"};
    for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
        FF_DIR dir = {0};
        dir.obj.fs = &fs;
        const TCHAR* path = names[i];
        FRESULT result = create_name(&dir, &path);
        assert(result == (FF_USE_LFN ? FR_OK : FR_INVALID_NAME));
    }
    FF_DIR dir = {0}; dir.obj.fs = &fs;
    const TCHAR* path = "t5msg.bin";
    assert(create_name(&dir, &path) == FR_OK);
    puts(FF_USE_LFN ? "PASS: actual FatFs accepts settings, temporary, backup and export filenames with LFN." : "PASS: reproduced FR_INVALID_NAME for settings with LFN disabled; message filename remains valid.");
}
