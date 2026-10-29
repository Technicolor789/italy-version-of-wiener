// Fermata - tabellone delle partenze in tempo reale per LilyGO T-Display S3.
//
// Boot animation, first-boot surprise, WiFi setup from the phone, live
// departures from the Italian GTFS-Realtime feeds (organised by region),
// a two-button menu and a settings page at http://fermata.local.
//
// Board: "LilyGo T-Display-S3" (esp32 by Espressif), PSRAM: "OPI PSRAM".
// Libraries: TFT_eSPI (configured with Setup206_LilyGo_T_Display_S3),
// WiFiManager by tzapu. See README-fermata.md.

#include <ESPmDNS.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "config.h"
#include "settings.h"
#include "gtfsrt.h"
#include "fetcher.h"
#include "ui.h"
#include "web.h"

#if !defined(USER_SETUP_ID) || USER_SETUP_ID != 206
#warning "TFT_eSPI non e' configurata per la T-Display S3: in User_Setup_Select.h attiva Setup206_LilyGo_T_Display_S3.h"
#endif

// ---------------------------------------------------------------------------
// Buttons: short press on release, long press after 650 ms while held.
// ---------------------------------------------------------------------------
class Button {
 public:
  enum Event { kNone, kShort, kLong };
  explicit Button(uint8_t pin) : pin_(pin) {}
  void begin() { pinMode(pin_, INPUT_PULLUP); }

  Event poll() {
    bool down = digitalRead(pin_) == LOW;
    uint32_t now = millis();
    if (down != raw_) {
      raw_ = down;
      changed_ = now;
    }
    if (now - changed_ < 25 || down == stable_) {
      if (stable_ && !long_fired_ && now - since_ > 650) {
        long_fired_ = true;
        return kLong;
      }
      return kNone;
    }
    stable_ = down;
    if (down) {
      since_ = now;
      long_fired_ = false;
      return kNone;
    }
    return long_fired_ ? kNone : kShort;
  }

 private:
  uint8_t pin_;
  bool raw_ = false, stable_ = false, long_fired_ = true;
  uint32_t changed_ = 0, since_ = 0;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class Mode { Boot, Welcome, WifiSetup, Board, Menu, Clock, Page };
enum class MenuId { Main, Regions, Stops, Reset };
enum class PageId { Info, Configure, RegionEmpty };

TFT_eSPI tft;
Ui ui(tft);
Settings settings;
Fetcher fetcher;
WebUi web(settings);
WiFiManager wm;
Button btn_ok(PIN_BTN_OK), btn_next(PIN_BTN_NEXT);

Mode mode = Mode::Boot;
uint32_t mode_since = 0;
uint32_t boot_offset = 0;  // to resume the boot screen after the surprise
bool has_creds = false;
bool online = false;       // services started after the first connection
bool demo = false;

MenuId menu_id = MenuId::Main;
int menu_sel = 0;
String menu_region;
PageId page_id = PageId::Info;
Mode page_back = Mode::Menu;

gtfsrt::Result result;
bool has_data = false;
uint32_t seen_generation = 0;

String toast_text;
uint32_t toast_until = 0;

static const time_t kDemoStart = 1796108280;  // 1 dicembre 2026, 07:58

void setMode(Mode m) {
  mode = m;
  mode_since = millis();
  if (m == Mode::Menu) ui.resetMenuAnimation();
}

void toast(const String& text) {
  toast_text = text;
  toast_until = millis() + 1600;
}

bool timeValid() { return time(nullptr) > 1700000000; }

time_t nowEpoch() {
  if (timeValid()) return time(nullptr);
  return kDemoStart + millis() / 1000;
}

void setBacklight(int level) {
  static const uint8_t duty[] = {0, 18, 45, 90, 160, 255};
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  static bool attached = false;
  if (!attached) attached = ledcAttach(PIN_BACKLIGHT, 5000, 8);
  ledcWrite(PIN_BACKLIGHT, duty[constrain(level, 1, 5)]);
#else
  static bool attached = false;
  if (!attached) {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_BACKLIGHT, 0);
    attached = true;
  }
  ledcWrite(0, duty[constrain(level, 1, 5)]);
#endif
}

