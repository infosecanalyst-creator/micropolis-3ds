// Micropolis 3DS frontend: game state, rendering, input.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
#include "app.h"
#include <dirent.h>
#include <strings.h>
#include "fb.h"
#include "../assets/icons_data.h"

// ---------------------------------------------------------------------------
// Static tables
// ---------------------------------------------------------------------------

struct ToolDef { EditingTool tool; const char *icon; const char *name; int cost; int size; const char *desc; };
static const ToolDef kTools[] = {
    { TOOL_RESIDENTIAL,   "res",  "Residential",  100, 3, "Homes. Needs power and a road nearby." },
    { TOOL_COMMERCIAL,    "com",  "Commercial",   100, 3, "Shops, offices. Needs people and jobs." },
    { TOOL_INDUSTRIAL,    "ind",  "Industrial",   100, 3, "Factories: jobs and pollution." },
    { TOOL_FIRESTATION,   "fire", "Fire Dept",    500, 3, "Puts out fires within ~15 tiles." },
    { TOOL_POLICESTATION, "pol",  "Police Dept",  500, 3, "Cuts crime within ~15 tiles." },
    { TOOL_QUERY,         "qry",  "Query",          0, 1, "Inspect a tile: density, value, crime." },
    { TOOL_WIRE,          "wire", "Power Line",     5, 1, "Carries power to zones that need it." },
    { TOOL_BULLDOZER,     "dozr", "Bulldozer",      1, 1, "Clears land, rubble and trees. $1/tile." },
    { TOOL_RAILROAD,      "rail", "Rail",          20, 1, "Moves traffic without congestion." },
    { TOOL_ROAD,          "road", "Road",          10, 1, "Every zone needs a road within 3 tiles." },
    { TOOL_STADIUM,       "stad", "Stadium",     5000, 4, "Lets big cities keep growing." },
    { TOOL_PARK,          "park", "Park",          10, 1, "Raises land value, absorbs pollution." },
    { TOOL_SEAPORT,       "seap", "Seaport",     3000, 4, "Must touch water. Boosts industry." },
    { TOOL_COALPOWER,     "coal", "Coal Power",  3000, 4, "Cheap power for ~20 zones. Pollutes." },
    { TOOL_NUCLEARPOWER,  "nuc",  "Nuclear",     5000, 4, "Clean power, ~60 zones. Meltdown risk." },
    { TOOL_AIRPORT,       "airp", "Airport",    10000, 6, "Lets commerce grow in big cities." },
};
static const int kToolCount = sizeof(kTools) / sizeof(kTools[0]);

static const char *kMessages[] = {
    "", "More residential zones needed.", "More commercial zones needed.",
    "More industrial zones needed.", "More roads required.", "Inadequate rail system.",
    "Build a power plant.", "Residents demand a stadium.", "Industry requires a sea port.",
    "Commerce requires an airport.", "Pollution very high.", "Crime very high.",
    "Frequent traffic jams reported.", "Citizens demand a fire department.",
    "Citizens demand a police department.", "Blackouts reported. Check power map.",
    "Citizens upset. The tax rate is too high.", "Roads deteriorating, due to lack of funds.",
    "Fire departments need funding.", "Police departments need funding.", "Fire reported !",
    "A monster has been sighted !!", "Tornado reported !!", "Major earthquake reported !!!",
    "A plane has crashed !", "Shipwreck reported !", "A train crashed !", "A helicopter crashed !",
    "Unemployment rate is high.", "YOUR CITY HAS GONE BROKE!", "Firebombing reported !",
    "Need more parks.", "Explosion detected !", "Insufficient funds to build that.",
    "Area must be bulldozed first.", "Population has reached 2,000.",
    "Population has reached 10,000.", "Population has reached 50,000.",
    "Population has reached 100,000.", "Population has reached 500,000.",
    "Brownouts, build another power plant.", "Heavy traffic reported.", "Flooding reported !!",
    "A nuclear meltdown has occurred !!!", "They're rioting in the streets !!",
    "Started a new city.", "Restored a saved city.", "You won the scenario!",
    "You lost the scenario.", "About Micropolis.", "Scenario: DULLSVILLE, USA  1900.",
    "Scenario: SAN FRANCISCO, CA.  1906.", "Scenario: HAMBURG, GERMANY  1944.",
    "Scenario: BERN, SWITZERLAND  1965.", "Scenario: TOKYO, JAPAN  1957.",
    "Scenario: DETROIT, MI.  1972.", "Scenario: BOSTON, MA.  2010.",
    "Scenario: RIO DE JANEIRO, BRAZIL  2047.",
};
static const int kMessageCount = sizeof(kMessages) / sizeof(kMessages[0]);

static const char *kZoneCategories[] = {
    "Clear", "Water", "Trees", "Rubble", "Flood", "Radioactive Waste", "Fire", "Road",
    "Power", "Rail", "Residential", "Commercial", "Industrial", "Seaport", "Airport",
    "Coal Power", "Fire Department", "Police Department", "Stadium", "Nuclear Power",
    "Draw Bridge", "Radar Dish", "Fountain", "Industrial", "Steelers 38  Bears 3",
    "Draw Bridge", "Ur 238",
};
static const char *kZoneLevels[] = {
    "Low", "Medium", "High", "Very High",
    "Slum", "Lower Class", "Middle Class", "High",
    "Safe", "Light", "Moderate", "Dangerous",
    "None", "Moderate", "Heavy", "Very Heavy",
    "Declining", "Stable", "Slow Growth", "Fast Growth",
};

static const char *kScenarioNames[] = {
    "", "Dullsville 1900", "San Francisco 1906", "Hamburg 1944", "Bern 1965",
    "Tokyo 1957", "Detroit 1972", "Boston 2010", "Rio de Janeiro 2047",
};

// Depth layers for stereoscopic rendering (see drawCity). Buildings rise
// with zone density, so downtown towers pop further than suburbs.
enum { DEPTH_WATER = 0, DEPTH_GROUND = 1, DEPTH_LOW = 2, DEPTH_MID = 3, DEPTH_HIGH = 4,
       DEPTH_TOWER = 5, DEPTH_SPRITE = 6, DEPTH_COUNT = 7 };
static const float kParallaxPx = 0.9f;   // per layer at full slider (at zoom 1)

// Minimap colours per depth/tile class
static const uint8_t kMiniWater[3] = { 30, 70, 160 }, kMiniLand[3] = { 60, 120, 50 },
                     kMiniTree[3] = { 20, 80, 30 }, kMiniRoad[3] = { 110, 110, 110 },
                     kMiniZone[3] = { 200, 180, 90 }, kMiniBig[3] = { 230, 230, 230 },
                     kMiniFire[3] = { 240, 60, 20 };

// ---------------------------------------------------------------------------

App::App() {
    topBuf = new uint8_t[TOP_W * TOP_H * 3];
    topRBuf = new uint8_t[TOP_W * TOP_H * 3];
    botBuf = new uint8_t[BOT_W * BOT_H * 3];
    top = { TOP_W, TOP_H, topBuf };
    topR = { TOP_W, TOP_H, topRBuf };
    bottom = { BOT_W, BOT_H, botBuf };
    memset(&query, 0, sizeof(query));
    memset(msgs, 0, sizeof(msgs));

    // The constructor already ran init(); initGame() does the rest and the
    // engine's own simInit() runs from generateSomeCity()/loadCity().
    sim.resourceDir = platform::resDir();
    sim.callbackHook = callbackThunk;
    sim.callbackData = this;
    sim.initGame();
    sim.autoBudget = true;
    sim.setEnableDisasters(true);
    sim.setEnableSound(false);
}

App::~App() {
    delete[] topBuf;
    delete[] topRBuf;
    delete[] botBuf;
}

bool App::newCity(int seed) {
    if (seed == 0)
        seed = (int)(platform::millis() & 0x7fffffff);
    sim.generateSomeCity(seed);
    sim.setGameLevelFunds(LEVEL_EASY);
    sim.setSpeed(2);
    centerOn(WORLD_W / 2, WORLD_H / 2);
    pushMessage("Started a new city.", false);
    return true;
}

bool App::loadCity(const char *path) {
    if (!sim.loadCity(path)) {
        pushMessage("Could not load city.", true);
        return false;
    }
    sim.setSpeed(2);
    centerOn(WORLD_W / 2, WORLD_H / 2);
    pushMessage("Restored a saved city.", false);
    return true;
}

bool App::loadScenario(int which) {
    if (which < 1 || which > 8)
        return false;
    sim.loadScenario((Scenario)which);
    sim.setSpeed(2);
    centerOn(WORLD_W / 2, WORLD_H / 2);
    return true;
}

// ---------------------------------------------------------------------------
// Engine callbacks
// ---------------------------------------------------------------------------

void App::callbackThunk(Micropolis *, void *data, const char *name, const char *params, va_list args) {
    static_cast<App *>(data)->onCallback(name, params, args);
}

