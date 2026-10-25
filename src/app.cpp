// Micropolis 3DS frontend: game state, rendering, input.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
static inline float parallaxZoom(int zoom) { return zoom == 2 ? 1.5f : 1.f; }

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
            va_arg(args, int); va_arg(args, int);   // x, y
            va_arg(args, int);                       // picture
            const int important = va_arg(args, int);
            if (num > 0 && num < kMessageCount)
                pushMessage(kMessages[num], important != 0);
        }
    } else if (!strcmp(name, "autoGoto")) {
        const int x = va_arg(args, int), y = va_arg(args, int);
        centerOn(x, y);
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

void App::centerOn(int tx, int ty) {
    camX = tx - visibleCols() / 2;
    camY = ty - visibleRows() / 2;
    curX = tx;
    curY = ty;
    clampCamera();
}

void App::clampCamera() {
    const int maxX = WORLD_W - (TOP_W / (16 * zoom));
    const int maxY = WORLD_H - (TOP_H / (16 * zoom));
    if (camX > maxX) camX = maxX;
    if (camY > maxY) camY = maxY;
    if (camX < 0) camX = 0;
    if (camY < 0) camY = 0;
    if (curX < 0) curX = 0; if (curX >= WORLD_W) curX = WORLD_W - 1;
    if (curY < 0) curY = 0; if (curY >= WORLD_H) curY = WORLD_H - 1;
}

int App::toolCost(EditingTool t) const {
    for (int i = 0; i < kToolCount; ++i)
        if (kTools[i].tool == t) return kTools[i].cost;
    return 0;
}

void App::applyTool() {
    const ToolDef &t = kTools[toolIdx];
    const ToolResult r = sim.doTool(t.tool, (short)curX, (short)curY);
    if (r == TOOLRESULT_NO_MONEY) pushMessage("Insufficient funds to build that.", true);
    else if (r == TOOLRESULT_NEED_BULLDOZE) pushMessage("Area must be bulldozed first.", true);
}

void App::handlePlayInput(const Input &in, uint64_t now) {
    // --- cursor / camera
    int dx = 0, dy = 0;
    if (in.repeat & BTN_LEFT) dx = -1;
    if (in.repeat & BTN_RIGHT) dx = 1;
    if (in.repeat & BTN_UP) dy = -1;
    if (in.repeat & BTN_DOWN) dy = 1;
    // circle pad scrolls the camera continuously
    if (abs(in.cpadX) > 40 || abs(in.cpadY) > 40) {
        if (now - lastRepeatMs > 60) {
            lastRepeatMs = now;
            camX += (in.cpadX > 40) - (in.cpadX < -40);
            camY += (in.cpadY < -40) - (in.cpadY > 40);
            curX += (in.cpadX > 40) - (in.cpadX < -40);
            curY += (in.cpadY < -40) - (in.cpadY > 40);
        }
    }
    if (dx || dy) {
        curX += dx; curY += dy;
        clampCamera();
        // keep the cursor on screen with a margin
        const int cols = TOP_W / (16 * zoom), rows = TOP_H / (16 * zoom);
        if (curX < camX + 2) camX = curX - 2;
        if (curX > camX + cols - 3) camX = curX - cols + 3;
        if (curY < camY + 2) camY = curY - 2;
        if (curY > camY + rows - 3) camY = curY - rows + 3;
        // dragging: A held while moving applies the tool along the path
        if (dragging) applyTool();
    }
    clampCamera();

    // --- tools
    if (in.down & BTN_A) { applyTool(); dragging = true; }
    if (!(in.held & BTN_A)) dragging = false;
    if (in.down & BTN_L) toolIdx = (toolIdx + kToolCount - 1) % kToolCount;
    if (in.down & BTN_R) toolIdx = (toolIdx + 1) % kToolCount;
    if (in.down & BTN_B) { toolIdx = 7; }                       // bulldozer
    if (in.down & BTN_Y) { zoom = (zoom == 1) ? 2 : 1; centerOn(curX, curY); }
    if (in.down & BTN_X) { mode = MODE_BUDGET; budgetSel = 0; }
    if (in.down & BTN_START) { mode = MODE_MENU; menuSel = 0; }
    if (in.down & BTN_ZL) overlay = (overlay + OV_COUNT - 1) % OV_COUNT;
    if (in.down & BTN_ZR) overlay = (overlay + 1) % OV_COUNT;
    if (in.down & BTN_SELECT) {
        int sp = sim.simSpeed + 1;
        if (sp > 3) sp = 0;
        sim.setSpeed((short)sp);
    }

    // --- touch: toolbar / minimap on the bottom screen
    if (in.touchDown) {
        const int tx = in.touchX, ty = in.touchY;
        // minimap: 120x100 at (4,26)
        if (tx >= 4 && tx < 124 && ty >= 26 && ty < 126) {
            centerOn(tx - 4, ty - 26);
        }
        // overlay label strip under the minimap: left half = previous, right = next
        if (tx >= 4 && tx < 124 && ty >= 128 && ty < 144) {
            overlay = (tx < 64) ? (overlay + OV_COUNT - 1) % OV_COUNT : (overlay + 1) % OV_COUNT;
        }
        // toolbar: 4 columns x 4 rows of 46x26 at (132,26)
        if (tx >= 132 && tx < 132 + 4 * 46 && ty >= 26 && ty < 26 + 4 * 26) {
            const int col = (tx - 132) / 46, row = (ty - 26) / 26;
            const int idx = row * 4 + col;
            if (idx < kToolCount) toolIdx = idx;
        }
        // speed buttons strip at y 132..150, x 132..
        if (ty >= 132 && ty < 150 && tx >= 132) {
            const int i = (tx - 132) / 37;
            if (i >= 0 && i <= 3) sim.setSpeed((short)i);
            else if (i == 4) { mode = MODE_BUDGET; budgetSel = 0; }
        }
    }
}

void App::handleMenuInput(const Input &in) {
    const int items = 14; // new city, 8 scenarios, load, disasters, save, help, resume
    if (in.repeat & BTN_UP) menuSel = (menuSel + items - 1) % items;
    if (in.repeat & BTN_DOWN) menuSel = (menuSel + 1) % items;
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_PLAY; return; }
    if (in.down & BTN_A) {
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
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_PLAY; return; }
    if (in.down & BTN_A) {
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
    if (in.down & (BTN_A | BTN_B | BTN_X | BTN_START)) mode = MODE_PLAY;
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
    case MODE_QUERY: if (in.down & (BTN_A | BTN_B)) { mode = MODE_PLAY; query.valid = false; } break;
    }

    if (mode == MODE_PLAY || mode == MODE_QUERY)
        tickSim(now);

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

// Draw every visible cell whose class matches `pass`; pass GROUND also lays a
// base tile under elevated cells so the parallax never exposes black.
void App::drawCityPass(Screen &s, int pass, int dx) {
    const int ts = 16 * zoom;
    const int cols = TOP_W / ts + 2, rows = TOP_H / ts + 1;
    for (int r = 0; r < rows; ++r) {
        const int ty = camY + r;
        if (ty < 0 || ty >= WORLD_H) continue;
        for (int c = -1; c < cols; ++c) {
            const int tx = camX + c;
            if (tx < 0 || tx >= WORLD_W) continue;
            const unsigned short v = sim.map[tx][ty];
            const int cls = depthClass(v);
            int t = v & LOMASK;
            const int sx = c * ts + dx, sy = r * ts;
            if (cls == pass) {
                // unpowered zone centres blink in the original; show a dim variant
                const bool dim = (v & ZONEBIT) && !(v & PWRBIT) && (sim.blinkFlag < 0);
                if (zoom == 1) fb::tile(s, sx, sy, t, dim); else fb::tile2x(s, sx, sy, t);
            } else if (pass == DEPTH_GROUND && cls > DEPTH_GROUND) {
                // Fill behind an elevated tile so parallax never exposes black:
                // prefer the nearest ground-level neighbour in this row, else the tile itself.
                int fill = t;
                for (int d = 1; d <= 2; ++d) {
                    if (tx - d >= 0 && depthClass(sim.map[tx - d][ty]) <= DEPTH_GROUND) { fill = sim.map[tx - d][ty] & LOMASK; break; }
                    if (tx + d < WORLD_W && depthClass(sim.map[tx + d][ty]) <= DEPTH_GROUND) { fill = sim.map[tx + d][ty] & LOMASK; break; }
                }
                if (zoom == 1) fb::tile(s, sx, sy, fill); else fb::tile2x(s, sx, sy, fill);
            }
        }
    }
}

// Blend the overlay colour over each visible tile (checkerboard, so the city
// stays legible underneath).
void App::drawOverlayTint(Screen &s, int dx) {
    const int ts = 16 * zoom;
    const int cols = TOP_W / ts + 2, rows = TOP_H / ts + 1;
    for (int r = 0; r < rows; ++r) {
        const int ty = camY + r;
        if (ty < 0 || ty >= WORLD_H) continue;
        for (int c = -1; c < cols; ++c) {
            const int tx = camX + c;
            if (tx < 0 || tx >= WORLD_W) continue;
            const int v = overlayValue(tx, ty);
            if (v < 0) continue;
            uint8_t rgb[3]; overlayColour(v, rgb);
            const int sx = c * ts + dx, sy = r * ts;
            for (int y = 0; y < ts; ++y) {
                const int yy = sy + y; if (yy < 0 || yy >= s.h) continue;
                for (int x = (y & 1); x < ts; x += 2) {
                    const int xx = sx + x; if (xx < 0 || xx >= s.w) continue;
                    uint8_t *p = s.px(xx, yy);
                    p[0] = (p[0] + rgb[0]) / 2; p[1] = (p[1] + rgb[1]) / 2; p[2] = (p[2] + rgb[2]) / 2;
                }
            }
        }
    }
}

void App::drawSprites(Screen &s, int dx) {
    for (SimSprite *sp = sim.spriteList; sp; sp = sp->next) {
        if (sp->frame == 0) continue;
        const int sx = (sp->x - camX * 16) * zoom + dx;
        const int sy = (sp->y - camY * 16) * zoom;
        fb::sprite(s, sx, sy, sp->type, sp->frame, zoom);
    }
}

void App::drawCursor(Screen &s, int dx) {
    const int ts = 16 * zoom;
    const ToolDef &t = kTools[toolIdx];
    const int size = t.size;
    // tool footprint: 3x3 and 4x4 tools are placed by their centre (-1 offset)
    const int off = (size >= 3) ? 1 : 0;
    const int x = (curX - off - camX) * ts + dx, y = (curY - off - camY) * ts;
    const bool blink = (frameCounter / 15) & 1;
    const uint8_t c = blink ? 255 : 180;
    fb::rectOutline(s, x - 1, y - 1, size * ts + 2, size * ts + 2, c, c, 40);
    fb::rectOutline(s, x, y, size * ts, size * ts, 0, 0, 0);
}

void App::drawCity(Screen &s, int eye, float slider) {
    fb::clear(s, 0, 0, 0);
    // The cursor sits on the surface it points at: the ground for empty land,
    // roads and water, or the top of the tallest building under its footprint.
    // It is drawn right after that layer, so nearer (taller) layers can still
    // cover it -- the same depth rule as everything else.
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
            const float par = (float)(pass - DEPTH_GROUND) * kParallaxPx * slider * parallaxZoom(zoom);
            dx = -(int)(eye * par + (eye * par >= 0 ? 0.5f : -0.5f));
        }
        if (pass == DEPTH_SPRITE) drawSprites(s, dx);
        else drawCityPass(s, pass, dx);
        if (pass == DEPTH_GROUND && overlay != OV_NONE && overlayOnCity)
            drawOverlayTint(s, dx);
        if (pass == cursorPass) drawCursor(s, dx);
    }
    drawTopHud(s);
}

