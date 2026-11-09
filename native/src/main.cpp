// Hanomi native - entry point, frame loop, Win32 backend (GDI + waveOut + XInput) and a headless QA backend (PPM screenshots, scripted input).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#undef IN
#undef OUT
#undef near
#undef far
#undef RGB
#undef min
#undef max
#endif
#include "ui2.h"
#include <chrono>
#include <queue>
#include <set>
static Game* GM = nullptr; static bool gQuit = false, gShotReq = false; static Img FB; static int gWinW = 1280, gWinH = 720; static double gAcc = 0, gFpsT = 0; static int gFrames = 0; static double gFps = 60; static bool gFullReq = false;
static void appInit(const std::string& dir) { initDefs(); initAni(); loadAssets(); initTerrain(); initFont(); Audio::build(); GM = new Game(); GM->saveDir = dir; GM->savePath = ""; GM->modal = M_TITLE; Audio::setVolumes(SET.music, SET.sfx); Audio::setMood(0); }
static void drawCursor(Game& g) { if (!IN.mouseOn) return; int x = (int)IN.mx, y = (int)IN.my; T = &FB; bool game = g.modal == M_NONE && g.player && !g.player->dead;
  if (game) { u32 c = 0xFFffffff, d = 0xFF1c1424; for (int i = 3; i <= 9; i++) { putpx(x - i, y, d); putpx(x + i, y, d); putpx(x, y - i, d); putpx(x, y + i, d); } for (int i = 4; i <= 8; i++) { putpx(x - i, y, c); putpx(x + i, y, c); putpx(x, y - i, c); putpx(x, y + i, c); } frect(x - 1, y - 1, 3, 3, 0xFFf6b8ca); return; }
  static const char* P[] = {"X.......", "XX......", "XWX.....", "XWWX....", "XWWWX...", "XWWWWX..", "XWWXXXX.", "XXWWX...", "..XWWX..", "..XXWX..", "....XX.."}; for (int j = 0; j < 11; j++) for (int i = 0; i < 8; i++) { char ch = P[j][i]; if (ch == 'X') frect(x + i * 2, y + j * 2, 2, 2, 0xFF1c1424); else if (ch == 'W') frect(x + i * 2, y + j * 2, 2, 2, 0xFFffe8f0); } }
static void hotkeys(Game& g) { int m = g.modal; if (m == M_TITLE || m == M_NEW || m == M_LOAD || m == M_DEAD) { if (IN.p(K_ESC) && m == M_NEW) { g.modal = M_TITLE; UI::focusField = -1; } if (IN.p(K_ESC) && m == M_LOAD) { if (UI::confirmDel >= 0 || UI::renameIdx >= 0) { UI::confirmDel = UI::renameIdx = -1; UI::focusField = -1; } else g.modal = M_TITLE; } if (IN.p(K_ESC) && UI::fromTitle && (m == M_SETTINGS || m == M_HELP)) g.modal = M_TITLE; return; } if (UI::focusField >= 0) UI::focusField = -1;
  if (IN.p(K_F2)) gShotReq = true; if (IN.p(K_F11)) gFullReq = true; if (UI::fullReq) { UI::fullReq = false; gFullReq = true; }
  if (IN.p(K_ESC)) { if (!UI::heldId.empty()) { UI::closeModal(g); } else if (m == M_NONE) g.modal = M_PAUSE; else if (m == M_SETTINGS || m == M_HELP) g.modal = UI::fromTitle ? M_TITLE : M_PAUSE; else UI::closeModal(g); return; }
  auto tog = [&](int key, Modal md) { if (IN.p(key)) { if (m == md) UI::closeModal(g); else if (m == M_NONE) { g.modal = md; snd("open", 0.5f); } } };
  if (IN.p(K_TAB) || IN.p('I')) { if (m == M_INV || m == M_CRAFT) UI::closeModal(g); else if (m == M_NONE) { g.modal = M_INV; snd("open", 0.5f); } } tog('M', M_MAP); tog('N', M_REALM); if (m != M_NONE || !IN.p('J')) tog('J', M_JOURNAL); }
