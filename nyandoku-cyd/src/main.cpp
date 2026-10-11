// にゃんドク (ネコ配置パズル) for ESP32-2432S028R "Cheap Yellow Display"
// ルール: 各色エリア・各行・各列に猫を1匹ずつ。猫同士は斜めを含め隣り合わせ禁止。
// 操作: マスをタップ  空 → ✕ → 🐱 → 空
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

// ---- タッチ (別SPIバス) ----
#define XPT_CS 33
#define XPT_IRQ 36
#define XPT_CLK 25
#define XPT_MISO 39
#define XPT_MOSI 32

XPT2046_Touchscreen ts(XPT_CS, XPT_IRQ);
TFT_eSPI tft;

// ---- ゲーム状態 ----
const int MAXN = 9;
int N = 7;                       // 盤面サイズ 5..8
uint8_t reg[MAXN * MAXN];        // 各マスのエリア番号
uint8_t st[MAXN * MAXN];         // 0=空 1=✕ 2=猫
uint8_t solCol[MAXN];            // 正解(各行の猫の列)
uint8_t hist[40][MAXN * MAXN];
int histN = 0;
uint8_t drawn[MAXN * MAXN];      // 描画済みキャッシュ
bool over = false;
uint32_t startMs = 0, lastSec = 9999;

// ---- 画面レイアウト ----
int CELL, BX, BY;
const int PX = 246, PW = 70, BH = 36;
const int BTN_SIZE = 70, BTN_UNDO = 112, BTN_HINT = 154, BTN_NEW = 196;

uint16_t palette[MAXN];
const uint8_t PAL[MAXN][3] = {
  {255,170,170},{255,210,150},{250,240,140},{170,230,160},{150,220,230},
  {160,180,250},{210,170,240},{245,170,215},{200,200,200}};

// ---- 生成ロジック ----
bool colUsed[MAXN], regUsed[MAXN];
int cntSol;

