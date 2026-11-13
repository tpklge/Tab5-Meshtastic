#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
static bool fail_publish = false;
static int fat_rename(const char* src, const char* dst) {
    struct stat info;
    if (stat(dst, &info) == 0) { errno = EEXIST; return -1; }
    if (fail_publish && strstr(src, ".tmp")) { errno = EIO; return -1; }
    return rename(src, dst);
}
#define rename fat_rename
#include "../main/storage/sd_kv.cpp"
#undef rename
bool sd_storage_available() { return true; }
int main() {
    char directory[] = "/tmp/tab5-fat-XXXXXX";
    assert(mkdtemp(directory) && chdir(directory) == 0);
    sd_kv_handle_t h;
    assert(sd_kv_open("t5wifi", SD_KV_READWRITE, &h) == ESP_OK);
    for (uint8_t expected : {1, 2, 3}) {
        assert(sd_kv_set_u8(h, "auto", expected) == ESP_OK);
        uint8_t actual = 0;
        assert(sd_kv_get_u8(h, "auto", &actual) == ESP_OK && actual == expected);
    }
    fail_publish = true;
    assert(sd_kv_set_u8(h, "auto", 4) == ESP_FAIL);
    uint8_t actual = 0;
    assert(sd_kv_get_u8(h, "auto", &actual) == ESP_OK && actual == 3);
    fail_publish = false;
    // Simulate power loss after the old file was moved aside.
    assert(rename("t5wifi_auto.bin", "t5wifi_auto.bin.bak") == 0);
    assert(sd_kv_get_u8(h, "auto", &actual) == ESP_OK && actual == 3);
    assert(sd_kv_set_u8(h, "auto", 5) == ESP_OK);
    assert(sd_kv_get_u8(h, "auto", &actual) == ESP_OK && actual == 5);
    assert(rename("t5wifi_auto.bin", "t5wifi_auto.bin.bak") == 0);
    assert(sd_kv_erase_key(h, "auto") == ESP_OK);
    assert(sd_kv_get_u8(h, "auto", &actual) == ESP_ERR_NOT_FOUND);
    assert(chdir("/") == 0 && rmdir(directory) == 0);
    puts("PASS: FAT replacement, rollback, interrupted-save recovery and backup erasure.");
}
