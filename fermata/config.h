// Everything you may want to personalise before flashing the gift.
#pragma once

#define FERMATA_VERSION "1.1"

// ---------------------------------------------------------------------------
// Regalo: shown on the very first boot (with confetti) and as a greeting on
// every boot afterwards. Both can also be changed later from the web page.
// ---------------------------------------------------------------------------
#define GIFT_NAME "Luciano"
#define GIFT_MESSAGE "Buon viaggio, ogni giorno!"

// Optional: stops already configured when he turns it on for the first time.
// Format: "Nome|feed|stop_id,stop_id;Nome 2|feed|stop_id"
// Feed keys are listed in kFeeds below, es. "Termini|atac_roma|70017".
#define DEFAULT_FAVORITES \
  "Piazza d'Azeglio|toscana|143931,144181,143602;" \
  "Casa|toscana|145028,145462"

// Autolinee Toscane route_ids are not the numbers on the bus: route_id=line.
// Viareggio (also added by the proxy for all of Tuscany when it has the GTFS).
#define DEFAULT_LINE_NAMES \
  "547=21, 548=25, 549=27, 550=31, 561=E1, 569=E2, 571=E25, 572=E26, 573=E27, 574=E29"

// Optional: URL of proxy.php from this repository (es.
// "https://mio-server.it/proxy.php"). With it the display downloads only the
// trips of its stops instead of the whole regional feed.
#define DEFAULT_PROXY_URL "https://italy-version-of-wiener.onrender.com/proxy.php"

// ---------------------------------------------------------------------------
// Hardware: LilyGO T-Display S3
// ---------------------------------------------------------------------------
#define PIN_POWER_ON 15   // must be HIGH to power the LCD on battery
#define PIN_BACKLIGHT 38
#define PIN_BTN_OK 0      // "BOOT" button
#define PIN_BTN_NEXT 14   // "KEY" button
#define PIN_BAT_ADC 4

#define HOSTNAME "fermata"          // http://fermata.local
#define SETUP_AP_NAME "Fermata-Setup"
#define TZ_ITALY "CET-1CEST,M3.5.0,M10.5.0/3"

#define REFRESH_SECONDS 30
#define MAX_FAVORITES 8

// ---------------------------------------------------------------------------
// Regioni e operatori, in ordine di regione. Keys match proxy.php.
// ---------------------------------------------------------------------------
struct FeedInfo {
  const char* key;
  const char* region;
  const char* name;
  const char* url;
  bool needs_login;  // HTTP Basic: credentials are asked to the operator
  bool scheduled;    // timetable only (no live delays), served by proxy.php
};

static const FeedInfo kFeeds[] = {
  {"atac_roma", "Lazio", "ATAC Roma",
   "https://romamobilita.it/sites/default/files/rome_rtgtfs_trip_updates_feed.pb", false, false},
  // Lombardia: no open live feed; timetables built by proxy.php from the
  // static GTFS published by Regione Lombardia (needs the proxy URL).
  {"trenord", "Lombardia", "Trenord (orari)", "", false, true},
  {"atm", "Lombardia", "ATM Milano (orari)", "", false, true},
  {"gest", "Toscana", "Tramvia Firenze (orari)", "", false, true},
  {"toscana", "Toscana", "Autolinee Toscane",
   "https://regionetoscana.smartregion.toscana.it/mobility/artifacts/gtfs-rt/trip-updates", false, false},
  {"actv_veneto", "Veneto", "ACTV Venezia",
   "https://tpl.actv.it/aut/GTFSRT/tripUpdates", false, false},
  {"busitalia_veneto", "Veneto", "Busitalia Padova",
   "https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV/start-gtfs-rt-trip-updates-fc.pb", true, false},
  {"busitalia_tram", "Veneto", "Tram Padova",
   "https://gtfs-biv.fsbusitalia.com/GTFSRT-BIV-TRAM/gtfs-rt-trip-updates.pb", false, false},
  // "custom" uses the URL typed on the web page.
  {"custom", "Altro", "Feed personalizzato", "", false, false},
};
static const int kFeedCount = sizeof(kFeeds) / sizeof(kFeeds[0]);

inline int feedIndex(const char* key) {
  for (int i = 0; i < kFeedCount; ++i)
    if (strcmp(kFeeds[i].key, key) == 0) return i;
  return -1;
}

inline const FeedInfo* findFeed(const char* key) {
  int i = feedIndex(key);
  return i < 0 ? nullptr : &kFeeds[i];
}

// ---------------------------------------------------------------------------
// Temi
// ---------------------------------------------------------------------------
struct Theme {
  const char* name;
  uint16_t bg, panel, text, dim, accent, accent_text, ok, late, early;
};

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

static const Theme kThemes[] = {
  // Blu stazione, like the boards in Italian railway stations.
  {"Stazione", RGB565(4, 22, 64), RGB565(12, 40, 100), RGB565(255, 255, 255),
   RGB565(140, 160, 200), RGB565(255, 204, 0), RGB565(10, 20, 50),
   RGB565(80, 220, 120), RGB565(255, 90, 80), RGB565(120, 200, 255)},
  // Ambra su nero, LED display of the bus stops.
  {"Pensilina", RGB565(0, 0, 0), RGB565(30, 22, 0), RGB565(255, 190, 0),
   RGB565(150, 110, 0), RGB565(255, 190, 0), RGB565(0, 0, 0),
   RGB565(255, 220, 90), RGB565(255, 90, 40), RGB565(255, 220, 90)},
  {"Tricolore", RGB565(16, 18, 20), RGB565(34, 38, 42), RGB565(245, 245, 245),
   RGB565(150, 155, 160), RGB565(0, 146, 70), RGB565(255, 255, 255),
   RGB565(0, 200, 90), RGB565(206, 43, 55), RGB565(120, 200, 255)},
  {"Notte", RGB565(0, 0, 0), RGB565(18, 18, 30), RGB565(170, 175, 200),
   RGB565(90, 90, 110), RGB565(90, 100, 170), RGB565(220, 220, 240),
   RGB565(90, 170, 120), RGB565(190, 80, 80), RGB565(110, 150, 200)},
};
static const int kThemeCount = sizeof(kThemes) / sizeof(kThemes[0]);

// Line badges get a stable colour from their name, like real line colours.
static const uint16_t kLineColors[] = {
  RGB565(230, 50, 50),  RGB565(0, 140, 70),   RGB565(0, 110, 200),
  RGB565(240, 130, 0),  RGB565(140, 60, 170), RGB565(0, 150, 160),
  RGB565(200, 40, 120), RGB565(90, 90, 200),
};