// returns the framebuffer to present
static Img* appFrame(double dt, int W, int H) {
  Game& g = *GM; UI::winW = W; UI::winH = H; if (FB.w != W || FB.h != H) FB.alloc(W, H); UI::U = W >= 1700 ? 3 : 2; gFpsT += dt; gFrames++; if (gFpsT >= 0.5) { gFps = gFrames / gFpsT; gFpsT = 0; gFrames = 0; }
  bool titleMode = g.modal == M_TITLE || g.modal == M_NEW || g.modal == M_LOAD || ((g.modal == M_SETTINGS || g.modal == M_HELP) && UI::fromTitle); hotkeys(g); T = &FB;
  if (titleMode || !g.player) { UI::drawTitle(g, dt, FB); T = &FB; Audio::music(Audio::MU_TITLE); Audio::ambient(0, 0, 0, 0); }
  else {
    int sc = std::max(1, (int)std::round(std::sqrt((double)W * H / 129600.0)) + SET.zoom); sc = std::max(1, sc); int vw = (W + sc - 1) / sc, vh = (H + sc - 1) / sc; g.viewW = vw; g.viewH = vh; g.scale = sc; REN.resize(vw, vh);
    g.paused = g.modal == M_PAUSE || g.modal == M_DEAD || g.modal == M_SETTINGS || g.modal == M_HELP || g.sleeping; if (g.modal == M_NONE || g.modal == M_INV || g.modal == M_CRAFT || g.modal == M_MAP || g.modal == M_REALM || g.modal == M_JOURNAL || g.modal == M_CHEST || g.modal == M_TRADE || g.modal == M_TALK || g.modal == M_CHIEF || g.modal == M_BANNER || g.modal == M_SOLDIER) {}
    gAcc += std::min(dt, 0.1); int steps = 0; while (gAcc >= 1.0 / 60 && steps < 5) { g.update(1.0 / 60); gAcc -= 1.0 / 60; steps++; if (steps == 1) { /* edge-triggered input is consumed by the first step */ } }
    // fades / sleep
    if (g.fadeState == 1) { g.fadeT += dt / 0.3; if (g.fadeT >= 1) { g.fadeT = 1; if (g.fadeCb) { auto cb = g.fadeCb; g.fadeCb = nullptr; cb(); } g.fadeState = 2; } } else if (g.fadeState == 2) { g.fadeT -= dt / 0.3; if (g.fadeT <= 0) { g.fadeT = 0; g.fadeState = 0; g.fading = false; } }
    if (g.sleeping) { g.sleepT += dt; if (g.sleepT > 1.3 && g.sleepCb) { auto cb = g.sleepCb; g.sleepCb = nullptr; cb(); } if (g.sleepT > 2.6) { g.sleeping = false; g.toast("You wake up refreshed.", "good"); } }
    REN.render(g, g.paused ? 0 : dt); T = &FB; for (int y = 0; y < H; y++) { const u32* src = &REN.screen.p[(size_t)std::min(y / sc, vh - 1) * vw]; u32* dst = &FB.p[(size_t)y * W]; for (int x = 0; x < W; x++) dst[x] = src[std::min(x / sc, vw - 1)]; }
    UI::drawHUD(g); UI::drawModals(g);
    double fa = g.fadeState ? g.fadeT : 0; if (g.sleeping) fa = std::max(fa, g.sleepT < 1.3 ? g.sleepT / 1.3 : std::max(0.0, 1 - (g.sleepT - 1.3) / 1.3)); if (fa > 0.01) { frect(0, 0, W, H, RGB(8, 6, 14, (int)(clampf((float)fa, 0, 1) * 255))); if (g.sleeping && fa > 0.6) textC("Zzz...", W / 2, H / 2 - 20, 40, 0xFFf6b8ca); }
    // audio direction
    Ent* p = g.player.get(); std::string bm = g.world.biomeAt(p->tx(), p->ty()); Audio::music(g.mood == "battle" ? Audio::MU_BATTLE : g.mood == "night" ? Audio::MU_NIGHT : Audio::MU_DAY);
    Audio::ambient((float)g.weather.rain, (bm == "snow" || bm == "rock") ? 0.6f : 0.25f, (float)(1 - g.daylight()), (p->waterLevel() > 0 || bm == "water" || bm == "beach") ? 0.7f : 0.15f);
  }
  T = &FB; drawCursor(g); if (UI::quitReq) gQuit = true; IN.endFrame(); return &FB; }
