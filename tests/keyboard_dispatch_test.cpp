#include "../main/board/keyboard.cpp"
#include <cassert>
#include <string>
#include <cstdio>
static bool on_ui_task;
static std::string received;
extern "C" void ui_kbd_feed(const char* str,unsigned char len,unsigned char modifier) {
    assert(on_ui_task); assert(len==2 && modifier==0); received+=str;
}
int main() {
    kbd_start(); assert(keyboard_test_callback && keyboard_test_timer);
    for(char c:std::string("abcdef\n")) {
        m5_tab5_key_event_t event{};event.type=M5_TAB5_KB_MODE_STRING;
        event.str_data[0]=c;event.str_len=2;keyboard_test_callback(event,nullptr);
    }
    assert(received.empty()); // Driver callback never executes UI/send/history.
    on_ui_task=true; keyboard_test_timer(nullptr);assert(received=="abcd");
    keyboard_test_timer(nullptr);assert(received=="abcdef\n");
    keyboard_test_timer(nullptr);assert(received=="abcdef\n");
    puts("PASS: keyboard queues input and Enter; bounded FIFO dispatch exclusively from UI timer.");
}
