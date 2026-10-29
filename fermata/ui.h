// Drawing: boot animation, welcome, departures board, menu, clock.
//
// Every screen is drawn into one full-screen sprite and pushed in one go, so
// animations never flicker.
#pragma once

#include <TFT_eSPI.h>
#include <time.h>
#include <vector>
#include "config.h"
#include "gtfsrt.h"

static const int W = 320;
static const int H = 170;

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float easeOutCubic(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
inline float easeInOut(float t) { t = clamp01(t); return t * t * (3 - 2 * t); }
inline float easeOutBounce(float t) {
  t = clamp01(t);
  if (t < 1 / 2.75f) return 7.5625f * t * t;
  if (t < 2 / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
  if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
  t -= 2.625f / 2.75f;
  return 7.5625f * t * t + 0.984375f;
}

// Fonts only have ASCII glyphs: "Città" -> "Citta'", "Né" -> "Ne'".
inline String ascii(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); ++i) {
    uint8_t c = in[i];
    if (c < 0x80) { out += (char)c; continue; }
    if (c == 0xC3 && i + 1 < in.length()) {
      uint8_t d = in[++i];
      if (d == 0x9F) { out += "ss"; continue; }
      const char* lower = "aaaaaaaceeeeiiiidnooooo/ouuuuyby";  // 0xA0..0xBF
      if (d >= 0xA0) {
        out += lower[d - 0xA0];
        if (d == 0xA0 || d == 0xA8 || d == 0xA9 || d == 0xAC || d == 0xB2 || d == 0xB9) out += '\'';
      } else if (d >= 0x80) {
        out += (char)toupper(lower[d - 0x80]);
        if (d == 0x80 || d == 0x88 || d == 0x89 || d == 0x8C || d == 0x92 || d == 0x99) out += '\'';
      }
      continue;
    }
    // Skip the continuation bytes of any other multi-byte character.
    while (i + 1 < in.length() && (in[i + 1] & 0xC0) == 0x80) ++i;
  }
  return out;
}

struct Star { int16_t x, y; uint8_t phase; uint8_t size; };
struct Particle { float x, y, vx, vy, rot; uint16_t color; uint8_t size; };

class Ui {
 public:
  explicit Ui(TFT_eSPI& tft) : tft_(tft), s_(&tft) {}

  void begin(const Theme* theme) {
    th_ = theme;
    s_.setColorDepth(16);
    if (!s_.createSprite(W, H)) {
      // No PSRAM and not enough heap: 8 bit colour still looks fine.
      s_.setColorDepth(8);
      s_.createSprite(W, H);
    }
    for (auto& st : stars_) {
      st.x = random(W);
      st.y = random(110);
      st.phase = random(256);
      st.size = random(10) < 2 ? 2 : 1;
    }
  }

  void setTheme(const Theme* theme) { th_ = theme; }
  const Theme& theme() const { return *th_; }
  void push() { s_.pushSprite(0, 0); }

