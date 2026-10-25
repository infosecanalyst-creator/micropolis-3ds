// Headless desktop backend: drives the app from a script and dumps frames as
// PPM files. Used to test the frontend without a 3DS.
//
//   micropolis-headless [--city file.cty | --scenario N] [--frames N] [--out dir]
//                       [--slider 0.0-1.0] [--script "A,R,R,UP,UP,A,TOUCH:140:30,..."]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "../platform.h"
#include "../app.h"

static const char *g_out = ".";
static const char *g_res = "res";
static int g_frame = 0;
static int g_dumpEvery = 1;
static uint64_t g_fakeMs = 0;

namespace platform {
bool init() { return true; }
void shutdown() {}
bool running() { return true; }
void poll(Input &) {}
uint64_t millis() { return g_fakeMs; }
void sleepMs(unsigned) {}
const char *resDir() { return g_res; }
const char *saveDir() { return g_out; }
void log(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fputc('\n', stderr); }

static void writePPM(const char *path, const Screen &s) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", s.w, s.h);
    fwrite(s.rgb, 1, s.w * s.h * 3, f);
    fclose(f);
}

void present(const Screen &top, const Screen *topRight, const Screen &bottom) {
    if (g_frame % g_dumpEvery != 0) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/top_%04d.ppm", g_out, g_frame);
    writePPM(path, top);
    if (topRight) {
        snprintf(path, sizeof(path), "%s/topR_%04d.ppm", g_out, g_frame);
        writePPM(path, *topRight);
    }
    snprintf(path, sizeof(path), "%s/bottom_%04d.ppm", g_out, g_frame);
    writePPM(path, bottom);
}
}

// script tokens: A B X Y L R UP DOWN LEFT RIGHT START SELECT, TOUCH:x:y, WAIT:n (frames),
//   DRAG:x1:y1:x2:y2:n (stylus drag over n frames), PRESS:x:y:n (hold still n frames)
static uint32_t tokenButton(const char *t) {
    static const struct { const char *n; uint32_t b; } m[] = {
        {"A", BTN_A}, {"B", BTN_B}, {"X", BTN_X}, {"Y", BTN_Y}, {"L", BTN_L}, {"R", BTN_R},
        {"UP", BTN_UP}, {"DOWN", BTN_DOWN}, {"LEFT", BTN_LEFT}, {"RIGHT", BTN_RIGHT},
        {"START", BTN_START}, {"SELECT", BTN_SELECT}, {"ZL", BTN_ZL}, {"ZR", BTN_ZR},
    };
    for (auto &e : m) if (!strcmp(e.n, t)) return e.b;
    return 0;
}

int main(int argc, char **argv) {
    const char *city = nullptr, *script = "";
    int scenario = 0, frames = 60;
    float slider = 0.f;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--city") && i + 1 < argc) city = argv[++i];
        else if (!strcmp(argv[i], "--scenario") && i + 1 < argc) scenario = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) g_out = argv[++i];
        else if (!strcmp(argv[i], "--res") && i + 1 < argc) g_res = argv[++i];
        else if (!strcmp(argv[i], "--slider") && i + 1 < argc) slider = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) g_dumpEvery = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
    }

    App app;
    if (city) app.loadCity(city);
    else if (scenario) app.loadScenario(scenario);
    else app.newCity(12345);

    char *scr = strdup(script);
    char *tok = strtok(scr, ",");
    int waitFrames = 0;
    int gestFrames = 0, gestTotal = 1, gx1 = 0, gy1 = 0, gx2 = 0, gy2 = 0;
    bool gestRelease = false;
    for (g_frame = 0; g_frame < frames; ++g_frame) {
        Input in;
        in.slider = slider;
        if (gestFrames > 0) {
            // DRAG / PRESS in progress: stylus held, interpolated, then lifted
            const int total = gestTotal, i = gestTotal - gestFrames + 1;
            --gestFrames;
            if (gestFrames > 0 || i <= total) {
                in.touch = true;
                in.touchX = gx1 + (gx2 - gx1) * i / total;
                in.touchY = gy1 + (gy2 - gy1) * i / total;
            }
            if (gestFrames == 0) waitFrames = 0;
        } else if (gestRelease) {
            gestRelease = false;          // one frame with the stylus up
        } else if (waitFrames > 0) {
            --waitFrames;
        } else if (tok) {
            if (!strncmp(tok, "DRAG:", 5) || !strncmp(tok, "PRESS:", 6)) {
                int n = 10;
                if (tok[0] == 'D') sscanf(tok + 5, "%d:%d:%d:%d:%d", &gx1, &gy1, &gx2, &gy2, &n);
                else { sscanf(tok + 6, "%d:%d:%d", &gx1, &gy1, &n); gx2 = gx1; gy2 = gy1; }
                gestTotal = gestFrames = n > 0 ? n : 1;
                gestRelease = true;
                in.touch = in.touchDown = true;
                in.touchX = gx1; in.touchY = gy1;
            } else if (!strncmp(tok, "TOUCH:", 6)) {
                in.touch = in.touchDown = true;
                sscanf(tok + 6, "%d:%d", &in.touchX, &in.touchY);
            } else if (!strncmp(tok, "WAIT:", 5)) {
                waitFrames = atoi(tok + 5);
            } else if (!strncmp(tok, "HOLD:", 5)) {
                // HOLD:A:RIGHT  -> A held while RIGHT pressed (drag)
                char a[16], b[16];
                if (sscanf(tok + 5, "%15[^:]:%15s", a, b) == 2) {
                    in.held = tokenButton(a);
                    in.down = in.repeat = tokenButton(b);
                }
            } else {
                in.down = in.repeat = tokenButton(tok);
                in.held = in.down;
            }
            tok = strtok(nullptr, ",");
        }
        g_fakeMs += 33;
        app.frame(in);
    }
    free(scr);
    printf("rendered %d frames to %s\n", frames, g_out);
    return 0;
}
