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
    // The bottom (touch) screen is a flat 2D map you build on; the top screen
    // shows the same spot in stereoscopic 3D plus the city's vital signs.
    int camX = 0, camY = 0;      // top-left tile of the bottom map
    bool zoomOut = false;        // bottom map at 8 px tiles instead of 16
    int curX = 60, curY = 50;    // cursor tile (buttons and touch share it)
    int toolIdx = 9;             // index into kTools (road)
    int prevToolIdx = 9;         // for B: bulldozer toggle
    int extraTool = 11;          // the strip's 8th slot (last tool picked from More)
    bool toolPopup = false;      // the full 16-tool grid over the map
    bool dragging = false;       // A held + d-pad paints
    uint64_t lastTickMs = 0;
    uint64_t lastRepeatMs = 0;
    uint64_t lastEdgeScrollMs = 0;
    int frameCounter = 0;

    // ---- touch gesture state on the bottom map
    enum TouchState { T_NONE, T_PENDING, T_PAINT, T_PLACE, T_PAN, T_DONE } tstate = T_NONE;
    int tStartX = 0, tStartY = 0;          // stylus position at touch-down
    int tTileX = 0, tTileY = 0;            // last tile handled
    int panCamX0 = 0, panCamY0 = 0;
    uint64_t tStartMs = 0;
    long dragFunds0 = 0;                   // funds at the start of a paint drag
    long lastDragCost = 0; uint64_t lastDragAt = 0;
    bool shoulderUsedForPan = false;       // L/R held while panning: don't cycle tools on release

    // ---- alert location (from the engine's message callback)
    int alertX = -1, alertY = -1; uint64_t alertAt = 0;

    enum Mode { MODE_PLAY, MODE_BUDGET, MODE_MENU, MODE_QUERY, MODE_DISASTERS, MODE_HELP, MODE_LOADCITY } mode = MODE_PLAY;
    int helpPage = 0;
    // city files found in <saveDir>/cities for the Load city browser
    static const int kMaxCities = 64;
    char cityNames[kMaxCities][40];
    int nCities = 0, citySel = 0;
    bool hasAutosave = false;              // entry 0 is <saveDir>/autosave.cty
    void scanCities();
    int menuSel = 0;
    int budgetSel = 0;
    // Overlay / minimap modes
    enum Overlay { OV_NONE, OV_POWER, OV_POLLUTION, OV_CRIME, OV_LANDVALUE, OV_TRAFFIC,
                   OV_POPDENSITY, OV_FIRE, OV_POLICE, OV_GROWTH, OV_COUNT };
    int overlay = OV_NONE;
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
    void handleMapTouch(const Input &in, uint64_t now);
    void handleUiTap(int x, int y);
    void handleMenuInput(const Input &in);
    void handleBudgetInput(const Input &in);
    void handleDisasterInput(const Input &in);
    void handleHelpInput(const Input &in);
    void handleLoadCityInput(const Input &in);
    bool applyToolAt(int tx, int ty);      // false when the city ran out of money
    void paintLineTo(int tx, int ty);
    void selectTool(int idx);
    void cycleStripTool(int dir);
    bool siteOk(int tx, int ty) const;     // footprint is buildable and affordable
    void centerOn(int tx, int ty);
    void clampCamera();
    void keepCursorVisible();
    int bts() const { return zoomOut ? 8 : 16; }
    int mapCols() const { return BOT_W / bts(); }
    int mapRows() const { return (MAP_H + bts() - 1) / bts(); }
    void topOrigin(int &ox, int &oy) const;
    int topTs() const;
    void updateTopCamera();
    float topCx = -1.f, topCy = -1.f;   // top close-up centre, world px at 16/tile

    // ---- render
    struct View { int ox, oy, ts, sy0, w, h; };   // world-pixel origin at scale ts
    void render(float slider);
    void drawCity(Screen &s, int eye, float slider);
    void drawTiles(Screen &s, const View &v, int pass, int dx);   // pass -1 = all
    void drawSprites(Screen &s, const View &v, int dx);
    void drawOverlayTint(Screen &s, const View &v, int dx);
    void drawFootprint(Screen &s, const View &v, int dx, bool thick);
    void drawTopHud(Screen &s);
    void drawBottom();
    void drawBottomBar(Screen &s);
    void drawToolStrip(Screen &s);
    void drawToolPopup(Screen &s);
    void drawIcon(Screen &s, int x, int y, int toolIndex, int w, int srcY0, int h);
    void drawMinimap(Screen &s, int x0, int y0, int step);
    void drawBudget(Screen &s);
    void drawMenu(Screen &s);
    void drawQuery(Screen &s);
    void drawDisasters(Screen &s);
    void drawHelp(Screen &s);
    void drawLoadCity(Screen &s);

    int depthClass(int tileValue) const;
    int toolCost(EditingTool t) const;

    // bottom-screen layout
    // rule 11: every button >= 24 px tall, >= 4 px apart, >= 6 px from the screen edges
    static const int BAR_H = 32, MAP_Y0 = 32, MAP_H = 168, STRIP_Y = 200, STRIP_H = 40;
    static const int SLOT_X0 = 6, SLOT_W = 35, SLOT_STEP = 39;          // tool strip: 8 slots
    static const int POP_X0 = 6, POP_W = 74, POP_STEPX = 78, POP_H = 37, POP_STEPY = 41;   // 4 x 4 tool grid

    uint8_t *topBuf, *topRBuf, *botBuf;
};