  // -------------------------------------------------------------------------
  // Boot animation: a tricolour train arrives at the stop, the title drops in
  // letter by letter, then the greeting and the connection status appear.
  // Returns true once the intro part is over (the caller may keep calling it
  // while WiFi connects).
  // -------------------------------------------------------------------------
  bool bootFrame(uint32_t t, const String& greeting, const String& status, float progress) {
    const uint16_t sky_top = RGB565(2, 4, 18);
    const uint16_t sky_bot = RGB565(20, 30, 70);
    s_.fillRectVGradient(0, 0, W, 132, sky_top, sky_bot);
    s_.fillRect(0, 132, W, H - 132, RGB565(6, 8, 20));

    // Stars fade in and twinkle.
    float stars_in = clamp01(t / 800.0f);
    for (auto& st : stars_) {
      float tw = 0.5f + 0.5f * sinf(t / 300.0f + st.phase);
      uint8_t a = (uint8_t)(255 * stars_in * (0.35f + 0.65f * tw));
      uint16_t c = s_.alphaBlend(a, TFT_WHITE, sky_top);
      if (st.size == 2) s_.fillCircle(st.x, st.y, 1, c);
      else s_.drawPixel(st.x, st.y, c);
    }

    // A moon, because it's always a bit magic at night.
    s_.fillSmoothCircle(300, 18, 9, RGB565(250, 240, 200), sky_top);
    s_.fillSmoothCircle(305, 15, 8, s_.alphaBlend(220, sky_top, sky_bot), RGB565(250, 240, 200));

    // Rails drawn in green, white and red from left to right.
    const int rail_y = 124;
    float rails = easeOutCubic(((int32_t)t - 100) / 700.0f);
    int rail_w = (int)(W * rails);
    s_.fillRect(0, rail_y, rail_w, 2, RGB565(0, 146, 70));
    s_.fillRect(0, rail_y + 2, rail_w, 2, TFT_WHITE);
    s_.fillRect(0, rail_y + 4, rail_w, 2, RGB565(206, 43, 55));
    for (int x = 6; x < rail_w; x += 18) s_.fillRect(x, rail_y + 7, 8, 2, RGB565(60, 60, 80));

    // The train slides in and brakes at the stop.
    float arrive = easeOutCubic(((int32_t)t - 300) / 1500.0f);
    int train_x = -200 + (int)(arrive * 270);
    float speed = 1.0f - arrive;
    if (t > 300) drawTrain(train_x, 76, speed, t);

    // Stop sign on the platform.
    if (t > 1500) {
      float pop = easeOutBounce(((int32_t)t - 1500) / 500.0f);
      int sh = (int)(34 * pop);
      s_.fillRect(26, rail_y - sh, 3, sh, RGB565(180, 180, 190));
      if (pop > 0.6f) {
        s_.fillSmoothRoundRect(12, rail_y - sh - 12, 31, 16, 3, RGB565(255, 204, 0), sky_bot);
        s_.setTextColor(RGB565(10, 20, 50));
        s_.setTextFont(1);
        s_.setTextDatum(MC_DATUM);
        s_.drawString("BUS", 28, rail_y - sh - 4);
      }
    }

    // Title: letters drop from the sky with a bounce.
    const char* title = "FERMATA";
    s_.setFreeFont(&FreeSansBold24pt7b);
    s_.setTextDatum(TL_DATUM);
    int total = s_.textWidth(title);
    int x = (W - total) / 2;
    for (int i = 0; title[i]; ++i) {
      char ch[2] = {title[i], 0};
      float p = easeOutBounce(((int32_t)t - 1500 - i * 90) / 600.0f);
      if ((int32_t)t > 1500 + i * 90) {
        int y = 8 - (int)((1 - p) * 70);
        s_.setTextColor(RGB565(0, 0, 10));
        s_.drawString(ch, x + 2, y + 2);
        s_.setTextColor(i < 2 ? RGB565(60, 200, 110) : (i > 4 ? RGB565(255, 90, 90) : TFT_WHITE));
        s_.drawString(ch, x, y);
      }
      x += s_.textWidth(ch);
    }

    // Sparkles around the title once it has landed.
    if (t > 2400) {
      for (int i = 0; i < 6; ++i) {
        float ph = ((int32_t)t - 2400) / 700.0f + i * 0.37f;
        float k = sinf(ph * 3.14159f);
        if (k <= 0) continue;
        int sx = 40 + (i * 53) % 240, sy = 6 + (i * 29) % 46;
        int r = (int)(6 * k);
        uint16_t c = s_.alphaBlend((uint8_t)(255 * k), RGB565(255, 230, 120), sky_top);
        s_.drawFastHLine(sx - r, sy, 2 * r + 1, c);
        s_.drawFastVLine(sx, sy - r, 2 * r + 1, c);
      }
    }

    // Greeting and status at the bottom.
    float fade = clamp01(((int32_t)t - 2600) / 600.0f);
    if (fade > 0) {
      s_.setFreeFont(&FreeSansBold9pt7b);
      s_.setTextDatum(MC_DATUM);
      s_.setTextColor(s_.alphaBlend((uint8_t)(255 * fade), RGB565(255, 220, 120), RGB565(6, 8, 20)));
      s_.drawString(greeting, W / 2, 142);

      s_.setTextFont(1);
      s_.setTextColor(s_.alphaBlend((uint8_t)(200 * fade), RGB565(170, 180, 210), RGB565(6, 8, 20)));
      s_.drawString(status, W / 2, 157);
      int bw = 120, bx = (W - bw) / 2;
      s_.drawRoundRect(bx, 163, bw, 5, 2, RGB565(50, 60, 100));
      if (progress < 0) {  // indeterminate: a bouncing segment
        float k = 0.5f + 0.5f * sinf(t / 250.0f);
        s_.fillRoundRect(bx + 1 + (int)(k * (bw - 32)), 164, 30, 3, 1, RGB565(255, 204, 0));
      } else {
        s_.fillRoundRect(bx + 1, 164, (int)((bw - 2) * clamp01(progress)), 3, 1, RGB565(255, 204, 0));
      }
    }
    return t > 3600;
  }