void App::onCallback(const char *name, const char *params, va_list args) {
    if (!strcmp(name, "update")) {
        const char *what = va_arg(args, const char *);
        if (!strcmp(what, "message") && !strcmp(params, "sdddbb")) {
            const int num = va_arg(args, int);
            const int mx = va_arg(args, int), my = va_arg(args, int);
            va_arg(args, int);                       // picture
            const int important = va_arg(args, int);
            if (num > 0 && num < kMessageCount)
                pushMessage(kMessages[num], important != 0);
            if (mx >= 0 && my >= 0 && mx < WORLD_W && my < WORLD_H) {
                alertX = mx; alertY = my; alertAt = platform::millis();
            }
        }
    } else if (!strcmp(name, "autoGoto")) {
        const int x = va_arg(args, int), y = va_arg(args, int);
        alertX = x; alertY = y; alertAt = platform::millis();
        // don't yank the map away while the player is mid-stroke
        if (tstate == T_NONE) centerOn(x, y);
    } else if (!strcmp(name, "showZoneStatus")) {
        query.category = va_arg(args, int);
        for (int i = 0; i < 5; ++i) query.s[i] = va_arg(args, int);
        query.x = va_arg(args, int);
        query.y = va_arg(args, int);
        query.valid = true;
        mode = MODE_QUERY;
    } else if (!strcmp(name, "showBudgetAndWait")) {
        // autoBudget is on, so this only fires from the menu; show the screen
        mode = MODE_BUDGET;
    } else if (!strcmp(name, "winGame")) {
        pushMessage("You won the scenario!", true);
    } else if (!strcmp(name, "loseGame")) {
        pushMessage("You lost the scenario.", true);
    } else if (!strcmp(name, "didLoadScenario")) {
        if (sim.scenario > 0 && sim.scenario <= 8)
            pushMessage(kScenarioNames[sim.scenario], true);
    }
}

void App::pushMessage(const char *text, bool important) {
    if (nmsgs > 0 && !strcmp(msgs[0].text, text)) {
        msgs[0].at = platform::millis();
        return;
    }
    for (int i = 3; i > 0; --i) msgs[i] = msgs[i - 1];
    strncpy(msgs[0].text, text, sizeof(msgs[0].text) - 1);
    msgs[0].text[sizeof(msgs[0].text) - 1] = 0;
    msgs[0].at = platform::millis();
    msgs[0].important = important;
    if (nmsgs < 4) ++nmsgs;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void App::tickSim(uint64_t now) {
    // ~16 sim ticks a second, like the original 60 ms timer; the engine
    // throttles itself per speed setting inside simFrame().
    if (now - lastTickMs >= 62) {
        lastTickMs = now;
        sim.simTick();
        if (sim.doAnimation && !sim.tilesAnimated)
            sim.animateTiles();
    }
}

// The strip under the map: the seven tools you use all game, plus one slot
// that remembers whatever you last picked from the full grid ("More").
static const int kStripTools[7] = { 7 /*dozer*/, 9 /*road*/, 8 /*rail*/, 6 /*wire*/,
                                    0 /*res*/, 1 /*com*/, 2 /*ind*/ };
static int stripSlotOf(int toolIdx) {
    for (int i = 0; i < 7; ++i) if (kStripTools[i] == toolIdx) return i;
    return -1;
}
static const int kHoldQueryMs = 550;   // press and hold on the map: query that tile
static const int kEdgeScrollPx = 12;   // dragging near the map edge scrolls it

void App::centerOn(int tx, int ty) {
    camX = tx - mapCols() / 2;
    camY = ty - (MAP_H / bts()) / 2;
    curX = tx;
    curY = ty;
    clampCamera();
}

void App::clampCamera() {
    const int maxX = WORLD_W - mapCols();
    const int maxY = WORLD_H - mapRows();
    if (camX > maxX) camX = maxX;
    if (camY > maxY) camY = maxY;
    if (camX < 0) camX = 0;
    if (camY < 0) camY = 0;
    if (curX < 0) curX = 0; if (curX >= WORLD_W) curX = WORLD_W - 1;
    if (curY < 0) curY = 0; if (curY >= WORLD_H) curY = WORLD_H - 1;
}

void App::keepCursorVisible() {
    const int cols = mapCols(), rows = MAP_H / bts();
    if (curX < camX + 1) camX = curX - 1;
    if (curX > camX + cols - 2) camX = curX - cols + 2;
    if (curY < camY + 1) camY = curY - 1;
    if (curY > camY + rows - 2) camY = curY - rows + 2;
    clampCamera();
}

// The 3D view on top is a close-up (twice the bottom map's scale) that
// glides after the cursor, so it always shows what you are building.
int App::topTs() const { return zoomOut ? 16 : 32; }

void App::topOrigin(int &ox, int &oy) const {
    const int ts = topTs();
    ox = (int)(topCx * ts / 16) - TOP_W / 2;
    oy = (int)(topCy * ts / 16) - TOP_H / 2;
    if (ox > WORLD_W * ts - TOP_W) ox = WORLD_W * ts - TOP_W;
    if (oy > WORLD_H * ts - TOP_H) oy = WORLD_H * ts - TOP_H;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;
}

void App::updateTopCamera() {
    const float tx = curX * 16 + 8.f, ty = curY * 16 + 8.f;
    if (topCx < 0.f || fabsf(tx - topCx) > 16 * 40 || fabsf(ty - topCy) > 16 * 30) { topCx = tx; topCy = ty; return; }
    topCx += (tx - topCx) * 0.25f;
    topCy += (ty - topCy) * 0.25f;
}

int App::toolCost(EditingTool t) const {
    for (int i = 0; i < kToolCount; ++i)
        if (kTools[i].tool == t) return kTools[i].cost;
    return 0;
}

bool App::siteOk(int tx, int ty) const {
    const ToolDef &t = kTools[toolIdx];
    if (t.tool == TOOL_QUERY) return true;
    if (sim.totalFunds < t.cost) return false;
    if (t.size == 1) return true;
    const int off = (t.size >= 3) ? 1 : 0;
    for (int y = ty - off; y < ty - off + t.size; ++y)
        for (int x = tx - off; x < tx - off + t.size; ++x) {
            if (x < 0 || y < 0 || x >= WORLD_W || y >= WORLD_H) return false;
            const int v = sim.map[x][y] & LOMASK;
            if (v == DIRT) continue;
            // same rule as Micropolis::tally(): trees, rubble, shore and stray wire auto-clear
            const bool clears = (v >= FIRSTRIVEDGE && v <= LASTRUBBLE) ||
                                (v >= POWERBASE + 2 && v <= POWERBASE + 12) ||
                                (v >= TINYEXP && v <= LASTTINYEXP + 2);
            if (!sim.autoBulldoze || !clears) return false;
        }
    return true;
}

bool App::applyToolAt(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= WORLD_W || ty >= WORLD_H) return true;
    const ToolDef &t = kTools[toolIdx];
    const ToolResult r = sim.doTool(t.tool, (short)tx, (short)ty);
    if (r == TOOLRESULT_NO_MONEY) { pushMessage("Insufficient funds to build that.", true); return false; }
    if (r == TOOLRESULT_NEED_BULLDOZE) pushMessage("Area must be bulldozed first.", true);
    return true;
}

// Paint road / rail / wire / bulldozer / park from the last tile to (tx,ty)
// in 4-connected steps, so a fast stroke still leaves a connected line.
void App::paintLineTo(int tx, int ty) {
    int x = tTileX, y = tTileY;
    while (x != tx || y != ty) {
        if (abs(tx - x) >= abs(ty - y)) x += (tx > x) ? 1 : -1;
        else y += (ty > y) ? 1 : -1;
        if (!applyToolAt(x, y)) { tstate = T_DONE; break; }
    }
    tTileX = tx; tTileY = ty;
}

void App::selectTool(int idx) {
    if (idx < 0 || idx >= kToolCount) return;
    if (idx != 7) prevToolIdx = idx;
    toolIdx = idx;
    if (stripSlotOf(idx) < 0) extraTool = idx;
}

void App::cycleStripTool(int dir) {
    int slot = stripSlotOf(toolIdx);
    if (slot < 0) slot = 7;
    slot = (slot + 8 + dir) % 8;
    selectTool(slot < 7 ? kStripTools[slot] : extraTool);
}