void countRec(int r, int prev) {
  if (cntSol > 1) return;
  if (r == N) { cntSol++; return; }
  for (int c = 0; c < N; c++) {
    if (colUsed[c] || (r && abs(c - prev) < 2)) continue;
    int k = reg[r * N + c];
    if (regUsed[k]) continue;
    colUsed[c] = regUsed[k] = true;
    countRec(r + 1, c);
    colUsed[c] = regUsed[k] = false;
  }
}
int countSolutions() {
  memset(colUsed, 0, sizeof colUsed); memset(regUsed, 0, sizeof regUsed);
  cntSol = 0; countRec(0, -9); return cntSol;
}
bool placeRec(int r) {
  if (r == N) return true;
  int ord[MAXN]; for (int i = 0; i < N; i++) ord[i] = i;
  for (int i = N - 1; i > 0; i--) { int j = random(i + 1); int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
  for (int i = 0; i < N; i++) {
    int c = ord[i];
    if (colUsed[c] || (r && abs(c - (int)solCol[r - 1]) < 2)) continue;
    solCol[r] = c; colUsed[c] = true;
    if (placeRec(r + 1)) return true;
    colUsed[c] = false;
  }
  return false;
}
void growRegions() {
  int8_t g[MAXN * MAXN]; memset(g, -1, sizeof g);
  for (int r = 0; r < N; r++) g[r * N + solCol[r]] = r;
  int left = N * N - N;
  while (left > 0) {
    int cand[MAXN * MAXN], src[MAXN * MAXN], n = 0;
    for (int i = 0; i < N * N; i++) {
      if (g[i] >= 0) continue;
      int r = i / N, c = i % N, nb[4], m = 0;
      if (r > 0 && g[i - N] >= 0) nb[m++] = g[i - N];
      if (r < N - 1 && g[i + N] >= 0) nb[m++] = g[i + N];
      if (c > 0 && g[i - 1] >= 0) nb[m++] = g[i - 1];
      if (c < N - 1 && g[i + 1] >= 0) nb[m++] = g[i + 1];
      if (m) { cand[n] = i; src[n] = nb[random(m)]; n++; }
    }
    int k = random(n); g[cand[k]] = src[k]; left--;
  }
  for (int i = 0; i < N * N; i++) reg[i] = g[i];
}
void generate() {
  for (int t = 0; t < 4000; t++) {
    memset(colUsed, 0, sizeof colUsed);
    if (!placeRec(0)) continue;
    growRegions();
    if (countSolutions() == 1) return;
    if ((t & 15) == 0) yield();
  }
}

// ---- 描画 ----
void catIcon(int cx, int cy, int s) {
  int r = s * 36 / 100;
  tft.fillTriangle(cx - r, cy - r / 4, cx - r, cy - r - r / 3, cx - r / 4, cy - r + 1, 0xFD20);
  tft.fillTriangle(cx + r, cy - r / 4, cx + r, cy - r - r / 3, cx + r / 4, cy - r + 1, 0xFD20);
  tft.fillCircle(cx, cy, r, 0xFD20);
  int e = max(1, r / 5);
  tft.fillCircle(cx - r / 2, cy - r / 6, e, TFT_BLACK);
  tft.fillCircle(cx + r / 2, cy - r / 6, e, TFT_BLACK);
  tft.fillTriangle(cx - e, cy + r / 6, cx + e, cy + r / 6, cx, cy + r / 6 + e + 1, 0xF81F);
  if (s >= 28) {
    tft.drawLine(cx - r, cy + r / 4, cx - r / 3, cy + r / 3, TFT_BLACK);
    tft.drawLine(cx + r, cy + r / 4, cx + r / 3, cy + r / 3, TFT_BLACK);
  }
}
void drawCell(int i, bool bad) {
  int r = i / N, c = i % N, x = BX + c * CELL, y = BY + r * CELL;
  tft.fillRect(x, y, CELL, CELL, palette[reg[i]]);
  uint16_t dk = TFT_BLACK;
  if (r == 0 || reg[i - N] != reg[i]) tft.fillRect(x, y, CELL, 2, dk);
  if (r == N - 1 || reg[i + N] != reg[i]) tft.fillRect(x, y + CELL - 2, CELL, 2, dk);
  if (c == 0 || reg[i - 1] != reg[i]) tft.fillRect(x, y, 2, CELL, dk);
  if (c == N - 1 || reg[i + 1] != reg[i]) tft.fillRect(x + CELL - 2, y, 2, CELL, dk);
  if (st[i] == 2) catIcon(x + CELL / 2, y + CELL / 2 + 1, CELL);
  else if (st[i] == 1) {
    int m = CELL / 3;
    tft.drawLine(x + m, y + m, x + CELL - m, y + CELL - m, 0x7BEF);
    tft.drawLine(x + CELL - m, y + m, x + m, y + CELL - m, 0x7BEF);
  }
  if (bad) { tft.drawRect(x + 3, y + 3, CELL - 6, CELL - 6, TFT_RED); tft.drawRect(x + 4, y + 4, CELL - 8, CELL - 8, TFT_RED); }
}
void button(int y, const char* label) {
  tft.fillRoundRect(PX, y, PW, BH, 8, 0xFDA0);
  tft.drawRoundRect(PX, y, PW, BH, 8, TFT_BLACK);
  tft.setTextColor(TFT_BLACK, 0xFDA0); tft.setTextDatum(MC_DATUM);
  tft.drawString(label, PX + PW / 2, y + BH / 2, 2);
}
void drawPanel() {
  tft.setTextDatum(TC_DATUM); tft.setTextColor(TFT_BLACK, 0xF71C);
  tft.fillRect(240, 0, 80, 240, 0xF71C);
  tft.drawString("NYANDOKU", 280, 4, 2);
  char b[16]; snprintf(b, sizeof b, "SIZE %dx%d", N, N);
  button(BTN_SIZE, b); button(BTN_UNDO, "UNDO"); button(BTN_HINT, "HINT"); button(BTN_NEW, "NEW");
}
void drawStatus(int cats) {
  char b[24];
  tft.setTextDatum(TC_DATUM); tft.setTextColor(TFT_BLACK, 0xF71C);
  uint32_t s = over ? lastSec : (millis() - startMs) / 1000;
  snprintf(b, sizeof b, "%u:%02u  ", (unsigned)(s / 60), (unsigned)(s % 60));
  tft.drawString(b, 280, 26, 4);
  snprintf(b, sizeof b, "CAT %d/%d  ", cats, N);
  tft.drawString(b, 280, 54, 2);
}
bool conflicts(bool* bad, int& cats) {
  int idx[MAXN * MAXN]; cats = 0; bool any = false;
  for (int i = 0; i < N * N; i++) { bad[i] = false; if (st[i] == 2) idx[cats++] = i; }
  for (int a = 0; a < cats; a++) for (int b = a + 1; b < cats; b++) {
    int i = idx[a], j = idx[b], ri = i / N, ci = i % N, rj = j / N, cj = j % N;
    if (ri == rj || ci == cj || reg[i] == reg[j] || max(abs(ri - rj), abs(ci - cj)) == 1) { bad[i] = bad[j] = true; any = true; }
  }
  return any;
}
void refresh(bool force = false) {
  bool bad[MAXN * MAXN]; int cats;
  bool any = conflicts(bad, cats);
  for (int i = 0; i < N * N; i++) {
    uint8_t key = st[i] | (bad[i] ? 4 : 0);
    if (force || key != drawn[i]) { drawCell(i, bad[i]); drawn[i] = key; }
  }
  if (!over && cats == N && !any) {
    over = true; lastSec = (millis() - startMs) / 1000;
    tft.fillRoundRect(40, 90, 160, 60, 10, TFT_WHITE);
    tft.drawRoundRect(40, 90, 160, 60, 10, TFT_BLACK);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString("CLEAR!", 120, 112, 4);
    tft.drawString("tap NEW", 120, 138, 2);
  }
  drawStatus(cats);
}
void newGame() {
  tft.fillScreen(0xF71C);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_BLACK, 0xF71C);
  tft.drawString("Generating...", 120, 120, 4);
  generate();
  memset(st, 0, sizeof st); memset(drawn, 255, sizeof drawn);
  histN = 0; over = false; startMs = millis();
  CELL = 232 / N; BX = (240 - CELL * N) / 2; BY = (240 - CELL * N) / 2;
  for (int i = 0; i < N; i++) { const uint8_t* p = PAL[i]; palette[i] = tft.color565(p[0], p[1], p[2]); }
  tft.fillScreen(0xF71C);
  drawPanel(); refresh(true);
}
void pushHist() {
  if (histN == 40) { memmove(hist[0], hist[1], 39 * MAXN * MAXN); histN = 39; }
  memcpy(hist[histN++], st, MAXN * MAXN);
}
void hint() {
  pushHist();
  for (int i = 0; i < N * N; i++) if (st[i] == 2 && solCol[i / N] != i % N) st[i] = 0;
  int cand[MAXN], n = 0;
  for (int r = 0; r < N; r++) if (st[r * N + solCol[r]] != 2) cand[n++] = r;
  if (n) { int r = cand[random(n)]; st[r * N + solCol[r]] = 2; }
  refresh();
}
void handleTap(int x, int y) {
  if (x >= PX && x < PX + PW) {
    if (y >= BTN_SIZE && y < BTN_SIZE + BH) { N = (N >= 8) ? 5 : N + 1; newGame(); }
    else if (y >= BTN_NEW && y < BTN_NEW + BH) newGame();
    else if (over) return;
    else if (y >= BTN_UNDO && y < BTN_UNDO + BH) { if (histN) { memcpy(st, hist[--histN], MAXN * MAXN); refresh(); } }
    else if (y >= BTN_HINT && y < BTN_HINT + BH) hint();
    return;
  }
  if (over) return;
  int c = (x - BX) / CELL, r = (y - BY) / CELL;
  if (x < BX || y < BY || c < 0 || c >= N || r < 0 || r >= N) return;
  pushHist();
  int i = r * N + c; st[i] = (st[i] + 1) % 3;
  refresh();
}

