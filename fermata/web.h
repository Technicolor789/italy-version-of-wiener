// Settings page served by the display itself: http://fermata.local
#pragma once

#include <WebServer.h>
#include "config.h"
#include "settings.h"

class WebUi {
 public:
  typedef std::function<void()> Callback;
  typedef std::function<String()> StatusHtml;

  WebUi(Settings& s) : server_(80), s_(s) {}

  void begin(Callback on_save, StatusHtml status) {
    on_save_ = on_save;
    status_ = status;
    server_.on("/", HTTP_GET, [this] { server_.send(200, "text/html; charset=utf-8", page()); });
    server_.on("/save", HTTP_POST, [this] { save(); });
    server_.onNotFound([this] {
      server_.sendHeader("Location", "/");
      server_.send(302, "text/plain", "");
    });
    server_.begin();
  }

  void loop() { server_.handleClient(); }

 private:
  static String esc(const String& in) {
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i) {
      char c = in[i];
      if (c == '&') out += "&amp;";
      else if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else if (c == '"') out += "&quot;";
      else out += c;
    }
    return out;
  }

  String feedOptions(const String& selected) {
    String out;
    const char* region = "";
    for (int i = 0; i < kFeedCount; ++i) {
      if (strcmp(region, kFeeds[i].region) != 0) {
        if (i) out += "</optgroup>";
        region = kFeeds[i].region;
        out += "<optgroup label=\"" + String(region) + "\">";
      }
      out += "<option value=\"" + String(kFeeds[i].key) + "\"";
      if (selected == kFeeds[i].key) out += " selected";
      out += ">" + String(kFeeds[i].name) + "</option>";
    }
    return out + "</optgroup>";
  }

  String page() {
    String h;
    h.reserve(9000);
    h += F("<!doctype html><html lang=it><head><meta charset=utf-8>"
           "<meta name=viewport content='width=device-width,initial-scale=1'>"
           "<title>Fermata</title><style>"
           ":root{--bg:#04163f;--card:#0c2864;--text:#fff;--dim:#a9b8d8;--acc:#ffcc00}"
           "*{box-sizing:border-box}body{margin:0;font:16px system-ui,sans-serif;background:var(--bg);color:var(--text)}"
           "header{padding:20px 16px 8px;max-width:720px;margin:auto}h1{margin:0;font-size:28px}"
           "h1 span{color:var(--acc)}main{max-width:720px;margin:auto;padding:0 16px 40px}"
           "section{background:var(--card);border-radius:14px;padding:16px;margin:14px 0}"
           "h2{margin:0 0 10px;font-size:18px;color:var(--acc)}label{display:block;font-size:13px;color:var(--dim);margin:10px 0 4px}"
           "input,select{width:100%;padding:10px;border-radius:8px;border:1px solid #2a4a8a;background:#061c4c;color:#fff;font-size:16px}"
           ".fav{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;padding:10px 0;border-top:1px solid #1d3d7d}"
           ".fav:first-of-type{border-top:0}.fav label{margin-top:0}"
           "@media(max-width:560px){.fav{grid-template-columns:1fr}}"
           "button{margin-top:16px;width:100%;padding:14px;border:0;border-radius:10px;background:var(--acc);color:#0a1432;font-size:18px;font-weight:700}"
           "small,.hint{color:var(--dim);font-size:13px}table{width:100%;border-collapse:collapse}"
           "td{padding:6px 4px;border-top:1px solid #1d3d7d}.line{display:inline-block;min-width:44px;text-align:center;"
           "background:var(--acc);color:#0a1432;border-radius:6px;font-weight:700;padding:2px 6px}"
           "details{margin-top:8px}summary{cursor:pointer;color:var(--acc)}"
           "</style></head><body><header><h1>&#128655; <span>Fermata</span></h1>"
           "<small>Il tabellone delle partenze sulla tua scrivania</small></header><main>");

    if (status_) h += "<section><h2>Ora sul display</h2>" + status_() + "</section>";

    h += F("<form method=post action=/save><section><h2>Fermate preferite</h2>"
           "<p class=hint>Scegli l'operatore e scrivi l'ID della fermata (stop_id del GTFS). "
           "Puoi mettere pi&ugrave; ID separati da virgola, es. le due direzioni della stessa fermata. "
           "Sul display passi da una fermata all'altra con il tasto KEY.</p>");
    for (int i = 0; i < MAX_FAVORITES; ++i) {
      const Favorite* f = i < s_.favorite_count ? &s_.favorites[i] : nullptr;
      String n(i);
      h += "<div class=fav><div><label>Nome</label><input name=n" + n + " maxlength=40 placeholder='es. Sotto casa' value=\"" +
           esc(f ? f->name : "") + "\"></div><div><label>Operatore</label><select name=f" + n + ">" +
           feedOptions(f ? f->feed : String(kFeeds[0].key)) + "</select></div><div><label>ID fermata</label><input name=s" + n +
           " maxlength=120 placeholder='es. 70017' value=\"" + esc(f ? f->stops : "") + "\"></div></div>";
    }
    h += F("<details><summary>Come trovo l'ID della fermata?</summary><p class=hint>"
           "&bull; Cerca la fermata su <b>transit.land</b> (scegli l'operatore, poi la fermata): "
           "il campo <i>stop_id</i> &egrave; quello che serve.<br>"
           "&bull; Oppure scarica il GTFS statico dell'operatore e apri <i>stops.txt</i>.<br>"
           "&bull; ATAC Roma: di solito &egrave; il numero della palina scritto sulla fermata.</p></details>"
           "</section><section><h2>Saluto</h2>");
    h += "<label>Nome di chi lo riceve</label><input name=gift_name maxlength=24 value=\"" + esc(s_.gift_name) + "\">";
    h += "<label>Messaggio</label><input name=gift_msg maxlength=90 value=\"" + esc(s_.gift_message) + "\">";
    h += "<label><input type=checkbox name=replay style='width:auto'> Mostra di nuovo la sorpresa al prossimo avvio</label>";

    h += F("</section><section><h2>Aspetto</h2><label>Tema</label><select name=theme>");
    for (int i = 0; i < kThemeCount; ++i)
      h += "<option value=" + String(i) + (i == s_.theme ? " selected>" : ">") + kThemes[i].name + "</option>";
    h += F("</select><label>Luminosit&agrave;</label><select name=bright>");
    for (int i = 1; i <= 5; ++i)
      h += "<option" + String(i == s_.brightness ? " selected" : "") + ">" + String(i) + "</option>";
    h += F("</select></section><section><h2>Avanzate</h2>"
           "<label>URL del proxy (opzionale, consigliato per la Toscana)</label>");
    h += "<input name=proxy placeholder='https://mio-server.it/proxy.php' value=\"" + esc(s_.proxy_url) + "\">";
    h += F("<small>Con <i>proxy.php</i> di questo progetto il display scarica solo le corse delle sue fermate.</small>"
           "<label>URL feed personalizzato (GTFS-RT TripUpdates)</label>");
    h += "<input name=custom_url placeholder='https://...' value=\"" + esc(s_.custom_url) + "\">";
    h += F("</section><section><h2>Accesso ai feed</h2><p class=hint>Alcuni operatori (es. Busitalia Padova) "
           "danno il feed solo con utente e password: chiedili all'operatore e scrivili qui. "
           "Lascia vuota la password per non cambiarla; svuota l'utente per toglierle.</p>");
    for (int i = 0; i < kFeedCount; ++i) {
      if (!kFeeds[i].needs_login && !s_.feed_user[i].length() && strcmp(kFeeds[i].key, "custom") != 0) continue;
      String n(i);
      h += "<div class=fav><div><label>" + String(kFeeds[i].name) + "</label><input value=\"" + String(kFeeds[i].region) +
           "\" disabled></div><div><label>Utente</label><input name=u" + n + " autocomplete=off value=\"" +
           esc(s_.feed_user[i]) + "\"></div><div><label>Password</label><input name=p" + n +
           " type=password autocomplete=new-password placeholder='" + (s_.feed_pass[i].length() ? "(salvata)" : "") +
           "'></div></div>";
    }
    h += F("</section><button>Salva</button></form>"
           "<p class=hint style='text-align:center'>Fermata " FERMATA_VERSION "</p></main></body></html>");
    return h;
  }

  void save() {
    int n = 0;
    for (int i = 0; i < MAX_FAVORITES; ++i) {
      String idx(i);
      String stops = Settings::clean(server_.arg("s" + idx));
      stops.replace(" ", "");
      String feed = server_.arg("f" + idx);
      if (!stops.length() || !findFeed(feed.c_str())) continue;
      Favorite& f = s_.favorites[n++];
      f.stops = stops;
      f.feed = feed;
      f.name = Settings::clean(server_.arg("n" + idx));
      if (!f.name.length()) f.name = "Fermata " + stops;
    }
    s_.favorite_count = n;
    if (s_.active >= n) s_.active = 0;
    s_.gift_name = Settings::clean(server_.arg("gift_name"));
    s_.gift_message = Settings::clean(server_.arg("gift_msg"));
    if (server_.hasArg("replay")) s_.welcomed = false;
    s_.theme = constrain(server_.arg("theme").toInt(), 0, kThemeCount - 1);
    s_.brightness = constrain(server_.arg("bright").toInt(), 1, 5);
    s_.proxy_url = Settings::clean(server_.arg("proxy"));
    s_.custom_url = Settings::clean(server_.arg("custom_url"));
    for (int i = 0; i < kFeedCount; ++i) {
      String n(i);
      if (!server_.hasArg("u" + n)) continue;
      s_.feed_user[i] = server_.arg("u" + n);
      s_.feed_user[i].trim();
      String pass = server_.arg("p" + n);
      if (!s_.feed_user[i].length()) s_.feed_pass[i] = "";
      else if (pass.length()) s_.feed_pass[i] = pass;
    }
    s_.save();
    if (on_save_) on_save_();

    server_.send(200, "text/html; charset=utf-8",
                 F("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
                   "<meta http-equiv=refresh content='2;url=/'>"
                   "<body style='background:#04163f;color:#fff;font:20px system-ui;text-align:center;padding-top:30vh'>"
                   "&#9989; Salvato! Guarda il display.</body>"));
  }

  WebServer server_;
  Settings& s_;
  Callback on_save_;
  StatusHtml status_;
};
