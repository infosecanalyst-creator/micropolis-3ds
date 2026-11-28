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
#include "manual3ds.h"
#include "manual_text.h"
#include "../engine/text.h"   // MessageNumber (MP-09)

static manual3ds::Manual g_manual;   // Start > How to play
#include "../assets/icons_data.h"
#ifndef PORT_VERSION
#define PORT_VERSION "unknown"
#endif

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
    g_manual.load(kManualText, "sdmc:/3ds/micropolis/manual.txt", "Micropolis for 3DS: manual");
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
    centerOnLand();
    cityEdited = false; pendScenario = 0;
    pushMessage("New city. Circle Pad scrolls the map.", false);
    return true;
}

// MP-X1: open the view on the 12 x 12 block with the most open land (ties: nearest the middle
// of the map), so a first city never starts on open sea.
void App::centerOnLand() {
    int bestX = WORLD_W / 2, bestY = WORLD_H / 2, best = -1;
    for (int y = 6; y + 6 <= WORLD_H; y += 3)
        for (int x = 6; x + 6 <= WORLD_W; x += 3) {
            int land = 0;
            for (int yy = y - 6; yy < y + 6; ++yy)
                for (int xx = x - 6; xx < x + 6; ++xx) {
                    const int t = sim.map[xx][yy] & LOMASK;
                    if (t >= ROADBASE) land += 8;                              // a loaded city: go where it is built
                    else if (t == DIRT) land += 2;                             // open ground
                    else if (t > WATER_HIGH) land += 1;                        // trees
                }
            const int ddx = x - WORLD_W / 2, ddy = y - WORLD_H / 2;
            const int score = land * 16 - (abs(ddx) + abs(ddy));
            if (score > best) { best = score; bestX = x; bestY = y; }
        }
    centerOn(bestX, bestY);
}

// ---- MP-Q6: a scenario's goal and deadline live beside the city file (<name>.scn), because the
// classic .cty format has no room for them.
static void scnPathOf(const char *cty, char *out, size_t n) {
    snprintf(out, n, "%s", cty);
    const size_t len = strlen(out);
    if (len > 4 && !strcasecmp(out + len - 4, ".cty")) snprintf(out + len - 4, n - (len - 4), ".scn");
    else if (len + 5 < n) strcat(out, ".scn");
}
void App::writeScenarioNote(const char *ctyPath) {
    char scn[340], tmp[348];
    scnPathOf(ctyPath, scn, sizeof(scn));
    int sc = (int)sim.scenario, score = sim.scoreWait, dis = sim.disasterWait, ev = (int)sim.disasterEvent, st = (int)sim.scoreType;
    if (pendScenario) { sc = pendScenario; score = pendScore; dis = pendDisWait; ev = pendDisEvent; st = pendScoreType; }
    if (sc < 1 || sc > 8 || st < 1 || score <= 0) { remove(scn); return; }   // no scenario running (or already judged)
    snprintf(tmp, sizeof(tmp), "%s.tmp", scn);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    const bool ok = fprintf(f, "%d %d %d %d %d\n", sc, score, dis, ev, st) > 0;
    if (fclose(f) != 0 || !ok) { remove(tmp); return; }
    remove(scn);
    if (rename(tmp, scn) != 0) remove(tmp);
}
void App::readScenarioNote(const char *ctyPath) {
    char scn[340];
    scnPathOf(ctyPath, scn, sizeof(scn));
    pendScenario = 0;
    FILE *f = fopen(scn, "r");
    if (!f) return;
    int sc = 0, score = 0, dis = 0, ev = 0, st = 0;
    const int n = fscanf(f, "%d %d %d %d %d", &sc, &score, &dis, &ev, &st);
    fclose(f);
    if (n != 5 || sc < 1 || sc > 8 || st < 1 || st > 8 || ev < 0 || ev > 8 || score <= 0 || score > 30 * 48 || dis < 0 || dis > 2 * 48) return;
    sim.scenario = (Scenario)sc;   // the engine sets its timers up on the next tick; ours go in after that (tickSim)
    pendScenario = sc; pendScore = score; pendDisWait = dis; pendDisEvent = ev; pendScoreType = st;
}

bool App::loadCity(const char *path) {
    if (!sim.loadCity(path)) {
        pushMessage("Could not load city.", true);
        return false;
    }
    sim.setSpeed(2);
    centerOnLand();
    readScenarioNote(path);
    // the city in the game's own save has no other copy; a city opened from the list still has its file
    cityEdited = strstr(path, "autosave.cty") != nullptr;
    if (pendScenario) pushMessage("Restored a saved city. The scenario goes on.", false);
    else pushMessage("Restored a saved city.", false);
    return true;
}

bool App::loadScenario(int which) {
    if (which < 1 || which > 8)
        return false;
    sim.loadScenario((Scenario)which);
    sim.setSpeed(2);
    centerOnLand();
    cityEdited = false; pendScenario = 0;
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
            const int picture = va_arg(args, int);
            const int flagged = va_arg(args, int);
            // MP-09: the engine flags only some disasters "important" (monster, tornado, meltdown);
            // fire, flood, earthquake and the crashes come with just the picture flag, or with
            // neither (a fire that starts by itself). All of them count here, so they also show
            // on the touch-screen map.
            const bool important = flagged != 0 || picture != 0 || num == MESSAGE_FIRE_REPORTED
                                   || num == MESSAGE_FLOODING_REPORTED || num == MESSAGE_EXPLOSION_REPORTED;
            if (num > 0 && num < kMessageCount)
                pushMessage(kMessages[num], important);
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
    } else if (!strcmp(name, "didntSaveCity")) {
        saveFailed = true;
    } else if (!strcmp(name, "didSaveCity")) {
        saveFailed = false;
    } else if (!strcmp(name, "didLoadScenario")) {
        if (sim.scenario > 0 && sim.scenario <= 8)
            pushMessage(kScenarioNames[sim.scenario], true);
    }
}

bool App::saveCity() {
    char path[256];
    snprintf(path, sizeof(path), "%s/autosave.cty", platform::saveDir());
    saveFailed = false;
    sim.saveCityAs(path);   // reports through didSaveCity / didntSaveCity
    playMsSinceSave = 0;    // MP-01: any save restarts the autosave clock
    if (!saveFailed) writeScenarioNote(path);
    return !saveFailed;
}