// Taps on the bar above the map and the tool strip below it.
void App::handleUiTap(int x, int y) {
    if (y < BAR_H) {
        if (x < 80) { mode = MODE_BUDGET; budgetSel = 0; }                    // funds: budget
        else if (x >= 140 && x < 184) { int sp = sim.simSpeed + 1; if (sp > 3) sp = 0; sim.setSpeed((short)sp); }
        else if (x >= 184 && x < 262) overlay = (overlay + 1) % OV_COUNT;      // overlay cycle
        else if (x >= 262 && x < 288) {                                       // jump to alert
            if (alertX >= 0 && platform::millis() - alertAt < 30000) centerOn(alertX, alertY);
        } else if (x >= 288) { mode = MODE_MENU; menuSel = 0; }
        return;
    }
    if (y >= STRIP_Y) {
        const int slot = x / 40;
        if (slot >= 0 && slot < 7) { selectTool(kStripTools[slot]); toolPopup = false; }
        else if (slot == 7) {
            if (toolIdx == extraTool && !toolPopup) toolPopup = true;
            else if (toolPopup) toolPopup = false;
            else selectTool(extraTool);
        }
    }
}

void App::handleMapTouch(const Input &in, uint64_t now) {
    const int ts = bts();
    const bool inMap = in.touchY >= MAP_Y0 && in.touchY < STRIP_Y;
    const int tileX = camX + in.touchX / ts, tileY = camY + (in.touchY - MAP_Y0) / ts;
    const ToolDef &tool = kTools[toolIdx];
    const bool paintTool = tool.size == 1 && tool.tool != TOOL_QUERY;

    if (in.touchDown) {
        tStartX = in.touchX; tStartY = in.touchY; tStartMs = now;
        if (toolPopup) {
            toolPopup = false;
            if (inMap) {
                // the grid: 4 x 4 cells of 80 x 47 over the map area
                const int col = in.touchX / 80, row = (in.touchY - MAP_Y0) / 47;
                if (col >= 0 && col < 4 && row >= 0 && row < 4) selectTool(row * 4 + col);
                tstate = T_DONE;
                return;
            }
        }
        if (!inMap) { handleUiTap(in.touchX, in.touchY); tstate = T_DONE; return; }
        if (in.held & (BTN_L | BTN_R)) {
            tstate = T_PAN; panCamX0 = camX; panCamY0 = camY; shoulderUsedForPan = true;
            return;
        }
        tstate = T_PENDING;
        tTileX = tileX; tTileY = tileY;
        curX = tileX; curY = tileY; clampCamera();
        dragFunds0 = sim.totalFunds;
        return;
    }

    if (!in.touch) {   // stylus lifted
        if (tstate == T_PENDING || tstate == T_PLACE) {
            if (tool.tool == TOOL_QUERY || tstate == T_PLACE || !paintTool) applyToolAt(curX, curY);
            else applyToolAt(tTileX, tTileY);
        }
        if (tstate == T_PAINT || tstate == T_PENDING) {
            lastDragCost = dragFunds0 - sim.totalFunds;
            lastDragAt = now;
        }
        tstate = T_NONE;
        return;
    }

    // stylus held and moving
    switch (tstate) {
    case T_PAN: {
        camX = panCamX0 + (tStartX - in.touchX) / ts;
        camY = panCamY0 + (tStartY - in.touchY) / ts;
        clampCamera();
        return;
    }
    case T_PENDING:
        if (tileX != tTileX || tileY != tTileY) {
            if (paintTool) {
                if (!applyToolAt(tTileX, tTileY)) { tstate = T_DONE; return; }
                tstate = T_PAINT;
                paintLineTo(tileX, tileY);
            } else {
                tstate = T_PLACE;
            }
            curX = tileX; curY = tileY; clampCamera();
        } else if (now - tStartMs > (uint64_t)kHoldQueryMs && tool.tool != TOOL_QUERY) {
            // press and hold: what is this?
            sim.doTool(TOOL_QUERY, (short)tileX, (short)tileY);
            tstate = T_DONE;
        }
        return;
    case T_PAINT:
    case T_PLACE: {
        // near the map edge: scroll the map under the stylus
        if (now - lastEdgeScrollMs > 110) {
            int ex = 0, ey = 0;
            if (in.touchX < kEdgeScrollPx) ex = -1; else if (in.touchX >= BOT_W - kEdgeScrollPx) ex = 1;
            if (in.touchY < MAP_Y0 + kEdgeScrollPx) ey = -1; else if (in.touchY >= STRIP_Y - kEdgeScrollPx) ey = 1;
            if (ex || ey) { camX += ex; camY += ey; clampCamera(); lastEdgeScrollMs = now; }
        }
        int nx = camX + in.touchX / ts, ny = camY + (std::min(std::max(in.touchY, MAP_Y0), STRIP_Y - 1) - MAP_Y0) / ts;
        if (nx < 0) nx = 0; if (nx >= WORLD_W) nx = WORLD_W - 1;
        if (ny < 0) ny = 0; if (ny >= WORLD_H) ny = WORLD_H - 1;
        if (tstate == T_PAINT && (nx != tTileX || ny != tTileY)) paintLineTo(nx, ny);
        curX = nx; curY = ny;
        return;
    }
    default:
        return;   // T_DONE / T_NONE (a touch left over from a menu): ignore until lifted
    }
}

void App::handlePlayInput(const Input &in, uint64_t now) {
    // --- cursor / camera
    int dx = 0, dy = 0;
    if (in.repeat & BTN_LEFT) dx = -1;
    if (in.repeat & BTN_RIGHT) dx = 1;
    if (in.repeat & BTN_UP) dy = -1;
    if (in.repeat & BTN_DOWN) dy = 1;
    // circle pad scrolls the map continuously (faster when pushed fully)
    if (abs(in.cpadX) > 40 || abs(in.cpadY) > 40) {
        const int interval = (abs(in.cpadX) > 130 || abs(in.cpadY) > 130) ? 35 : 70;
        if (now - lastRepeatMs > (uint64_t)interval) {
            lastRepeatMs = now;
            const int sx = (in.cpadX > 40) - (in.cpadX < -40), sy = (in.cpadY < -40) - (in.cpadY > 40);
            camX += sx; camY += sy; curX += sx; curY += sy;
            clampCamera();
        }
    }
    if (dx || dy) {
        curX += dx; curY += dy;
        clampCamera();
        keepCursorVisible();
        if (dragging) applyToolAt(curX, curY);   // A held while moving paints the path
    }

    // --- buttons
    if (in.down & BTN_A) { applyToolAt(curX, curY); dragging = true; }
    if (!(in.held & BTN_A)) dragging = false;
    // L / R: previous / next tool in the strip -- on release, so L/R + drag can pan
    static uint32_t prevHeld = 0;
    const uint32_t released = prevHeld & ~in.held;
    prevHeld = in.held;
    if (released & (BTN_L | BTN_R)) {
        if (!shoulderUsedForPan) cycleStripTool((released & BTN_L) ? -1 : 1);
        if (!(in.held & (BTN_L | BTN_R))) shoulderUsedForPan = false;
    }
    if (in.down & BTN_B) {
        if (toolPopup) toolPopup = false;
        else if (toolIdx == 7) toolIdx = prevToolIdx;   // B again: back to what you had
        else selectTool(7);
    }
    if (in.down & BTN_Y) { zoomOut = !zoomOut; centerOn(curX, curY); }
    if (in.down & BTN_X) { mode = MODE_BUDGET; budgetSel = 0; }
    if (in.down & BTN_START) { mode = MODE_MENU; menuSel = 0; toolPopup = false; }
    if (in.down & BTN_ZL) overlay = (overlay + OV_COUNT - 1) % OV_COUNT;
    if (in.down & BTN_ZR) overlay = (overlay + 1) % OV_COUNT;
    if (in.down & BTN_SELECT) {
        int sp = sim.simSpeed + 1;
        if (sp > 3) sp = 0;
        sim.setSpeed((short)sp);
    }

    // --- stylus
    if (in.touch || tstate != T_NONE) handleMapTouch(in, now);
}

void App::handleMenuInput(const Input &in) {
    const int items = 14; // new city, 8 scenarios, load, disasters, save, help, resume
    if (in.repeat & BTN_UP) menuSel = (menuSel + items - 1) % items;
    if (in.repeat & BTN_DOWN) menuSel = (menuSel + 1) % items;
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_PLAY; tstate = T_DONE; return; }
    bool pick = (in.down & BTN_A) != 0;
    if (in.touchDown) {
        const int i = (in.touchY - 33) / 14;
        if (in.touchX >= 40 && in.touchX < 280 && in.touchY >= 33 && i >= 0 && i < items) { menuSel = i; pick = true; }
        else if (in.touchX < 40 || in.touchX >= 280) { mode = MODE_PLAY; tstate = T_DONE; return; }   // tap outside: close
    }
    if (pick) {
        tstate = T_DONE;
        if (menuSel == 0) { newCity(0); mode = MODE_PLAY; }
        else if (menuSel >= 1 && menuSel <= 8) { loadScenario(menuSel); mode = MODE_PLAY; }
        else if (menuSel == 9) { scanCities(); citySel = 0; mode = MODE_LOADCITY; }
        else if (menuSel == 10) { mode = MODE_DISASTERS; menuSel = 0; }
        else if (menuSel == 12) { helpPage = 0; mode = MODE_HELP; }
        else if (menuSel == 11) {
            char path[256];
            snprintf(path, sizeof(path), "%s/autosave.cty", platform::saveDir());
            sim.saveCityAs(path);
            pushMessage("City saved.", false);
            mode = MODE_PLAY;
        } else mode = MODE_PLAY;
    }
}