void applyTheme() {
  ui.setTheme(&kThemes[settings.theme]);
  tft.setRotation(settings.flip ? 3 : 1);
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------
String urlEncode(const String& s) {
  String out;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == ',') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 15];
      out += hex[c & 15];
    }
  }
  return out;
}

void requestDepartures() {
  const Favorite* f = settings.current();
  FetchRequest r;
  if (f) {
    r.stops = f->stops;
    r.label = f->name;
    const FeedInfo* fi = findFeed(f->feed.c_str());
    if (f->feed == "custom") {
      r.url = settings.custom_url;
    } else if (settings.proxy_url.length()) {
      r.url = settings.proxy_url + (settings.proxy_url.indexOf('?') >= 0 ? "&" : "?") + "feed=" + f->feed +
              "&stops=" + urlEncode(f->stops);
    } else if (fi) {
      r.url = fi->url;
    }
  }
  has_data = false;
  fetcher.request(r);
}

// Departures that never run out, for trying the display without WiFi.
void demoDepartures(time_t now) {
  struct DemoLine { const char* route; const char* headsign; int period; int phase; int delay; };
  static const DemoLine lines[] = {
    {"64", "San Pietro", 420, 50, 120},  {"H", "Capolinea Inferno", 600, 200, 0},
    {"8", "Casaletto", 540, 330, -60},   {"MEA", "Battistini", 300, 100, 0},
    {"40", "Borgo Pio", 780, 610, 300},  {"910", "Piazza Mancini", 900, 420, 60},
  };
  result = gtfsrt::Result();
  for (const auto& l : lines) {
    for (int k = 0; k < 2; ++k) {
      gtfsrt::Departure d = {};
      strncpy(d.route, l.route, sizeof(d.route) - 1);
      strncpy(d.headsign, l.headsign, sizeof(d.headsign) - 1);
      d.time = now + (l.period - (now + l.phase) % l.period) + k * l.period;
      d.delay = l.delay;
      d.has_delay = true;
      result.add(d);
    }
  }
  result.ok = true;
  has_data = true;
}

String regionOf(const Favorite& f) {
  const FeedInfo* fi = findFeed(f.feed.c_str());
  return fi ? fi->region : "";
}

String feedName(const Favorite& f) {
  const FeedInfo* fi = findFeed(f.feed.c_str());
  return fi ? fi->name : f.feed;
}