void App::drawTopHud(Screen &s) {
    // Newest message as a strip at the bottom of the top screen (fades after 6s)
    if (nmsgs > 0 && platform::millis() - msgs[0].at < 6000) {
        const int w = fb::textWidth(msgs[0].text, fb::FONT_BOLD) + 10;
        fb::fillRect(s, 0, TOP_H - 17, w, 17, 0, 0, 0);
        fb::textB(s, 5, TOP_H - 15, msgs[0].text, msgs[0].important ? 255 : 220, msgs[0].important ? 200 : 220, msgs[0].important ? 80 : 220);
    }
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
    fb::text(s, x0 + 8, y0 + h - 16, "A/B: close", 150, 150, 150);
}

void App::drawMinimap(Screen &s, int x0, int y0) {
    for (int y = 0; y < WORLD_H; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            if (overlay != OV_NONE) {
                const int ov = overlayValue(x, y);
                uint8_t *p = s.px(x0 + x, y0 + y);
                if (ov < 0) { p[0] = 20; p[1] = 20; p[2] = 24; }
                else overlayColour(ov, p);
                continue;
            }
            const unsigned short v = sim.map[x][y];
            const int t = v & LOMASK;
            const uint8_t *c = kMiniLand;
            if (t >= WATER_LOW && t <= WATER_HIGH) c = kMiniWater;
            else if (t >= WOODS_LOW && t <= WOODS_HIGH) c = kMiniTree;
            else if (t >= FIREBASE && t <= LASTFIRE) c = kMiniFire;
            else if (t >= ROADBASE && t < RESBASE) c = kMiniRoad;
            else if (t >= PORTBASE && t <= LASTZONE) c = kMiniBig;
            else if (t >= RESBASE) c = kMiniZone;
            uint8_t *p = s.px(x0 + x, y0 + y);
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
        }
    }
    // viewport rectangle
    fb::rectOutline(s, x0 + camX, y0 + camY, TOP_W / (16 * zoom), TOP_H / (16 * zoom), 255, 255, 255);
    fb::rectOutline(s, x0 - 1, y0 - 1, WORLD_W + 2, WORLD_H + 2, 90, 90, 90);
}