static void writePPM(const Img& im, const std::string& path) { FILE* f = fopen(path.c_str(), "wb"); if (!f) return; fprintf(f, "P6\n%d %d\n255\n", im.w, im.h); std::vector<u8> row(im.w * 3); for (int y = 0; y < im.h; y++) { for (int x = 0; x < im.w; x++) { u32 c = im.p[(size_t)y * im.w + x]; row[x * 3] = cr(c); row[x * 3 + 1] = cg(c); row[x * 3 + 2] = cb(c); } fwrite(row.data(), 1, row.size(), f); } fclose(f); }
#ifdef _WIN32
static HWND hwnd; static BITMAPINFO bmi; static bool gFull = false; static RECT gSavedRect; static LONG gSavedStyle; static HWAVEOUT hwo; static WAVEHDR whdr[4]; static int16_t* abuf[4]; static HANDLE aEvt; static bool aRun = true; static HANDLE aThread;
static DWORD WINAPI audioThread(LPVOID) { while (aRun) { WaitForSingleObject(aEvt, 50); for (int i = 0; i < 4; i++) if (whdr[i].dwFlags & WHDR_DONE) { Audio::mix(abuf[i], 512); waveOutWrite(hwo, &whdr[i], sizeof(WAVEHDR)); } } return 0; }
static void audioStart() { WAVEFORMATEX wf = {}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 44100; wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 44100 * 4; aEvt = CreateEvent(nullptr, FALSE, FALSE, nullptr); if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD_PTR)aEvt, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { hwo = nullptr; return; }
  for (int i = 0; i < 4; i++) { abuf[i] = (int16_t*)calloc(512 * 2, 2); whdr[i] = {}; whdr[i].lpData = (LPSTR)abuf[i]; whdr[i].dwBufferLength = 512 * 4; waveOutPrepareHeader(hwo, &whdr[i], sizeof(WAVEHDR)); Audio::mix(abuf[i], 512); waveOutWrite(hwo, &whdr[i], sizeof(WAVEHDR)); } aThread = CreateThread(nullptr, 0, audioThread, nullptr, 0, nullptr); SetThreadPriority(aThread, THREAD_PRIORITY_ABOVE_NORMAL); }
