// Platform abstraction for the Micropolis 3DS frontend.
// The app renders into plain RGB888 buffers; a platform backend shows them.
#pragma once
#include <stdint.h>

struct Screen {
    int w, h;
    uint8_t *rgb;                 // packed RGB, row-major, (y*w + x)*3
    inline uint8_t *px(int x, int y) { return rgb + (y * w + x) * 3; }
};

enum Button {
    BTN_A = 1 << 0, BTN_B = 1 << 1, BTN_X = 1 << 2, BTN_Y = 1 << 3,
    BTN_L = 1 << 4, BTN_R = 1 << 5,
    BTN_UP = 1 << 6, BTN_DOWN = 1 << 7, BTN_LEFT = 1 << 8, BTN_RIGHT = 1 << 9,
    BTN_START = 1 << 10, BTN_SELECT = 1 << 11,
    BTN_ZL = 1 << 12, BTN_ZR = 1 << 13,
    BTN_CUP = 1 << 14, BTN_CDOWN = 1 << 15,   // New 3DS C-Stick up/down
};

struct Input {
    uint32_t down = 0;    // pressed this frame
    uint32_t held = 0;    // currently held
    uint32_t repeat = 0;  // pressed, with key-repeat for the d-pad
    bool touch = false;   // stylus down
    bool touchDown = false; // stylus went down this frame
    int touchX = 0, touchY = 0;
    int cpadX = 0, cpadY = 0;   // -156..156
    float slider = 0.f;         // 3D slider, 0..1
};

namespace platform {
bool init();
void shutdown();
bool running();                 // false when the user closes the app
void poll(Input &in);
bool leftyOn();                 // left-handed play: the D-pad and A/B/X/Y swap jobs
void setLefty(bool on);         // remembered between runs
// topRight may be null (no stereo); otherwise it is the right-eye image.
void present(const Screen &top, const Screen *topRight, const Screen &bottom);
uint64_t millis();
void sleepMs(unsigned ms);
const char *resDir();           // where res/ (scenarios) lives
const char *saveDir();          // writable city save directory
void log(const char *fmt, ...);
}