void App::drawToolbar(Screen &s, int x0, int y0) {
    for (int i = 0; i < kToolCount; ++i) {
        const int col = i % 4, row = i / 4;
        const int x = x0 + col * 46, y = y0 + row * 26;
        const bool sel = (i == toolIdx);
        fb::fillRect(s, x, y, 44, 24, sel ? 90 : 40, sel ? 90 : 40, sel ? 150 : 50);
        // icons are 56x24; show the left 44 px (the pictogram is left-aligned)
        int ic = -1;
        for (int k = 0; k < ICON_COUNT; ++k)
            if (!strcmp(mp_icon_names[k], kTools[i].icon)) { ic = k; break; }
        if (ic >= 0) {
            // clipped blit
            for (int yy = 0; yy < 24; ++yy)
                for (int xx = 0; xx < 44; ++xx) {
                    const unsigned char idx = mp_icons[ic][yy * ICON_W + xx];
                    if (idx == PAL_TRANSPARENT) continue;
                    const unsigned char *c = mp_palette[idx];
                    uint8_t *p = s.px(x + xx, y + yy);
                    p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
                }
        }
        if (sel) fb::rectOutline(s, x, y, 44, 24, 255, 255, 120);
    }
}

void App::drawMessages(Screen &s, int x0, int y0, int w, int h) {
    fb::fillRect(s, x0, y0, w, h, 14, 14, 18);
    const int lineH = 14, maxLines = (h - 2) / lineH;
    int line = 0;
    for (int i = 0; i < nmsgs && line < maxLines; ++i) {
        const uint8_t g = (uint8_t)(225 - i * 35);
        line += fb::textWrap(s, x0 + 4, y0 + 3 + line * lineH, w - 8, lineH, maxLines - line, msgs[i].text,
                             msgs[i].important ? 255 : g, g, g);
    }
}

