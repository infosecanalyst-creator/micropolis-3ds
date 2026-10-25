// Micropolis 3DS frontend: game state, rendering, input.
#pragma once
#include <stdint.h>
#include <stdarg.h>
#include "platform.h"
#include "../engine/micropolis.h"

static const int TOP_W = 400, TOP_H = 240, BOT_W = 320, BOT_H = 240;

class App {
public:
    App();
    ~App();

    // Start a fresh random city (seed 0 = random), or load a .cty file.
    bool newCity(int seed = 0);
    bool loadCity(const char *path);
    bool loadScenario(int which);   // 1..8

    // One frame: advance the simulation if due, apply input, render.
    void frame(const Input &in);

    Screen top, topR, bottom;
    bool quitRequested = false;

private:
    Micropolis sim;

    // ---- view state
    int camX = 0, camY = 0;      // top-left visible tile
    int zoom = 1;                // 1 or 2
    int curX = 60, curY = 50;    // cursor tile
    int toolIdx = 9;             // index into kTools (road)
    bool dragging = false;
    uint64_t lastTickMs = 0;
    uint64_t lastRepeatMs = 0;
    int frameCounter = 0;

    enum Mode { MODE_PLAY, MODE_BUDGET, MODE_MENU, MODE_QUERY, MODE_DISASTERS, MODE_HELP, MODE_LOADCITY } mode = MODE_PLAY;
    int helpPage = 0;
    // city files found in <saveDir>/cities for the Load city browser
    static const int kMaxCities = 64;
    char cityNames[kMaxCities][40];
    int nCities = 0, citySel = 0;
    void scanCities();
    int menuSel = 0;
    int budgetSel = 0;
    // Overlay / minimap modes
    enum Overlay { OV_NONE, OV_POWER, OV_POLLUTION, OV_CRIME, OV_LANDVALUE, OV_TRAFFIC,
                   OV_POPDENSITY, OV_FIRE, OV_POLICE, OV_GROWTH, OV_COUNT };
    int overlay = OV_NONE;
    bool overlayOnCity = true;
    // sample the current overlay at a world tile: 0..255, or -1 for "none"
    int overlayValue(int tx, int ty) const;
    int overlayMax() const;
    int stretch(int v) const;
    int overlayMaxCached = 255;
    void overlayColour(int value, uint8_t *rgb) const;

    // ---- messages
    struct Msg { char text[72]; uint64_t at; bool important; };
    Msg msgs[4];
    int nmsgs = 0;

    // ---- zone query result
    struct Query { int category, s[5], x, y; bool valid; } query;

    // ---- callbacks from the engine
    static void callbackThunk(Micropolis *m, void *data, const char *name, const char *params, va_list args);
    void onCallback(const char *name, const char *params, va_list args);
    void pushMessage(const char *text, bool important);

    // ---- update
    void tickSim(uint64_t now);
    void handlePlayInput(const Input &in, uint64_t now);
    void handleMenuInput(const Input &in);
    void handleBudgetInput(const Input &in);
    void handleDisasterInput(const Input &in);
    void handleHelpInput(const Input &in);
    void handleLoadCityInput(const Input &in);
    void applyTool();
    void centerOn(int tx, int ty);
    void clampCamera();
    int visibleCols() const { return TOP_W / (16 * zoom) + 1; }
    int visibleRows() const { return TOP_H / (16 * zoom) + 1; }

    // ---- render
    void render(float slider);
    void drawCity(Screen &s, int eye, float slider);
    void drawCityPass(Screen &s, int pass, int dx);
    void drawSprites(Screen &s, int dx);
    void drawOverlayTint(Screen &s, int dx);
    void drawCursor(Screen &s, int dx);
    void drawTopHud(Screen &s);
    void drawBottom();
    void drawMinimap(Screen &s, int x0, int y0);
    void drawToolbar(Screen &s, int x0, int y0);
    void drawMessages(Screen &s, int x0, int y0, int w, int h);
    void drawBudget(Screen &s);
    void drawMenu(Screen &s);
    void drawQuery(Screen &s);
    void drawDisasters(Screen &s);
    void drawHelp(Screen &s);
    void drawLoadCity(Screen &s);

    int depthClass(int tileValue) const;
    int toolCost(EditingTool t) const;

    uint8_t *topBuf, *topRBuf, *botBuf;
};