  // -------------------------------------------------------------------------
  // First boot: confetti and a little gift box.
  // -------------------------------------------------------------------------
  void welcomeFrame(uint32_t t, const String& name, const String& message) {
    if (t < 40 || confetti_.empty()) resetConfetti();
    s_.fillRectVGradient(0, 0, W, H, RGB565(30, 10, 60), RGB565(4, 22, 64));

    for (auto& p : confetti_) {
      p.x += p.vx;
      p.y += p.vy;
      p.vx += sinf((t + p.rot * 100) / 400.0f) * 0.03f;
      p.rot += 0.15f;
      if (p.y > H + 6) { p.y = -6; p.x = random(W); }
      int w = (int)(p.size * fabsf(cosf(p.rot)) + 1);
      s_.fillRect((int)p.x, (int)p.y, w, p.size, p.color);
    }

    // Gift box bouncing in.
    float in = easeOutBounce(t / 900.0f);
    int bx = 34, by = 50 + (int)((1 - in) * -90) + (int)(3 * sinf(t / 300.0f));
    uint16_t red = RGB565(206, 43, 55), gold = RGB565(255, 204, 0);
    s_.fillSmoothRoundRect(bx, by + 18, 56, 44, 4, red, RGB565(20, 16, 60));
    float lid = t > 1400 ? easeOutCubic(((int32_t)t - 1400) / 600.0f) : 0;
    int ly = by + 6 - (int)(lid * 14);
    s_.fillSmoothRoundRect(bx - 4, ly, 64, 14, 3, s_.alphaBlend(200, red, TFT_WHITE), RGB565(20, 16, 60));
    s_.fillRect(bx + 24, by + 18, 8, 44, gold);
    s_.fillRect(bx + 24, ly, 8, 14, gold);
    s_.fillSmoothCircle(bx + 20, ly - 4, 6, gold, RGB565(20, 16, 60));
    s_.fillSmoothCircle(bx + 36, ly - 4, 6, gold, RGB565(20, 16, 60));
    if (lid > 0.5f) {  // a little light comes out of the box
      for (int i = 0; i < 5; ++i) {
        float k = 0.5f + 0.5f * sinf(t / 200.0f + i);
        s_.drawLine(bx + 28, ly + 6, bx + 8 + i * 10, ly - 26 - (int)(8 * k), s_.alphaBlend(160, gold, RGB565(30, 10, 60)));
      }
    }

    float txt = clamp01(((int32_t)t - 600) / 700.0f);
    uint16_t bg = RGB565(18, 18, 62);
    s_.setTextDatum(TL_DATUM);
    s_.setFreeFont(&FreeSansBold18pt7b);
    s_.setTextColor(s_.alphaBlend((uint8_t)(255 * txt), TFT_WHITE, bg));
    String hello = name.length() ? "Ciao " + ascii(name) + "!" : String("Sorpresa!");
    fitFont(hello, 205, &FreeSansBold18pt7b, &FreeSansBold12pt7b);
    s_.drawString(hello, 108, 30);
    s_.setFreeFont(&FreeSans9pt7b);
    s_.setTextColor(s_.alphaBlend((uint8_t)(255 * txt), RGB565(255, 220, 120), bg));
    wrap(ascii(message), 108, 76, 205, 3);

    if (t > 2500 && (t / 600) % 2) {
      s_.setTextFont(1);
      s_.setTextDatum(BR_DATUM);
      s_.setTextColor(RGB565(170, 180, 210));
      s_.drawString("premi un tasto", W - 6, H - 5);
    }
  }

  // -------------------------------------------------------------------------
  // WiFi setup instructions.
  // -------------------------------------------------------------------------
  void wifiSetupFrame(uint32_t t) {
    const Theme& th = *th_;
    s_.fillSprite(th.bg);
    header("Primo avvio: WiFi", nullptr, false, 0, 0);

    // A phone with WiFi waves.
    int px = 26, py = 44;
    s_.fillSmoothRoundRect(px, py, 50, 92, 8, th.dim, th.bg);
    s_.fillRoundRect(px + 4, py + 10, 42, 70, 3, th.panel);
    for (int i = 0; i < 3; ++i) {
      float k = fmodf(t / 900.0f + i / 3.0f, 1.0f);
      uint16_t c = s_.alphaBlend((uint8_t)(255 * (1 - k)), th.accent, th.panel);
      s_.drawSmoothArc(px + 25, py + 62, 6 + (int)(k * 22), 4 + (int)(k * 22), 135, 225, c, th.panel);
    }
    s_.fillSmoothCircle(px + 25, py + 62, 3, th.accent, th.panel);

    s_.setTextDatum(TL_DATUM);
    s_.setFreeFont(&FreeSans9pt7b);
    s_.setTextColor(th.text);
    s_.drawString("1. Col telefono collegati", 92, 40);
    s_.drawString("   alla rete WiFi:", 92, 60);
    s_.setFreeFont(&FreeSansBold9pt7b);
    s_.setTextColor(th.accent);
    s_.drawString(SETUP_AP_NAME, 106, 82);
    s_.setFreeFont(&FreeSans9pt7b);
    s_.setTextColor(th.text);
    s_.drawString("2. Scegli la tua rete di casa", 92, 106);
    s_.drawString("   e inserisci la password.", 92, 126);
    footer("BOOT = modalita' demo senza WiFi");
  }

  // -------------------------------------------------------------------------
  // Departures board.
  // -------------------------------------------------------------------------
  struct BoardInfo {
    String stop_name;
    String ticker;
    int status;       // 0 ok, 1 stale/connecting, 2 error
    bool busy;
    bool demo;
    bool time_ok;     // false until NTP has answered: no clock in the header
    bool scheduled;   // timetable feed: no live delays
  };