void App::drawBottom() {
    Screen &s = bottom;
    fb::clear(s, 24, 24, 30);

    // status strip (bold)
    char line[96];
    static const char *months[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
    const int m = (sim.cityMonth >= 0 && sim.cityMonth < 12) ? (int)sim.cityMonth : 0;
    snprintf(line, sizeof(line), "$%ld", (long)sim.totalFunds);
    fb::fillRect(s, 0, 0, BOT_W, 22, 10, 10, 14);
    fb::textB(s, 5, 4, line, 255, 240, 150);
    snprintf(line, sizeof(line), "%s %ld", months[m], (long)sim.cityYear);
    fb::textB(s, 104, 4, line, 230, 230, 230);
    snprintf(line, sizeof(line), "Pop %ld", (long)sim.cityPop);
    fb::textB(s, 176, 4, line, 230, 230, 230);
    const char *spd = sim.simSpeed == 0 ? "PAUSED" : sim.simSpeed == 1 ? "slow" : sim.simSpeed == 2 ? "medium" : "fast";
    fb::textB(s, BOT_W - 5 - fb::textWidth(spd, fb::FONT_BOLD), 4, spd,
              sim.simSpeed == 0 ? 255 : 160, sim.simSpeed == 0 ? 120 : 220, sim.simSpeed == 0 ? 120 : 160);

    drawMinimap(s, 4, 26);
    snprintf(line, sizeof(line), "\u25c4 %s \u25ba", kOverlayNames[overlay]);
    fb::text(s, 4 + (120 - fb::textWidth(line)) / 2, 130, line, 200, 220, 255);
    drawToolbar(s, 132, 26);

    // speed / budget buttons
    static const char *sp[5] = { "||", ">", ">>", ">>>", "Budget" };
    for (int i = 0; i < 5; ++i) {
        const int x = 132 + i * 37, bw = 36;
        const bool on = (i < 4 && sim.simSpeed == i);
        fb::fillRect(s, x, 132, bw, 18, on ? 120 : 50, on ? 120 : 50, on ? 60 : 60);
        fb::text(s, x + (bw - fb::textWidth(sp[i])) / 2, 135, sp[i], 230, 230, 230);
    }

    // RCI demand bars (left) + tool / help (right)
    float rd, cd, id;
    sim.getDemands(&rd, &cd, &id);
    const float d[3] = { rd, cd, id };
    static const uint8_t col[3][3] = { {80, 200, 80}, {80, 120, 240}, {240, 200, 60} };
    static const char *lab[3] = { "R", "C", "I" };
    for (int i = 0; i < 3; ++i) {
        const int x = 10 + i * 16, mid = 168;
        fb::fillRect(s, x, mid - 16, 10, 32, 36, 36, 44);
        const int hgt = (int)(d[i] * 16);
        if (hgt >= 0) fb::fillRect(s, x, mid - hgt, 10, hgt, col[i][0], col[i][1], col[i][2]);
        else fb::fillRect(s, x, mid, 10, -hgt, col[i][0] / 2, col[i][1] / 2, col[i][2] / 2);
        fb::text(s, x + 2, mid + 16, lab[i], 200, 200, 200);
    }
    snprintf(line, sizeof(line), "%s   $%d", kTools[toolIdx].name, kTools[toolIdx].cost);
    fb::textB(s, 70, 152, line, 255, 255, 160);
    fb::textWrap(s, 70, 166, BOT_W - 74, 12, 1, kTools[toolIdx].desc, 200, 200, 210);
    fb::text(s, 70, 182, "A build  B dozer  Y zoom  Start menu", 130, 130, 140);

    fb::fillRect(s, 0, 196, BOT_W, 1, 60, 60, 70);
    drawMessages(s, 0, 197, BOT_W, BOT_H - 197);

    if (mode == MODE_BUDGET) drawBudget(s);
    else if (mode == MODE_MENU) drawMenu(s);
    else if (mode == MODE_DISASTERS) drawDisasters(s);
    else if (mode == MODE_HELP) drawHelp(s);
    else if (mode == MODE_LOADCITY) drawLoadCity(s);
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
    }
    snprintf(line, sizeof(line), "Cash flow  $%ld", (long)(sim.taxFund - (long)(sim.roadFund * sim.roadPercent)
             - (long)(sim.policeFund * sim.policePercent) - (long)(sim.fireFund * sim.firePercent)));
    fb::text(s, 30, 170, line, 200, 255, 200);
    fb::text(s, 30, 192, "Up/Down select  Left/Right adjust  A done", 150, 150, 160);
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
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_PLAY; return; }
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
    fb::text(s, 8, BOT_H - 14, "\u25c4\u25ba page  L/R chapter  tap title: index  B: back", 130, 130, 150);
}