static void toggleFullscreen() { if (!gFull) { gSavedStyle = GetWindowLong(hwnd, GWL_STYLE); GetWindowRect(hwnd, &gSavedRect); MONITORINFO mi = {sizeof mi}; GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi); SetWindowLong(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE); SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED); gFull = true; } else { SetWindowLong(hwnd, GWL_STYLE, gSavedStyle); SetWindowPos(hwnd, nullptr, gSavedRect.left, gSavedRect.top, gSavedRect.right - gSavedRect.left, gSavedRect.bottom - gSavedRect.top, SWP_FRAMECHANGED | SWP_NOZORDER); gFull = false; } }
static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CLOSE: if (GM && GM->player && GM->modal != M_TITLE) GM->saveGame(true); gQuit = true; return 0; case WM_DESTROY: PostQuitMessage(0); return 0; case WM_SIZE: gWinW = LOWORD(l); gWinH = HIWORD(l); return 0; case WM_ERASEBKGND: return 1; case WM_SETCURSOR: if (LOWORD(l) == HTCLIENT) { SetCursor(nullptr); return TRUE; } break;
    case WM_CHAR: { char c = (char)w; if (c == 8 || (c >= 32 && c < 127)) IN.typed += c; break; }
    case WM_KEYDOWN: case WM_SYSKEYDOWN: { int k = (int)w; if (k == VK_LSHIFT || k == VK_RSHIFT) k = VK_SHIFT; if (k > 255) break; if (k == VK_F4 && (GetKeyState(VK_MENU) & 0x8000)) { gQuit = true; return 0; } if (!(l & (1 << 30))) IN.hit[k] = true; IN.down[k] = true; if (k == VK_TAB || k == VK_SPACE || (k >= 37 && k <= 40) || k == VK_MENU) return 0; break; }
    case WM_KEYUP: case WM_SYSKEYUP: { int k = (int)w; if (k == VK_LSHIFT || k == VK_RSHIFT) k = VK_SHIFT; if (k < 256) IN.down[k] = false; break; } case WM_KILLFOCUS: memset(IN.down, 0, sizeof IN.down); IN.mdown[0] = IN.mdown[1] = IN.mdown[2] = false; break;
    case WM_MOUSEMOVE: { IN.mx = (float)(short)LOWORD(l); IN.my = (float)(short)HIWORD(l); IN.mouseOn = true; TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); break; } case WM_MOUSELEAVE: IN.mouseOn = false; break;
    case WM_LBUTTONDOWN: IN.mdown[0] = IN.mhit[0] = true; SetCapture(h); break; case WM_LBUTTONUP: IN.mdown[0] = false; ReleaseCapture(); break; case WM_RBUTTONDOWN: IN.mdown[2] = IN.mhit[2] = true; break; case WM_RBUTTONUP: IN.mdown[2] = false; break; case WM_MOUSEWHEEL: IN.wheel += (short)HIWORD(w) > 0 ? -1 : 1; break; }
  return DefWindowProc(h, m, w, l); }