// ---- タッチ校正 (9点・2次多項式フィット。初回起動時に実行し保存) ----
// 再校正したいときは、画面に触れたまま電源を入れてください。
Preferences prefs;
struct Cal { float cx[6], cy[6]; bool valid; } cal = {{0}, {0}, false};

void feat(float u, float v, float* f) { f[0] = 1; f[1] = u; f[2] = v; f[3] = u * u; f[4] = u * v; f[5] = v * v; }

void crosshair(int x, int y) {
  tft.drawLine(x - 12, y, x + 12, y, TFT_WHITE); tft.drawLine(x, y - 12, x, y + 12, TFT_WHITE);
  tft.drawCircle(x, y, 6, TFT_RED);
}
void rawTap(int sx, int sy, int& rx, int& ry) {
  while (true) {
    tft.fillScreen(TFT_BLACK); crosshair(sx, sy);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Tap the cross", 160, 120, 4);
    while (!ts.touched()) delay(10);
    delay(60);
    long ax = 0, ay = 0; int n = 0; uint32_t t0 = millis();
    while (ts.touched() && millis() - t0 < 400) { TS_Point p = ts.getPoint(); ax += p.x; ay += p.y; n++; delay(8); }
    while (ts.touched()) delay(10);
    delay(200);
    if (n >= 5) { rx = ax / n; ry = ay / n; return; }
  }
}
bool solve6(double A[6][7]) {
  for (int i = 0; i < 6; i++) {
    int m = i; for (int r = i + 1; r < 6; r++) if (fabs(A[r][i]) > fabs(A[m][i])) m = r;
    if (fabs(A[m][i]) < 1e-12) return false;
    for (int c = 0; c < 7; c++) { double t = A[i][c]; A[i][c] = A[m][c]; A[m][c] = t; }
    for (int r = 0; r < 6; r++) if (r != i) { double k = A[r][i] / A[i][i]; for (int c = i; c < 7; c++) A[r][c] -= k * A[i][c]; }
  }
  for (int i = 0; i < 6; i++) A[i][6] /= A[i][i];
  return true;
}
void calibrate() {
  const int SX[3] = {20, 160, 300}, SY[3] = {20, 120, 220};
  float U[9], V[9], TX[9], TY[9]; int k = 0;
  for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) {
    int rx, ry; rawTap(SX[i], SY[j], rx, ry);
    U[k] = (rx - 2048) / 2048.0f; V[k] = (ry - 2048) / 2048.0f; TX[k] = SX[i]; TY[k] = SY[j]; k++;
  }
  for (int axis = 0; axis < 2; axis++) {
    double A[6][7] = {{0}};
    for (int p = 0; p < 9; p++) {
      float f[6]; feat(U[p], V[p], f); float t = axis ? TY[p] : TX[p];
      for (int r = 0; r < 6; r++) { for (int c = 0; c < 6; c++) A[r][c] += f[r] * f[c]; A[r][6] += f[r] * t; }
    }
    if (!solve6(A)) { Serial.println("cal fit failed"); return; }
    for (int r = 0; r < 6; r++) (axis ? cal.cy : cal.cx)[r] = A[r][6];
  }
  cal.valid = true;
  prefs.putBytes("cal2", &cal, sizeof cal);
}
void loadCal() {
  if (prefs.getBytesLength("cal2") == sizeof cal) prefs.getBytes("cal2", &cal, sizeof cal);
}
void toScreen(const TS_Point& p, int& x, int& y) {
  float f[6]; feat((p.x - 2048) / 2048.0f, (p.y - 2048) / 2048.0f, f);
  float sx = 0, sy = 0; for (int i = 0; i < 6; i++) { sx += cal.cx[i] * f[i]; sy += cal.cy[i] * f[i]; }
  x = constrain((int)sx, 0, 319); y = constrain((int)sy, 0, 239);
}

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
  tft.init(); tft.setRotation(1);
  SPI.begin(XPT_CLK, XPT_MISO, XPT_MOSI, XPT_CS);  // 表示はHSPI、タッチはVSPI(標準のSPI)を使用
  ts.begin(); ts.setRotation(1);
  prefs.begin("nyan", false); loadCal();
  delay(300);
