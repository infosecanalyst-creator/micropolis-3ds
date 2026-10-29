// Nintendo 3DS backend (libctru, software framebuffers, stereoscopic top).
#include <3ds.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include "../platform.h"
#include "../app.h"

// libctru's default main-thread stack is 32 KB; the simulator and the
// frontend want more headroom. 1 MB.
extern "C" u32 __stacksize__ = 1024 * 1024;

static const char *kSaveDir = "sdmc:/3ds/micropolis";
static const char *kResDir = "romfs:/res";

namespace platform {

bool init() {
    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);
    gfxSet3D(true);
    osSetSpeedupEnable(true);
    hidSetRepeatParameters(14, 4);
    romfsInit();
    mkdir("sdmc:/3ds", 0777);
    mkdir(kSaveDir, 0777);
    mkdir("sdmc:/3ds/micropolis/cities", 0777);
    return true;
}

void shutdown() {
    romfsExit();
    gfxExit();
}

bool running() { return aptMainLoop(); }

void poll(Input &in) {
    hidScanInput();
    const u32 down = hidKeysDown(), held = hidKeysHeld(), rep = hidKeysDownRepeat();
    static const struct { u32 ctr; uint32_t btn; } map[] = {
        { KEY_A, BTN_A }, { KEY_B, BTN_B }, { KEY_X, BTN_X }, { KEY_Y, BTN_Y },
        { KEY_L, BTN_L }, { KEY_R, BTN_R }, { KEY_ZL, BTN_ZL }, { KEY_ZR, BTN_ZR },
        { KEY_DUP, BTN_UP }, { KEY_DDOWN, BTN_DOWN }, { KEY_DLEFT, BTN_LEFT }, { KEY_DRIGHT, BTN_RIGHT },
        { KEY_START, BTN_START }, { KEY_SELECT, BTN_SELECT },
        { KEY_CSTICK_UP, BTN_CUP }, { KEY_CSTICK_DOWN, BTN_CDOWN },
    };
    in.down = in.held = in.repeat = 0;
    for (auto &m : map) {
        if (down & m.ctr) in.down |= m.btn;
        if (held & m.ctr) in.held |= m.btn;
        if (rep & m.ctr) in.repeat |= m.btn;
    }
    in.touch = (held & KEY_TOUCH) != 0;
    in.touchDown = (down & KEY_TOUCH) != 0;
    if (in.touch) {
        touchPosition tp;
        hidTouchRead(&tp);
        in.touchX = tp.px;
        in.touchY = tp.py;
    }
    circlePosition cp;
    hidCircleRead(&cp);
    in.cpadX = cp.dx;
    in.cpadY = cp.dy;
    in.slider = osGet3DSliderState();
}

// Copy a landscape RGB buffer into the 3DS's portrait-rotated BGR framebuffer.
static void blit(gfxScreen_t screen, gfx3dSide_t side, const Screen &s) {
    u16 fbw, fbh;
    u8 *fb = gfxGetFramebuffer(screen, side, &fbw, &fbh);
    const int H = fbw;  // 240
    for (int y = 0; y < s.h; ++y) {
        const uint8_t *src = s.rgb + y * s.w * 3;
        for (int x = 0; x < s.w; ++x) {
            u8 *p = fb + 3 * (x * H + (H - 1 - y));
            p[0] = src[2]; p[1] = src[1]; p[2] = src[0];
            src += 3;
        }
    }
}

void present(const Screen &top, const Screen *topRight, const Screen &bottom) {
    blit(GFX_TOP, GFX_LEFT, top);
    blit(GFX_TOP, GFX_RIGHT, topRight ? *topRight : top);
    blit(GFX_BOTTOM, GFX_LEFT, bottom);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

uint64_t millis() { return osGetTime(); }
void sleepMs(unsigned ms) { svcSleepThread((s64)ms * 1000000LL); }
const char *resDir() { return kResDir; }
const char *saveDir() { return kSaveDir; }

void log(const char *fmt, ...) {
    char buf[256];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof(buf), fmt, a); va_end(a);
    FILE *f = fopen("sdmc:/3ds/micropolis/log.txt", "a");
    if (f) { fputs(buf, f); fputc('\n', f); fclose(f); }
}

} // namespace platform

int main(int argc, char **argv) {
    platform::init();
    {
        // The App (and the Micropolis object inside it) lives on the heap,
        // never on the stack.
        App *app = new App();
        // Resume the autosave if there is one, otherwise a fresh city.
        char path[256];
        snprintf(path, sizeof(path), "%s/autosave.cty", kSaveDir);
        struct stat st;
        if (stat(path, &st) == 0) app->loadCity(path);
        else app->newCity(0);

        while (platform::running() && !app->quitRequested) {
            Input in;
            platform::poll(in);
            app->frame(in);
        }
        delete app;
    }
    platform::shutdown();
    return 0;
}