static std::string gCrashPath;
static LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) { FILE* f = fopen(gCrashPath.c_str(), "wb"); if (f) { fprintf(f, "Hanomi crashed. code=%08lx addr=%p modal=%d\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, GM ? (int)GM->modal : -1); fclose(f); } MessageBoxA(nullptr, ("Hanomi hit an unexpected error.\nDetails were written to:\n" + gCrashPath).c_str(), "Hanomi", MB_OK | MB_ICONERROR); return EXCEPTION_EXECUTE_HANDLER; }
typedef struct { WORD wButtons; BYTE bLT, bRT; SHORT lx, ly, rx, ry; } XPAD; typedef struct { DWORD pk; XPAD g; } XSTATE; typedef DWORD(WINAPI* XGetState)(DWORD, XSTATE*); static XGetState xGet = nullptr; static bool pA = false, pB = false, pX = false;
static void pollPad() { if (!xGet) return; XSTATE s = {}; if (xGet(0, &s) != 0) return; float lx = s.g.lx / 32767.f, ly = -s.g.ly / 32767.f; if (std::fabs(lx) < 0.22f) lx = 0; if (std::fabs(ly) < 0.22f) ly = 0; IN.padX = lx; IN.padY = ly; bool a = s.g.wButtons & 0x1000, b = s.g.wButtons & 0x2000, x = s.g.wButtons & 0x4000; if (a && !pA) IN.padUse = true; if (x && !pX) IN.padAlt = true; if (b && !pB) IN.padRoll = true; pA = a; pB = b; pX = x; if (s.g.wButtons & 0x0010) IN.hit[K_ESC] = true; if (s.g.wButtons & 0x0020) IN.hit[K_TAB] = true; }
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
  timeBeginPeriod(1); std::string dir; { const char* ap = getenv("APPDATA"); dir = ap ? std::string(ap) + "\\Hanomi" : "."; CreateDirectoryA(dir.c_str(), nullptr); if (!Sv::mkdirs(Sv::worldsDir(dir)) || !Sv::writeAll(dir + "\\.probe", "x", 1)) { char tmp[MAX_PATH]; DWORD n = GetTempPathA(MAX_PATH, tmp); dir = (n ? std::string(tmp) : std::string(".\\")) + "Hanomi"; CreateDirectoryA(dir.c_str(), nullptr); if (!Sv::mkdirs(Sv::worldsDir(dir))) dir = "."; } Sv::del(dir + "\\.probe"); gCrashPath = dir + "\\crash.txt"; SetUnhandledExceptionFilter(crashFilter); }
  HMODULE xi = LoadLibraryA("xinput1_4.dll"); if (!xi) xi = LoadLibraryA("xinput9_1_0.dll"); if (xi) xGet = (XGetState)GetProcAddress(xi, "XInputGetState");
  WNDCLASSA wc = {}; wc.lpfnWndProc = wndProc; wc.hInstance = hi; wc.lpszClassName = "HanomiWnd"; wc.hIcon = LoadIconA(hi, MAKEINTRESOURCEA(1)); RegisterClassA(&wc); RECT r = {0, 0, 1280, 720}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  hwnd = CreateWindowExA(0, "HanomiWnd", "Hanomi - a sakura village survival game", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hi, nullptr); RECT c2; GetClientRect(hwnd, &c2); gWinW = c2.right; gWinH = c2.bottom;
  { HDC d0 = GetDC(hwnd); RECT rc = {0, 0, gWinW, gWinH}; HBRUSH br = CreateSolidBrush((COLORREF)(27 | (20 << 8) | (32 << 16))); FillRect(d0, &rc, br); DeleteObject(br); SetBkMode(d0, TRANSPARENT); SetTextColor(d0, (COLORREF)(246 | (184 << 8) | (202 << 16))); DrawTextA(d0, "Hanomi - loading...", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE); ReleaseDC(hwnd, d0); }
  appInit(dir); audioStart();
  HDC dc = GetDC(hwnd); LARGE_INTEGER fq, t0; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0); double last = 0; int shotN = 0;
  while (!gQuit) { MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) gQuit = true; TranslateMessage(&msg); DispatchMessage(&msg); } if (gQuit) break; LARGE_INTEGER t1; QueryPerformanceCounter(&t1); double now = (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart; float dt = (float)(now - last); last = now; if (dt > 0.25f) dt = 0.25f; pollPad();
    Img* im = appFrame(dt, std::max(gWinW, 320), std::max(gWinH, 200)); if (gFullReq) { gFullReq = false; toggleFullscreen(); }
    if (gShotReq) { gShotReq = false; CreateDirectoryA("screenshots", nullptr); char b[64]; snprintf(b, 64, "screenshots\\hanomi_%03d.ppm", shotN++); writePPM(*im, b); GM->toast("Screenshot saved."); }
    memset(&bmi, 0, sizeof bmi); bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth = im->w; bmi.bmiHeader.biHeight = -im->h; bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32; bmi.bmiHeader.biCompression = BI_RGB; StretchDIBits(dc, 0, 0, gWinW, gWinH, 0, 0, im->w, im->h, im->p.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    QueryPerformanceCounter(&t1); double el = (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart - now; if (el < 1.0 / 60 - 0.0015) Sleep((DWORD)((1.0 / 60 - el) * 1000 - 1)); while (true) { QueryPerformanceCounter(&t1); double e2 = (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart - now; if (e2 >= 1.0 / 60 - 0.0004) break; } }
  aRun = false; if (hwo) { SetEvent(aEvt); WaitForSingleObject(aThread, 200); waveOutReset(hwo); } timeEndPeriod(1); return 0; }
#else
// Offline audio render for QA: writes a 16-bit stereo WAV and prints RMS/peak
static void renderWav(const std::string& path, double secs, const std::function<void()>& setup) {
  Audio::reset(); Audio::bypassComp = getenv("NOCOMP") != nullptr; setup(); int frames = (int)(secs * Audio::SR); std::vector<int16_t> buf((size_t)frames * 2); for (int i = 0; i < frames; i += 512) Audio::mix(&buf[(size_t)i * 2], std::min(512, frames - i));
  double ss = 0; int pk = 0; for (int16_t v : buf) { ss += (double)v * v; pk = std::max(pk, (int)std::abs(v)); } fprintf(stderr, "%s rms=%.4f peak=%.3f\n", path.c_str(), std::sqrt(ss / buf.size()) / 32768.0, pk / 32768.0);
  FILE* f = fopen(path.c_str(), "wb"); if (!f) return; u32 dl = (u32)buf.size() * 2, sr = Audio::SR, br = sr * 4; u16 ch = 2, bits = 16, fmt = 1, ba = 4; u32 sz = 36 + dl, fs = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fs, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dl, 4, 1, f); fwrite(buf.data(), 2, buf.size(), f); fclose(f);
}
// Headless QA: hanomi_headless script.txt outdir
int main(int argc, char** argv) {
  std::string script = argc > 1 ? argv[1] : "", out = argc > 2 ? argv[2] : "shots"; int W = argc > 3 ? atoi(argv[3]) : 1280, H = argc > 4 ? atoi(argv[4]) : 720; if (system(("mkdir -p " + out).c_str())) {} appInit(out); FILE* f = fopen(script.c_str(), "r"); if (!f) { fprintf(stderr, "no script\n"); return 1; } char line[512]; std::vector<std::string> lines; while (fgets(line, sizeof line, f)) lines.push_back(line); fclose(f);
  double worst = 0, total = 0; int frames = 0; Game& g = *GM; auto step = [&](int n) { for (int i = 0; i < n; i++) { auto t0 = std::chrono::steady_clock::now(); appFrame(1.0 / 60, W, H); double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); worst = std::max(worst, ms); total += ms; frames++; } };
  auto keyOf = [](const std::string& a) { if (a == "SHIFT") return (int)K_SHIFT; if (a == "SPACE") return (int)K_SPACE; if (a == "TAB") return (int)K_TAB; if (a == "ESC") return (int)K_ESC; if (a == "LEFT") return (int)K_LEFT; if (a == "RIGHT") return (int)K_RIGHT; if (a == "UP") return (int)K_UP; if (a == "DOWN") return (int)K_DOWN; return (int)toupper(a[0]); };
  for (auto& L : lines) { char cmd[64] = {0}, a1[128] = {0}, a2[128] = {0}; int n = sscanf(L.c_str(), "%63s %127s %127s", cmd, a1, a2); if (n <= 0 || cmd[0] == '#') continue; std::string c = cmd; double f1 = atof(a1), f2 = atof(a2);
    if (c == "wait") step((int)f1); else if (c == "new") { GameOpts o; o.seed = (u32)f1; g.newGame(o); g.modal = M_NONE; } else if (c == "key") IN.down[keyOf(a1) & 255] = f2 > 0 || a2[0] == 0; else if (c == "keyup") IN.down[keyOf(a1) & 255] = false; else if (c == "tap") { IN.hit[keyOf(a1) & 255] = true; step(1); }
    else if (c == "tp") { g.player->x = f1 * TS; g.player->y = f2 * TS; REN.inited = false; } else if (c == "tpv") { for (int rr = 1; rr <= 3; rr++) for (int ry = -rr; ry <= rr; ry++) for (int rx = -rr; rx <= rr; rx++) { if (std::max(std::abs(rx), std::abs(ry)) != rr) continue; Village* v = g.world.villageForRegion(rx, ry); if (v && !v->home && f1-- <= 0) { g.player->x = v->x * TS; g.player->y = (v->y + 6) * TS; REN.inited = false; g.activateVillages(); goto tpdone; } } tpdone:; }
    else if (c == "time") g.clock = DAY_LEN * (f1 / 24.0 - 0.25 + (g.day - 1)); else if (c == "day") { g.day = (int)f1; g.clock = DAY_LEN * (g.day - 1) + std::fmod(g.clock, (double)DAY_LEN); } else if (c == "give") g.player->add(a1, f2 > 0 ? (int)f2 : 1); else if (c == "sel") g.player->sel = (int)f1; else if (c == "rain") g.weather.rain = g.weather.target = f1; else if (c == "coins") g.player->coins = (int)f1; else if (c == "hp") g.player->hp = f1;
    else if (c == "mouse") { IN.mx = (float)f1; IN.my = (float)f2; IN.mouseOn = true; } else if (c == "click") { IN.mx = (float)f1; IN.my = (float)f2; IN.mouseOn = true; IN.mdown[0] = IN.mhit[0] = true; step(1); IN.mdown[0] = false; step(1); } else if (c == "rclick") { IN.mdown[2] = IN.mhit[2] = true; step(1); IN.mdown[2] = false; } else if (c == "hold") IN.mdown[0] = f1 > 0;
    else if (c == "modal") g.modal = (Modal)(int)f1; else if (c == "dir") g.player->dir = (int)f1; else if (c == "area") { UI::fromTitle = false; } else if (c == "talk") { Ent* best = nullptr; for (auto& e : g.ents) if (e->kind == E_VILLAGER && e->role == a1) best = e.get(); if (best) { g.player->x = best->x; g.player->y = best->y + 14; REN.inited = false; g.modalEnt = best; g.modal = a1[0] == 'c' ? M_CHIEF : a1[0] == 'm' ? M_TRADE : M_TALK; } }
    else if (c == "spawn") { std::string k = a1; double px = g.player->x + 3 * TS, py = g.player->y; if (k == "zombie") g.spawnZombie(px, py, 0); else if (k == "zombie2") g.spawnZombie(px, py, 2); else if (k == "soldier") g.spawnSoldier(px, py, "0,0", 0xe87a9a, false, true, "follow", px, py); else if (k == "archer") g.spawnSoldier(px, py, "0,0", 0xe87a9a, true, true, "follow", px, py); else if (k == "wolf") g.spawnDog(px, py, "wolf", "", false, true); else if (k == "dog") g.spawnDog(px, py, a2[0] ? a2 : "shiba", "Pochi", true, false); else g.spawnAnimal(k, px, py); }
    else if (c == "set") { int tx = (int)f2, ty = (int)atof(a2); (void)tx; (void)ty; } else if (c == "build") { int o = (int)f1; g.world.set(g.player->tx() + (int)f2 + 2, g.player->ty() + 1, -1, -1, o, 0); }
    else if (c == "sfx") { std::string nm = a1; renderWav(out + "/sfx_" + nm + ".wav", f2 > 0 ? f2 : 2.0, [&] { Audio::play(nm.c_str(), 1, 1, 0); }); }
    else if (c == "music") { int md = (int)f1; renderWav(out + "/music_" + a1 + ".wav", f2 > 0 ? f2 : 20.0, [&] { Audio::music(md); }); }
    else if (c == "amb") { int k = (int)f1; renderWav(out + "/amb_" + a1 + ".wav", f2 > 0 ? f2 : 6.0, [&] { Audio::ambient(k == 0 ? 1.f : 0.f, k == 1 ? 1.f : 0.f, k == 2 ? 1.f : 0.f, k == 3 ? 1.f : 0.f); }); }
    else if (c == "shot") { step(2); writePPM(FB, out + "/" + a1 + ".ppm"); } else if (c == "perf") { fprintf(stderr, "frames=%d avg=%.2fms worst=%.2fms\n", frames, total / std::max(1, frames), worst); total = 0; frames = 0; worst = 0; }
    else if (c == "save") g.saveGame(); else if (c == "load") fprintf(stderr, "load=%d\n", g.loadGame()); else if (c == "stat") fprintf(stderr, "hp=%.1f food=%.1f coins=%d ents=%zu chunks=%zu day=%d hour=%.1f goal=%d\n", g.player->hp, g.player->food, g.player->coins, g.ents.size(), g.world.chunks.size(), g.day, g.hour(), g.goal); }
  fprintf(stderr, "done\n"); return 0; }
#endif
