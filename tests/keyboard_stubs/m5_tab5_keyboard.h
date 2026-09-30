#pragma once
#include <cstdint>
#define M5_TAB5_KB_MODE_STRING 1
#define M5_TAB5_KB_OK 0
#define I2C_NUM_1 1
#define M5_TAB5_KB_DEFAULT_ADDR 0x6d
#define M5_TAB5_KB_DEFAULT_SDA 0
#define M5_TAB5_KB_DEFAULT_SCL 1
#define M5_TAB5_KB_I2C_FREQ_400K 400000
#define M5_TAB5_KB_INT_MODE_POLLING 0
using m5_tab5_kb_err_t=int;
struct m5_tab5_key_event_t {int type; char str_data[16]; unsigned char str_len,str_modifier;};
inline void (*keyboard_test_callback)(m5_tab5_key_event_t,void*);
namespace m5 {struct M5Tab5Keyboard {
    int begin(int,int,int,int,int,int){return 0;}
    void setInterruptMode(int,int){}
    void enableStringMode(decltype(keyboard_test_callback) cb,void*){keyboard_test_callback=cb;}
    void getVersion(uint8_t* version){*version=1;}
};}