  void boardFrame(uint32_t ms, time_t now, const gtfsrt::Result& res, bool has_data, const BoardInfo& info) {
    const Theme& th = *th_;
    scheduled_ = info.scheduled;
    s_.fillSprite(th.bg);

    const int top = 28, rows_h = 126, row_h = 42;
    int count = has_data ? res.count : 0;
    // Hide departures that have already left since the last download.
    int first = 0;
    while (first < count && res.deps[first].time < now - 30) ++first;
    int visible = count - first;

    if (!has_data) {
      if (info.status == 2) emptyState(ms, "Dati non pronti", "Riprovo tra poco", kWarning);
      else emptyState(ms, "Carico...", "Un attimo di pazienza", kSpinner);
    } else if (visible == 0) {
      emptyState(ms, "Nessuna partenza", "nelle prossime 3 ore", kMoon);
    } else {
      int pages = (visible + 2) / 3;
      const uint32_t page_ms = 6000, slide_ms = 450;
      int page = (ms / page_ms) % pages;
      uint32_t in_page = ms % page_ms;
      int dy = 0;
      if (pages > 1 && in_page > page_ms - slide_ms)
        dy = (int)(rows_h * easeInOut((in_page - (page_ms - slide_ms)) / (float)slide_ms));
      for (int p = 0; p < 2; ++p) {
        int pg = (page + p) % pages;
        int base = top + p * rows_h - dy;
        if (base >= top + rows_h || (p == 1 && dy == 0)) continue;
        for (int r = 0; r < 3; ++r) {
          int idx = first + pg * 3 + r;
          if (idx >= count) break;
          drawDeparture(res.deps[idx], now, base + r * row_h, row_h, ms);
        }
      }
    }

    header(info.stop_name, info.time_ok ? &now : nullptr, info.busy, info.status, ms, true);
    if (info.demo) badge("DEMO", 4, 6);
    tickerFooter(info.ticker, ms, has_data && visible > 3 ? (visible + 2) / 3 : 1);
  }

  // -------------------------------------------------------------------------
  // Menu.
  // -------------------------------------------------------------------------
  struct MenuItem { String label; String value; };

  void menuFrame(uint32_t ms, const String& title, const MenuItem* items, int count, int selected) {
    const Theme& th = *th_;
    s_.fillSprite(th.bg);
    const int top = 30, item_h = 30, visible = 4;

    // Smooth scroll and highlight.
    int first = selected - visible + 1;
    if (first < 0) first = 0;
    float target_scroll = first * item_h;
    menu_scroll_ += (target_scroll - menu_scroll_) * 0.35f;
    float target_hl = top + selected * item_h - target_scroll;
    menu_hl_ += (target_hl - menu_hl_) * 0.35f;
    if (fabsf(menu_hl_ - target_hl) > 60) menu_hl_ = target_hl;

    s_.fillSmoothRoundRect(6, (int)menu_hl_ + 2, W - 12, item_h - 4, 8, th.accent, th.bg);
    for (int i = 0; i < count; ++i) {
      int y = top + i * item_h - (int)menu_scroll_;
      if (y < top - item_h || y > top + visible * item_h) continue;
      bool sel = i == selected;
      s_.setFreeFont(&FreeSansBold9pt7b);
      s_.setTextDatum(ML_DATUM);
      s_.setTextColor(sel ? th.accent_text : th.text);
      s_.drawString(ascii(items[i].label), 18, y + item_h / 2);
      if (items[i].value.length()) {
        s_.setFreeFont(&FreeSans9pt7b);
        s_.setTextDatum(MR_DATUM);
        s_.setTextColor(sel ? th.accent_text : th.dim);
        s_.drawString(ascii(items[i].value), W - 18, y + item_h / 2);
      }
    }
    // Scrollbar.
    if (count > visible) {
      int track = visible * item_h - 8;
      int bar = track * visible / count;
      int by = top + 4 + (int)((track - bar) * (float)selected / (count - 1));
      s_.fillRoundRect(W - 4, by, 2, bar, 1, th.dim);
    }
    header(title, nullptr, false, 0, ms);
    footer("KEY giu'   BOOT ok   tieni = indietro");
  }

  void resetMenuAnimation() { menu_hl_ = 30; menu_scroll_ = 0; }

