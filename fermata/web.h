// Settings page served by the display itself: http://fermata.local
#pragma once

#include <WebServer.h>
#include "config.h"
#include "settings.h"

static const char kWifiPage[] PROGMEM = R"HTML(<!doctype html><html lang=it><head><meta charset=utf-8>
<meta name=viewport content='width=device-width,initial-scale=1'><title>WiFi - Fermata</title><style>
:root{--bg:#04163f;--card:#0c2864;--text:#fff;--dim:#a9b8d8;--acc:#ffcc00}
*{box-sizing:border-box}body{margin:0;font:16px system-ui,sans-serif;background:var(--bg);color:var(--text)}
header{display:flex;align-items:center;gap:12px;padding:18px 16px 6px;max-width:560px;margin:auto}
header a{color:var(--acc);text-decoration:none;font-size:22px}h1{margin:0;font-size:24px}
main{max-width:560px;margin:auto;padding:0 16px 40px}.list{background:var(--card);border-radius:14px;overflow:hidden;margin-top:12px}
.net{padding:14px 16px;border-top:1px solid #1d3d7d;cursor:pointer}.net:first-child{border-top:0}
.row{display:flex;align-items:center;gap:12px}.name{flex:1;font-size:17px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.cur{color:var(--acc);font-size:13px}.lock{opacity:.8}.form{display:none;margin-top:12px}.open .form{display:block}
input{width:100%;padding:12px;border-radius:10px;border:1px solid #2a4a8a;background:#061c4c;color:#fff;font-size:17px}
label{display:flex;gap:8px;align-items:center;color:var(--dim);font-size:14px;margin:8px 0}
button{width:100%;margin-top:8px;padding:13px;border:0;border-radius:10px;background:var(--acc);color:#0a1432;font-size:17px;font-weight:700}
.hint{color:var(--dim);font-size:14px;text-align:center;margin:14px 0}.msg{background:var(--card);border-radius:14px;padding:18px;margin-top:12px;line-height:1.5}
svg{flex:none}</style></head><body>
<header><a href=/>&#8249;</a><h1>WiFi</h1></header><main>
<div id=list class=list><div class=net><div class=row><span class=name>Cerco le reti...</span></div></div></div>
<p class=hint><a href=# onclick='scan();return false' style='color:var(--acc)'>Aggiorna</a></p></main>
<script>
function bars(r){var n=r>-55?4:r>-65?3:r>-75?2:1,s='<svg width=22 height=18 viewBox="0 0 22 18">';
for(var i=0;i<4;i++){var h=4+i*4;s+='<rect x="'+(i*6)+'" y="'+(18-h)+'" width="4" height="'+h+'" rx="1" fill="'+(i<n?'#fff':'#ffffff40')+'"/>'}return s+'</svg>'}
function esc(t){var d=document.createElement('div');d.textContent=t;return d.innerHTML}
function scan(){document.getElementById('list').innerHTML='<div class=net><div class=row><span class=name>Cerco le reti...</span></div></div>';poll()}
function poll(){fetch('/api/scan').then(function(r){return r.json()}).then(function(j){if(j.scanning){setTimeout(poll,800);return}show(j)}).catch(function(){setTimeout(poll,1500)})}
function show(j){var h='';j.networks.sort(function(a,b){return (b.ssid==j.current)-(a.ssid==j.current)||b.rssi-a.rssi});
j.networks.forEach(function(n,i){var c=n.ssid==j.current;h+='<div class=net id=n'+i+' onclick="tap('+i+')"><div class=row><span class=name>'+esc(n.ssid)+(c?' <span class=cur>&#10003; connesso</span>':'')+'</span>'+(n.secure?'<span class=lock>&#128274;</span>':'')+bars(n.rssi)+'</div>'
+(c?'':'<div class=form onclick="event.stopPropagation()">'+(n.secure?'<input type=password id=p'+i+' placeholder=Password autocomplete=off><label><input type=checkbox style="width:auto" onchange="document.getElementById(\'p'+i+'\').type=this.checked?\'text\':\'password\'"> Mostra password</label>':'')
+'<button onclick="go('+i+')">Connetti</button></div>')+'</div>'});
window.nets=j.networks;document.getElementById('list').innerHTML=h||'<div class=net>Nessuna rete trovata</div>'}
function tap(i){var e=document.getElementById('n'+i),o=e.classList.contains('open');document.querySelectorAll('.open').forEach(function(x){x.classList.remove('open')});if(!o){e.classList.add('open');var p=document.getElementById('p'+i);if(p)p.focus()}}
function go(i){var n=window.nets[i],p=document.getElementById('p'+i),b=new URLSearchParams();b.append('ssid',n.ssid);b.append('pass',p?p.value:'');
fetch('/api/wifi',{method:'POST',body:b}).then(function(){document.querySelector('main').innerHTML='<div class=msg>&#128246; Il display si sta collegando a <b>'+esc(n.ssid)+'</b>.<br><br>Se la password &egrave; giusta, collega anche il telefono a <b>'+esc(n.ssid)+'</b> e riapri <b>fermata.local</b>.<br><br>Se &egrave; sbagliata, entro 30 secondi il display torna alla rete di prima: lo vedi scritto sullo schermo.</div>'})}
scan();
</script></body></html>)HTML";

class WebUi {
 public:
  typedef std::function<void()> Callback;
  typedef std::function<String()> StatusHtml;
  typedef std::function<void(const String&, const String&)> WifiConnect;

  WebUi(Settings& s) : server_(80), s_(s) {}

  void begin(Callback on_save, StatusHtml status, WifiConnect on_wifi) {
    on_save_ = on_save;
    status_ = status;
    on_wifi_ = on_wifi;
    server_.on("/", HTTP_GET, [this] { server_.send(200, "text/html; charset=utf-8", page()); });
    server_.on("/wifi", HTTP_GET, [this] { server_.send(200, "text/html; charset=utf-8", String(FPSTR(kWifiPage))); });
    server_.on("/api/scan", HTTP_GET, [this] { scan(); });
    server_.on("/api/wifi", HTTP_POST, [this] {
      String ssid = server_.arg("ssid");
      if (!ssid.length()) {
        server_.send(400, "application/json", "{\"ok\":false}");
        return;
      }
      server_.send(200, "application/json", "{\"ok\":true}");
      if (on_wifi_) on_wifi_(ssid, server_.arg("pass"));
    });
    server_.on("/save", HTTP_POST, [this] { save(); });
    server_.onNotFound([this] {
      server_.sendHeader("Location", "/");
      server_.send(302, "text/plain", "");
    });
    server_.begin();
  }

  void loop() { server_.handleClient(); }

  static String json(const String& in) {
    String out = "\"";
    for (size_t i = 0; i < in.length(); ++i) {
      char c = in[i];
      if (c == '"' || c == '\\') {
        out += '\\';
        out += c;
      } else if ((uint8_t)c >= 0x20) {
        out += c;
      }
    }
    return out + "\"";
  }

 private:
  // Asynchronous scan: the page polls until the results are ready.
  void scan() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true);
    if (n < 0) {
      server_.send(200, "application/json", "{\"scanning\":true}");
      return;
    }
    String out = "{\"current\":" + json(WiFi.SSID()) + ",\"networks\":[";
    bool first = true;
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;
      bool dup = false;  // the same network on several access points
      for (int k = 0; k < i && !dup; ++k) dup = WiFi.SSID(k) == ssid;
      if (dup) continue;
      if (!first) out += ',';
      first = false;
      out += "{\"ssid\":" + json(ssid) + ",\"rssi\":" + String(WiFi.RSSI(i)) +
             ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
    }
    WiFi.scanDelete();
    server_.send(200, "application/json", out + "]}");
  }

 public:

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

  static String hhmm(int minutes) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
  }

  static int parseHhmm(const String& s, int fallback) {
    int colon = s.indexOf(':');
    if (colon < 1) return fallback;
    int m = s.substring(0, colon).toInt() * 60 + s.substring(colon + 1).toInt();
    return m >= 0 && m < 1440 ? m : fallback;
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
           "input,select,textarea{width:100%;padding:10px;border-radius:8px;border:1px solid #2a4a8a;background:#061c4c;color:#fff;font-size:16px}"
           ".fav{display:grid;grid-template-columns:1.2fr 1.2fr 1fr 1fr;gap:8px;padding:10px 0;border-top:1px solid #1d3d7d}"
           ".fav:first-of-type{border-top:0}.fav label{margin-top:0}"
           "@media(max-width:560px){.fav{grid-template-columns:1fr}}"
           "button{margin-top:16px;width:100%;padding:14px;border:0;border-radius:10px;background:var(--acc);color:#0a1432;font-size:18px;font-weight:700}"
           "small,.hint{color:var(--dim);font-size:13px}table{width:100%;border-collapse:collapse}"
           "td{padding:6px 4px;border-top:1px solid #1d3d7d}.line{display:inline-block;min-width:44px;text-align:center;"
           "background:var(--acc);color:#0a1432;border-radius:6px;font-weight:700;padding:2px 6px}"
           "details{margin-top:8px}summary{cursor:pointer;color:var(--acc)}"
           "</style></head><body><header><h1>&#128655; <span>Fermata</span></h1>"
           "<small>Il tabellone delle partenze sulla tua scrivania</small>"
           "<p><a href=/wifi style='display:inline-block;padding:10px 16px;border-radius:10px;background:var(--card);"
           "color:var(--acc);text-decoration:none;font-weight:700'>&#128246; Rete WiFi</a></p></header><main>");

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
           " maxlength=120 placeholder='es. 70017' value=\"" + esc(f ? f->stops : "") +
           "\"></div><div><label>Solo linee (facoltativo)</label><input name=l" + n +
           " maxlength=120 placeholder='es. 31,E1,E24' value=\"" + esc(f ? f->lines : "") + "\"></div></div>";
    }
    h += F("<details><summary>Come trovo l'ID della fermata?</summary><p class=hint>"
           "&bull; Cerca la fermata su <b>transit.land</b> (scegli l'operatore, poi la fermata): "
           "il campo <i>stop_id</i> &egrave; quello che serve.<br>"
           "&bull; Oppure scarica il GTFS statico dell'operatore e apri <i>stops.txt</i>.<br>"
           "&bull; ATAC Roma: di solito &egrave; il numero della palina scritto sulla fermata.<br>"
           "&bull; Trenord: apri <i>URL-del-proxy</i><b>?feed=trenord&amp;search=Milano</b> e copia l'<i>id</i> della stazione.</p></details>"
           "</section><section><h2>Saluto</h2>");
    h += "<label>Nome di chi lo riceve</label><input name=gift_name maxlength=24 value=\"" + esc(s_.gift_name) + "\">";
    h += "<label>Messaggio</label><input name=gift_msg maxlength=90 value=\"" + esc(s_.gift_message) + "\">";
    h += "<label><input type=checkbox name=replay style='width:auto'> Mostra di nuovo la sorpresa al prossimo avvio</label>";

    h += F("</section><section><h2>La sera</h2><p class=hint>Quando i suoi bus finiscono, il display pu&ograve; "
           "passare da solo a un'altra fermata (anche a Firenze) e tornare a quella di sempre la mattina.</p>"
           "<label>Fermata serale</label><select name=night_fav><option value=-1>Nessuna</option>");
    for (int i = 0; i < s_.favorite_count; ++i)
      h += "<option value=" + String(i) + (i == s_.night_fav ? " selected>" : ">") + esc(s_.favorites[i].name) + "</option>";
    h += "</select><div class=fav><div><label>Dalle</label><input type=time name=night_from value=" + hhmm(s_.night_from) +
         "></div><div><label>Alle</label><input type=time name=night_to value=" + hhmm(s_.night_to) + "></div></div>";
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
    h += F("</section><section><h2>Linee e flotta</h2><p class=hint>Nel feed le linee hanno spesso un codice "
           "interno (es. la 31 &egrave; <i>550</i>): scrivi qui <b>codice=linea</b> per vedere il numero giusto sul display.</p>"
           "<label>Nomi delle linee</label>");
    h += "<textarea name=line_names rows=3 placeholder='550=31, 478=30'>" + esc(s_.line_names) + "</textarea>";
    h += F("<label>Flotta: matricola=modello (una per riga)</label>");
    h += "<textarea name=fleet rows=4 placeholder='K4153=Iveco Crossway 10,8 m'>" + esc(s_.fleet) + "</textarea>";
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
      f.lines = Settings::clean(server_.arg("l" + idx));
      if (!f.name.length()) f.name = "Fermata " + stops;
    }
    s_.favorite_count = n;
    if (s_.active >= n) s_.active = 0;
    if (s_.day_active >= n) s_.day_active = 0;
    s_.night_fav = server_.hasArg("night_fav") ? (int)server_.arg("night_fav").toInt() : -1;
    if (s_.night_fav >= n) s_.night_fav = -1;
    s_.night_from = parseHhmm(server_.arg("night_from"), s_.night_from);
    s_.night_to = parseHhmm(server_.arg("night_to"), s_.night_to);
    s_.gift_name = Settings::clean(server_.arg("gift_name"));
    s_.gift_message = Settings::clean(server_.arg("gift_msg"));
    if (server_.hasArg("replay")) s_.welcomed = false;
    s_.theme = constrain(server_.arg("theme").toInt(), 0, kThemeCount - 1);
    s_.brightness = constrain(server_.arg("bright").toInt(), 1, 5);
    s_.proxy_url = Settings::clean(server_.arg("proxy"));
    s_.custom_url = Settings::clean(server_.arg("custom_url"));
    s_.line_names = server_.arg("line_names").substring(0, 1500);
    s_.line_names.trim();
    s_.fleet = server_.arg("fleet").substring(0, 3000);
    s_.fleet.replace("\r", "");
    s_.fleet.trim();
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
  WifiConnect on_wifi_;
};