String ageText(uint32_t ms) {
  uint32_t s = (millis() - ms) / 1000;
  if (s < 60) return String(s) + " s fa";
  return String(s / 60) + " min fa";
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
String departuresHtml() {
  if (demo) return "<p class=hint>Modalit&agrave; demo attiva.</p>";
  const Favorite* f = settings.current();
  if (!f) return "<p class=hint>Nessuna fermata: aggiungine una qui sotto.</p>";
  gtfsrt::Result r;
  bool data = false;
  uint32_t seen = UINT32_MAX;
  fetcher.snapshot(r, data, seen);
  Fetcher::Status st = fetcher.status();
  String h = "<p><b>" + f->name + "</b> &middot; " + feedName(*f) + " &middot; ID " + f->stops + "</p>";
  if (st.error.length()) h += "<p class=hint>Errore: " + st.error + "</p>";
  if (!data) return h + "<p class=hint>In attesa dei dati...</p>";
  if (!r.count) return h + "<p class=hint>Nessuna partenza nelle prossime 3 ore.</p>";
  h += "<table>";
  time_t now = nowEpoch();
  for (int i = 0; i < r.count && i < 8; ++i) {
    long m = (long)(r.deps[i].time - now) / 60;
    h += "<tr><td><span class=line>" + String(r.deps[i].route) + "</span></td><td>" + String(r.deps[i].headsign) +
         "</td><td style='text-align:right'>" + (m <= 0 ? String("ora") : String(String(m) + " min")) + "</td></tr>";
  }
  return h + "</table>";
}

void goOnline() {
  if (online) return;
  online = true;
  configTzTime(TZ_ITALY, "pool.ntp.org", "time.inrim.it", "time.google.com");
  MDNS.begin(HOSTNAME);
  MDNS.addService("http", "tcp", 80);
  web.begin(
      [] {
        applyTheme();
        setBacklight(settings.brightness);
        requestDepartures();
        toast("Impostazioni salvate");
      },
      departuresHtml);
  fetcher.begin();
  requestDepartures();
}

void startPortal() {
  wm.setConfigPortalBlocking(false);
  wm.setTitle("Fermata");
  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(0);
  wm.setShowInfoUpdate(false);
  wm.startConfigPortal(SETUP_AP_NAME);
}

void stopPortal() {
  if (wm.getConfigPortalActive()) wm.stopConfigPortal();
}

void afterIntro() {
  if (WiFi.status() == WL_CONNECTED || demo) {
    if (WiFi.status() == WL_CONNECTED) goOnline();
    setMode(Mode::Board);
  } else if (has_creds && millis() < 20000) {
    boot_offset = 3600;  // keep showing "connecting" on the boot screen
    setMode(Mode::Boot);
  } else {
    startPortal();
    setMode(Mode::WifiSetup);
  }
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
static Ui::MenuItem items[12];
static int item_count = 0;
static int stop_index[MAX_FAVORITES];

void buildMenu() {
  item_count = 0;
  auto add = [](const String& label, const String& value) {
    items[item_count].label = label;
    items[item_count].value = value;
    item_count++;
  };
  switch (menu_id) {
    case MenuId::Main: {
      const Favorite* f = settings.current();
      add("Partenze", "");
      add("Fermate", f ? f->name : "nessuna");
      add("Orologio", "");
      add("Tema", kThemes[settings.theme].name);
      add("Luminosita'", String(settings.brightness) + " / 5");
      add("Ruota schermo", settings.flip ? "si'" : "no");
      add("Configura dal telefono", "");
      add("Informazioni", "");
      add("Modalita' demo", demo ? "si'" : "no");
      add("Rivedi la sorpresa", "");
      add("Reset", "");
      break;
    }
    case MenuId::Regions: {
      // In region order, as in kFeeds.
      for (int i = 0; i < kFeedCount; ++i) {
        if (i && strcmp(kFeeds[i].region, kFeeds[i - 1].region) == 0) continue;
        int n = 0;
        for (int k = 0; k < settings.favorite_count; ++k)
          if (regionOf(settings.favorites[k]) == kFeeds[i].region) n++;
        add(kFeeds[i].region, n ? String(String(n) + (n == 1 ? " fermata" : " fermate")) : String("-"));
      }
      break;
    }
    case MenuId::Stops: {
      for (int k = 0; k < settings.favorite_count; ++k) {
        if (regionOf(settings.favorites[k]) != menu_region) continue;
        stop_index[item_count] = k;
        add((k == settings.active ? "> " : "") + settings.favorites[k].name, feedName(settings.favorites[k]));
      }
      break;
    }
    case MenuId::Reset:
      add("No, torna indietro", "");
      add("Cancella solo il WiFi", "");
      add("Cancella tutto", "");
      break;
  }
  if (menu_sel >= item_count) menu_sel = 0;
}

String menuTitle() {
  switch (menu_id) {
    case MenuId::Main: return "Menu'";
    case MenuId::Regions: return "Fermate per regione";
    case MenuId::Stops: return "Fermate - " + menu_region;
    case MenuId::Reset: return "Reset";
  }
  return "";
}

void openMenu(MenuId id, int sel) {
  menu_id = id;
  menu_sel = sel;
  buildMenu();
  setMode(Mode::Menu);
}

void openPage(PageId id, Mode back) {
  page_id = id;
  page_back = back;
  setMode(Mode::Page);
}

void menuBack() {
  switch (menu_id) {
    case MenuId::Main: setMode(Mode::Board); break;
    case MenuId::Regions: openMenu(MenuId::Main, 1); break;
    case MenuId::Stops: openMenu(MenuId::Regions, 0); break;
    case MenuId::Reset: openMenu(MenuId::Main, 10); break;
  }
}

void menuSelect() {
  switch (menu_id) {
    case MenuId::Main:
      switch (menu_sel) {
        case 0: setMode(Mode::Board); return;
        case 1: {
          // Start on the region of the current stop.
          int sel = 0;
          if (const Favorite* f = settings.current()) {
            String region = regionOf(*f);
            for (int i = 0, r = 0; i < kFeedCount; ++i) {
              if (i && strcmp(kFeeds[i].region, kFeeds[i - 1].region) == 0) continue;
              if (region == kFeeds[i].region) sel = r;
              r++;
            }
          }
          openMenu(MenuId::Regions, sel);
          return;
        }
        case 2: setMode(Mode::Clock); return;
        case 3:
          settings.theme = (settings.theme + 1) % kThemeCount;
          applyTheme();
          settings.save();
          break;
        case 4:
          settings.brightness = settings.brightness % 5 + 1;
          setBacklight(settings.brightness);
          settings.save();
          break;
        case 5:
          settings.flip = !settings.flip;
          applyTheme();
          settings.save();
          break;
        case 6: openPage(PageId::Configure, Mode::Menu); return;
        case 7: openPage(PageId::Info, Mode::Menu); return;
        case 8:
          demo = !demo;
          if (demo) {
            stopPortal();
            setMode(Mode::Board);
            return;
          }
          has_data = false;
          seen_generation = UINT32_MAX;  // show the real data again
          if (WiFi.status() != WL_CONNECTED && !online) {
            startPortal();
            setMode(Mode::WifiSetup);
            return;
          }
          break;
        case 9: setMode(Mode::Welcome); return;
        case 10: openMenu(MenuId::Reset, 0); return;
      }
      buildMenu();
      return;

    case MenuId::Regions: {
      int region_sel = menu_sel;
      int r = 0;
      for (int i = 0; i < kFeedCount; ++i) {
        if (i && strcmp(kFeeds[i].region, kFeeds[i - 1].region) == 0) continue;
        if (r++ == menu_sel) menu_region = kFeeds[i].region;
      }
      menu_id = MenuId::Stops;
      menu_sel = 0;
      buildMenu();
      if (item_count == 0) {
        menu_id = MenuId::Regions;
        menu_sel = region_sel;
        openPage(PageId::RegionEmpty, Mode::Menu);
        buildMenu();
        return;
      }
      for (int i = 0; i < item_count; ++i)
        if (stop_index[i] == settings.active) menu_sel = i;
      ui.resetMenuAnimation();
      return;
    }

    case MenuId::Stops:
      settings.active = stop_index[menu_sel];
      settings.save();
      demo = false;
      requestDepartures();
      toast(settings.favorites[settings.active].name);
      setMode(Mode::Board);
      return;

    case MenuId::Reset:
      if (menu_sel == 0) {
        menuBack();
        return;
      }
      if (menu_sel == 2) Settings::factoryReset();
      wm.resetSettings();
      delay(300);
      ESP.restart();
      return;
  }
}

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------
int boardStatus(const Fetcher::Status& st) {
  if (demo) return 0;
  if (WiFi.status() != WL_CONNECTED) return 2;
  if (st.error.length()) return 2;
  if (st.has_data && millis() - st.last_ok_ms < (REFRESH_SECONDS * 2 + 30) * 1000UL) return 0;
  return 1;
}

void drawBoard(uint32_t ms) {
  time_t now = nowEpoch();
  Ui::BoardInfo info;
  info.demo = demo;
  info.busy = false;
  info.time_ok = demo || timeValid();

  if (demo) {
    demoDepartures(now);
    info.stop_name = "Roma Termini";
    info.status = 0;
    info.ticker = "Modalita' demo  -  ";
    if (settings.gift_name.length()) info.ticker += "Ciao " + settings.gift_name + "!  -  ";
    info.ticker += "Aggiungi le tue fermate da http://" HOSTNAME ".local";
    ui.boardFrame(ms, now, result, true, info);
    return;
  }

  const Favorite* f = settings.current();
  if (!f) {
    String lines[] = {"*http://" HOSTNAME ".local", "oppure http://" + WiFi.localIP().toString(),
                      "Aprilo dal telefono (stessa rete WiFi)", "e aggiungi la tua fermata."};
    ui.pageFrame(ms, "Aggiungi una fermata", lines, 4, "BOOT = menu'");
    return;
  }

  fetcher.snapshot(result, has_data, seen_generation);
  Fetcher::Status st = fetcher.status();
  info.busy = st.busy;
  info.status = boardStatus(st);
  info.stop_name = f->name;
  String t;
  if (WiFi.status() != WL_CONNECTED) t = "WiFi non connesso  -  ";
  else if (st.error.length()) t = "Errore: " + st.error + "  -  ";
  if (st.has_data) t += "Aggiornato " + ageText(st.last_ok_ms) + "  -  ";
  t += feedName(*f) + " (" + regionOf(*f) + ")  -  fermata " + f->stops;
  if (settings.gift_message.length()) t += "  -  " + settings.gift_message;
  info.ticker = t;
  ui.boardFrame(ms, now, result, has_data, info);
}

void drawPage(uint32_t ms) {
  String lines[5];
  int n = 0;
  String title;
  switch (page_id) {
    case PageId::Info: {
      title = "Informazioni";
      Fetcher::Status st = fetcher.status();
      if (WiFi.status() == WL_CONNECTED) {
        lines[n++] = "*" + WiFi.SSID();
        lines[n++] = "IP " + WiFi.localIP().toString() + "   segnale " + String(WiFi.RSSI()) + " dBm";
      } else {
        lines[n++] = "*WiFi non connesso";
      }
      if (st.has_data)
        lines[n++] = "Dati: " + ageText(st.last_ok_ms) + ", " + String((uint32_t)(st.last_bytes / 1024)) + " KB in " +
                     String(st.last_duration_ms / 100 / 10.0f, 1) + " s";
      else if (st.error.length())
        lines[n++] = "Errore: " + st.error;
      uint32_t mv = analogReadMilliVolts(PIN_BAT_ADC) * 2;
      lines[n++] = mv > 4400 || mv < 2500 ? String("Alimentazione USB") : String("Batteria " + String(mv / 1000.0f, 2) + " V");
      uint32_t up = millis() / 60000;
      lines[n++] = "Versione " FERMATA_VERSION "   acceso da " + String(up / 60) + " h " + String(up % 60) + " min";
      break;
    }
    case PageId::Configure:
      title = "Configura dal telefono";
      lines[n++] = "*http://" HOSTNAME ".local";
      lines[n++] = WiFi.status() == WL_CONNECTED ? String("oppure http://" + WiFi.localIP().toString())
                                                 : String("(prima collega il WiFi)");
      lines[n++] = "Dal telefono, sulla stessa rete WiFi:";
      lines[n++] = "fermate, saluto, tema e luminosita'.";
      break;
    case PageId::RegionEmpty:
      title = "Fermate - " + menu_region;
      lines[n++] = "*Nessuna fermata qui";
      lines[n++] = "Aggiungine una da http://" HOSTNAME ".local";
      lines[n++] = "scegliendo un operatore della regione";
      lines[n++] = menu_region + ".";
      break;
  }
  ui.pageFrame(ms, title, lines, n, "premi un tasto per tornare");
}

String nextDepartureLine() {
  if (!has_data && !demo) return "";
  time_t now = nowEpoch();
  for (int i = 0; i < result.count; ++i) {
    const auto& d = result.deps[i];
    if (d.time < now - 30) continue;
    long m = (long)(d.time - now) / 60;
    return "Prossima: linea " + String(d.route) + (m <= 0 ? String(" in arrivo") : String(" tra " + String(m) + " min"));
  }
  return "";
}

// ---------------------------------------------------------------------------
// Setup and loop
// ---------------------------------------------------------------------------
void setup() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);
  Serial.begin(115200);

  settings.load();
  tft.init();
  applyTheme();
  tft.fillScreen(TFT_BLACK);
  setBacklight(settings.brightness);
  ui.begin(&kThemes[settings.theme]);
  btn_ok.begin();
  btn_next.begin();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  has_creds = wm.getWiFiIsSaved();
  if (has_creds) WiFi.begin();
  setMode(Mode::Boot);
}