  // -------------------------------------------------------------------------
  // Big clock (with snow in December).
  // -------------------------------------------------------------------------
  void clockFrame(uint32_t ms, time_t now, bool time_ok, const String& next_line) {
    const Theme& th = *th_;
    s_.fillSprite(th.bg);
    struct tm tmv;
    localtime_r(&now, &tmv);

    if (time_ok && tmv.tm_mon == 11) snow(ms);

    char buf[16];
    s_.setTextDatum(MC_DATUM);
    s_.setTextColor(th.accent);
    if (time_ok) {
      snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      s_.setTextFont(7);  // 7 segment digits
      s_.drawString(buf, W / 2, 62);
    } else {
      s_.setFreeFont(&FreeSansBold24pt7b);
      s_.drawString("--:--", W / 2, 62);
    }

    static const char* days[] = {"domenica", "lunedi'", "martedi'", "mercoledi'", "giovedi'", "venerdi'", "sabato"};
    static const char* months[] = {"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio",
                                   "agosto", "settembre", "ottobre", "novembre", "dicembre"};
    s_.setFreeFont(&FreeSansBold12pt7b);
    s_.setTextColor(th.text);
    if (time_ok) {
      snprintf(buf, sizeof(buf), "%d", tmv.tm_mday);
      s_.drawString(String(days[tmv.tm_wday]) + " " + buf + " " + months[tmv.tm_mon], W / 2, 112);
    }
    // Seconds as a thin progress bar.
    s_.fillRect(0, H - 3, W * tmv.tm_sec / 59, 3, th.accent);
    if (next_line.length()) {
      s_.setFreeFont(&FreeSans9pt7b);
      s_.setTextColor(th.dim);
      s_.drawString(ascii(next_line), W / 2, 142);
    }
  }

  // -------------------------------------------------------------------------
  // Generic page with a title and some lines (info, help, confirmations).
  // -------------------------------------------------------------------------
  void pageFrame(uint32_t ms, const String& title, const String* lines, int n, const String& hint) {
    const Theme& th = *th_;
    s_.fillSprite(th.bg);
    header(title, nullptr, false, 0, ms);
    s_.setTextDatum(TL_DATUM);
    int y = 34;
    for (int i = 0; i < n; ++i) {
      const String& l = lines[i];
      bool strong = l.startsWith("*");
      s_.setFreeFont(strong ? &FreeSansBold12pt7b : &FreeSans9pt7b);
      s_.setTextColor(strong ? th.accent : th.text);
      s_.drawString(ascii(strong ? l.substring(1) : l), 10, y);
      y += strong ? 28 : 21;
    }
    footer(hint);
  }

  // Small message over whatever is on screen.
  void toast(const String& text) {
    const Theme& th = *th_;
    s_.setFreeFont(&FreeSansBold9pt7b);
    String t = ascii(text);
    int w = min(W - 20, (int)s_.textWidth(t) + 28), h = 30;
    int x = (W - w) / 2, y = H - h - 22;
    s_.fillSmoothRoundRect(x + 2, y + 3, w, h, 10, s_.alphaBlend(120, TFT_BLACK, th.bg), th.bg);
    s_.fillSmoothRoundRect(x, y, w, h, 10, th.accent, th.bg);
    s_.setTextDatum(MC_DATUM);
    s_.setTextColor(th.accent_text);
    s_.drawString(t, W / 2, y + h / 2);
  }

  void resetConfetti() {
    confetti_.clear();
    static const uint16_t colors[] = {RGB565(0, 146, 70), TFT_WHITE, RGB565(206, 43, 55), RGB565(255, 204, 0)};
    for (int i = 0; i < 70; ++i) {
      Particle p;
      p.x = random(W);
      p.y = -random(H);
      p.vx = (random(100) - 50) / 100.0f;
      p.vy = 0.8f + random(100) / 60.0f;
      p.rot = random(628) / 100.0f;
      p.color = colors[i % 4];
      p.size = 3 + random(3);
      confetti_.push_back(p);
    }
  }