void App::handleDisasterInput(const Input &in) {
    const int items = 7;
    if (in.repeat & BTN_UP) menuSel = (menuSel + items - 1) % items;
    if (in.repeat & BTN_DOWN) menuSel = (menuSel + 1) % items;
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_PLAY; tstate = T_DONE; return; }
    bool pick = (in.down & BTN_A) != 0;
    if (in.touchDown) {
        const int i = (in.touchY - 73) / 14;
        if (in.touchX >= 60 && in.touchX < 260 && in.touchY >= 73 && i >= 0 && i < items) { menuSel = i; pick = true; }
        else { mode = MODE_PLAY; tstate = T_DONE; return; }
    }
    if (pick) {
        tstate = T_DONE;
        switch (menuSel) {
        case 0: sim.makeFire(); break;
        case 1: sim.makeFlood(); break;
        case 2: sim.makeTornado(); break;
        case 3: sim.makeEarthquake(); break;
        case 4: sim.makeMonster(); break;
        case 5: sim.makeMeltdown(); break;
        default: break;
        }
        mode = MODE_PLAY;
    }
}

void App::handleBudgetInput(const Input &in) {
    if (in.repeat & BTN_UP) budgetSel = (budgetSel + 3) % 4;
    if (in.repeat & BTN_DOWN) budgetSel = (budgetSel + 1) % 4;
    int delta = 0;
    if (in.repeat & BTN_LEFT) delta = -1;
    if (in.repeat & BTN_RIGHT) delta = 1;
    if (in.touchDown) {
        const int x = in.touchX, y = in.touchY;
        const int row = (y - 73) / 22;
        if (y >= 73 && row >= 0 && row < 4 && x >= 20 && x < 300) {
            budgetSel = row;
            if (x >= 230 && x < 258) delta = -1;
            else if (x >= 262 && x < 290) delta = 1;
        } else if (y >= 186 && y < 206 && x >= 230 && x < 290) {
            mode = MODE_PLAY; tstate = T_DONE; return;                                   // Done
        } else if (x < 20 || x >= 300 || y < 30 || y >= 210) {
            mode = MODE_PLAY; tstate = T_DONE; return;                                   // tap outside
        }
    }
    if (delta) {
        switch (budgetSel) {
        case 0: { int t = sim.cityTax + delta; if (t < 0) t = 0; if (t > 20) t = 20; sim.setCityTax((short)t); break; }
        case 1: sim.roadPercent = (float)((int)(sim.roadPercent * 100 + delta * 5 + 0.5f) / 100.0f); break;
        case 2: sim.policePercent = (float)((int)(sim.policePercent * 100 + delta * 5 + 0.5f) / 100.0f); break;
        case 3: sim.firePercent = (float)((int)(sim.firePercent * 100 + delta * 5 + 0.5f) / 100.0f); break;
        }
        if (sim.roadPercent < 0) sim.roadPercent = 0; if (sim.roadPercent > 1) sim.roadPercent = 1;
        if (sim.policePercent < 0) sim.policePercent = 0; if (sim.policePercent > 1) sim.policePercent = 1;
        if (sim.firePercent < 0) sim.firePercent = 0; if (sim.firePercent > 1) sim.firePercent = 1;
        sim.updateFundEffects();
    }
    if (in.down & (BTN_A | BTN_B | BTN_X | BTN_START)) { mode = MODE_PLAY; tstate = T_DONE; }
}