// ---- MP-Q1 / MP-Q2: the city in play is never thrown away. Before NEW CITY, a scenario or LOAD
// CITY replaces it, it is written to <saveDir>/kept/kept-NNNN.cty (its own file, not the game's
// save), and LOAD CITY lists those as "Earlier city". The newest kKeepMax are kept.
static int keptNumber(const char *name) {
    int n = 0;
    if (strncasecmp(name, "kept-", 5) != 0) return -1;
    const char *q = name + 5;
    if (!*q) return -1;
    for (; *q >= '0' && *q <= '9'; ++q) n = n * 10 + (*q - '0');
    return strcasecmp(q, ".cty") == 0 ? n : -1;
}
bool App::keepCurrentCity() {
    if (!cityEdited) return true;   // nothing of the player's in it, or its file is still on the card
    char dir[256], path[340];
    snprintf(dir, sizeof(dir), "%s/kept", platform::saveDir());
    platform::makeDir(dir);
    int nums[80], n = 0, hi = 0;
    if (DIR *d = opendir(dir)) {
        struct dirent *e;
        while ((e = readdir(d))) {
            const int k = keptNumber(e->d_name);
            if (k < 0) continue;
            if (k > hi) hi = k;
            if (n < 80) nums[n++] = k;
        }
        closedir(d);
    }
    snprintf(path, sizeof(path), "%s/kept-%04d.cty", dir, hi + 1);
    saveFailed = false;
    sim.saveCityAs(path);
    if (saveFailed) { platform::log("keep: could not write %s", path); return false; }
    writeScenarioNote(path);
    platform::log("keep: city in play kept as kept-%04d.cty", hi + 1);
    // only when the new copy is safely written: drop the oldest beyond the limit
    std::sort(nums, nums + n);
    for (int i = 0; i + (kKeepMax - 1) < n; ++i) {
        char scn[340];
        snprintf(path, sizeof(path), "%s/kept-%04d.cty", dir, nums[i]);
        scnPathOf(path, scn, sizeof(scn));
        remove(path); remove(scn);
    }
    cityEdited = false;
    return true;
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
    // MP-06: the ticks follow the clock, not the frame rate. A slow frame (below 16 fps the
    // old one-tick-per-frame rule slowed the city) runs the ticks it owes, at most 3 per frame;
    // after a long gap (HOME Menu, a save, a menu) the city does not fast-forward.
    if (now - lastTickMs >= 62) {
        if (now - lastTickMs > 400) lastTickMs = now - 62;
        int n = 0;
        while (now - lastTickMs >= 62 && n < 3) {
            lastTickMs += 62;
            sim.simTick();
            platform::noteSimTick();
            ++n;
            if (pendScenario && sim.initSimLoad == 0) {   // MP-Q6: the saved scenario clock, once the engine has set up
                sim.scenario = (Scenario)pendScenario; sim.scoreType = (Scenario)pendScoreType; sim.scoreWait = (short)pendScore;
                sim.disasterEvent = (Scenario)pendDisEvent; sim.disasterWait = (short)pendDisWait;
                pendScenario = 0;
            }
        }
        if (now - lastTickMs >= 62) lastTickMs = now;   // still behind: drop the rest
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

void App::topOrigin(int &ox, int &oy, int w) const {
    const int ts = topTs();
    ox = (int)(topCx * ts / 16) - w / 2;
    oy = (int)(topCy * ts / 16) - TOP_H / 2;
    if (ox > WORLD_W * ts - w) ox = WORLD_W * ts - w;
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
    if (r == TOOLRESULT_OK && t.tool != TOOL_QUERY) cityEdited = true;
    if (r == TOOLRESULT_NEED_BULLDOZE) {
        // MP-Q4: open water cannot be bulldozed, so do not send the player to the bulldozer
        bool water = false;
        const int off = (t.size >= 3) ? 1 : 0;
        for (int y = ty - off; y < ty - off + t.size && !water; ++y)
            for (int x = tx - off; x < tx - off + t.size; ++x) {
                if (x < 0 || y < 0 || x >= WORLD_W || y >= WORLD_H) continue;
                const int v = sim.map[x][y] & LOMASK;
                if (v >= WATER_LOW && v < FIRSTRIVEDGE) { water = true; break; }
            }
        pushMessage(water ? "Can't build on water here." : "Area must be bulldozed first.", true);
    }
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

// MP-X2: L / R walk the seven strip tools; one step past either end opens the grid of all 16.
void App::cycleStripTool(int dir) {
    int slot = stripSlotOf(toolIdx);
    if (slot < 0) slot = 7;
    slot += dir;
    if (slot < 0 || slot >= 7) { openToolGrid(); return; }
    selectTool(kStripTools[slot]);
}
void App::openToolGrid() { toolPopup = true; gridSel = toolIdx; }

// Taps on the bar above the map and the tool strip below it.
void App::handleUiTap(int x, int y) {
    if (y < BAR_H) {
        // buttons sit at y 6..29; the bar's 6-px margins still count as the nearest button
        if (x < 80) { mode = MODE_BUDGET; budgetSel = 0; }                    // funds: budget
        else if (x >= 132 && x < 176) { int sp = sim.simSpeed + 1; if (sp > 3) sp = 0; sim.setSpeed((short)sp); }
        else if (x >= 176 && x < 258) overlay = (overlay + 1) % OV_COUNT;      // overlay cycle
        else if (x >= 258 && x < 286) {                                       // jump to alert
            if (alertX >= 0 && platform::millis() - alertAt < 30000) centerOn(alertX, alertY);
        } else if (x >= 286) { mode = MODE_MENU; pmPage = 0; menuSel = 0; }
        return;
    }
    if (y >= STRIP_Y) {
        int slot = (x - SLOT_X0 + 2) / SLOT_STEP;
        if (x < SLOT_X0) slot = 0;
        if (slot > 7) slot = 7;
        if (slot >= 0 && slot < 7) { selectTool(kStripTools[slot]); toolPopup = false; }
        else if (slot == 7) {   // MP-X2: "More" always opens (or closes) the grid with one tap
            if (toolPopup) toolPopup = false;
            else openToolGrid();
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
                int col = (in.touchX - POP_X0 + 2) / POP_STEPX, row = (in.touchY - MAP_Y0 - 4 + 2) / POP_STEPY;
                if (in.touchX < POP_X0) col = 0;
                if (col > 3) col = 3;
                if (row > 3) row = 3;
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
    if (toolPopup) {
        // the grid of all 16 tools: D-pad moves the gold ring, A picks, B / L / R / START close
        if (in.repeat & BTN_LEFT)  gridSel = (gridSel & ~3) | ((gridSel + 3) & 3);
        if (in.repeat & BTN_RIGHT) gridSel = (gridSel & ~3) | ((gridSel + 1) & 3);
        if (in.repeat & BTN_UP)    gridSel = (gridSel + 12) & 15;
        if (in.repeat & BTN_DOWN)  gridSel = (gridSel + 4) & 15;
        if (in.down & BTN_A) { selectTool(gridSel); toolPopup = false; return; }
        if (in.down & (BTN_B | BTN_L | BTN_R)) { toolPopup = false; if (in.down & (BTN_L | BTN_R)) shoulderUsedForPan = true; return; }
        if (in.down & BTN_START) { toolPopup = false; mode = MODE_MENU; pmPage = 0; menuSel = 0; return; }
        if (in.touch || tstate != T_NONE) handleMapTouch(in, now);
        return;
    }
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
        // MP-X3: B is "back" everywhere else, so say what it did here
        char note[64];
        const char *bName = platform::leftyOn() ? "D-Down" : "B";
        if (toolIdx == 7) {
            toolIdx = prevToolIdx;   // B again: back to what you had
            snprintf(note, sizeof(note), "Tool: %s.", kTools[toolIdx].name);
        } else {
            snprintf(note, sizeof(note), "Bulldozer. %s again: back to %s.", bName, kTools[toolIdx].name);
            selectTool(7);
        }
        pushMessage(note, true);
    }
    if (in.down & BTN_Y) { zoomOut = !zoomOut; centerOn(curX, curY); }
    if (in.down & BTN_X) { mode = MODE_BUDGET; budgetSel = 0; }
    if (in.down & BTN_START) { mode = MODE_MENU; pmPage = 0; menuSel = 0; toolPopup = false; }
    if (in.down & BTN_ZR) overlay = (overlay + 1) % OV_COUNT;
    if (in.down & BTN_SELECT) {
        int sp = sim.simSpeed + 1;
        if (sp > 3) sp = 0;
        sim.setSpeed((short)sp);
    }

    // --- stylus
    if (in.touch || tstate != T_NONE) handleMapTouch(in, now);
}

// START menu pages (house style, claude/3ds-look-and-feel.md). Rule 11: every button is 24 px
// tall or more, 4 px or more from its neighbours and 6 px from the screen edges.
enum { PM_MAIN, PM_CITIES, PM_EXIT, PM_SAVEFAIL, PM_REPLACE, PM_SETTINGS, PM_DISASTERS, PM_DISASTER_ASK };
struct PmBtn { int x, y, w, h; };
static const char *const kPmMain[7] = { "RESUME", "MANUAL", "SAVE CITY", "LOAD CITY", "NEW CITY", "SETTINGS", "EXIT GAME" };
static const char *const kPmCities[10] = { "New city", "Dullsville 1900", "San Francisco 1906", "Hamburg 1944",
    "Bern 1965", "Tokyo 1957", "Detroit 1972", "Boston 2010", "Rio de Janeiro 2047", "Back" };
static const char *const kPmDisasters[7] = { "Fire", "Flood", "Tornado", "Earthquake", "Monster", "Meltdown", "BACK" };
static const char *const kDisasterAsk[6] = { "Start a fire?", "Start a flood?", "Start a tornado?", "Start an earthquake?",
    "Call the monster?", "Melt down a nuclear plant?" };
// Fills b[] with the page's buttons; returns how many. `rows` = buttons per column (for Left / Right).
static int pmLayout(int page, PmBtn *b, int &rows) {
    int n = 0;
    switch (page) {
    case PM_MAIN:       // one column of seven, y 27..219
        rows = 7;
        for (int i = 0; i < 7; ++i) b[n++] = { 6, 27 + i * 28, 308, 24 };
        break;
    case PM_CITIES:     // 2 x 5, column-major
        rows = 5;
        for (int i = 0; i < 10; ++i) b[n++] = { 6 + (i / 5) * 156, 32 + (i % 5) * 28, 152, 24 };
        break;
    case PM_SETTINGS:   // one column of three
        rows = 3;
        for (int i = 0; i < 3; ++i) b[n++] = { 6, 32 + i * 32, 308, 28 };
        break;
    case PM_DISASTERS:  // 2 x 3, column-major, and BACK on its own under them
        rows = 3;
        for (int i = 0; i < 6; ++i) b[n++] = { 6 + (i / 3) * 156, 32 + (i % 3) * 32, 152, 28 };
        b[n++] = { 84, 144, 152, 28 };
        break;
    default:            // a question: two buttons side by side
        rows = 1;
        b[n++] = { 6, 124, 152, 32 };
        b[n++] = { 162, 124, 152, 32 };
        break;
    }
    return n;
}
static void pmButton(Screen &s, const PmBtn &r, const char *label, bool sel) {
    fb::fillRect(s, r.x + 2, r.y + 2, r.w, r.h, 20, 30, 70);
    fb::fillRect(s, r.x, r.y, r.w, r.h, 40, 56, 120);
    fb::rectOutline(s, r.x, r.y, r.w, r.h, 120, 150, 230);
    if (sel) { fb::rectOutline(s, r.x - 2, r.y - 2, r.w + 4, r.h + 4, 255, 204, 64); fb::rectOutline(s, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 255, 204, 64); }
    fb::text(s, r.x + (r.w - fb::textWidth(label)) / 2, r.y + (r.h - 13) / 2 + 1, label, 235, 240, 250);
}
// House frame: navy, a header bar with the title in gold, a hint strip at the bottom.
static void houseFrame(Screen &s, const char *title, const char *hint) {
    fb::fillRect(s, 0, 0, BOT_W, BOT_H, 8, 12, 40);
    fb::fillRect(s, 0, 0, BOT_W, 22, 40, 56, 120);
    fb::fillRect(s, 0, 22, BOT_W, 1, 120, 150, 230);
    fb::textB(s, 8, 5, title, 255, 204, 64);
    fb::fillRect(s, 0, 224, BOT_W, 16, 40, 56, 120);
    fb::fillRect(s, 0, 224, BOT_W, 1, 120, 150, 230);
    fb::text(s, (BOT_W - fb::textWidth(hint)) / 2, 226, hint, 255, 204, 64);
}
// the hint strip names the buttons the player really presses (left hand: the D-pad is A/B/X/Y)
static const char *pmHint(bool main) {
    if (platform::leftyOn()) return main ? "D-Right Select   D-Down Back   ZL Swap" : "D-Right Select   D-Down Back";
    return main ? "A Select   B Back   ZL Swap" : "A Select   B Back";
}

// MP-Q1 / MP-Q2: ask before the city in play is replaced. kind 1: new city (arg 0) or scenario
// (arg 1..8); kind 2: the city at citySel in the LOAD CITY list.
void App::askReplace(int kind, int arg) {
    askKind = kind; askArg = arg;
    if (kind == 1 && arg == 0) snprintf(askLine1, sizeof(askLine1), "Start a new city?");
    else if (kind == 1) snprintf(askLine1, sizeof(askLine1), "Start %s?", kScenarioNames[arg]);
    else snprintf(askLine1, sizeof(askLine1), "Load this city?");
    mode = MODE_MENU; pmPage = PM_REPLACE; menuSel = 1;   // focus on NO
}

void App::doReplace() {
    if (!keepCurrentCity()) {   // could not be written: the city in play stays
        pushMessage("Save failed - your city was not replaced.", true);
        mode = MODE_PLAY; pmPage = PM_MAIN;
        return;
    }
    if (askKind == 1) {
        if (askArg == 0) newCity(0); else loadScenario(askArg);
    } else if (citySel >= 0 && citySel < nCities) {
        if (loadCity(cityPaths[citySel])) pushMessage(pendScenario ? "City loaded. The scenario goes on." : "City loaded.", false);
        else pushMessage("Could not load that city file.", true);
    }
    mode = MODE_PLAY; pmPage = PM_MAIN; menuSel = 0;
}

void App::handleMenuInput(const Input &in) {
    PmBtn b[12]; int rows = 1;
    const int items = pmLayout(pmPage, b, rows);
    if (in.repeat & BTN_UP) menuSel = (menuSel + items - 1) % items;
    if (in.repeat & BTN_DOWN) menuSel = (menuSel + 1) % items;
    if (in.repeat & (BTN_LEFT | BTN_RIGHT)) {
        int n = menuSel + ((in.repeat & BTN_RIGHT) ? rows : -rows);
        if (rows == 1 && items == 2) n = (in.repeat & BTN_RIGHT) ? 1 : 0;
        if (n >= 0 && n < items && !(pmPage == PM_DISASTERS && (n == 6 || menuSel == 6))) menuSel = n;
    }
    if (in.down & (BTN_B | BTN_START)) {
        // B always goes one step back (MP-X4); START leaves the menu
        tstate = T_DONE;
        if (in.down & BTN_START) { mode = MODE_PLAY; pmPage = PM_MAIN; return; }
        switch (pmPage) {
        case PM_MAIN: case PM_SAVEFAIL: mode = MODE_PLAY; pmPage = PM_MAIN; break;   // save failed: back to the city
        case PM_CITIES: pmPage = PM_MAIN; menuSel = 4; break;
        case PM_SETTINGS: pmPage = PM_MAIN; menuSel = 5; break;
        case PM_EXIT: pmPage = PM_MAIN; menuSel = 6; break;
        case PM_DISASTERS: pmPage = PM_SETTINGS; menuSel = 1; break;
        case PM_DISASTER_ASK: pmPage = PM_DISASTERS; menuSel = askArg; break;
        case PM_REPLACE:
            if (askKind == 2) { mode = MODE_LOADCITY; pmPage = PM_MAIN; }
            else { pmPage = PM_CITIES; menuSel = askArg; }
            break;
        }
        return;
    }
    bool pick = (in.down & BTN_A) != 0;
    if (in.touchDown) {
        for (int i = 0; i < items; ++i)
            if (in.touchX >= b[i].x - 2 && in.touchX < b[i].x + b[i].w + 2 && in.touchY >= b[i].y - 2 && in.touchY < b[i].y + b[i].h + 2) {
                menuSel = i; pick = true; break;
            }
    }
    if (!pick) return;
    tstate = T_DONE;
    switch (pmPage) {
    case PM_EXIT:
        if (menuSel == 0) {   // never lose progress: the city is saved before the game ends
            if (saveCity()) quitRequested = true;
            else { pmPage = PM_SAVEFAIL; menuSel = 1; return; }   // MP-02: it failed, ask (focus on NO)
        }
        pmPage = PM_MAIN; menuSel = 6;
        return;
    case PM_SAVEFAIL:
        if (menuSel == 0) quitRequested = true;          // EXIT ANYWAY
        else mode = MODE_PLAY;                           // NO: back to the city
        pmPage = PM_MAIN; menuSel = 6;
        return;
    case PM_REPLACE:
        if (menuSel == 0) doReplace();
        else if (askKind == 2) { mode = MODE_LOADCITY; pmPage = PM_MAIN; }
        else { pmPage = PM_CITIES; menuSel = askArg; }
        return;
    case PM_CITIES:
        if (menuSel <= 8) askReplace(1, menuSel);
        else { pmPage = PM_MAIN; menuSel = 4; }
        return;
    case PM_SETTINGS:
        if (menuSel == 0) platform::setLefty(!platform::leftyOn());
        else if (menuSel == 1) { pmPage = PM_DISASTERS; menuSel = 6; }
        else { pmPage = PM_MAIN; menuSel = 5; }
        return;
    case PM_DISASTERS:
        if (menuSel < 6) { askArg = menuSel; pmPage = PM_DISASTER_ASK; menuSel = 1; }   // MP-Q3: ask, focus on NO
        else { pmPage = PM_SETTINGS; menuSel = 1; }
        return;
    case PM_DISASTER_ASK:
        if (menuSel == 0) {
            switch (askArg) {
            case 0: sim.makeFire(); break;
            case 1: sim.makeFlood(); break;
            case 2: sim.makeTornado(); break;
            case 3: sim.makeEarthquake(); break;
            case 4: sim.makeMonster(); break;
            case 5: sim.makeMeltdown(); break;
            }
            cityEdited = true;
            mode = MODE_PLAY; pmPage = PM_MAIN;
        } else { pmPage = PM_DISASTERS; menuSel = askArg; }
        return;
    }
    switch (menuSel) {
    case 0: mode = MODE_PLAY; break;
    case 1: g_manual.open(); mode = MODE_HELP; break;
    case 2: {
        if (saveCity()) pushMessage("City saved.", false);
        else pushMessage("Save failed - is the SD card full?", true);
        mode = MODE_PLAY;
        break;
    }
    case 3: scanCities(); citySel = 0; mode = MODE_LOADCITY; break;
    case 4: pmPage = PM_CITIES; menuSel = 0; break;
    case 5: pmPage = PM_SETTINGS; menuSel = 0; break;
    case 6: pmPage = PM_EXIT; menuSel = 1; break;
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
        const int row = (y - 46) / 30;   // rows of 24-px buttons at y 48 + 30 * row (see drawBudget)
        if (y >= 46 && row >= 0 && row < 4) {
            budgetSel = row;
            if (x >= 234 && x < 274) delta = -1;
            else if (x >= 276) delta = 1;
        } else if (y >= 200 && x >= 240) {
            mode = MODE_PLAY; tstate = T_DONE; return;                                   // Done
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
        cityEdited = true;
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
    case MODE_HELP: handleHelpInput(in); break;
    case MODE_LOADCITY: handleLoadCityInput(in); break;
    case MODE_QUERY: if ((in.down & (BTN_A | BTN_B)) || in.touchDown) { mode = MODE_PLAY; query.valid = false; tstate = T_DONE; } break;
    }

    if (mode == MODE_PLAY || mode == MODE_QUERY) {
        tickSim(now);
        // MP-01: count play time; a gap over 100 ms (HOME Menu, sleep, a slow save) counts as 100 ms
        const uint64_t dt = lastFrameMs && now > lastFrameMs ? now - lastFrameMs : 0;
        playMsSinceSave += dt > 100 ? 100 : dt;
        if (playMsSinceSave >= kAutosaveMs) {
            const bool ok = saveCity();   // between sim ticks, on the main thread: the same save as Menu > Save city
            platform::log("timed autosave %s", ok ? "ok" : "FAILED");
            if (ok) pushMessage("City autosaved.", false);
            else pushMessage("Save failed - is the SD card full?", true);
        }
    }
    lastFrameMs = now;
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
// MP-U7: a tile takes the overlay colour only when it has something to show; empty land and
// water keep their own look, so the city's outline stays readable under an overlay
bool App::overlayShows(int v) const {
    if (v < 0) return false;
    if (overlay == OV_POWER) return true;
    if (overlay == OV_GROWTH) return abs(v - 128) > 6;
    return v > 12;
}

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
            if (!overlayShows(val)) continue;
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
    View v; topOrigin(v.ox, v.oy, s.w); v.ts = topTs(); v.sy0 = 0; v.w = s.w; v.h = TOP_H;
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

// The newest city message (8 s) as a banner: one line, or two broken at a space when it is too wide
// for this screen, never cut off. yLine is the top of a one-line banner; the two-line one grows
// upward (fromFoot) or downward. onlyImportant: the touch-screen copy (MP-09) shows disasters and
// other important messages only.
void App::drawMessageBanner(Screen &s, int TW, int yLine, bool growUp, bool onlyImportant) {
    const uint64_t now = platform::millis();
    if (nmsgs <= 0 || now - msgs[0].at >= 8000) return;
    if (onlyImportant && !msgs[0].important) return;
    const uint8_t mr = msgs[0].important ? 255 : 230, mg = msgs[0].important ? 200 : 230, mb = msgs[0].important ? 80 : 230;
    const int full = fb::textWidth(msgs[0].text, fb::FONT_BOLD) + 10;
    if (full <= TW) {
        fb::darken(s, 0, yLine, full, 17);
        fb::textB(s, 5, yLine + 1, msgs[0].text, mr, mg, mb);
        return;
    }
    char a[128], b[128];
    const char *t = msgs[0].text;
    int cut = 0;
    for (int i = (int)strlen(t) - 1; i > 0; --i) {
        if (t[i] != ' ') continue;
        snprintf(a, sizeof a, "%.*s", i, t);
        if (fb::textWidth(a, fb::FONT_BOLD) + 10 <= TW) { cut = i; break; }
    }
    if (cut <= 0) cut = (int)strlen(t) / 2;
    snprintf(a, sizeof a, "%.*s", cut, t);
    snprintf(b, sizeof b, "%s", msgs[0].text + cut + (msgs[0].text[cut] == ' ' ? 1 : 0));
    const int y0 = growUp ? yLine - 17 : yLine;
    fb::darken(s, 0, y0, TW, 34);
    fb::textB(s, 5, y0 + 1, a, mr, mg, mb);
    fb::textB(s, 5, y0 + 18, b, mr, mg, mb);
}

// Top screen HUD: the things you glance at while building on the bottom.
//   bar:    funds, date, population, speed, R C I demand
//   corner: whole-city minimap with the bottom map's frame
//   foot:   newest city message, and the selected tool with its price
void App::drawTopHud(Screen &s) {
    char line[128], num[32];
    // laid out for the screen it is drawn on: 400 on top, 320 on the touch screen when swapped
    // (principle: swapped text stays full size and readable, never squeezed or cut off)
    const int TW = s.w;
    const bool narrow = TW < 400;
    fb::darken(s, 0, 0, TW, 18);
    fmtThousands(num, sizeof(num), (long)sim.totalFunds);
    snprintf(line, sizeof(line), "$%s", num);
    fb::textB(s, 5, 2, line, sim.totalFunds < 500 ? 255 : 255, sim.totalFunds < 500 ? 110 : 240, sim.totalFunds < 500 ? 90 : 150);
    const int m = (sim.cityMonth >= 0 && sim.cityMonth < 12) ? (int)sim.cityMonth : 0;
    snprintf(line, sizeof(line), "%s %ld", kMonths[m], (long)sim.cityYear);
    fb::textB(s, narrow ? 66 : 104, 2, line, 230, 230, 230);
    fmtThousands(num, sizeof(num), (long)sim.cityPop);
    snprintf(line, sizeof(line), "Pop %s", num);
    fb::textB(s, narrow ? 136 : 184, 2, line, 230, 230, 230);
    const char *spd = sim.simSpeed == 0 ? (narrow ? "PAUSE" : "PAUSED") : sim.simSpeed == 1 ? "Slow" : sim.simSpeed == 2 ? (narrow ? "Med" : "Medium") : "Fast";
    fb::textB(s, narrow ? TW - 104 : 284, 2, spd, sim.simSpeed == 0 ? 255 : 170, sim.simSpeed == 0 ? 120 : 220, sim.simSpeed == 0 ? 120 : 170);
    // R C I demand: small bars around a centre line
    float rd, cd, id;
    sim.getDemands(&rd, &cd, &id);
    const float d[3] = { rd, cd, id };
    // MP-U3: white letters and 8-px bars in bright zone colours, so the demand reads at a glance
    static const uint8_t col[3][3] = { {110, 240, 110}, {120, 180, 255}, {255, 204, 64} };
    static const char *lab[3] = { "R", "C", "I" };
    for (int i = 0; i < 3; ++i) {
        const int x = TW - 56 + i * 19, mid = 9;
        fb::textB(s, x - 8, 2, lab[i], 255, 255, 255);
        fb::fillRect(s, x, 1, 8, 16, 52, 56, 72);
        int h = (int)(d[i] * 8); if (h > 8) h = 8; if (h < -8) h = -8;
        if (h > 0) fb::fillRect(s, x, mid - h, 8, h, col[i][0], col[i][1], col[i][2]);
        else if (h < 0) fb::fillRect(s, x, mid + 1, 8, -h, 255, 110, 90);
        fb::fillRect(s, x - 1, mid, 10, 1, 255, 255, 255);
    }

    // minimap, top-right (every 2nd tile: 60 x 50)
    fb::fillRect(s, TW - 64, 20, 64, 54, 0, 0, 0);
    drawMinimap(s, TW - 62, 22, 2);
    if (overlay != OV_NONE) {
        const int w = fb::textWidth(kOverlayNames[overlay]) + 6;
        fb::darken(s, TW - w, 75, w, 14);
        fb::text(s, TW - w + 3, 76, kOverlayNames[overlay], 200, 220, 255);
        // UI rule 20: a key with words, so the colours are never the only signal
        const char *lo = overlay == OV_POWER ? "off" : overlay == OV_GROWTH ? "fall" : "low";
        const char *hi = overlay == OV_POWER ? "on" : overlay == OV_GROWTH ? "rise" : "high";
        const int bw = 32, lw = fb::textWidth(lo), hw = fb::textWidth(hi);
        const int kw = lw + bw + hw + 12, kx = TW - kw;
        fb::darken(s, kx, 90, kw, 14);
        fb::text(s, kx + 3, 91, lo, 200, 220, 255);
        for (int i = 0; i < bw; i++) {
            uint8_t rgb[3];
            overlayColour(i * 255 / (bw - 1), rgb);
            fb::fillRect(s, kx + 6 + lw + i, 93, 1, 8, rgb[0], rgb[1], rgb[2]);
        }
        fb::text(s, kx + 9 + lw + bw, 91, hi, 200, 220, 255);
    }

    // foot: newest message (8 s), then the tool line
    const uint64_t now = platform::millis();
    drawMessageBanner(s, TW, TOP_H - 34, true, false);
    const ToolDef &t = kTools[toolIdx];
    if (now - lastDragAt < 2500 && lastDragCost > 0) {
        fmtThousands(num, sizeof(num), lastDragCost);
        snprintf(line, sizeof(line), "%s: spent $%s", t.name, num);
    } else if (tstate == T_PAINT) {
        fmtThousands(num, sizeof(num), (long)(dragFunds0 - sim.totalFunds));
        snprintf(line, sizeof(line), "%s: $%s so far", t.name, num);
    } else if (t.cost > 0) {
        snprintf(line, sizeof(line), "%s $%d%s  %s", t.name, t.cost, t.size == 1 ? "/tile" : "", t.desc);
        if (fb::textWidth(line) + 10 > TW) snprintf(line, sizeof(line), "%s $%d%s", t.name, t.cost, t.size == 1 ? "/tile" : "");
    } else {
        snprintf(line, sizeof(line), "%s  %s", t.name, t.desc);
        if (fb::textWidth(line) + 10 > TW) snprintf(line, sizeof(line), "%s", t.name);
    }
    fb::darken(s, 0, TOP_H - 17, TW, 17);
    fb::text(s, 5, TOP_H - 15, line, 255, 255, 170);

    if (mode == MODE_QUERY && query.valid) drawQuery(s);
}

void App::drawQuery(Screen &s) {
    char line[64];
    const int w = 300, h = 124, x0 = (s.w - w) / 2, y0 = 36;
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
            // MP-U7: only tiles that have something to show take the overlay colour; the rest stay
            // (dimmed) land and water, so the city's outline is still there
            if (overlayShows(ov)) { overlayColour(ov, p); continue; }
            const unsigned short v = sim.map[x][y];
            const int t = v & LOMASK;
            const uint8_t *c = kMiniLand;
            if (t >= WATER_LOW && t <= WATER_HIGH) c = kMiniWater;
            else if (t >= WOODS_LOW && t <= WOODS_HIGH) c = kMiniTree;
            else if (t >= FIREBASE && t <= LASTFIRE) c = kMiniFire;
            else if (t >= ROADBASE && t < RESBASE) c = kMiniRoad;
            else if (t >= PORTBASE && t <= LASTZONE) c = kMiniBig;
            else if (t >= RESBASE) c = kMiniZone;
            const int sh = overlay != OV_NONE ? 1 : 0;    // terrain dimmed under an overlay
            p[0] = c[0] >> sh; p[1] = c[1] >> sh; p[2] = c[2] >> sh;
        }
    }
    fb::rectOutline(s, x0 + camX / step, y0 + camY / step, mapCols() / step, (MAP_H / bts()) / step, 255, 255, 255);
}

static const char *kShortNames[16] = { "Res", "Com", "Ind", "Fire", "Cops", "Query", "Wire", "Doze",
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
    fb::fillRect(s, 6, 6, 72, 24, 36, 34, 22);
    fb::textB(s, 6 + (72 - fb::textWidth(line, fb::FONT_BOLD)) / 2, 11, line, 255, 240, 150);
    const int m = (sim.cityMonth >= 0 && sim.cityMonth < 12) ? (int)sim.cityMonth : 0;
    snprintf(line, sizeof(line), "%s %ld", kMonths[m], (long)sim.cityYear);
    fb::text(s, 82, 11, line, 210, 210, 210);
    static const char *sp[4] = { "Pause", "Slow", "Med", "Fast" };   // MP-X6: the same words as the top screen
    const int si = (sim.simSpeed >= 0 && sim.simSpeed < 4) ? (int)sim.simSpeed : 0;
    fb::fillRect(s, 134, 6, 40, 24, si == 0 ? 110 : 40, si == 0 ? 40 : 60, si == 0 ? 40 : 50);
    fb::textB(s, 134 + (40 - fb::textWidth(sp[si], fb::FONT_BOLD)) / 2, 11, sp[si], 240, 240, 240);
    fb::fillRect(s, 178, 6, 78, 24, overlay ? 40 : 28, overlay ? 50 : 28, overlay ? 90 : 36);
    snprintf(line, sizeof(line), "%s", overlay ? kOverlayNames[overlay] : "Overlay");
    fb::text(s, 178 + (78 - fb::textWidth(line)) / 2, 11, line, overlay ? 220 : 150, overlay ? 230 : 150, 255);
    const bool alert = alertX >= 0 && platform::millis() - alertAt < 30000;
    fb::fillRect(s, 260, 6, 24, 24, alert ? 150 : 28, alert ? 40 : 28, alert ? 30 : 36);
    fb::textB(s, 269, 11, "!", alert ? 255 : 80, alert ? 230 : 80, alert ? 120 : 90);
    fb::fillRect(s, 288, 6, 26, 24, 40, 40, 60);
    for (int i = 0; i < 3; ++i) fb::fillRect(s, 294, 12 + i * 5, 14, 2, 230, 230, 240);   // menu "hamburger"
}

// Strip under the map: 7 fixed tools + the "More" slot.
void App::drawToolStrip(Screen &s) {
    fb::fillRect(s, 0, STRIP_Y, BOT_W, STRIP_H, 16, 16, 22);
    for (int slot = 0; slot < 8; ++slot) {
        // MP-X2: the last slot is "More" (one tap: all 16 tools). While a tool from the grid is in
        // hand the slot shows that tool, lit, so the strip always shows what is selected.
        const bool more = slot == 7 && (stripSlotOf(toolIdx) >= 0 || toolPopup);
        const int ti = slot < 7 ? kStripTools[slot] : toolIdx;
        const int x = SLOT_X0 + slot * SLOT_STEP, by = STRIP_Y + 4, bh = STRIP_H - 10;   // 204..234
        const bool sel = more ? toolPopup : (ti == toolIdx);
        fb::fillRect(s, x, by, SLOT_W, bh, sel ? 90 : 38, sel ? 90 : 38, sel ? 150 : 50);
        if (more) {
            for (int gy = 0; gy < 3; ++gy)          // a little 4 x 3 grid of squares
                for (int gx = 0; gx < 4; ++gx)
                    fb::fillRect(s, x + 6 + gx * 6, by + 2 + gy * 5, 4, 3, 255, 204, 64);
            fb::text(s, x + SLOT_W / 2 - fb::textWidth("More") / 2, by + 16, "More", sel ? 255 : 235, sel ? 255 : 240, sel ? 160 : 250);
        } else {
            drawIcon(s, x + 1, by + 1, ti, SLOT_W - 2, 4, 16);
            fb::text(s, x + SLOT_W / 2 - fb::textWidth(kShortNames[ti]) / 2, by + 16, kShortNames[ti],
                     sel ? 255 : 210, sel ? 255 : 210, sel ? 160 : 210);
        }
        if (slot == 7 && !more) {   // a small ▲: tap again for the grid
            fb::fillRect(s, x + SLOT_W - 12, by, 12, 9, 70, 70, 110);
            for (int r = 0; r < 4; ++r) fb::fillRect(s, x + SLOT_W - 6 - r, by + 2 + r, 1 + 2 * r, 1, 255, 255, 200);
        }
        if (sel) fb::rectOutline(s, x, by, SLOT_W, bh, 255, 255, 120);
    }
}

// Every tool, 4 x 4, with its price: one tap picks it.
void App::drawToolPopup(Screen &s) {
    fb::fillRect(s, 0, MAP_Y0, BOT_W, MAP_H, 18, 20, 30);
    char line[32];
    for (int i = 0; i < kToolCount; ++i) {
        const int col = i % 4, row = i / 4;
        const int x = POP_X0 + col * POP_STEPX - 2, y = MAP_Y0 + 4 + row * POP_STEPY - 2;   // cell box at x+2, y+2
        const bool sel = (i == toolIdx);
        fb::fillRect(s, x + 2, y + 2, POP_W, POP_H, sel ? 80 : 36, sel ? 80 : 38, sel ? 140 : 52);
        drawIcon(s, x + 4, y + 3, i, 34, 0, std::min(ICON_H, 20));
        if (kTools[i].cost > 0) snprintf(line, sizeof(line), "$%d", kTools[i].cost);
        else snprintf(line, sizeof(line), "free");
        fb::text(s, x + POP_W - fb::textWidth(line), y + 6, line, 200, 230, 200);
        fb::text(s, x + 5, y + 24, kTools[i].name, 235, 235, 235);
        if (sel) fb::rectOutline(s, x + 2, y + 2, POP_W, POP_H, 255, 255, 120);
        if (i == gridSel) { fb::rectOutline(s, x, y, POP_W + 4, POP_H + 4, 255, 204, 64); fb::rectOutline(s, x + 1, y + 1, POP_W + 2, POP_H + 2, 255, 204, 64); }
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
    // MP-09: a disaster (or any important message) also shows here, on the map the player is looking at
    if (mode == MODE_PLAY && !toolPopup) drawMessageBanner(s, BOT_W, BAR_H + 2, false, true);

    if (mode == MODE_BUDGET) drawBudget(s);
    else if (mode == MODE_MENU) drawMenu(s);
}

void App::drawBudget(Screen &s) {
    fb::fillRect(s, 0, 0, BOT_W, BOT_H, 20, 24, 36);
    fb::textB(s, 8, 10, "BUDGET", 255, 255, 180);
    char line[80];
    snprintf(line, sizeof(line), "Taxes collected $%ld", (long)sim.taxFund);
    fb::text(s, 8, 28, line, 220, 220, 220);
    struct Row { const char *name; int pct; long fund; } rows[4] = {
        { "Tax rate", sim.cityTax, 0 },
        { "Roads",  (int)(sim.roadPercent * 100 + 0.5f), (long)sim.roadFund },
        { "Police", (int)(sim.policePercent * 100 + 0.5f), (long)sim.policeFund },
        { "Fire",   (int)(sim.firePercent * 100 + 0.5f), (long)sim.fireFund },
    };
    for (int i = 0; i < 4; ++i) {
        const int by = 48 + i * 30, y = by + 6;
        if (i == budgetSel) fb::fillRect(s, 6, by, 224, 24, 60, 60, 100);
        if (i == 0) snprintf(line, sizeof(line), "%-8s %3d%%", rows[i].name, rows[i].pct);
        else snprintf(line, sizeof(line), "%-8s %3d%% of $%ld", rows[i].name, rows[i].pct, rows[i].fund);
        fb::text(s, 10, y, line, 240, 240, 240);   // <= 36 chars = 216 px
        fb::fillRect(s, 236, by, 36, 24, 70, 50, 50);
        fb::textB(s, 251, y, "-", 255, 220, 220);
        fb::fillRect(s, 278, by, 36, 24, 50, 70, 50);
        fb::textB(s, 293, y, "+", 220, 255, 220);
    }
    snprintf(line, sizeof(line), "Cash flow  $%ld", (long)(sim.taxFund - (long)(sim.roadFund * sim.roadPercent)
             - (long)(sim.policeFund * sim.policePercent) - (long)(sim.fireFund * sim.firePercent)));
    fb::text(s, 8, 176, line, 200, 255, 200);
    fb::text(s, 8, 212, "\u25c4\u25ba or -/+ adjust", 150, 150, 160);
    fb::fillRect(s, 242, 204, 72, 30, 60, 60, 100);
    fb::textB(s, 242 + (72 - fb::textWidth("Done", fb::FONT_BOLD)) / 2, 213, "Done", 255, 255, 200);
}

void App::drawMenu(Screen &s) {
    PmBtn b[12]; int rows = 1;
    const int items = pmLayout(pmPage, b, rows);
    const char *q = nullptr, *q2 = nullptr, *q3 = nullptr, *yes = "YES";
    const char *title = "MENU";
    switch (pmPage) {
    case PM_SAVEFAIL: title = "SAVE FAILED"; q = "The city could not be saved."; q2 = "Is the SD card full? Exit anyway?"; yes = "EXIT ANYWAY"; break;
    case PM_EXIT: title = "EXIT GAME"; q = "Exit Micropolis?"; q2 = "Your city is saved first."; break;
    case PM_REPLACE:
        title = askKind == 2 ? "LOAD CITY" : "NEW CITY"; q = askLine1;
        if (askKind == 2 && citySel >= 0 && citySel < nCities) q2 = cityNames[citySel];
        q3 = cityEdited ? "Your city is kept: LOAD CITY lists it." : "You changed nothing in this city: it is not kept.";
        break;
    case PM_DISASTER_ASK: title = "DISASTERS"; q = kDisasterAsk[askArg % 6]; q2 = "It strikes your city at once."; break;
    case PM_CITIES: title = "NEW CITY"; break;
    case PM_SETTINGS: title = "SETTINGS"; break;
    case PM_DISASTERS: title = "DISASTERS"; break;
    }
    houseFrame(s, title, pmHint(pmPage == PM_MAIN));
    if (pmPage == PM_MAIN) {   // port version (git describe of micropolis-3ds), right of the title
        const char *v = "Build " PORT_VERSION;
        fb::text(s, BOT_W - 8 - fb::textWidth(v), 7, v, 170, 185, 225);
    }
    if (q) {
        fb::textB(s, (BOT_W - fb::textWidth(q, fb::FONT_BOLD)) / 2, 56, q, 235, 240, 250);
        if (q2) fb::text(s, (BOT_W - fb::textWidth(q2)) / 2, 78, q2, q3 ? 235 : 170, q3 ? 240 : 185, q3 ? 250 : 225);
        if (q3) fb::text(s, (BOT_W - fb::textWidth(q3)) / 2, 98, q3, 170, 185, 225);
        pmButton(s, b[0], yes, menuSel == 0);
        pmButton(s, b[1], "NO", menuSel == 1);
        return;
    }
    for (int i = 0; i < items; ++i) {
        const char *label = "";
        switch (pmPage) {
        case PM_MAIN: label = kPmMain[i]; break;
        case PM_CITIES: label = kPmCities[i]; break;
        case PM_SETTINGS: label = i == 0 ? (platform::leftyOn() ? "LEFT HAND: ON" : "LEFT HAND: OFF") : i == 1 ? "DISASTERS..." : "BACK"; break;
        case PM_DISASTERS: label = kPmDisasters[i]; break;
        }
        pmButton(s, b[i], label, i == menuSel);
    }
    if (pmPage == PM_SETTINGS) {
        const char *n1 = "Left hand: the D-Pad and A/B/X/Y swap jobs,";
        const char *n2 = "so the stylus can be held in the left hand.";
        fb::text(s, (BOT_W - fb::textWidth(n1)) / 2, 140, n1, 170, 185, 225);
        fb::text(s, (BOT_W - fb::textWidth(n2)) / 2, 156, n2, 170, 185, 225);
    }
    if (pmPage == PM_DISASTERS) {
        const char *n1 = "Each one asks before it starts.";
        fb::text(s, (BOT_W - fb::textWidth(n1)) / 2, 190, n1, 170, 185, 225);
    }
}

// ---- Manual (docs/manual.txt -> assets/manual.h) ---------------------------

void App::handleHelpInput(const Input &in) {
    manual3ds::Input mi;
    static const struct { uint32_t b; uint32_t k; bool rep; } map[] = {
        { BTN_UP, manual3ds::K_UP, true }, { BTN_DOWN, manual3ds::K_DOWN, true },
        { BTN_CUP, manual3ds::K_SCROLL_UP, true }, { BTN_CDOWN, manual3ds::K_SCROLL_DOWN, true },
        { BTN_LEFT, manual3ds::K_LEFT, true }, { BTN_RIGHT, manual3ds::K_RIGHT, true },
        { BTN_L, manual3ds::K_L, false }, { BTN_R, manual3ds::K_R, false },
        { BTN_A, manual3ds::K_A, false }, { BTN_B, manual3ds::K_B, false },
        { BTN_START, manual3ds::K_START, false },
    };
    for (auto &m : map)
        if ((m.rep ? in.repeat : in.down) & m.b) mi.down |= m.k;
    mi.touch_down = in.touchDown;
    mi.tx = in.touchX; mi.ty = in.touchY;
    mi.has_hold = true; mi.touch_held = in.touch; mi.hx = in.touchX; mi.hy = in.touchY;   // drag to scroll
    mi.cpad_y = in.cpadY;
    g_manual.set_toggle(platform::leftyOn() ? "Left hand: on" : "Left hand: off");
    g_manual.set_footer(platform::leftyOn() ? "Sticks: scroll   A/B/X/Y, L / R: section   D-Down / START: close" : "");
    const bool open = g_manual.update(mi);
    if (g_manual.take_toggle()) platform::setLefty(!platform::leftyOn());
    if (!open) mode = MODE_PLAY;
}

void App::drawHelp(Screen &s) {
    manual3ds::Canvas c = { s.w, s.h, s.rgb };
    g_manual.draw_bottom(c);
}

// ---- Load city ------------------------------------------------------------
// MP-X5: a row says what the city is (date, people, money), read from the file's header, not its
// file name. The classic .cty layout: six 480-byte histories, then 120 big-endian shorts of
// "misc" values (2-4: R, C, I population; 8-9: city time; 50-51: funds), then the map.
static bool cityInfo(const char *path, char *out, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    unsigned char h[240];
    const bool ok = fseek(f, 6 * 480, SEEK_SET) == 0 && fread(h, 1, sizeof(h), f) == sizeof(h);
    fclose(f);
    if (!ok) return false;
    #define MISC(i) ((int)(short)((h[(i) * 2] << 8) | h[(i) * 2 + 1]))
    const long time = ((long)(unsigned short)MISC(8) << 16) | (unsigned short)MISC(9);
    const long funds = (long)(int32_t)(((uint32_t)(unsigned short)MISC(50) << 16) | (unsigned short)MISC(51));
    const long pop = ((long)MISC(2) + ((long)MISC(3) + MISC(4)) * 8L) * 20L;
    #undef MISC
    static const char *mon[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    char a[24], b[24];
    fmtThousands(a, sizeof(a), pop < 0 ? 0 : pop);
    fmtThousands(b, sizeof(b), funds < 0 ? 0 : funds);
    const long t = time < 0 ? 0 : time;
    snprintf(out, n, "%s %ld, pop %s, $%s", mon[(t % 48) / 4], 1900 + t / 48, a, b);
    return true;
}

void App::scanCities() {
    nCities = 0;
    hasAutosave = false;
    char dir[256], info[72];
    {
        snprintf(cityPaths[0], sizeof(cityPaths[0]), "%s/autosave.cty", platform::saveDir());
        if (cityInfo(cityPaths[0], info, sizeof(info))) {
            hasAutosave = true;
            snprintf(cityNames[nCities++], sizeof(cityNames[0]), "Last save: %s", info);
        }
    }
    // the cities the game kept when they were replaced, newest first
    snprintf(dir, sizeof(dir), "%s/kept", platform::saveDir());
    if (DIR *d = opendir(dir)) {
        int nums[80], n = 0;
        struct dirent *e;
        while ((e = readdir(d))) { const int k = keptNumber(e->d_name); if (k >= 0 && n < 80) nums[n++] = k; }
        closedir(d);
        std::sort(nums, nums + n);
        for (int i = n - 1; i >= 0 && nCities < kMaxCities - 1; --i) {
            snprintf(cityPaths[nCities], sizeof(cityPaths[0]), "%s/kept-%04d.cty", dir, nums[i]);
            if (!cityInfo(cityPaths[nCities], info, sizeof(info))) continue;
            snprintf(cityNames[nCities], sizeof(cityNames[0]), "Earlier: %s", info);
            ++nCities;
        }
    }
    const int first = nCities;
    snprintf(dir, sizeof(dir), "%s/cities", platform::saveDir());
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && nCities < kMaxCities) {
        const char *n = e->d_name;
        const size_t len = strlen(n);
        if (len < 5 || strcasecmp(n + len - 4, ".cty") != 0) continue;
        if ((size_t)snprintf(cityPaths[nCities], sizeof(cityPaths[0]), "%s/%s", dir, n) >= sizeof(cityPaths[0])) continue;   // name too long to open
        snprintf(cityNames[nCities], sizeof(cityNames[0]), "%.*s", (int)(len - 4), n);
        if (strlen(n) - 4 > 49) memcpy(cityNames[nCities] + 46, "...", 4);   // 49 x 6 px fits 296
        ++nCities;
    }
    closedir(d);
    // simple insertion sort by name (the game's own saves stay first)
    for (int i = first + 1; i < nCities; ++i)
        for (int j = i; j > first && strcasecmp(cityNames[j - 1], cityNames[j]) > 0; --j) {
            std::swap_ranges(cityNames[j], cityNames[j] + sizeof(cityNames[0]), cityNames[j - 1]);
            std::swap_ranges(cityPaths[j], cityPaths[j] + sizeof(cityPaths[0]), cityPaths[j - 1]);
        }
}

void App::handleLoadCityInput(const Input &in) {
    if (in.down & BTN_START) { mode = MODE_PLAY; pmPage = PM_MAIN; tstate = T_DONE; return; }
    if (in.down & BTN_B) { mode = MODE_MENU; pmPage = PM_MAIN; menuSel = 3; return; }
    if (in.touchDown && in.touchY >= 194 && in.touchX >= 236) { mode = MODE_MENU; pmPage = PM_MAIN; menuSel = 3; return; }
    if (nCities == 0) return;
    if (in.repeat & BTN_UP) citySel = (citySel + nCities - 1) % nCities;
    if (in.repeat & BTN_DOWN) citySel = (citySel + 1) % nCities;
    if (in.repeat & BTN_LEFT) citySel = citySel >= 6 ? citySel - 6 : citySel;
    if (in.repeat & BTN_RIGHT) citySel = citySel + 6 < nCities ? citySel + 6 : citySel;
    bool pick = (in.down & BTN_A) != 0;
    if (in.touchDown && in.touchY >= 28 && in.touchY < 28 + 6 * 28) {
        const int i = (citySel / 6) * 6 + (in.touchY - 28) / 28;
        if (i < nCities) { citySel = i; pick = true; }
    }
    if (pick) { tstate = T_DONE; askReplace(2, citySel); }   // MP-Q2: ask first
}

void App::drawLoadCity(Screen &s) {
    houseFrame(s, "LOAD CITY", platform::leftyOn() ? "D-Right Load   D-Down Back" : "A Load   B Back");
    if (nCities == 0) {
        fb::text(s, 8, 34, "No saved city yet.", 235, 240, 250);
        fb::text(s, 8, 56, "SAVE CITY in the menu saves the city in play.", 170, 185, 225);
        fb::text(s, 8, 72, "Classic .cty city files can be copied to", 170, 185, 225);
        fb::text(s, 8, 86, "sdmc:/3ds/micropolis/cities/", 200, 220, 255);
    } else {
        const int perPage = 6, first = (citySel / perPage) * perPage;
        for (int i = first; i < nCities && i < first + perPage; ++i) {
            const int y = 30 + (i - first) * 28;
            fb::fillRect(s, 6, y, BOT_W - 12, 24, 40, 56, 120);
            fb::rectOutline(s, 6, y, BOT_W - 12, 24, 120, 150, 230);
            if (i == citySel) { fb::rectOutline(s, 4, y - 2, BOT_W - 8, 28, 255, 204, 64); fb::rectOutline(s, 5, y - 1, BOT_W - 10, 26, 255, 204, 64); }
            fb::text(s, 12, y + 6, cityNames[i], 235, 240, 250);
        }
        char line[64];
        if (nCities > perPage) snprintf(line, sizeof(line), "%d of %d   D-Pad left / right: page", citySel + 1, nCities);
        else snprintf(line, sizeof(line), "%d of %d", citySel + 1, nCities);
        fb::text(s, 8, 204, line, 170, 185, 225);
    }
    const PmBtn back = { 242, 196, 72, 24 };
    pmButton(s, back, "BACK", false);
}

void App::render(float slider) {
    if (mode == MODE_HELP) {
        manual3ds::Canvas c = { top.w, top.h, top.rgb };
        g_manual.draw_top(c);
        drawBottom();
        platform::present(top, nullptr, bottom);
        return;
    }
    overlayMaxCached = (overlay == OV_NONE) ? 255 : overlayMax();
    if (platform::swapped()) {
        // swapped: the dashboard goes on the touch screen, so it is drawn at 320 wide (full-size
        // text) rather than drawn at 400 and squeezed; no stereo there
        static uint8_t dashBuf[BOT_W * BOT_H * 3];
        Screen dash = { BOT_W, BOT_H, dashBuf };
        drawCity(dash, 0, 0.f);
        drawBottom();
        platform::present(dash, nullptr, bottom);
        return;
    }
    drawCity(top, slider > 0.f ? -1 : 0, slider);
    if (slider > 0.f) drawCity(topR, 1, slider);
    drawBottom();
    platform::present(top, slider > 0.f ? &topR : nullptr, bottom);
}