 private:
  void drawTrain(int x, int y, float speed, uint32_t t) {
    const int w = 200, h = 44;
    const uint16_t sky = RGB565(14, 20, 50);
    const uint16_t body = RGB565(240, 242, 245);
    const uint16_t window = RGB565(30, 50, 90);
    const uint16_t green = RGB565(0, 146, 70), red = RGB565(206, 43, 55);

    // Speed lines behind the train.
    if (speed > 0.05f) {
      for (int i = 0; i < 6; ++i) {
        int len = (int)(60 * speed) + (i * 13) % 30;
        int ly = y + 6 + i * 7;
        s_.drawFastHLine(x - len - 8 - (i * 17) % 20, ly, len, s_.alphaBlend((uint8_t)(200 * speed), TFT_WHITE, sky));
      }
    }
    // Headlight beam on the rails.
    float glow = 0.6f + 0.4f * sinf(t / 180.0f);
    for (int dx = 2; dx < 90; dx += 2) {
      float k = 1.0f - dx / 90.0f;
      int half = 2 + dx / 6;
      uint16_t c = s_.alphaBlend((uint8_t)(110 * k * k * glow), RGB565(255, 240, 160), sky);
      s_.drawFastVLine(x + w - 4 + dx, y + h - 13 - half + dx / 8, 2 * half, c);
    }

    // Pantograph.
    s_.drawLine(x + 60, y, x + 74, y - 10, RGB565(120, 120, 140));
    s_.drawLine(x + 88, y, x + 74, y - 10, RGB565(120, 120, 140));
    s_.drawFastHLine(x + 64, y - 10, 22, RGB565(160, 160, 180));

    // Body with a rounded nose.
    s_.fillSmoothRoundRect(x, y, w - 30, h, 6, body, sky);
    s_.fillSmoothRoundRect(x + w - 70, y, 70, h, 22, body, sky);
    // Tricolour livery.
    s_.fillRect(x + 2, y + h - 15, w - 24, 4, green);
    s_.fillRect(x + 2, y + h - 11, w - 20, 4, red);
    // Windscreen.
    s_.fillTriangle(x + w - 40, y + 5, x + w - 14, y + 5, x + w - 40, y + 24, window);
    s_.fillTriangle(x + w - 14, y + 5, x + w - 6, y + 20, x + w - 40, y + 24, window);
    s_.fillRect(x + w - 40, y + 5, 2, 19, window);
    // Windows with a reflection.
    for (int i = 0; i < 6; ++i) {
      int wx = x + 10 + i * 24;
      s_.fillRoundRect(wx, y + 8, 18, 14, 3, window);
      s_.drawLine(wx + 3, y + 19, wx + 10, y + 10, RGB565(90, 120, 170));
    }
    // Door.
    s_.drawRect(x + 154, y + 6, 12, 28, RGB565(180, 185, 195));
    // Headlight.
    s_.fillSmoothCircle(x + w - 7, y + h - 13, 3, s_.alphaBlend((uint8_t)(255 * glow), RGB565(255, 245, 180), body), body);

    // Bogies and spinning wheels.
    float angle = x / 6.0f;
    for (int b = 0; b < 2; ++b) {
      int bx = x + 22 + b * 120;
      s_.fillRoundRect(bx - 4, y + h - 2, 44, 6, 2, RGB565(50, 52, 60));
      for (int k = 0; k < 2; ++k) {
        int cx = bx + 6 + k * 24, cy = y + h + 5;
        s_.fillCircle(cx, cy, 5, RGB565(40, 40, 46));
        s_.drawLine(cx - (int)(4 * cosf(angle)), cy - (int)(4 * sinf(angle)), cx + (int)(4 * cosf(angle)),
                    cy + (int)(4 * sinf(angle)), RGB565(150, 150, 160));
      }
    }
  }

  void header(const String& title, const time_t* now, bool busy, int status, uint32_t ms, bool icon = false) {
    const Theme& th = *th_;
    s_.fillRect(0, 0, W, 26, th.panel);
    s_.drawFastHLine(0, 26, W, th.accent);

    int right = W - 6;
    if (now) {
      struct tm tmv;
      localtime_r(now, &tmv);
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      s_.setFreeFont(&FreeSansBold12pt7b);
      s_.setTextDatum(MR_DATUM);
      s_.setTextColor(th.text);
      int tw = s_.textWidth("00:00");
      s_.drawString(buf, right, 12);
      right -= tw + 10;

      // Live dot: green pulse when the data is fresh.
      uint16_t c = status == 0 ? th.ok : (status == 1 ? th.accent : th.late);
      float k = 0.5f + 0.5f * sinf(ms / 250.0f);
      if (busy) {
        for (int i = 0; i < 8; ++i) {
          float a = (ms / 80 + i) % 8 * 0.785f;
          s_.fillCircle(right - 5 + (int)(5 * cosf(a)), 13 + (int)(5 * sinf(a)), 1,
                        s_.alphaBlend(32 * i, c, th.panel));
        }
      } else {
        uint16_t dot = status == 0 ? s_.alphaBlend((uint8_t)(120 + 135 * k), c, th.panel) : c;
        s_.fillSmoothCircle(right - 5, 13, 4, dot, th.panel);
      }
      right -= 16;
    }

    // Title; scrolls if it does not fit.
    int left = 8;
    if (icon) {
      drawBusIcon(left, 6, th.accent, th.panel);
      left += 26;
    }
    s_.setFreeFont(&FreeSansBold9pt7b);
    s_.setTextDatum(ML_DATUM);
    s_.setTextColor(th.text);
    String t = ascii(title);
    int avail = right - left;
    int tw = s_.textWidth(t);
    if (tw <= avail) {
      s_.drawString(t, left, 12);
    } else {
      s_.setViewport(left, 0, avail, 26);
      int span = tw + 40;
      int off = (ms / 30) % span;
      s_.drawString(t, -off, 12);
      s_.drawString(t, -off + span, 12);
      s_.resetViewport();
    }
  }

  void drawBusIcon(int x, int y, uint16_t c, uint16_t bg) {
    s_.fillSmoothRoundRect(x, y, 20, 14, 3, c, bg);
    s_.fillRect(x + 3, y + 3, 6, 5, bg);
    s_.fillRect(x + 11, y + 3, 6, 5, bg);
    s_.fillCircle(x + 5, y + 14, 2, bg);
    s_.fillCircle(x + 15, y + 14, 2, bg);
  }