void App::frame(const Input &in) {
    const uint64_t now = platform::millis();
    ++frameCounter;

    switch (mode) {
    case MODE_PLAY: handlePlayInput(in, now); break;
    case MODE_MENU: handleMenuInput(in); break;
    case MODE_BUDGET: handleBudgetInput(in); break;
    case MODE_DISASTERS: handleDisasterInput(in); break;
    case MODE_HELP: handleHelpInput(in); break;
    case MODE_LOADCITY: handleLoadCityInput(in); break;
    case MODE_QUERY: if ((in.down & (BTN_A | BTN_B)) || in.touchDown) { mode = MODE_PLAY; query.valid = false; tstate = T_DONE; } break;
    }

    if (mode == MODE_PLAY || mode == MODE_QUERY)
        tickSim(now);
    updateTopCamera();

    render(in.slider);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

int App::depthClass(int v) const {
    const int t = v & LOMASK;
    if (t >= WATER_LOW && t <= WATER_HIGH) return DEPTH_GROUND;   // water sits on the screen plane: no gaps at shorelines
    if (t == DIRT) return DEPTH_GROUND;
    if (t >= WOODS_LOW && t <= WOODS_HIGH) return DEPTH_LOW;      // trees
    if (t < RESBASE) return DEPTH_GROUND;                           // rubble, roads, rail, wire, fire
    if (t <= HHTHR) return DEPTH_LOW;                               // empty res zone, single houses
    if (t >= RZB - 4 && t < HOSPITAL - 4) {                         // residential 3x3, 4 densities
        const int den = ((t - (RZB - 4)) / 9) % 4;
        return DEPTH_LOW + (den >= 3 ? 2 : den >= 1 ? 1 : 0);
    }
    if (t < COMBASE) return DEPTH_MID;                              // hospital, church
    if (t <= COMBASE + 8) return DEPTH_LOW;                         // empty commercial
    if (t <= COMLAST) {                                             // commercial, 5 densities
        const int den = ((t - (CZB - 4)) / 9) % 5;
        return den >= 4 ? DEPTH_TOWER : den >= 2 ? DEPTH_HIGH : den >= 1 ? DEPTH_MID : DEPTH_LOW;
    }
    if (t <= LASTIND) return DEPTH_LOW;                             // empty industrial
    if (t < PORTBASE) {                                             // industrial, 4 densities
        const int den = ((t - (IZB - 4)) / 9) % 4;
        return den >= 2 ? DEPTH_HIGH : DEPTH_MID;
    }
    if (t >= STADIUMBASE && t <= STADIUMBASE + 15) return DEPTH_HIGH;
    if (t >= NUCLEARBASE && t <= LASTZONE) return DEPTH_HIGH;
    if (t >= COALBASE && t <= LASTPOWERPLANT) return DEPTH_HIGH;
    if (t >= PORTBASE && t <= LASTZONE) return DEPTH_MID;          // seaport, airport, stations
    return DEPTH_MID;
}

static const char *kOverlayNames[] = {
    "City", "Power", "Pollution", "Crime", "Land value", "Traffic", "Density", "Fire cover",
    "Police cover", "Growth",
};

int App::overlayMax() const {
    int m = 1;
    for (int y = 0; y < WORLD_H; y += 2)
        for (int x = 0; x < WORLD_W; x += 2) {
            int v = 0;
            switch (overlay) {
            case OV_POLLUTION:  v = sim.pollutionDensityMap.worldGet(x, y); break;
            case OV_CRIME:      v = sim.crimeRateMap.worldGet(x, y); break;
            case OV_LANDVALUE:  v = sim.landValueMap.worldGet(x, y); break;
            case OV_TRAFFIC:    v = sim.trafficDensityMap.worldGet(x, y); break;
            case OV_POPDENSITY: v = sim.populationDensityMap.worldGet(x, y); break;
            default: return 255;
            }
            if (v > m) m = v;
        }
    return m;
}

int App::stretch(int v) const {
    const int s = v * 255 / overlayMaxCached;
    return s > 255 ? 255 : s;
}

int App::overlayValue(int tx, int ty) const {
    switch (overlay) {
    case OV_POWER: {
        const unsigned short v = sim.map[tx][ty];
        if (!(v & CONDBIT)) return -1;
        return (v & PWRBIT) ? 255 : 40;
    }
    // the density maps are 0..~250 in theory but rarely exceed 100 in play;
    // stretch to the map's current maximum so the ramp is always readable
    case OV_POLLUTION:  return stretch(sim.pollutionDensityMap.worldGet(tx, ty));
    case OV_CRIME:      return stretch(sim.crimeRateMap.worldGet(tx, ty));
    case OV_LANDVALUE:  return stretch(sim.landValueMap.worldGet(tx, ty));
    case OV_TRAFFIC:    return stretch(sim.trafficDensityMap.worldGet(tx, ty));
    case OV_POPDENSITY: return stretch(sim.populationDensityMap.worldGet(tx, ty));
    case OV_FIRE:       { int v = sim.fireStationEffectMap.worldGet(tx, ty); return v > 255 ? 255 : v; }
    case OV_POLICE:     { int v = sim.policeStationEffectMap.worldGet(tx, ty); return v > 255 ? 255 : v; }
    case OV_GROWTH:     { int v = sim.rateOfGrowthMap.worldGet(tx, ty); v = 128 + v * 2; return v < 0 ? 0 : v > 255 ? 255 : v; }
    default:            return -1;
    }
}

// Heat ramp: blue (low) -> green -> yellow -> red (high). Growth uses a
// diverging ramp around 128 instead.
void App::overlayColour(int v, uint8_t *rgb) const {
    if (overlay == OV_GROWTH) {
        const int d = v - 128;
        if (d < 0) { rgb[0] = 200; rgb[1] = (uint8_t)(200 + d * 1.5f); rgb[2] = (uint8_t)(200 + d * 1.5f); }
        else       { rgb[0] = (uint8_t)(200 - d * 1.5f); rgb[1] = 200; rgb[2] = (uint8_t)(200 - d * 1.5f); }
        return;
    }
    if (overlay == OV_POWER) {
        if (v > 128) { rgb[0] = 255; rgb[1] = 230; rgb[2] = 60; }
        else         { rgb[0] = 60;  rgb[1] = 60;  rgb[2] = 220; }
        return;
    }
    if (v < 85)       { rgb[0] = 0;   rgb[1] = (uint8_t)(v * 3);          rgb[2] = (uint8_t)(255 - v * 3); }
    else if (v < 170) { rgb[0] = (uint8_t)((v - 85) * 3); rgb[1] = 255;   rgb[2] = 0; }
    else              { rgb[0] = 255; rgb[1] = (uint8_t)(255 - (v - 170) * 3); rgb[2] = 0; }
}

static inline void drawTileAt(Screen &s, int sx, int sy, int t, int ts, bool dim = false) {
    if (ts == 16) fb::tile(s, sx, sy, t, dim);
    else if (ts == 8) fb::tileHalf(s, sx, sy, t, dim);
    else fb::tile2x(s, sx, sy, t);
}
static inline int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Draw every visible cell whose depth class matches `pass` (-1: every cell,
// for the flat map). The GROUND pass also lays a base tile under elevated
// cells so the stereo parallax never exposes black.
void App::drawTiles(Screen &s, const View &v, int pass, int dx) {
    const int ts = v.ts;
    const int c0 = floorDiv(v.ox, ts) - 1, c1 = (v.ox + v.w) / ts + 1;
    const int r0 = floorDiv(v.oy, ts), r1 = (v.oy + v.h - 1) / ts;
    for (int ty = r0; ty <= r1; ++ty) {
        if (ty < 0 || ty >= WORLD_H) continue;
        const int sy = v.sy0 + ty * ts - v.oy;
        for (int tx = c0; tx <= c1; ++tx) {
            if (tx < 0 || tx >= WORLD_W) continue;
            const unsigned short val = sim.map[tx][ty];
            const int t = val & LOMASK;
            const int sx = tx * ts - v.ox + dx;
            // unpowered zone centres blink in the original; show a dim variant
            const bool dim = (val & ZONEBIT) && !(val & PWRBIT) && (sim.blinkFlag < 0);
            if (pass < 0) { drawTileAt(s, sx, sy, t, ts, dim); continue; }
            const int cls = depthClass(val);
            if (cls == pass) {
                drawTileAt(s, sx, sy, t, ts, dim);
            } else if (pass == DEPTH_GROUND && cls > DEPTH_GROUND) {
                int fill = t;
                for (int d = 1; d <= 2; ++d) {
                    if (tx - d >= 0 && depthClass(sim.map[tx - d][ty]) <= DEPTH_GROUND) { fill = sim.map[tx - d][ty] & LOMASK; break; }
                    if (tx + d < WORLD_W && depthClass(sim.map[tx + d][ty]) <= DEPTH_GROUND) { fill = sim.map[tx + d][ty] & LOMASK; break; }
                }
                drawTileAt(s, sx, sy, fill, ts);
            }
        }
    }
}

// Blend the overlay colour over each visible tile (checkerboard, so the city
// stays legible underneath).
void App::drawOverlayTint(Screen &s, const View &v, int dx) {
    const int ts = v.ts;
    const int c0 = floorDiv(v.ox, ts) - 1, c1 = (v.ox + v.w) / ts + 1;
    const int r0 = floorDiv(v.oy, ts), r1 = (v.oy + v.h - 1) / ts;
    for (int ty = r0; ty <= r1; ++ty) {
        if (ty < 0 || ty >= WORLD_H) continue;
        for (int tx = c0; tx <= c1; ++tx) {
            if (tx < 0 || tx >= WORLD_W) continue;
            const int val = overlayValue(tx, ty);
            if (val < 0) continue;
            uint8_t rgb[3]; overlayColour(val, rgb);
            const int sx = tx * ts - v.ox + dx, sy = v.sy0 + ty * ts - v.oy;
            for (int y = 0; y < ts; ++y) {
                const int yy = sy + y; if (yy < v.sy0 || yy >= v.sy0 + v.h || yy >= s.h) continue;
                for (int x = (y & 1); x < ts; x += 2) {
                    const int xx = sx + x; if (xx < 0 || xx >= s.w) continue;
                    uint8_t *p = s.px(xx, yy);
                    p[0] = (p[0] + rgb[0]) / 2; p[1] = (p[1] + rgb[1]) / 2; p[2] = (p[2] + rgb[2]) / 2;
                }
            }
        }
    }
}

void App::drawSprites(Screen &s, const View &v, int dx) {
    for (SimSprite *sp = sim.spriteList; sp; sp = sp->next) {
        if (sp->frame == 0) continue;
        const int sx = sp->x * v.ts / 16 - v.ox + dx;
        const int sy = v.sy0 + sp->y * v.ts / 16 - v.oy;
        if (v.ts == 8) fb::spriteHalf(s, sx, sy, sp->type, sp->frame);
        else fb::sprite(s, sx, sy, sp->type, sp->frame, v.ts / 16);
    }
}

// The selected tool's footprint at the cursor: yellow when it can be built
// there, red when the site is blocked or the city can't afford it.
void App::drawFootprint(Screen &s, const View &v, int dx, bool thick) {
    const int ts = v.ts;
    const ToolDef &t = kTools[toolIdx];
    const int size = t.size, off = (size >= 3) ? 1 : 0;
    const int x = (curX - off) * ts - v.ox + dx, y = v.sy0 + (curY - off) * ts - v.oy;
    const bool ok = siteOk(curX, curY);
    const bool blink = (frameCounter / 15) & 1;
    const uint8_t hi = blink ? 255 : 190;
    const uint8_t r = hi, g = ok ? hi : 60, b = ok ? 40 : 40;
    fb::rectOutline(s, x - 1, y - 1, size * ts + 2, size * ts + 2, r, g, b);
    if (thick) fb::rectOutline(s, x - 2, y - 2, size * ts + 4, size * ts + 4, r, g, b);
    fb::rectOutline(s, x, y, size * ts, size * ts, 0, 0, 0);
}

void App::drawCity(Screen &s, int eye, float slider) {
    fb::clear(s, 0, 0, 0);
    View v; topOrigin(v.ox, v.oy); v.ts = topTs(); v.sy0 = 0; v.w = TOP_W; v.h = TOP_H;
    const float parScale = v.ts == 32 ? 1.5f : 1.f;
    // The cursor sits on the surface it points at: the ground for empty land,
    // roads and water, or the top of the tallest building under its footprint.
    const ToolDef &tool = kTools[toolIdx];
    const int off = (tool.size >= 3) ? 1 : 0;
    int cursorPass = DEPTH_GROUND;
    for (int y = curY - off; y < curY - off + tool.size; ++y)
        for (int x = curX - off; x < curX - off + tool.size; ++x)
            if (x >= 0 && y >= 0 && x < WORLD_W && y < WORLD_H)
                cursorPass = std::max(cursorPass, depthClass(sim.map[x][y]));
    if (cursorPass >= DEPTH_SPRITE) cursorPass = DEPTH_TOWER;
    // Layers: water and ground on the screen plane, buildings rise with
    // density, vehicles nearest. Right eye shifts left for in-front content.
    for (int pass = 0; pass < DEPTH_COUNT; ++pass) {
        int dx = 0;
        if (eye != 0 && slider > 0.f) {
            const float par = (float)(pass - DEPTH_GROUND) * kParallaxPx * slider * parScale;
            dx = -(int)(eye * par + (eye * par >= 0 ? 0.5f : -0.5f));
        }
        if (pass == DEPTH_SPRITE) drawSprites(s, v, dx);
        else drawTiles(s, v, pass, dx);
        if (pass == DEPTH_GROUND && overlay != OV_NONE)
            drawOverlayTint(s, v, dx);
        if (pass == cursorPass) drawFootprint(s, v, dx, false);
    }
    drawTopHud(s);
}

static void fmtThousands(char *out, size_t n, long v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%ld", v < 0 ? -v : v);
    const int len = (int)strlen(tmp);
    size_t o = 0;
    if (v < 0 && o < n - 1) out[o++] = '-';
    for (int i = 0; i < len && o < n - 1; ++i) {
        out[o++] = tmp[i];
        const int rest = len - 1 - i;
        if (rest > 0 && rest % 3 == 0 && o < n - 1) out[o++] = ',';
    }
    out[o] = 0;
}

static const char *kMonths[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };

// Top screen HUD: the things you glance at while building on the bottom.
//   bar:    funds, date, population, speed, R C I demand
//   corner: whole-city minimap with the bottom map's frame
//   foot:   newest city message, and the selected tool with its price
void App::drawTopHud(Screen &s) {
    char line[128], num[32];
    fb::darken(s, 0, 0, TOP_W, 18);
    fmtThousands(num, sizeof(num), (long)sim.totalFunds);
    snprintf(line, sizeof(line), "$%s", num);
    fb::textB(s, 5, 2, line, sim.totalFunds < 500 ? 255 : 255, sim.totalFunds < 500 ? 110 : 240, sim.totalFunds < 500 ? 90 : 150);
    const int m = (sim.cityMonth >= 0 && sim.cityMonth < 12) ? (int)sim.cityMonth : 0;
    snprintf(line, sizeof(line), "%s %ld", kMonths[m], (long)sim.cityYear);
    fb::textB(s, 104, 2, line, 230, 230, 230);
    fmtThousands(num, sizeof(num), (long)sim.cityPop);
    snprintf(line, sizeof(line), "Pop %s", num);
    fb::textB(s, 184, 2, line, 230, 230, 230);
    const char *spd = sim.simSpeed == 0 ? "PAUSED" : sim.simSpeed == 1 ? "Slow" : sim.simSpeed == 2 ? "Medium" : "Fast";
    fb::textB(s, 290, 2, spd, sim.simSpeed == 0 ? 255 : 170, sim.simSpeed == 0 ? 120 : 220, sim.simSpeed == 0 ? 120 : 170);
    // R C I demand: small bars around a centre line
    float rd, cd, id;
    sim.getDemands(&rd, &cd, &id);
    const float d[3] = { rd, cd, id };
    static const uint8_t col[3][3] = { {80, 220, 80}, {90, 140, 255}, {250, 210, 60} };
    static const char *lab[3] = { "R", "C", "I" };
    for (int i = 0; i < 3; ++i) {
        const int x = 352 + i * 16, mid = 9;
        fb::text(s, x - 7, 3, lab[i], col[i][0], col[i][1], col[i][2]);
        fb::fillRect(s, x, 1, 6, 16, 40, 40, 48);
        fb::fillRect(s, x, mid, 6, 1, 120, 120, 130);
        int h = (int)(d[i] * 8); if (h > 8) h = 8; if (h < -8) h = -8;
        if (h > 0) fb::fillRect(s, x, mid - h, 6, h, col[i][0], col[i][1], col[i][2]);
        else if (h < 0) fb::fillRect(s, x, mid + 1, 6, -h, col[i][0] / 2, col[i][1] / 2, col[i][2] / 2);
    }

    // minimap, top-right (every 2nd tile: 60 x 50)
    fb::fillRect(s, TOP_W - 64, 20, 64, 54, 0, 0, 0);
    drawMinimap(s, TOP_W - 62, 22, 2);
    if (overlay != OV_NONE) {
        const int w = fb::textWidth(kOverlayNames[overlay]) + 6;
        fb::darken(s, TOP_W - w, 75, w, 14);
        fb::text(s, TOP_W - w + 3, 76, kOverlayNames[overlay], 200, 220, 255);
    }

    // foot: newest message (8 s), then the tool line
    const uint64_t now = platform::millis();
    if (nmsgs > 0 && now - msgs[0].at < 8000) {
        const int w = std::min(TOP_W, fb::textWidth(msgs[0].text, fb::FONT_BOLD) + 10);
        fb::darken(s, 0, TOP_H - 34, w, 17);
        fb::textB(s, 5, TOP_H - 33, msgs[0].text, msgs[0].important ? 255 : 230,
                  msgs[0].important ? 200 : 230, msgs[0].important ? 80 : 230);
    }
    const ToolDef &t = kTools[toolIdx];
    if (now - lastDragAt < 2500 && lastDragCost > 0) {
        fmtThousands(num, sizeof(num), lastDragCost);
        snprintf(line, sizeof(line), "%s: spent $%s", t.name, num);
    } else if (tstate == T_PAINT) {
        fmtThousands(num, sizeof(num), (long)(dragFunds0 - sim.totalFunds));
        snprintf(line, sizeof(line), "%s: $%s so far", t.name, num);
    } else if (t.cost > 0) {
        snprintf(line, sizeof(line), "%s $%d%s  %s", t.name, t.cost, t.size == 1 ? "/tile" : "", t.desc);
    } else {
        snprintf(line, sizeof(line), "%s  %s", t.name, t.desc);
    }
    fb::darken(s, 0, TOP_H - 17, TOP_W, 17);
    fb::text(s, 5, TOP_H - 15, line, 255, 255, 170);

    if (mode == MODE_QUERY && query.valid) drawQuery(s);
}

void App::drawQuery(Screen &s) {
    char line[64];
    const int x0 = 50, y0 = 36, w = 300, h = 124;
    fb::fillRect(s, x0, y0, w, h, 20, 20, 30);
    fb::rectOutline(s, x0, y0, w, h, 200, 200, 200);
    const int cat = (query.category >= 0 && query.category < 27) ? query.category : 0;
    snprintf(line, sizeof(line), "%s  (%d,%d)", kZoneCategories[cat], query.x, query.y);
    fb::textB(s, x0 + 8, y0 + 6, line, 255, 255, 180);
    static const char *labels[5] = { "Density", "Value", "Crime", "Pollution", "Growth" };
    for (int i = 0; i < 5; ++i) {
        const int li = i * 4 + ((query.s[i] >= 0 && query.s[i] < 4) ? query.s[i] : 0);
        snprintf(line, sizeof(line), "%-10s %s", labels[i], kZoneLevels[li]);
        fb::text(s, x0 + 8, y0 + 26 + i * 14, line, 220, 220, 220);
    }
    fb::text(s, x0 + 8, y0 + h - 16, "Tap or A/B: close", 150, 150, 150);
}

// Whole-city minimap, sampling every `step` tiles; frames the bottom map.
void App::drawMinimap(Screen &s, int x0, int y0, int step) {
    for (int y = 0; y < WORLD_H; y += step) {
        for (int x = 0; x < WORLD_W; x += step) {
            uint8_t *p = s.px(x0 + x / step, y0 + y / step);
            const int ov = overlay != OV_NONE ? overlayValue(x, y) : -1;
            if (ov >= 0) { overlayColour(ov, p); continue; }
            const unsigned short v = sim.map[x][y];
            const int t = v & LOMASK;
            const uint8_t *c = kMiniLand;
            if (t >= WATER_LOW && t <= WATER_HIGH) c = kMiniWater;
            else if (t >= WOODS_LOW && t <= WOODS_HIGH) c = kMiniTree;
            else if (t >= FIREBASE && t <= LASTFIRE) c = kMiniFire;
            else if (t >= ROADBASE && t < RESBASE) c = kMiniRoad;
            else if (t >= PORTBASE && t <= LASTZONE) c = kMiniBig;
            else if (t >= RESBASE) c = kMiniZone;
            const int sh = overlay != OV_NONE ? 2 : 0;    // terrain dimmed under an overlay
            p[0] = c[0] >> sh; p[1] = c[1] >> sh; p[2] = c[2] >> sh;
        }
    }
    fb::rectOutline(s, x0 + camX / step, y0 + camY / step, mapCols() / step, (MAP_H / bts()) / step, 255, 255, 255);
}

static const char *kShortNames[16] = { "Res", "Com", "Ind", "Fire", "Police", "Query", "Wire", "Doze",
                                       "Rail", "Road", "Stadm", "Park", "Port", "Coal", "Nuke", "Airpt" };

void App::drawIcon(Screen &s, int x, int y, int i, int w, int sy0, int h) {
    int ic = -1;
    for (int k = 0; k < ICON_COUNT; ++k)
        if (!strcmp(mp_icon_names[k], kTools[i].icon)) { ic = k; break; }
    if (ic < 0) return;
    for (int yy = 0; yy < h && sy0 + yy < ICON_H; ++yy)
        for (int xx = 0; xx < w && xx < ICON_W; ++xx) {
            const unsigned char idx = mp_icons[ic][(sy0 + yy) * ICON_W + xx];
            if (idx == PAL_TRANSPARENT) continue;
            const unsigned char *c = mp_palette[idx];
            uint8_t *p = s.px(x + xx, y + yy);
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
        }
}

// Bar above the map: [$funds -> budget] [date] [speed] [overlay] [! alert] [menu]
void App::drawBottomBar(Screen &s) {
    char line[48], num[32];
    fb::fillRect(s, 0, 0, BOT_W, BAR_H, 12, 12, 18);
    fmtThousands(num, sizeof(num), (long)sim.totalFunds);
    snprintf(line, sizeof(line), "$%s", num);
    fb::fillRect(s, 1, 1, 77, BAR_H - 2, 36, 34, 22);
    fb::textB(s, 5, 2, line, 255, 240, 150);
    const int m = (sim.cityMonth >= 0 && sim.cityMonth < 12) ? (int)sim.cityMonth : 0;
    snprintf(line, sizeof(line), "%s %ld", kMonths[m], (long)sim.cityYear);
    fb::text(s, 84, 3, line, 210, 210, 210);
    static const char *sp[4] = { "||", ">", ">>", ">>>" };
    const int si = (sim.simSpeed >= 0 && sim.simSpeed < 4) ? (int)sim.simSpeed : 0;
    fb::fillRect(s, 141, 1, 42, BAR_H - 2, si == 0 ? 110 : 40, si == 0 ? 40 : 60, si == 0 ? 40 : 50);
    fb::textB(s, 141 + (42 - fb::textWidth(sp[si], fb::FONT_BOLD)) / 2, 2, sp[si], 240, 240, 240);
    fb::fillRect(s, 185, 1, 76, BAR_H - 2, overlay ? 40 : 28, overlay ? 50 : 28, overlay ? 90 : 36);
    snprintf(line, sizeof(line), "%s", overlay ? kOverlayNames[overlay] : "Overlay");
    fb::text(s, 185 + (76 - fb::textWidth(line)) / 2, 3, line, overlay ? 220 : 150, overlay ? 230 : 150, 255);
    const bool alert = alertX >= 0 && platform::millis() - alertAt < 30000;
    fb::fillRect(s, 263, 1, 24, BAR_H - 2, alert ? 150 : 28, alert ? 40 : 28, alert ? 30 : 36);
    fb::textB(s, 272, 2, "!", alert ? 255 : 80, alert ? 230 : 80, alert ? 120 : 90);
    fb::fillRect(s, 289, 1, 30, BAR_H - 2, 40, 40, 60);
    for (int i = 0; i < 3; ++i) fb::fillRect(s, 297, 4 + i * 4, 14, 2, 230, 230, 240);   // menu "hamburger"
}

// Strip under the map: 7 fixed tools + the "More" slot.
void App::drawToolStrip(Screen &s) {
    fb::fillRect(s, 0, STRIP_Y, BOT_W, STRIP_H, 16, 16, 22);
    for (int slot = 0; slot < 8; ++slot) {
        const int ti = slot < 7 ? kStripTools[slot] : extraTool;
        const int x = slot * 40;
        const bool sel = (ti == toolIdx);
        fb::fillRect(s, x + 1, STRIP_Y + 2, 38, STRIP_H - 3, sel ? 90 : 38, sel ? 90 : 38, sel ? 150 : 50);
        drawIcon(s, x + 2, STRIP_Y + 3, ti, 36, 4, 16);
        fb::text(s, x + 20 - fb::textWidth(kShortNames[ti]) / 2, STRIP_Y + 19, kShortNames[ti],
                 sel ? 255 : 210, sel ? 255 : 210, sel ? 160 : 210);
        if (slot == 7) {   // "More": a small ▲ marks the grid
            fb::fillRect(s, x + 27, STRIP_Y + 2, 12, 9, 70, 70, 110);
            for (int r = 0; r < 4; ++r) fb::fillRect(s, x + 33 - r, STRIP_Y + 4 + r, 1 + 2 * r, 1, 255, 255, 200);
        }
        if (sel) fb::rectOutline(s, x + 1, STRIP_Y + 2, 38, STRIP_H - 3, 255, 255, 120);
    }
}

// Every tool, 4 x 4, with its price: one tap picks it.
void App::drawToolPopup(Screen &s) {
    fb::fillRect(s, 0, MAP_Y0, BOT_W, MAP_H, 18, 20, 30);
    char line[32];
    for (int i = 0; i < kToolCount; ++i) {
        const int col = i % 4, row = i / 4;
        const int x = col * 80, y = MAP_Y0 + row * 47;
        const bool sel = (i == toolIdx);
        fb::fillRect(s, x + 2, y + 2, 76, 43, sel ? 80 : 36, sel ? 80 : 38, sel ? 140 : 52);
        drawIcon(s, x + 4, y + 3, i, 44, 0, ICON_H);
        if (kTools[i].cost > 0) snprintf(line, sizeof(line), "$%d", kTools[i].cost);
        else snprintf(line, sizeof(line), "free");
        fb::text(s, x + 76 - fb::textWidth(line), y + 8, line, 200, 230, 200);
        fb::text(s, x + 5, y + 29, kTools[i].name, 235, 235, 235);
        if (sel) fb::rectOutline(s, x + 2, y + 2, 76, 43, 255, 255, 120);
    }
}

void App::drawBottom() {
    Screen &s = bottom;
    if (mode == MODE_HELP) { drawHelp(s); return; }
    if (mode == MODE_LOADCITY) { drawLoadCity(s); return; }
    fb::clear(s, 0, 0, 0);

    View v; v.ts = bts(); v.ox = camX * v.ts; v.oy = camY * v.ts; v.sy0 = MAP_Y0; v.w = BOT_W; v.h = MAP_H;
    if (toolPopup) {
        drawToolPopup(s);
    } else {
        drawTiles(s, v, -1, 0);
        if (overlay != OV_NONE) drawOverlayTint(s, v, 0);
        drawSprites(s, v, 0);
        drawFootprint(s, v, 0, true);
    }
    drawBottomBar(s);
    drawToolStrip(s);

    if (mode == MODE_BUDGET) drawBudget(s);
    else if (mode == MODE_MENU) drawMenu(s);
    else if (mode == MODE_DISASTERS) drawDisasters(s);
}

void App::drawBudget(Screen &s) {
    fb::fillRect(s, 20, 30, 280, 180, 20, 24, 36);
    fb::rectOutline(s, 20, 30, 280, 180, 200, 200, 220);
    fb::textB(s, 30, 38, "BUDGET", 255, 255, 180);
    char line[80];
    snprintf(line, sizeof(line), "Taxes collected   $%ld", (long)sim.taxFund);
    fb::text(s, 30, 54, line, 220, 220, 220);
    struct Row { const char *name; int pct; long fund; } rows[4] = {
        { "Tax rate", sim.cityTax, 0 },
        { "Roads",  (int)(sim.roadPercent * 100 + 0.5f), (long)sim.roadFund },
        { "Police", (int)(sim.policePercent * 100 + 0.5f), (long)sim.policeFund },
        { "Fire",   (int)(sim.firePercent * 100 + 0.5f), (long)sim.fireFund },
    };
    for (int i = 0; i < 4; ++i) {
        const int y = 76 + i * 22;
        if (i == budgetSel) fb::fillRect(s, 26, y - 3, 268, 18, 60, 60, 100);
        if (i == 0) snprintf(line, sizeof(line), "%-10s %3d%%", rows[i].name, rows[i].pct);
        else snprintf(line, sizeof(line), "%-10s %3d%%   of $%ld", rows[i].name, rows[i].pct, rows[i].fund);
        fb::text(s, 30, y, line, 240, 240, 240);
        fb::fillRect(s, 230, y - 3, 28, 18, 70, 50, 50);
        fb::textB(s, 240, y - 1, "-", 255, 220, 220);
        fb::fillRect(s, 262, y - 3, 28, 18, 50, 70, 50);
        fb::textB(s, 272, y - 1, "+", 220, 255, 220);
    }
    snprintf(line, sizeof(line), "Cash flow  $%ld", (long)(sim.taxFund - (long)(sim.roadFund * sim.roadPercent)
             - (long)(sim.policeFund * sim.policePercent) - (long)(sim.fireFund * sim.firePercent)));
    fb::text(s, 30, 170, line, 200, 255, 200);
    fb::text(s, 30, 192, "\u25c4\u25ba or -/+ adjust", 150, 150, 160);
    fb::fillRect(s, 230, 186, 60, 20, 60, 60, 100);
    fb::textB(s, 242, 189, "Done", 255, 255, 200);
}

void App::drawMenu(Screen &s) {
    fb::fillRect(s, 40, 8, 240, 226, 20, 24, 36);
    fb::rectOutline(s, 40, 8, 240, 226, 200, 200, 220);
    fb::textB(s, 50, 14, "MICROPOLIS", 255, 255, 180);
    static const char *items[14] = { "New city", "Dullsville 1900", "San Francisco 1906", "Hamburg 1944",
        "Bern 1965", "Tokyo 1957", "Detroit 1972", "Boston 2010", "Rio de Janeiro 2047",
        "Load city...", "Disasters...", "Save city", "How to play", "Resume" };
    for (int i = 0; i < 14; ++i) {
        const int y = 34 + i * 14;
        if (i == menuSel) fb::fillRect(s, 46, y - 1, 228, 14, 60, 60, 100);
        fb::text(s, 50, y, items[i], 230, 230, 230);
    }
}

void App::drawDisasters(Screen &s) {
    fb::fillRect(s, 60, 50, 200, 130, 36, 20, 20);
    fb::rectOutline(s, 60, 50, 200, 130, 220, 180, 180);
    fb::textB(s, 70, 56, "DISASTERS", 255, 200, 200);
    static const char *items[7] = { "Fire", "Flood", "Tornado", "Earthquake", "Monster", "Meltdown", "Back" };
    for (int i = 0; i < 7; ++i) {
        const int y = 74 + i * 14;
        if (i == menuSel) fb::fillRect(s, 66, y - 1, 188, 14, 100, 60, 60);
        fb::text(s, 70, y, items[i], 230, 230, 230);
    }
}

// ---- Manual (docs/manual.txt -> assets/manual.h) ---------------------------
#include "../assets/manual.h"
static const int kHelpPageCount = MANUAL_PAGES;

void App::handleHelpInput(const Input &in) {
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_PLAY; tstate = T_DONE; return; }
    if (in.touchDown && in.touchY >= BOT_H - 18 && in.touchX >= BOT_W - 60) { mode = MODE_PLAY; tstate = T_DONE; return; }
    if (in.down & (BTN_RIGHT | BTN_A)) helpPage = (helpPage + 1) % kHelpPageCount;
    if (in.down & BTN_LEFT) helpPage = (helpPage + kHelpPageCount - 1) % kHelpPageCount;
    if (in.down & (BTN_R | BTN_L)) {
        // jump to the next / previous chapter start
        int ch = 0;
        for (int i = 0; i < MANUAL_CHAPTERS; ++i) if (kManualChapterStart[i] <= helpPage) ch = i;
        if (in.down & BTN_R) ch = (ch + 1) % MANUAL_CHAPTERS;
        else ch = (kManualChapterStart[ch] == helpPage) ? (ch + MANUAL_CHAPTERS - 1) % MANUAL_CHAPTERS : ch;
        helpPage = kManualChapterStart[ch];
    }
    if (in.touchDown && helpPage == 0 && in.touchY >= 26 + 14 && in.touchY < BOT_H - 20) {
        const int ch = (in.touchY - 26) / 14;                       // contents: tap a chapter
        if (ch >= 1 && ch < MANUAL_CHAPTERS) { helpPage = kManualChapterStart[ch]; return; }
    }
    if (in.touchDown) {
        if (in.touchY < 24 && helpPage != 0) helpPage = 0;          // tap the title: contents
        else helpPage = (in.touchX < BOT_W / 2) ? (helpPage + kHelpPageCount - 1) % kHelpPageCount
                                                : (helpPage + 1) % kHelpPageCount;
    }
}

void App::drawHelp(Screen &s) {
    fb::fillRect(s, 0, 0, BOT_W, BOT_H, 18, 22, 34);
    fb::rectOutline(s, 0, 0, BOT_W, BOT_H, 120, 130, 170);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%d/%d", helpPage + 1, kHelpPageCount);
    fb::textB(s, 8, 6, kManual[helpPage][0], 255, 255, 180);
    fb::text(s, BOT_W - 8 - fb::textWidth(hdr), 7, hdr, 140, 140, 160);
    for (int i = 1; i < MANUAL_LINES; ++i)
        if (kManual[helpPage][i] && kManual[helpPage][i][0])
            fb::text(s, 8, 26 + (i - 1) * 14, kManual[helpPage][i], 220, 220, 220);
    fb::text(s, 8, BOT_H - 14, "\u25c4\u25ba/tap page  L/R chapter  title: index", 130, 130, 150);
    fb::fillRect(s, BOT_W - 58, BOT_H - 18, 54, 16, 60, 60, 100);
    fb::textB(s, BOT_W - 50, BOT_H - 17, "Close", 255, 255, 200);
}

// ---- Load city ------------------------------------------------------------
void App::scanCities() {
    nCities = 0;
    hasAutosave = false;
    char dir[256];
    {
        snprintf(dir, sizeof(dir), "%s/autosave.cty", platform::saveDir());
        FILE *f = fopen(dir, "rb");
        if (f) { fclose(f); hasAutosave = true; snprintf(cityNames[nCities++], sizeof(cityNames[0]), "autosave.cty  (last save)"); }
    }
    snprintf(dir, sizeof(dir), "%s/cities", platform::saveDir());
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nCities < kMaxCities) {
        const char *n = e->d_name;
        const size_t len = strlen(n);
        if (len < 5 || strcasecmp(n + len - 4, ".cty") != 0) continue;
        snprintf(cityNames[nCities], sizeof(cityNames[0]), "%s", n);
        ++nCities;
    }
    closedir(d);
    // simple insertion sort by name (the autosave stays first)
    const int first = hasAutosave ? 1 : 0;
    for (int i = first + 1; i < nCities; ++i)
        for (int j = i; j > first && strcasecmp(cityNames[j - 1], cityNames[j]) > 0; --j) {
            char tmp[40]; memcpy(tmp, cityNames[j], 40); memcpy(cityNames[j], cityNames[j - 1], 40); memcpy(cityNames[j - 1], tmp, 40);
        }
}

