// Persistent settings, kept in the ESP32 NVS (survive reflashing the sketch).
#pragma once

#include <Preferences.h>
#include "config.h"

struct Favorite {
  String name;
  String feed;   // key of kFeeds
  String stops;  // "70017,70018"
};

struct Settings {
  String gift_name = GIFT_NAME;
  String gift_message = GIFT_MESSAGE;
  String proxy_url = DEFAULT_PROXY_URL;
  String custom_url;
  String feed_user[kFeedCount];  // HTTP Basic credentials, by kFeeds index
  String feed_pass[kFeedCount];
  Favorite favorites[MAX_FAVORITES];
  int favorite_count = 0;
  int active = 0;
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
    for (int i = 0; i < kFeedCount; ++i) {
      feed_user[i] = p.getString(("u" + String(i)).c_str(), "");
      feed_pass[i] = p.getString(("p" + String(i)).c_str(), "");
    }
    theme = constrain(p.getInt("theme", 0), 0, kThemeCount - 1);
    brightness = constrain(p.getInt("bright", 3), 1, 5);
    flip = p.getBool("flip", false);
    welcomed = p.getBool("welcomed", false);
    active = p.getInt("active", 0);
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
    for (int i = 0; i < kFeedCount; ++i) {
      p.putString(("u" + String(i)).c_str(), feed_user[i]);
      p.putString(("p" + String(i)).c_str(), feed_pass[i]);
    }
    p.putInt("theme", theme);
    p.putInt("bright", brightness);
    p.putBool("flip", flip);
    p.putBool("welcomed", welcomed);
    p.putInt("active", active);
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

  // "Nome|feed|stops;Nome|feed|stops" - '|' and ';' are stripped from input.
  String favoritesString() const {
    String out;
    for (int i = 0; i < favorite_count; ++i) {
      if (i) out += ';';
      out += favorites[i].name + '|' + favorites[i].feed + '|' + favorites[i].stops;
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
        f.stops = item.substring(b + 1);
        if (f.stops.length() && findFeed(f.feed.c_str())) favorite_count++;
      }
      start = end + 1;
    }
  }

  static String clean(String s) {
    s.replace("|", "/");
    s.replace(";", ",");
    s.trim();
    return s;
  }
};