  void footer(const String& text) {
    const Theme& th = *th_;
    s_.fillRect(0, H - 16, W, 16, th.panel);
    s_.setTextFont(1);
    s_.setTextDatum(MC_DATUM);
    s_.setTextColor(th.dim);
    s_.drawString(text, W / 2, H - 8);
  }

  void tickerFooter(const String& text, uint32_t ms, int pages) {
    const Theme& th = *th_;
    s_.fillRect(0, H - 16, W, 16, th.panel);
    int right = pages > 1 ? W - 12 - pages * 9 : W;
    s_.setViewport(0, H - 16, right, 16);
    s_.setTextFont(1);
    s_.setTextDatum(ML_DATUM);
    s_.setTextColor(th.dim);
    String t = ascii(text);
    int tw = s_.textWidth(t);
    if (tw < right - 12) {
      s_.drawString(t, 6, 8);
    } else {
      int span = tw + 60;
      int off = (ms / 25) % span;
      s_.drawString(t, 6 - off, 8);
      s_.drawString(t, 6 - off + span, 8);
    }
    s_.resetViewport();
    if (pages > 1) {  // redraw the dots over the panel
      int page = (ms / 6000) % pages;
      for (int i = 0; i < pages; ++i)
        s_.fillSmoothCircle(W - 8 - (pages - 1 - i) * 9, H - 8, 3, i == page ? th.accent : th.dim, th.panel);
    }
  }

  void badge(const char* text, int x, int y) {
    s_.setTextFont(1);
    int w = s_.textWidth(text) + 8;
    s_.fillRoundRect(x, y, w, 14, 3, th_->late);
    s_.setTextDatum(MC_DATUM);
    s_.setTextColor(TFT_WHITE);
    s_.drawString(text, x + w / 2, y + 7);
  }

  uint16_t lineColor(const char* route) {
    uint32_t h = 5381;
    for (const char* p = route; *p; ++p) h = h * 33 + (uint8_t)*p;
    return kLineColors[h % (sizeof(kLineColors) / sizeof(kLineColors[0]))];
  }