// ---- Load city ------------------------------------------------------------
void App::scanCities() {
    nCities = 0;
    char dir[256];
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
    // simple insertion sort by name
    for (int i = 1; i < nCities; ++i)
        for (int j = i; j > 0 && strcasecmp(cityNames[j - 1], cityNames[j]) > 0; --j) {
            char tmp[40]; memcpy(tmp, cityNames[j], 40); memcpy(cityNames[j], cityNames[j - 1], 40); memcpy(cityNames[j - 1], tmp, 40);
        }
}

void App::handleLoadCityInput(const Input &in) {
    if (in.down & (BTN_B | BTN_START)) { mode = MODE_MENU; menuSel = 9; return; }
    if (nCities == 0) return;
    if (in.repeat & BTN_UP) citySel = (citySel + nCities - 1) % nCities;
    if (in.repeat & BTN_DOWN) citySel = (citySel + 1) % nCities;
    if (in.down & BTN_A) {
        char path[320];
        snprintf(path, sizeof(path), "%s/cities/%s", platform::saveDir(), cityNames[citySel]);
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
    fb::text(s, 8, BOT_H - 16, "A: load    B: back", 140, 140, 160);
}

void App::render(float slider) {
    overlayMaxCached = (overlay == OV_NONE) ? 255 : overlayMax();
    drawCity(top, slider > 0.f ? -1 : 0, slider);
    if (slider > 0.f) drawCity(topR, 1, slider);
    drawBottom();
    platform::present(top, slider > 0.f ? &topR : nullptr, bottom);
}