void loop() {
  static uint32_t last_frame = 0;
  Button::Event ok = btn_ok.poll();
  Button::Event next = btn_next.poll();
  bool any_short = ok == Button::kShort || next == Button::kShort;
  bool any_long = ok == Button::kLong || next == Button::kLong;

  if (online) web.loop();
  if (wm.getConfigPortalActive()) wm.process();

  uint32_t ms = millis();
  uint32_t t = ms - mode_since;

  switch (mode) {
    case Mode::Boot: {
      bool connected = WiFi.status() == WL_CONNECTED;
      t += boot_offset;
      bool waiting = has_creds && !connected && ms < 20000;
      bool intro_done = t > 4600;
      if (intro_done && (!settings.welcomed && boot_offset == 0)) {
        boot_offset = 3600;
        setMode(Mode::Welcome);
        break;
      }
      if (intro_done && !waiting) {
        boot_offset = 0;
        afterIntro();
        break;
      }
      if (any_short && intro_done) {  // skip the WiFi wait
        demo = true;
        afterIntro();
      }
      break;
    }
    case Mode::Welcome:
      if ((any_short || any_long) && t > 1500) {
        settings.welcomed = true;
        settings.save();
        afterIntro();
      }
      break;
    case Mode::WifiSetup:
      if (WiFi.status() == WL_CONNECTED) {
        stopPortal();
        has_creds = true;
        goOnline();
        toast("Connesso a " + WiFi.SSID());
        setMode(Mode::Board);
      } else if (ok == Button::kShort) {
        stopPortal();
        demo = true;
        setMode(Mode::Board);
      }
      break;
    case Mode::Board:
      if (ok == Button::kShort) {
        openMenu(MenuId::Main, 0);
      } else if (ok == Button::kLong) {
        setMode(Mode::Clock);
      } else if (next == Button::kShort && settings.favorite_count > 1 && !demo) {
        settings.active = (settings.active + 1) % settings.favorite_count;
        settings.save();
        requestDepartures();
        toast(settings.favorites[settings.active].name);
      } else if (next == Button::kLong && !demo) {
        fetcher.refreshNow();
        toast("Aggiorno...");
      }
      break;
    case Mode::Menu:
      if (next == Button::kShort) {
        menu_sel = (menu_sel + 1) % item_count;
      } else if (ok == Button::kShort) {
        menuSelect();
      } else if (any_long) {
        menuBack();
      }
      break;
    case Mode::Clock:
    case Mode::Page:
      if (any_short || any_long) {
        if (mode == Mode::Page && page_back == Mode::Menu) setMode(Mode::Menu);
        else setMode(Mode::Board);
      }
      break;
  }

  if (ms - last_frame < 33) return;  // ~30 fps
  last_frame = ms;
  t = ms - mode_since;

  switch (mode) {
    case Mode::Boot: {
      bool connected = WiFi.status() == WL_CONNECTED;
      String greeting = settings.gift_name.length() ? "Ciao " + ascii(settings.gift_name) + "!"
                                                    : String("Il tuo tabellone delle partenze");
      String status;
      float progress = -1;
      if (connected) {
        status = "Connesso a " + WiFi.SSID();
        progress = 1;
      } else if (has_creds) {
        status = "Mi collego al WiFi...";
      } else {
        status = "Benvenuto!";
        progress = clamp01((t + boot_offset) / 4600.0f);
      }
      ui.bootFrame(t + boot_offset, greeting, ascii(status), progress);
      break;
    }
    case Mode::Welcome: ui.welcomeFrame(t, settings.gift_name, settings.gift_message); break;
    case Mode::WifiSetup: ui.wifiSetupFrame(t); break;
    case Mode::Board: drawBoard(ms); break;
    case Mode::Menu: ui.menuFrame(ms, menuTitle(), items, item_count, menu_sel); break;
    case Mode::Clock: {
      if (demo) demoDepartures(nowEpoch());
      else fetcher.snapshot(result, has_data, seen_generation);
      ui.clockFrame(ms, nowEpoch(), timeValid() || demo, nextDepartureLine());
      break;
    }
    case Mode::Page: drawPage(ms); break;
  }
  if (ms < toast_until) ui.toast(toast_text);
  ui.push();
}
