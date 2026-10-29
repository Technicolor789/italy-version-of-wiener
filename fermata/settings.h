// Persistent settings, kept in the ESP32 NVS (survive reflashing the sketch).
#pragma once

#include <Preferences.h>
#include "config.h"

struct Favorite {
  String name;
  String feed;   // key of kFeeds
  String stops;  // "70017,70018"
  String lines;  // optional: only these lines, "31,E1" (names or route_ids)
};

struct Settings {
  String gift_name = GIFT_NAME;
  String gift_message = GIFT_MESSAGE;
  String proxy_url = DEFAULT_PROXY_URL;
  String custom_url;
  String line_names;  // route_id -> line shown on the badge: "550=31, 478=30"
  String fleet;       // fleet number -> model, one per line: "K4153=Iveco Crossway 10,8 m"
  String feed_user[kFeedCount];  // HTTP Basic credentials, by kFeeds index
  String feed_pass[kFeedCount];
  Favorite favorites[MAX_FAVORITES];
  int favorite_count = 0;
  int active = 0;
  // Evening: from night_from to night_to (minutes after midnight) the display
  // shows favourite night_fav, then goes back to the daytime one.
  int night_fav = -1;
  int night_from = 21 * 60 + 30;
  int night_to = 6 * 60;
  bool night_override = false;  // not saved: currently showing night_fav
  int day_active = 0;
  int theme = 0;
  int brightness = 3;  // 1..5
  bool flip = false;
  bool welcomed = false;

  void load() {
    Preferences p;
    p.begin("fermata", true);
    bool fresh = !p.isKey("theme");
    gift_name = p.getString("gift_name", GIFT_NAME);
    gift_message = p.getString("gift_msg", GIFT_MESSAGE);
    proxy_url = p.getString("proxy", DEFAULT_PROXY_URL);
    custom_url = p.getString("custom_url", "");
    line_names = p.getString("line_names", DEFAULT_LINE_NAMES);
    fleet = p.getString("fleet", "");
    for (int i = 0; i < kFeedCount; ++i) {
      feed_user[i] = p.getString(("u" + String(i)).c_str(), "");
      feed_pass[i] = p.getString(("p" + String(i)).c_str(), "");
    }
    theme = constrain(p.getInt("theme", 0), 0, kThemeCount - 1);
    brightness = constrain(p.getInt("bright", 3), 1, 5);
    flip = p.getBool("flip", false);
    welcomed = p.getBool("welcomed", false);
    active = p.getInt("active", 0);
    night_fav = p.getInt("night_fav", -1);
    night_from = constrain(p.getInt("night_from", night_from), 0, 1439);
    night_to = constrain(p.getInt("night_to", night_to), 0, 1439);
    String favs = p.getString("favs", DEFAULT_FAVORITES);
    p.end();
    parseFavorites(favs);
    if (active >= favorite_count) active = 0;
    if (fresh) save();
  }

  void save() {
    Preferences p;
    p.begin("fermata", false);
    p.putString("gift_name", gift_name);
    p.putString("gift_msg", gift_message);
    p.putString("proxy", proxy_url);
    p.putString("custom_url", custom_url);
    p.putString("line_names", line_names);
    p.putString("fleet", fleet);
    for (int i = 0; i < kFeedCount; ++i) {
      p.putString(("u" + String(i)).c_str(), feed_user[i]);
      p.putString(("p" + String(i)).c_str(), feed_pass[i]);
    }
    p.putInt("theme", theme);
    p.putInt("bright", brightness);
    p.putBool("flip", flip);
    p.putBool("welcomed", welcomed);
    p.putInt("active", night_override ? day_active : active);
    p.putInt("night_fav", night_fav);
    p.putInt("night_from", night_from);
    p.putInt("night_to", night_to);
    p.putString("favs", favoritesString());
    p.end();
  }

  static void factoryReset() {
    Preferences p;
    p.begin("fermata", false);
    p.clear();
    p.end();
  }

  const Favorite* current() const {
    return favorite_count ? &favorites[active] : nullptr;
  }

  // "Nome|feed|stops|lines;Nome|feed|stops" - '|' and ';' are stripped from input.
  String favoritesString() const {
    String out;
    for (int i = 0; i < favorite_count; ++i) {
      if (i) out += ';';
      out += favorites[i].name + '|' + favorites[i].feed + '|' + favorites[i].stops;
      if (favorites[i].lines.length()) out += '|' + favorites[i].lines;
    }
    return out;
  }

  void parseFavorites(const String& s) {
    favorite_count = 0;
    int start = 0;
    while (start < (int)s.length() && favorite_count < MAX_FAVORITES) {
      int end = s.indexOf(';', start);
      if (end < 0) end = s.length();
      String item = s.substring(start, end);
      int a = item.indexOf('|'), b = item.indexOf('|', a + 1);
      if (a > 0 && b > a) {
        Favorite& f = favorites[favorite_count];
        f.name = item.substring(0, a);
        f.feed = item.substring(a + 1, b);
        int c = item.indexOf('|', b + 1);
        f.stops = c < 0 ? item.substring(b + 1) : item.substring(b + 1, c);
        f.lines = c < 0 ? String("") : item.substring(c + 1);
        if (f.stops.length() && findFeed(f.feed.c_str())) favorite_count++;
      }
      start = end + 1;
    }
  }

  // Calls fn(key, value) for each "key=value" of a table. Entries are split
  // by newlines, and also by commas unless the values may contain commas.
  template <typename Fn>
  static void forEachPair(const String& table, bool commas, Fn fn) {
    int start = 0, len = table.length();
    while (start < len) {
      int end = start;
      while (end < len && table[end] != '\n' && !(commas && table[end] == ',')) ++end;
      String item = table.substring(start, end);
      int eq = item.indexOf('=');
      if (eq > 0) {
        String k = item.substring(0, eq), v = item.substring(eq + 1);
        k.trim();
        v.trim();
        if (k.length() && v.length()) fn(k, v);
      }
      start = end + 1;
    }
  }

  static String lookup(const String& table, const String& key, bool commas) {
    String found;
    forEachPair(table, commas, [&](const String& k, const String& v) {
      if (!found.length() && k == key) found = v;
    });
    return found;
  }

  static String clean(String s) {
    s.replace("|", "/");
    s.replace(";", ",");
    s.trim();
    return s;
  }
};
