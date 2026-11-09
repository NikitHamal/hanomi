// Hanomi native - game.js + entities.js + player.js + fx.js ported: world state, entities, AI, combat, farming, fishing, nations, raids, weather.
#pragma once
#include "world.h"
#include "terrain.h"
#include "chars.h"
#include "assets.h"
#include "audio.h"
enum { K_TAB = 9, K_ENTER = 13, K_SHIFT = 16, K_ESC = 27, K_SPACE = 32, K_LEFT = 37, K_UP, K_RIGHT, K_DOWN, K_F2 = 113, K_F11 = 122 };
struct Input { bool down[256] = {}, hit[256] = {}; float mx = 0, my = 0; bool mouseOn = false, mdown[3] = {}, mhit[3] = {}; int wheel = 0; float padX = 0, padY = 0; bool padUse = false, padAlt = false, padRoll = false; std::string typed;
  bool d(int k) const { return down[k & 255]; } bool p(int k) const { return hit[k & 255]; } void endFrame() { memset(hit, 0, sizeof hit); mhit[0] = mhit[1] = mhit[2] = false; wheel = 0; padUse = padAlt = padRoll = false; typed.clear(); } };
static Input IN;
// every sound name is the original js/audio.js SFX name (the native engine is a 1:1 port of that synth)
static void snd(const char* n, float vol = 1, float pitch = 1, float pan = 0) { Audio::play(n, vol, pitch, pan); }
inline double rnd() { return frand(); } inline double rndr(double a, double b) { return a + frand() * (b - a); } inline int rndi(int a, int b) { return (int)std::floor(rndr(a, b + 1)); }
template <class T> inline const T& rpk(const std::vector<T>& v) { return v[(size_t)(frand() * v.size())]; }
inline int dirOf(double dx, double dy) { return std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 0 : 2); }
static const int DIRV[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
inline double dist2d(double ax, double ay, double bx, double by) { double dx = ax - bx, dy = ay - by; return dx * dx + dy * dy; }

/* ---------------- fx (particles) ---------------- */
struct Particle { bool on = false; double x, y, z, vx, vy, vz, g, life, max, drag, ph; u32 c; int s; std::string k; bool top, fade; };
struct SpawnO { double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, g = 0, life = 1; u32 c = 0xFFFFFFFF; int s = 1; const char* k = "dot"; double drag = 1; bool top = false, fade = true; };
struct FX { std::vector<Particle> p; int n = 0; double shakeT = 0, shakeA = 0; FX() { p.resize(900); }
  Particle* spawn(const SpawnO& o) { Particle* q = nullptr; for (int k = 0; k < 40; k++) { Particle& c = p[(n++) % 900]; if (!c.on) { q = &c; break; } } if (!q) q = &p[(n++) % 900];
    q->on = true; q->x = o.x; q->y = o.y; q->z = o.z; q->vx = o.vx; q->vy = o.vy; q->vz = o.vz; q->g = o.g; q->life = 0; q->max = o.life; q->c = o.c; q->s = o.s; q->k = o.k; q->drag = o.drag; q->ph = rnd() * TAU; q->top = o.top; q->fade = o.fade; return q; }
  void burst(double x, double y, int n_, std::initializer_list<u32> cols, double sp0 = 15, double sp1 = 50, double vz0 = 20, double vz1 = 60, double gg = 160, double l0 = 0.4, double l1 = 0.8, double z = -1, const char* k = "dot") {
    std::vector<u32> cv(cols); for (int i = 0; i < n_; i++) { double a = rnd() * TAU, sp = rndr(sp0, sp1); SpawnO o; o.x = x; o.y = y; o.z = z >= 0 ? z : rndr(2, 8); o.vx = std::cos(a) * sp; o.vy = std::sin(a) * sp * 0.6; o.vz = rndr(vz0, vz1); o.g = gg; o.life = rndr(l0, l1); o.c = cv[(size_t)(rnd() * cv.size())]; o.drag = 0.9; o.k = k; spawn(o); } }
  void shake(double a, double t = 0.2) { if (a > shakeA) { shakeA = a; shakeT = t; } }
  void update(double dt) { if (shakeT > 0) { shakeT -= dt; if (shakeT <= 0) shakeA = 0; }
    for (auto& q : p) { if (!q.on) continue; q.life += dt; if (q.life >= q.max) { q.on = false; continue; } double d = std::pow(q.drag, dt * 60); q.vx *= d; q.vy *= d; q.x += q.vx * dt; q.y += q.vy * dt;
      if (q.g) { q.vz -= q.g * dt; q.z += q.vz * dt; if (q.z < 0) { q.z = 0; q.vz *= -0.3; q.vx *= 0.6; q.vy *= 0.6; } } else q.z += q.vz * dt; if (q.k == "petal" || q.k == "leaf") q.x += std::sin(q.life * 3 + q.ph) * 12 * dt; } }
};
inline u32 C3(int h, int a = 255) { return RGB((h >> 16) & 255, (h >> 8) & 255, h & 255, a); }

struct Game; struct Ent; using EP = std::shared_ptr<Ent>;
enum EK { E_PLAYER, E_VILLAGER, E_SOLDIER, E_ZOMBIE, E_DOG, E_ANIMAL, E_DROP, E_ARROW };
struct Stack { std::string id; int n = 0, d = 0; bool empty() const { return id.empty(); } };
struct AniDef { int hp; double sp; int flee; bool aggro; std::vector<Drop3> drops; std::vector<std::string> food; const char* sound; };
static std::unordered_map<std::string, AniDef> ANI;
static void initAni() { ANI["chicken"] = {4, 22, 1, false, {{"raw_meat", 1, 1}, {"feather", 1, 2}}, {"wheat_seeds", "rice_seeds"}, "chicken"}; ANI["cow"] = {14, 16, 0, false, {{"raw_meat", 2, 3}, {"leather", 1, 2}}, {"wheat"}, "cow"}; ANI["sheep"] = {10, 18, 0, false, {{"raw_meat", 1, 2}, {"wool", 1, 2}}, {"wheat"}, "sheep"};
  ANI["pig"] = {10, 18, 0, false, {{"raw_meat", 2, 3}}, {"carrot", "pumpkin"}, ""}; ANI["rabbit"] = {3, 46, 2, false, {{"raw_meat", 1, 1}, {"leather", 0, 1}}, {"carrot"}, ""}; ANI["deer"] = {10, 52, 2, false, {{"raw_meat", 2, 2}, {"leather", 1, 2}}, {"apple"}, ""};
  ANI["fox"] = {6, 58, 2, false, {{"leather", 0, 1}, {"bone", 0, 1}}, {"raw_meat"}, ""}; ANI["boar"] = {16, 40, 0, true, {{"raw_meat", 2, 3}, {"leather", 1, 1}}, {"mushroom"}, ""}; }
struct Goal2 { double x, y; std::string task; };
struct Ent : std::enable_shared_from_this<Ent> {
  // ---- locomotion (v5): smoothed velocity, distance-driven gait phase, facing hysteresis, stuck watchdog ----
  double svx = 0, svy = 0, mspd = 0, ph = 0, gait = 0, turnCd = 0, avoidT = 0, avoidA = 0, progT = 0, px0 = 0, py0 = 0, lx = 0, ly = 0, kbStep = 0, repathCd = 0, pathGx = 1e9, pathGy = 1e9, offT = 0, lungeT = 0, alertT = 0, jinkT = 0, restT = 0, hopZ = 0;
  int stuckN = 0, avoidSide = 1; bool walking = false, steered = false, failed = false, hitX = false, hitY = false, lxInit = false, follow = false, fleeing = false;
  Game* g; int kind; double x, y, vx = 0, vy = 0, z = 0, vz = 0, r = 4, hp = 10, maxHp = 10; bool dead = false; int dir = 0; double t = 0, flash = 0, kbx = 0, kby = 0; int id; std::string team = "nature"; double speed = 40; bool solidEnt = true, persist = false, swims = false, noWater = false, moving = false, isPlayer = false;
  double atkCd = 0, stuck = 0, ifr = 0; EP target; std::vector<std::pair<int, int>> path; int wlT = -1, wl = 0; bool wasWet = false; double rip = 0;
  // humanoid
  Look look; const CharFrames* frames = nullptr; int act = 0; double actT = 0, actDur = 0.3; std::string held, bubble; double bubbleT = 0; double walkFps = 8; double handx = 0, handy = 0; bool hasHand = false; std::vector<double> bloodCols;
  // villager / soldier / zombie
  std::string role = "villager", vid, name; double hx = 0, hy = 0, wanderT = 0, taskT = 0; bool inside = false, hasGoal = false; Goal2 goal; std::string task; EP chatWith; std::vector<std::pair<int, std::string>> chatLines; double chatT = 0; int doorX = -1, doorY = -1, homeSi = -1; std::string inRoom; unsigned seed = 0;
  bool archer = false, mine = false; int color = 0; std::string order; double postX = 0, postY = 0; int slot = 0, temp = 0; double dmg = 4; int light = 0; bool raid = false, hasRaidTo = false; double raidX = 0, raidY = 0; bool allyHelp = false;
  int zkind = 0; double groanT = 0, burn = 0;
  // dog
  std::string breed = "bernese", state = "sit"; double st = 0; int f = 1; double love = 0, down = 0, heartT = 0, barkT = 8; bool owned = false, wild = false, carry = false; EP fetch; std::string indoor;
  // animal
  std::string akind; double baby = 0, produceT = 100, milked = 0, panic = 0, love2 = 0; bool shorn = false, livestock = false, hasPen = false, angryOn = false; EP angry; Ent* panicFrom = nullptr; EP panicSrc; bool isDog = false;
  // drop / arrow
  std::string item; int n = 1; double life = 300, pickDelay = 0.4, zz = 9; EP src; bool fetchFlag = false;
  // player
  std::vector<Stack> inv; int sel = 0; Stack armor; int coins = 0; double food = 20, useCd = 0, rollT = 0, rollCd = 0, idleT = 0, hungerT = 0, regenT = 0, buff = 0, stepT = 0, rollVx = 0, rollVy = 0; bool facingLock = false, swingHit = false, alive = true; int swingK = 0;
  struct Fish { std::string st; double x0, y0, x, y, tx, ty, t, wait, prog, zone, zv, fishPos, fv, diff; std::string item, biome; } fish; bool fishing = false;
  Ent(Game* gg, int k, double X, double Y);
  int tx() const { return (int)std::floor(x / TS); } int ty() const { return (int)std::floor(y / TS); }
  inline bool hasTeam(const char* p) const { return team.compare(0, strlen(p), p) == 0; }
  bool isHuman() const { return kind == E_PLAYER || kind == E_VILLAGER || kind == E_SOLDIER || kind == E_ZOMBIE; }
  bool blocked(double px, double py);
  // axis-separated move with corner assist: when clipped by a tile edge, slide toward the nearest opening (<=5px) so nothing snags on trees/house corners
  bool move(double dx, double dy, bool assist = true) { hitX = hitY = false;
    if (dx != 0) { if (!blocked(x + dx, y)) x += dx; else { hitX = true; if (assist) nudge(0, dx); } }
    if (dy != 0) { if (!blocked(x, y + dy)) y += dy; else { hitY = true; if (assist) nudge(1, dy); } } return hitX || hitY; }
  void nudge(int axis, double d) { double m = std::min(1.6, std::fabs(d) * 1.25 + 0.2); for (int k = 1; k <= 5; k++) for (int s = -1; s <= 1; s += 2) { double ox = axis == 1 ? s * k : 0, oy = axis == 0 ? s * k : 0; if (blocked(x + (axis == 0 ? d : 0) + ox, y + (axis == 1 ? d : 0) + oy)) continue; double st = std::min((double)k, m); if (!blocked(x + (axis == 1 ? s * st : 0), y + (axis == 0 ? s * st : 0))) { if (axis == 1) x += s * st; else y += s * st; return; } } }
  void face(double dx, double dy, bool force = false) { double ax = std::fabs(dx), ay = std::fabs(dy); if (ax + ay < 1e-4) return; int nd = dirOf(dx, dy); if (nd == dir) return; if (!force) { bool hz = dir & 1; double cur = hz ? ax : ay, oth = hz ? ay : ax; if (cur >= oth * 0.72 || turnCd > 0) return; } dir = nd; turnCd = 0.14; }
  void faceX(double dx) { if (dx > 2.5) f = 1; else if (dx < -2.5) f = -1; }
  void drive(double ux, double uy, double sp, double dt, double accel = 11); void sampleMotion(double dt); double stride() const;
  int waterLevel(); void waterFx(double dt); void physics(double dt); bool hurt(double dmg, Ent* src, double kb = 90); bool seek(double tx, double ty, double sp, double dt, double stopAt = 2);
  void update(double dt); void die(Ent* src); void onHurt(Ent* src); void say(const std::string& s, double tt = 3) { bubble = s; bubbleT = tt; } void swing(double d = 0.32) { actT = actDur = d; }
  // player
  bool add(const std::string& id, int n = 1); int count(const std::string& id) const; bool removeItem(const std::string& id, int n = 1); void useUp(int slot = -1, int n = 1); void wear(int slot = -1);
  void updatePlayer(double dt); void updateVillager(double dt); void updateSoldier(double dt); void updateZombie(double dt); void updateDog(double dt); void updateWild(double dt); void updateAnimal(double dt); void updateDrop(double dt); void updateArrow(double dt);
  void updateVitals(double dt); void usePlayer(bool fresh); void updateSwing(double dt); void eat(); void plant(int tx, int ty, Item* it); void place(int tx, int ty, Item* it); void shoot(); void waterCan(int tx, int ty); void animalTool(const std::string& tool); void dropSel(); bool interact(); void cast(); void cancelFish(); void updateFish(double dt, bool press); bool canHit(Ent* e);
  void petDog(); void setShorn(bool v) { shorn = v; } const AniDef* def() const { auto it = ANI.find(akind); return it == ANI.end() ? nullptr : &it->second; } double shadowRx() const; 
};
struct Nation { std::string id, name, ruler, temper, status = "neutral"; int color; double rel; bool spawned = false, met = false; int troopsDay = -9; double x, y; int questItem = 0; std::string questId; int questN = 0, questReward = 0; bool hasQuest = false; };
struct Toast { std::string text, kind, icon; double t = 0, max = 3.2; };
enum Modal { M_NONE, M_TITLE, M_INV, M_CRAFT, M_CHEST, M_TRADE, M_TALK, M_CHIEF, M_BANNER, M_SOLDIER, M_MAP, M_REALM, M_JOURNAL, M_PAUSE, M_SETTINGS, M_HELP, M_NEW, M_LOAD, M_DEAD };
struct Settings { float music = 0.55f, sfx = 0.8f; int zoom = 0; bool shake = true; };
static Settings SET;
struct GoalDef { const char* t; int r; };
static const GoalDef GOALS[] = {{"Gather 10 wood", 10}, {"Craft a workbench", 10}, {"Throw a stick for your dog", 10}, {"Craft a stone pickaxe", 15}, {"Harvest a crop", 15}, {"Catch a fish", 15}, {"Feed the koi in Hanomi", 10}, {"Survive your first night", 20}, {"Pray at a wayside shrine", 20}, {"Find buried treasure", 20},
  {"Meet a neighbouring clan", 20}, {"Complete a chief's task", 30}, {"Smelt an iron ingot", 25}, {"Recruit a soldier", 25}, {"Survive a Blood Moon", 50}, {"Forge an iron sword", 40}, {"Form an alliance", 60}, {"Claim a village for Hanomi", 100}, {"Forge the Jade katana", 150}};
constexpr int NGOALS = sizeof(GOALS) / sizeof(GOALS[0]);
struct Room { std::string key, vid, kind; int si, x, y, w, h; double retx, rety; };
struct GameOpts { std::string name = "Sakura Vale"; u32 seed = 1; std::string difficulty = "normal"; int skin = 1, hair = 1, shirt = 0, style = HS_SHORT; std::string breed = "bernese", dogName = "Kuma"; };
struct Game {
  GameOpts opts; World world; FX fx; std::vector<EP> ents; EP player; std::unordered_map<i64, std::vector<Ent*>> grid; std::unordered_map<i64, std::pair<double, double>> dmg; std::unordered_map<i64, double> shakeTiles;
  double clock = DAY_LEN * (2.0 / 24), lastHour = 0; int day = 1; struct { double rain = 0, target = 0, t = 300, bolt = 6; } weather; std::map<std::string, Nation> nations; struct { int zombies = 0, fish = 0, built = 0, eaten = 0, born = 0, harvested = 0, mined = 0, chopped = 0, fetches = 0, prayed = 0, dug = 0, tasks = 0; std::set<std::string> made; } stats;
  struct { int x = 0, y = 0; bool aim = false, ok = false; } target; double hitstop = 0, playTime = 0, flash = 0, koiT = 0; bool paused = false; bool raidOn = false; double raidT = 0; double diff = 1; int goal = 0; std::map<std::string, int> prayed; int koiFeeds = 0, koiDay = 0; bool hasSpawnPoint = false; double spawnX = 0, spawnY = 0;
  std::map<std::string, Room> interiors; Room* inside = nullptr; bool fading = false; std::string mood = "day"; std::string saveId = "slot1";
  std::vector<Toast> toasts; std::string bannerT, bannerS; double bannerTime = 0; std::string lastToast; double lastToastT = -9; int nextId = 1; double now = 0;
  // ui hooks
  Modal modal = M_NONE; std::string modalArg; Ent* modalEnt = nullptr; Village* modalVillage = nullptr; std::string modalChest; std::string craftStation; bool showTarget = true;
  double camX = 0, camY = 0; int viewW = 480, viewH = 270; int scale = 4; double fadeT = 0; std::function<void()> fadeCb; int fadeState = 0; double sleepT = 0; std::function<void()> sleepCb; bool sleeping = false; std::function<void()> deathCb; double hurtT = 0;
  Game() {}
  void toast(const std::string& t, const std::string& kind = "", const std::string& icon = "") { Toast x; x.text = t; x.kind = kind; x.icon = icon; toasts.push_back(x); if (toasts.size() > 5) toasts.erase(toasts.begin()); }
  void toastOnce(const std::string& t) { if (lastToast == t && now - lastToastT < 2.5) return; lastToast = t; lastToastT = now; toast(t, "hint"); snd("error", 0.4f); }
  void banner(const std::string& a, const std::string& b) { bannerT = a; bannerS = b; bannerTime = 4.5; }
  void fade(std::function<void()> cb) { fadeCb = cb; fadeState = 1; fadeT = 0; fading = true; }
  /* ---- time ---- */
  double frac() const { return std::fmod(clock, (double)DAY_LEN) / DAY_LEN; } double hour() const { return std::fmod(frac() * 24 + 6, 24.0); }
  double daylight() const { double h = hour(); if (h >= 7 && h < 17.5) return 1; if (h >= 20 || h < 4.5) return 0; if (h < 7) return (h - 4.5) / 2.5; return 1 - (h - 17.5) / 2.5; }
  bool bloodMoon() const { return day % 5 == 0 && (hour() >= 20 || hour() < 5) && diff > 0; } bool raining() const { return weather.rain > 0.3; }
  /* ---- setup ---- */
  Village* home() { return world.villageForRegion(0, 0); }
  Village* villageById(const std::string& id) { size_t c = id.find(','); return world.villageForRegion(atoi(id.substr(0, c).c_str()), atoi(id.substr(c + 1).c_str())); }
  Nation* nationOf(const std::string& vid) { auto it = nations.find(vid); return it == nations.end() ? nullptr : &it->second; }
  Nation* nationState(Village* v) { if (!v) return nullptr; auto it = nations.find(v->id); if (it != nations.end()) return &it->second; Nation n; n.id = v->id; n.name = v->name; n.color = v->color; n.ruler = v->ruler; n.temper = v->temper;
    n.rel = v->temper == "peaceful" ? 60 : v->temper == "proud" ? 45 : v->temper == "greedy" ? 40 : v->temper == "warlike" ? 25 : v->temper == "honest" ? 50 : v->temper == "kind" ? 100 : 45; n.status = v->home ? "home" : "neutral"; n.x = v->x; n.y = v->y; n.met = v->home; return &(nations[v->id] = n); }
  EP add(EP e) { ents.push_back(e); return e; }
  EP mk(int kind, double x, double y) { return std::make_shared<Ent>(this, kind, x, y); }
  void newGame(const GameOpts& o);
  EP spawnVillager(double x, double y, const std::string& vid, const std::string& role, unsigned seed);
  EP spawnSoldier(double x, double y, const std::string& vid, int color, bool archer, bool mine, const std::string& order, double px, double py);
  EP spawnZombie(double x, double y, int kind); EP spawnDog(double x, double y, const std::string& breed, const std::string& name, bool owned, bool wild, double love = 0); EP spawnAnimal(const std::string& kind, double x, double y, bool pen = false, bool owned = false, double baby = 0);
  EP dropItem(const std::string& id, int n, double x, double y);
  void activateVillages(bool force = false);
  /* ---- queries ---- */
  void rebuildGrid() { for (auto it = grid.begin(); it != grid.end();) { if (it->second.empty() && ++gridIdle[it->first] > 600) { gridIdle.erase(it->first); it = grid.erase(it); } else { it->second.clear(); ++it; } }
    for (auto& e : ents) { if (e->dead || e->kind == E_DROP) continue; i64 k = pkey((int)std::floor(e->x / 64), (int)std::floor(e->y / 64)); grid[k].push_back(e.get()); } } std::unordered_map<i64, int> gridIdle;
  template <class F> std::vector<Ent*> query(double x, double y, double r, F f) { std::vector<Ent*> out; int c0 = (int)std::floor((x - r) / 64), c1 = (int)std::floor((x + r) / 64), r0 = (int)std::floor((y - r) / 64), r1 = (int)std::floor((y + r) / 64); double r2 = r * r;
    for (int cy = r0; cy <= r1; cy++) for (int cx = c0; cx <= c1; cx++) { auto it = grid.find(pkey(cx, cy)); if (it == grid.end()) continue; for (Ent* e : it->second) if (!e->dead && dist2d(e->x, e->y, x, y) <= r2 && f(e)) out.push_back(e); }
    Ent* p = player.get(); if (p && !p->dead && dist2d(p->x, p->y, x, y) <= r2 && f(p) && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p); return out; }
  // allocation-free visitors (hot paths: AI target scans, separation)
  template <class F> void query2(double x, double y, double r, F f) { int c0 = (int)std::floor((x - r) / 64), c1 = (int)std::floor((x + r) / 64), r0 = (int)std::floor((y - r) / 64), r1 = (int)std::floor((y + r) / 64); double r2 = r * r;
    for (int cy = r0; cy <= r1; cy++) for (int cx = c0; cx <= c1; cx++) { auto it = grid.find(pkey(cx, cy)); if (it == grid.end()) continue; for (Ent* e : it->second) if (!e->dead && dist2d(e->x, e->y, x, y) <= r2) f(e); } }
  template <class F> Ent* nearest(double x, double y, double r, F f) { Ent* best = nullptr; double bd = 1e18; Ent* p = player.get(); query2(x, y, r, [&](Ent* e) { double d = dist2d(e->x, e->y, x, y); if (d < bd && f(e)) { bd = d; best = e; } });
    if (p && !p->dead) { double d = dist2d(p->x, p->y, x, y); if (d <= r * r && d < bd && f(p)) { bool inGrid = false; (void)inGrid; best = p; } } return best; }
  EP sp(Ent* e) { if (!e) return nullptr; if (e == player.get()) return player; return e->weak_from_this().lock(); }
  bool hostile(Ent* a, Ent* b);
  bool holding(const std::vector<std::string>& foods) { Stack& s = player->inv[player->sel]; return !s.empty() && std::find(foods.begin(), foods.end(), s.id) != foods.end(); }
  bool entAtTile(int tx, int ty) { for (auto& e : ents) if (!e->dead && e->solidEnt && e->kind != E_DROP && e->kind != E_ARROW && e->tx() == tx && e->ty() == ty) return true; return player->tx() == tx && player->ty() == ty; }
  float pan(double x) { return (float)clampf((float)((x - player->x) / 200), -1, 1); } float vol(double x, double y) { return (float)clampf((float)(1 - std::hypot(x - player->x, y - player->y) / 260), 0, 1); }
  bool aimValid() const { return IN.mouseOn && modal == M_NONE; } void aimWorld(double& ax, double& ay) { ax = camX + IN.mx / scale; ay = camY + IN.my / scale; }
  std::vector<std::pair<int, int>> findPath(int sx, int sy, int tx, int ty, bool avoidWater = false);
  bool lit(int tx, int ty) { for (int y = ty - 5; y <= ty + 5; y++) for (int x = tx - 5; x <= tx + 5; x++) { int o = world.obj(x, y); if (o && OBJ[o].light) return true; } return false; }
  /* ---- world interaction ---- */
  void pickup(Ent* d); void shootArrow(Ent* src, double tx, double ty, double dmg); bool useTool(int tx, int ty, Item* it, Ent* src); void damageTile(int tx, int ty, double pow, Ent* src, bool zombie = false); void breakBlock(int tx, int ty, int b); void breakObj(int tx, int ty, int o, int m); void harvest(int tx, int ty);
  bool interactEnt(Ent* e); bool interactTile(int tx, int ty); void dig(int tx, int ty); void pray(int tx, int ty); void throwStick(double tx, double ty); bool feedKoi(Struct* s, Village* v); bool structAt(int tx, int ty, Village*& v, Struct*& s); Struct* doorAt(int tx, int ty, Village*& v);
  void enterHouse(Village* v, Struct* s); void exitHouse(); void buildInterior(Room& rec, Village* v); void trySleep(double ax, double ay);
  Room* interiorOf(const std::string& vid, int si) { auto it = interiors.find(vid + ":" + std::to_string(si)); return it == interiors.end() ? nullptr : &it->second; }
  /* ---- simulation ---- */
  void update(double dt); void tickTime(double dt); void onHour(int h); void dawn(); void middayEvents(); void warParty(Nation& n); void startRaid(); void tickWeather(double dt); std::string waterBiome(int tx, int ty); void randomTicks(double dt, bool fast = false); void tickSpawns();
  void onDeath(Ent* e, Ent* src); void playerDied(); std::vector<Ent*> army() { std::vector<Ent*> o; for (auto& e : ents) if (e->kind == E_SOLDIER && e->mine && !e->dead) o.push_back(e.get()); return o; }
  int armyCap() { int v = 0; for (auto& kv : nations) if (kv.second.status == "vassal") v++; return 6 + v * 3; } bool recruit(bool archer, double ax, double ay); bool conquer(Village* v); bool goalOk(int i);
  bool made(const std::string& id) { return stats.made.count(id) > 0; } int hungerMax() { return 20; }
  /* ---- saving ---- */
  std::string savePath, saveDir; bool saveGame(bool autos = false); bool loadGame(); bool loadFile(const std::string& path); bool loadBuf(const std::string& buf); std::string loadErr; bool hasSave = false; double autosaveT = 0, gl = 0, vs = 0, spT = 0, onsT = 0; bool onsMsg = false; double thumbT = 0; bool craftAt(const std::string& station, const std::string& out); bool canCraft(const Recipe& r); bool station(const std::string& st);
};
