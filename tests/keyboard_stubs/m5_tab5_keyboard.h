#pragma once
#include <cstdint>
#define M5_TAB5_KB_MODE_STRING 1
#define M5_TAB5_KB_OK 0
#define I2C_NUM_1 1
#define M5_TAB5_KB_DEFAULT_ADDR 0x6d
#define M5_TAB5_KB_DEFAULT_SDA 0
#define M5_TAB5_KB_DEFAULT_SCL 1
#define M5_TAB5_KB_I2C_FREQ_400K 400000
#define M5_TAB5_KB_INT_MODE_DISABLED 0
#define M5_TAB5_KB_INT_MODE_POLLING 1
using m5_tab5_kb_err_t=int;
struct m5_tab5_key_event_t {int type; char str_data[16]; unsigned char str_len,str_modifier;};
inline void (*keyboard_test_callback)(m5_tab5_key_event_t,void*);
inline bool keyboard_test_normal_mode = false;
inline bool keyboard_test_ended = false;
inline bool keyboard_test_polling_stopped = false;
namespace m5 {struct M5Tab5Keyboard {
    int begin(int,int,int,int,int,int){return 0;}
    int setInterruptMode(int mode,int=100){if(mode==M5_TAB5_KB_INT_MODE_DISABLED) keyboard_test_polling_stopped=true;return 0;}
    int enableStringMode(decltype(keyboard_test_callback) cb,void*){keyboard_test_callback=cb;return 0;}
    int enableNormalMode(){keyboard_test_normal_mode=true;return 0;}
    void end(){keyboard_test_ended=true;}
    void getVersion(uint8_t* version){*version=1;}
};}