#ifdef FORCE_CAL
  calibrate();                       // 毎回校正(確認用)
#else
  if (!cal.valid || ts.touched()) calibrate();
#endif
  Serial.println(cal.valid ? "cal loaded" : "cal missing");
  newGame();
}

// 押し始めの値は不安定なので、少し待って複数サンプルの中央値を使う
bool readStable(TS_Point& out) {
  delay(25);
  int xs[10], ys[10], n = 0;
  uint32_t t0 = millis();
  while (n < 10 && ts.touched() && millis() - t0 < 120) {
    TS_Point p = ts.getPoint();
    if (p.z > 300) { xs[n] = p.x; ys[n] = p.y; n++; }
    delay(4);
  }
  if (n < 4) return false;
  auto med = [](int* a, int n) { for (int i = 1; i < n; i++) { int v = a[i], j = i - 1; while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; } a[j + 1] = v; } return a[n / 2]; };
  out.x = med(xs, n); out.y = med(ys, n); out.z = 1000;
  return true;
}

bool wasTouch = false; uint32_t lastTap = 0;
void loop() {
  bool t = ts.touched();
  if (t && !wasTouch && millis() - lastTap > 120) {
    TS_Point p;
    if (readStable(p)) {
      int x, y; toScreen(p, x, y);
      Serial.printf("tap raw=%d,%d -> %d,%d\n", p.x, p.y, x, y);
      lastTap = millis();
      handleTap(x, y);
    }
  }
  wasTouch = t;
  if (!over) {
    uint32_t s = (millis() - startMs) / 1000;
    if (s != lastSec) { lastSec = s; bool b[MAXN * MAXN]; int c; conflicts(b, c); drawStatus(c); }
  }
}
