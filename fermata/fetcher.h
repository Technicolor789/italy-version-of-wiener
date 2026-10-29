// Background download of the departures, on its own FreeRTOS task so the
// animations keep running while a regional feed streams in.
#pragma once

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "gtfsrt.h"

// "Thu, 29 Oct 2026 13:05:00 GMT" (HTTP Date header) -> unix time, 0 if invalid.
inline time_t parseHttpDate(const String& s) {
  char mon[4] = {0};
  int d, y, hh, mm, ss;
  if (sscanf(s.c_str(), "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return 0;
  const char* names = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* p = strstr(names, mon);
  if (!p) return 0;
  int m = (p - names) / 3 + 1;
  // Days from civil date (Howard Hinnant's algorithm).
  y -= m <= 2;
  long era = y / 400, yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return (time_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}

class StreamSource : public gtfsrt::ByteSource {
 public:
  StreamSource(WiFiClient& c, int size, volatile uint32_t* counter)
      : c_(c), remaining_(size), counter_(counter) {}

  size_t read(uint8_t* buf, size_t cap) override {
    uint32_t start = millis();
    while (remaining_ != 0) {
      size_t avail = c_.available();
      if (avail) {
        size_t n = avail < cap ? avail : cap;
        if (remaining_ > 0 && (int)n > remaining_) n = remaining_;
        int got = c_.read(buf, n);
        if (got <= 0) return 0;
        if (remaining_ > 0) remaining_ -= got;
        *counter_ += got;
        return got;
      }
      if (!c_.connected()) return 0;
      if (millis() - start > 10000) {
        timed_out_ = true;
        return 0;
      }
      delay(2);
    }
    return 0;
  }

  // False if the server announced more bytes than we received.
  bool complete() const { return !timed_out_ && remaining_ <= 0; }

 private:
  WiFiClient& c_;
  int remaining_;  // -1 when the size is unknown
  volatile uint32_t* counter_;
  bool timed_out_ = false;
};

struct FetchRequest {
  String url;
  String stops;
  String label;
  String routes;  // route_ids to keep, empty = all
  String direct_url;  // the operator's own feed: fallback if the proxy fails
  String user;  // HTTP Basic, if the feed needs it
  String pass;
};

class Fetcher {
 public:
  void begin() {
    mutex_ = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(&Fetcher::taskEntry, "fetch", 20 * 1024, this, 1, &task_, 0);
  }

  // Sets what to download and wakes the task up now.
  void request(const FetchRequest& r) {
    lock();
    req_ = r;
    generation_req_++;
    have_data_ = false;
    error_ = "";
    unlock();
    if (task_) xTaskNotifyGive(task_);
  }

  void refreshNow() {
    if (task_) xTaskNotifyGive(task_);
  }

  // Copies the latest result; returns false if nothing new since `seen`.
  bool snapshot(gtfsrt::Result& out, bool& has_data, uint32_t& seen) {
    lock();
    bool changed = seen != generation_res_;
    if (changed) {
      out = result_;
      has_data = have_data_;
      seen = generation_res_;
    }
    unlock();
    return changed;
  }

  struct Status {
    bool busy;
    bool has_data;
    String error;
    uint32_t last_ok_ms;
    uint32_t last_duration_ms;
    uint64_t last_bytes;
    String phase;       // what the task is doing now, for the ticker
    uint32_t progress;  // bytes of the download in progress
  };

  Status status() {
    lock();
    Status s{busy_, have_data_, error_, last_ok_ms_, last_duration_ms_, last_bytes_, phase_, progress_};
    unlock();
    return s;
  }

 private:
  static void taskEntry(void* self) { static_cast<Fetcher*>(self)->run(); }

  void run() {
    for (;;) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(REFRESH_SECONDS * 1000));
      setPhase("Attendo il WiFi");
      while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));
      // The clock drops past departures. If NTP does not answer (some
      // routers block it) go on: download() takes the time from the server.
      setPhase("Attendo l'ora da internet");
      for (int i = 0; i < 30 && time(nullptr) < 1700000000; ++i) vTaskDelay(pdMS_TO_TICKS(500));
      lock();
      FetchRequest r = req_;
      uint32_t gen = generation_req_;
      busy_ = true;
      unlock();
      if (!r.url.length()) {
        lock();
        busy_ = false;
        if (r.stops.length() && gen == generation_req_) {
          error_ = "Serve l'URL del proxy";
          generation_res_++;
        }
        unlock();
        continue;
      }

      gtfsrt::Result* res = new gtfsrt::Result();
      String err;
      uint32_t t0 = millis();
      progress_ = 0;
      download(r, r.url, *res, err);
      if (err.length() && r.direct_url.length() && r.direct_url != r.url) {
        Serial.printf("[fetch] proxy: %s, provo il feed diretto\n", err.c_str());
        String direct_err;
        *res = gtfsrt::Result();
        progress_ = 0;
        download(r, r.direct_url, *res, direct_err);
        err = direct_err.length() ? "proxy: " + err + ", diretto: " + direct_err : String("");
      }
      uint32_t took = millis() - t0;
      setPhase("");

      lock();
      busy_ = false;
      if (gen == generation_req_) {  // ignore results for an old stop
        if (err.length()) {
          error_ = err;
        } else {
          result_ = *res;
          have_data_ = true;
          error_ = "";
          last_ok_ms_ = millis();
          last_bytes_ = res->bytes;
        }
        last_duration_ms_ = took;
        generation_res_++;
      }
      unlock();
      Serial.printf("[fetch] %s: %s, %d partenze, %llu byte in %lu ms\n", r.label.c_str(),
                    err.length() ? err.c_str() : "ok", res->count, (unsigned long long)res->bytes,
                    (unsigned long)took);
      delete res;
    }
  }

  void download(const FetchRequest& r, const String& url, gtfsrt::Result& res, String& err) {
    gtfsrt::Filter filter;
    filter.setStops(r.stops.c_str());
    filter.setRoutes(r.routes.c_str());
    filter.now = time(nullptr) > 1700000000 ? time(nullptr) : 0;  // 0: keep all

    bool https = url.startsWith("https");
    WiFiClientSecure secure;
    WiFiClient plain;
    WiFiClient& client = https ? (WiFiClient&)secure : plain;
    if (https) secure.setInsecure();  // public timetable data, no secrets

    HTTPClient http;
    http.useHTTP10(true);  // no chunked encoding: the body is the raw protobuf
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("Fermata-ESP32/" FERMATA_VERSION);
    if (r.user.length() && url == r.direct_url) http.setAuthorization(r.user.c_str(), r.pass.c_str());
    setPhase("Mi collego al server");
    const char* headers[] = {"Date"};
    if (!http.begin(client, url)) {
      err = "URL non valido";
      return;
    }
    http.collectHeaders(headers, 1);
    int code = http.GET();
    if (time(nullptr) < 1700000000) {
      time_t server_time = parseHttpDate(http.header("Date"));
      if (server_time > 1700000000) {
        struct timeval tv = {server_time, 0};
        settimeofday(&tv, nullptr);
        Serial.println("[fetch] ora presa dal server");
      }
    }
    if (code != HTTP_CODE_OK) {
      if (code == 401) err = "Servono utente e password";
      else err = code < 0 ? "Rete: " + http.errorToString(code) : "HTTP " + String(code);
      Serial.printf("[fetch] %s -> %s (heap libero %u, blocco max %u)\n", url.c_str(), err.c_str(),
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      http.end();
      return;
    }
    setPhase("Scarico i dati");
    StreamSource src(*http.getStreamPtr(), http.getSize(), &progress_);
    bool ok = gtfsrt::parse(src, filter, res) && src.complete();
    http.end();
    if (!ok) err = res.bytes ? "Formato non valido" : "Risposta vuota";
  }

  void setPhase(const char* p) {
    lock();
    phase_ = p;
    unlock();
  }

  void lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
  void unlock() { xSemaphoreGive(mutex_); }

  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  FetchRequest req_;
  gtfsrt::Result result_;
  bool have_data_ = false;
  bool busy_ = false;
  String error_;
  uint32_t generation_req_ = 0, generation_res_ = 0;
  uint32_t last_ok_ms_ = 0, last_duration_ms_ = 0;
  uint64_t last_bytes_ = 0;
  String phase_;
  volatile uint32_t progress_ = 0;
};