  void drawDeparture(const gtfsrt::Departure& d, time_t now, int y, int h, uint32_t ms) {
    const Theme& th = *th_;
    if (y > 4) s_.drawFastHLine(8, y, W - 16, th.panel);

    // Line badge.
    String route = ascii(d.route[0] ? String(d.route) : String("?"));
    uint16_t lc = lineColor(d.route);
    s_.fillSmoothRoundRect(8, y + 5, 58, h - 10, 8, lc, th.bg);
    s_.setTextDatum(MC_DATUM);
    s_.setTextColor(TFT_WHITE);
    fitFont(route, 52, &FreeSansBold12pt7b, &FreeSansBold9pt7b);
    if (s_.textWidth(route) > 52) s_.setTextFont(2);
    s_.drawString(route, 37, y + h / 2 + 1);

    // Minutes on the right.
    long secs = (long)(d.time - now);
    int mins = secs <= 0 ? 0 : (int)(secs / 60);
    int right = W - 10;
    s_.setTextDatum(MR_DATUM);
    int min_w;
    if (d.skipped) {
      s_.setFreeFont(&FreeSansBold9pt7b);
      s_.setTextColor(th.late);
      s_.drawString("SALTA", right, y + h / 2);
      min_w = s_.textWidth("SALTA");
    } else if (secs < 60) {
      // "ORA" pulses when the bus is arriving.
      float k = 0.5f + 0.5f * sinf(ms / 160.0f);
      s_.setFreeFont(&FreeSansBold12pt7b);
      s_.setTextColor(s_.alphaBlend((uint8_t)(110 + 145 * k), th.accent, th.bg));
      s_.drawString("ORA", right, y + h / 2);
      min_w = s_.textWidth("ORA");
    } else if (mins >= 60) {
      struct tm tmv;
      time_t tt = (time_t)d.time;
      localtime_r(&tt, &tmv);
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      s_.setFreeFont(&FreeSansBold12pt7b);
      s_.setTextColor(th.text);
      s_.drawString(buf, right, y + h / 2);
      min_w = s_.textWidth(buf);
    } else {
      s_.setTextFont(1);
      s_.setTextColor(th.dim);
      s_.drawString("min", right, y + h - 9);
      s_.setFreeFont(&FreeSansBold18pt7b);
      s_.setTextColor(th.accent);
      s_.drawString(String(mins), right - 1, y + h / 2 - 5);
      min_w = s_.textWidth("00");
    }

    // Middle: destination (if the feed has it) and punctuality.
    int mx = 74, mw = right - min_w - 8 - mx;
    // Destination and vehicle take turns when the feed has both.
    String head = ascii(String(d.headsign));
    if (d.info[0] && (!head.length() || (ms / 4000) % 2)) head = ascii(String(d.info));
    if (!head.length()) head = "Linea " + route;
    s_.setViewport(mx, y, mw, h);
    s_.setFreeFont(&FreeSansBold9pt7b);
    s_.setTextDatum(ML_DATUM);
    s_.setTextColor(th.text);
    int hw = s_.textWidth(head);
    if (hw <= mw) {
      s_.drawString(head, 0, 13);
    } else {
      int span = hw + 30;
      int off = (int)((ms / 35) % span);
      s_.drawString(head, -off, 13);
      s_.drawString(head, -off + span, 13);
    }

    struct tm tmv;
    time_t tt = (time_t)d.time;
    localtime_r(&tt, &tmv);
    char when[8];
    snprintf(when, sizeof(when), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    String punct;
    uint16_t pc;
    if (d.skipped) { punct = "fermata soppressa"; pc = th.late; }
    else if (!d.has_delay) { punct = scheduled_ ? "orario programmato" : "in tempo reale"; pc = th.dim; }
    else if (d.delay >= 90) { punct = "ritardo " + String((d.delay + 30) / 60) + " min"; pc = th.late; }
    else if (d.delay <= -90) { punct = "anticipo " + String((-d.delay + 30) / 60) + " min"; pc = th.early; }
    else { punct = "in orario"; pc = th.ok; }
    s_.setFreeFont(&FreeSans9pt7b);
    s_.setTextColor(pc);
    s_.drawString(punct, 0, 31);
    s_.setTextColor(th.dim);
    s_.setTextDatum(MR_DATUM);
    if (s_.textWidth(punct) + s_.textWidth(when) + 10 < mw) s_.drawString(when, mw, 31);
    s_.resetViewport();
  }

  enum EmptyIcon { kSpinner, kMoon, kWarning };

  void emptyState(uint32_t ms, const char* title, const char* subtitle, EmptyIcon icon) {
    const Theme& th = *th_;
    int cx = 56, cy = 90;
    if (icon == kWarning) {
      s_.fillTriangle(cx, cy - 24, cx - 26, cy + 20, cx + 26, cy + 20, th.late);
      s_.fillRect(cx - 2, cy - 10, 5, 18, TFT_WHITE);
      s_.fillRect(cx - 2, cy + 11, 5, 5, TFT_WHITE);
    } else if (icon == kSpinner) {
      for (int i = 0; i < 12; ++i) {
        float a = i * 0.5236f;
        int k = (int)((ms / 70 + i) % 12);
        s_.fillSmoothCircle(cx + (int)(18 * cosf(a)), cy + (int)(18 * sinf(a)), 3,
                            s_.alphaBlend(20 * k + 15, th.accent, th.bg), th.bg);
      }
    } else {
      // Sleeping moon.
      s_.fillSmoothCircle(cx, cy, 22, th.accent, th.bg);
      s_.fillSmoothCircle(cx + 10, cy - 8, 20, th.bg, th.accent);
      s_.setTextFont(2);
      s_.setTextColor(th.dim);
      int z = (ms / 600) % 3;
      for (int i = 0; i <= z; ++i) s_.drawString("z", cx + 24 + i * 9, cy - 20 - i * 9);
    }
    s_.setTextDatum(ML_DATUM);
    s_.setTextColor(th.text);
    fitFont(title, W - 104, &FreeSansBold12pt7b, &FreeSansBold9pt7b);
    s_.drawString(title, 96, 80);
    s_.setFreeFont(&FreeSans9pt7b);
    s_.setTextColor(th.dim);
    s_.drawString(subtitle, 96, 106);
  }

  void snow(uint32_t ms) {
    for (int i = 0; i < 40; ++i) {
      int x = (i * 83 + (int)(8 * sinf(ms / 900.0f + i))) % W;
      int y = (int)((ms / (18 + i % 7) + i * 37) % (H + 10)) - 5;
      s_.fillCircle(x, y, i % 3 == 0 ? 2 : 1, s_.alphaBlend(170, TFT_WHITE, th_->bg));
    }
  }

  // Uses the big font unless the text is too wide for it.
  void fitFont(const String& text, int width, const GFXfont* big, const GFXfont* small) {
    s_.setFreeFont(big);
    if (s_.textWidth(text) > width) s_.setFreeFont(small);
  }

  // Word-wraps text in the current font.
  void wrap(const String& text, int x, int y, int width, int max_lines) {
    int line = 0, start = 0;
    while (start < (int)text.length() && line < max_lines) {
      int end = start, last_space = -1;
      while (end < (int)text.length()) {
        if (text[end] == ' ') last_space = end;
        if (s_.textWidth(text.substring(start, end + 1)) > width) break;
        ++end;
      }
      if (end < (int)text.length() && last_space > start) end = last_space;
      s_.drawString(text.substring(start, end), x, y + line * 20);
      start = end;
      while (start < (int)text.length() && text[start] == ' ') ++start;
      ++line;
    }
  }

  TFT_eSPI& tft_;
  TFT_eSprite s_;
  const Theme* th_ = nullptr;
  Star stars_[60];
  std::vector<Particle> confetti_;
  float menu_hl_ = 30, menu_scroll_ = 0;
  bool scheduled_ = false;
};