void App::handleLoadCityInput(const Input &in) {
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_MENU; menuSel = 9; return; }
    if (in.touchDown && in.touchY >= BOT_H - 20 && in.touchX >= BOT_W - 70) { mode = MODE_MENU; menuSel = 9; return; }
    if (nCities == 0) return;
    if (in.repeat & BTN_UP) citySel = (citySel + nCities - 1) % nCities;
    if (in.repeat & BTN_DOWN) citySel = (citySel + 1) % nCities;
    bool pick = (in.down & BTN_A) != 0;
    if (in.touchDown && in.touchY >= 25 && in.touchY < 25 + 13 * 14) {
        const int i = (citySel / 13) * 13 + (in.touchY - 25) / 14;
        if (i < nCities) { citySel = i; pick = true; }
    }
    if (pick) {
        tstate = T_DONE;
        char path[320];
        if (hasAutosave && citySel == 0) snprintf(path, sizeof(path), "%s/autosave.cty", platform::saveDir());
        else snprintf(path, sizeof(path), "%s/cities/%s", platform::saveDir(), cityNames[citySel]);
        if (loadCity(path)) { pushMessage("City loaded.", false); mode = MODE_PLAY; }
        else pushMessage("Could not load that city file.", true);
    }
}

void App::drawLoadCity(Screen &s) {
    fb::fillRect(s, 0, 0, BOT_W, BOT_H, 18, 22, 34);
    fb::rectOutline(s, 0, 0, BOT_W, BOT_H, 120, 130, 170);
    fb::textB(s, 8, 6, "LOAD CITY", 255, 255, 180);
    if (nCities == 0) {
        fb::text(s, 8, 30, "No .cty files found in", 220, 220, 220);
        fb::text(s, 8, 44, "sdmc:/3ds/micropolis/cities/", 200, 220, 255);
        fb::text(s, 8, 66, "Copy SimCity / Micropolis city files there.", 180, 180, 190);
        fb::text(s, 8, 80, "(Menu > Save city writes autosave.cty.)", 160, 160, 170);
    } else {
        const int perPage = 13, first = (citySel / perPage) * perPage;
        for (int i = first; i < nCities && i < first + perPage; ++i) {
            const int y = 26 + (i - first) * 14;
            if (i == citySel) fb::fillRect(s, 4, y - 1, BOT_W - 8, 14, 60, 60, 100);
            fb::text(s, 8, y, cityNames[i], 230, 230, 230);
        }
        char line[48];
        snprintf(line, sizeof(line), "%d/%d", citySel + 1, nCities);
        fb::text(s, BOT_W - 8 - fb::textWidth(line), 6, line, 140, 140, 160);
    }
    fb::text(s, 8, BOT_H - 16, "Tap a city or A: load", 140, 140, 160);
    fb::fillRect(s, BOT_W - 66, BOT_H - 20, 62, 18, 60, 60, 100);
    fb::textB(s, BOT_W - 56, BOT_H - 18, "Back", 255, 255, 200);
}

void App::render(float slider) {
    overlayMaxCached = (overlay == OV_NONE) ? 255 : overlayMax();
    drawCity(top, slider > 0.f ? -1 : 0, slider);
    if (slider > 0.f) drawCity(topR, 1, slider);
    drawBottom();
    platform::present(top, slider > 0.f ? &topR : nullptr, bottom);
}
