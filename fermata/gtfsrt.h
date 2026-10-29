// Streaming GTFS-Realtime (protobuf) parser.
//
// Decodes a TripUpdates feed byte by byte while it is being downloaded, so a
// regional feed of several megabytes never has to fit into RAM. Only the
// departures for the requested stop_ids are kept.
//
// The file has no Arduino dependencies: it is also compiled on the host by
// tools/test_gtfsrt.sh to test it against real-looking feeds.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace gtfsrt {

static const int kMaxStops = 6;        // stop_ids per favourite
static const int kMaxDepartures = 16;  // departures kept per fetch
static const int kMaxStopsPerTrip = 3; // a circular line can pass twice

struct Departure {
  char route[12];
  char headsign[40];
  int64_t time;    // unix epoch of departure (or arrival)
  int32_t delay;   // seconds, valid if has_delay
  bool has_delay;
  bool skipped;
};

// Anything that can hand out bytes: an HTTP stream, a file, a buffer.
class ByteSource {
 public:
  virtual ~ByteSource() {}
  // Returns how many bytes were copied into buf, 0 on end of stream.
  virtual size_t read(uint8_t* buf, size_t cap) = 0;
};

class BufferSource : public ByteSource {
 public:
  BufferSource(const uint8_t* data, size_t len) : data_(data), len_(len) {}
  size_t read(uint8_t* buf, size_t cap) override {
    size_t n = len_ - pos_ < cap ? len_ - pos_ : cap;
    memcpy(buf, data_ + pos_, n);
    pos_ += n;
    return n;
  }

 private:
  const uint8_t* data_;
  size_t len_;
  size_t pos_ = 0;
};

class Reader {
 public:
  explicit Reader(ByteSource& src) : src_(src) {}

  uint64_t offset() const { return consumed_; }
  bool failed() const { return failed_; }
  bool atEnd() {
    if (pos_ < len_) return false;
    return !fill();
  }

  bool byte(uint8_t& b) {
    if (pos_ >= len_ && !fill()) return fail();
    b = buf_[pos_++];
    consumed_++;
    return true;
  }

  bool varint(uint64_t& out) {
    out = 0;
    for (int shift = 0; shift < 70; shift += 7) {
      uint8_t b;
      if (!byte(b)) return false;
      if (shift < 64) out |= static_cast<uint64_t>(b & 0x7f) << shift;
      if (!(b & 0x80)) return true;
    }
    return fail();
  }

  bool skip(uint64_t n) {
    while (n > 0) {
      if (pos_ >= len_ && !fill()) return fail();
      size_t avail = len_ - pos_;
      size_t step = n < avail ? static_cast<size_t>(n) : avail;
      pos_ += step;
      consumed_ += step;
      n -= step;
    }
    return true;
  }

  // Copies up to cap-1 bytes of a length-delimited field, skips the rest.
  bool string(uint64_t n, char* out, size_t cap) {
    size_t keep = n < cap - 1 ? static_cast<size_t>(n) : cap - 1;
    for (size_t i = 0; i < keep; ++i) {
      uint8_t b;
      if (!byte(b)) return false;
      out[i] = static_cast<char>(b);
    }
    out[keep] = 0;
    return skip(n - keep);
  }

  bool skipField(uint32_t wire_type) {
    uint64_t v;
    switch (wire_type) {
      case 0: return varint(v);
      case 1: return skip(8);
      case 2: return varint(v) && skip(v);
      case 5: return skip(4);
      default: return fail();  // groups (3, 4) are not used by GTFS-RT
    }
  }

 private:
  bool fill() {
    if (failed_) return false;
    pos_ = 0;
    len_ = src_.read(buf_, sizeof(buf_));
    return len_ > 0;
  }
  bool fail() {
    failed_ = true;
    return false;
  }

  ByteSource& src_;
  uint8_t buf_[1024];
  size_t len_ = 0;
  size_t pos_ = 0;
  uint64_t consumed_ = 0;
  bool failed_ = false;
};

struct Filter {
  char stops[kMaxStops][32];
  int count = 0;
  int64_t now = 0;              // drop departures before now - past_s
  int64_t past_s = 60;
  int64_t horizon_s = 3 * 3600; // ... and after now + horizon_s

  // Accepts "123, 456 ,789".
  void setStops(const char* csv) {
    count = 0;
    while (*csv && count < kMaxStops) {
      while (*csv == ' ' || *csv == ',' || *csv == ';') ++csv;
      size_t n = 0;
      while (csv[n] && csv[n] != ',' && csv[n] != ';') ++n;
      size_t end = n;
      while (end > 0 && csv[end - 1] == ' ') --end;
      if (end > 0) {
        size_t keep = end < sizeof(stops[0]) - 1 ? end : sizeof(stops[0]) - 1;
        memcpy(stops[count], csv, keep);
        stops[count][keep] = 0;
        ++count;
      }
      csv += n;
    }
  }

  bool matches(const char* id) const {
    for (int i = 0; i < count; ++i)
      if (strcmp(stops[i], id) == 0) return true;
    return false;
  }
};

struct Result {
  Departure deps[kMaxDepartures];
  int count = 0;
  uint32_t entities = 0;
  uint32_t trip_updates = 0;
  int64_t feed_timestamp = 0;
  uint64_t bytes = 0;
  bool ok = false;

  // Keeps the kMaxDepartures earliest departures, sorted by time.
  void add(const Departure& d) {
    int i = count;
    if (count == kMaxDepartures) {
      if (d.time >= deps[count - 1].time) return;
      i = count - 1;
    } else {
      ++count;
    }
    while (i > 0 && deps[i - 1].time > d.time) {
      deps[i] = deps[i - 1];
      --i;
    }
    deps[i] = d;
  }
};

class Parser {
 public:
  Parser(Reader& r, const Filter& f, Result& out) : r_(r), f_(f), out_(out) {}

  bool parseFeed() {
    while (!r_.atEnd()) {
      uint64_t tag;
      if (!r_.varint(tag)) return finish(false);
      uint32_t field = tag >> 3, wt = tag & 7;
      if (field == 1 && wt == 2) {
        if (!message(&Parser::header)) return finish(false);
      } else if (field == 2 && wt == 2) {
        out_.entities++;
        if (!message(&Parser::entity)) return finish(false);
      } else if (!r_.skipField(wt)) {
        return finish(false);
      }
    }
    return finish(!r_.failed());
  }

 private:
  typedef bool (Parser::*Body)(uint64_t end);

  struct StopEvent {
    int64_t time = 0;
    int64_t scheduled = 0;
    int32_t delay = 0;
    bool has_time = false;
    bool has_delay = false;
  };

  struct Match {
    StopEvent ev;
    bool skipped;
  };

  struct Trip {
    char route[12];
    char headsign[40];
    char label[40];
    int32_t trip_delay;
    bool has_trip_delay;
    Match matches[kMaxStopsPerTrip];
    int n_matches;
  };

  bool finish(bool ok) {
    // A proxy-filtered feed may legitimately have no entities at all.
    out_.ok = ok && r_.offset() > 0;
    out_.bytes = r_.offset();
    return out_.ok;
  }

  // Reads a length prefix and runs body until the end of that message.
  bool message(Body body) {
    uint64_t len;
    if (!r_.varint(len)) return false;
    uint64_t end = r_.offset() + len;
    if (!(this->*body)(end)) return false;
    return r_.offset() == end;
  }

  // Generic loop over the fields of a message; handler returns false on
  // error, and sets handled=false for fields it does not care about.
  template <typename F>
  bool fields(uint64_t end, F handler) {
    while (r_.offset() < end) {
      uint64_t tag;
      if (!r_.varint(tag)) return false;
      bool handled = true;
      if (!handler(static_cast<uint32_t>(tag >> 3), static_cast<uint32_t>(tag & 7), handled))
        return false;
      if (!handled && !r_.skipField(tag & 7)) return false;
    }
    return r_.offset() == end;
  }

  bool header(uint64_t end) {
    return fields(end, [&](uint32_t field, uint32_t wt, bool& handled) {
      handled = field == 3 && wt == 0;
      if (!handled) return true;
      uint64_t v;
      if (!r_.varint(v)) return false;
      out_.feed_timestamp = static_cast<int64_t>(v);
      return true;
    });
  }

  bool entity(uint64_t end) {
    return fields(end, [&](uint32_t field, uint32_t wt, bool& handled) {
      handled = field == 3 && wt == 2;
      if (!handled) return true;
      out_.trip_updates++;
      return message(&Parser::tripUpdate);
    });
  }

  bool tripUpdate(uint64_t end) {
    trip_ = Trip();
    bool ok = fields(end, [&](uint32_t field, uint32_t wt, bool& handled) {
      uint64_t len, v;
      if (field == 1 && wt == 2) return message(&Parser::tripDescriptor);
      if (field == 2 && wt == 2) return message(&Parser::stopTimeUpdate);
      if (field == 3 && wt == 2) return message(&Parser::vehicle);
      if (field == 5 && wt == 0) {
        if (!r_.varint(v)) return false;
        trip_.trip_delay = static_cast<int32_t>(static_cast<int64_t>(v));
        trip_.has_trip_delay = true;
        return true;
      }
      if (field == 6 && wt == 2) {
        if (!r_.varint(len)) return false;
        uint64_t sub_end = r_.offset() + len;
        return fields(sub_end, [&](uint32_t f2, uint32_t wt2, bool& h2) {
          h2 = f2 == 5 && wt2 == 2;
          if (!h2) return true;
          uint64_t n;
          return r_.varint(n) && r_.string(n, trip_.headsign, sizeof(trip_.headsign));
        });
      }
      handled = false;
      return true;
    });
    if (!ok) return false;

    for (int i = 0; i < trip_.n_matches; ++i) emit(trip_.matches[i]);
    return true;
  }

  void emit(const Match& m) {
    Departure d;
    memset(&d, 0, sizeof(d));
    const StopEvent& ev = m.ev;
    if (ev.has_time) {
      d.time = ev.time;
    } else if (ev.scheduled) {
      d.time = ev.scheduled + (ev.has_delay ? ev.delay : 0);
    } else {
      return;  // a bare delay needs the static timetable: nothing to show
    }
    if (f_.now && (d.time < f_.now - f_.past_s || d.time > f_.now + f_.horizon_s)) return;

    if (ev.has_delay) {
      d.delay = ev.delay;
      d.has_delay = true;
    } else if (ev.scheduled && ev.has_time) {
      d.delay = static_cast<int32_t>(ev.time - ev.scheduled);
      d.has_delay = true;
    } else if (trip_.has_trip_delay) {
      d.delay = trip_.trip_delay;
      d.has_delay = true;
    }
    d.skipped = m.skipped;
    strncpy(d.route, trip_.route, sizeof(d.route) - 1);
    strncpy(d.headsign, trip_.headsign[0] ? trip_.headsign : trip_.label, sizeof(d.headsign) - 1);
    out_.add(d);
  }

  bool tripDescriptor(uint64_t end) {
    return fields(end, [&](uint32_t field, uint32_t wt, bool& handled) {
      handled = field == 5 && wt == 2;
      if (!handled) return true;
      uint64_t n;
      return r_.varint(n) && r_.string(n, trip_.route, sizeof(trip_.route));
    });
  }

  bool vehicle(uint64_t end) {
    return fields(end, [&](uint32_t field, uint32_t wt, bool& handled) {
      handled = field == 2 && wt == 2;
      if (!handled) return true;
      uint64_t n;
      return r_.varint(n) && r_.string(n, trip_.label, sizeof(trip_.label));
    });
  }

  bool stopTimeUpdate(uint64_t end) {
    char stop_id[32] = {0};
    StopEvent arr, dep;
    bool has_arr = false, has_dep = false, skipped = false;
    bool ok = fields(end, [&](uint32_t field, uint32_t wt, bool& handled) {
      uint64_t n;
      if (field == 4 && wt == 2) return r_.varint(n) && r_.string(n, stop_id, sizeof(stop_id));
      if (field == 2 && wt == 2) {
        has_arr = true;
        return stopEvent(arr);
      }
      if (field == 3 && wt == 2) {
        has_dep = true;
        return stopEvent(dep);
      }
      if (field == 5 && wt == 0) {
        if (!r_.varint(n)) return false;
        skipped = n == 1;  // SKIPPED
        return true;
      }
      handled = false;
      return true;
    });
    if (!ok) return false;

    if (f_.matches(stop_id) && trip_.n_matches < kMaxStopsPerTrip) {
      Match& m = trip_.matches[trip_.n_matches++];
      // Passengers care about when the bus leaves; fall back to arrival.
      m.ev = has_dep && (dep.has_time || dep.scheduled) ? dep : arr;
      if (!m.ev.has_delay && has_arr && arr.has_delay) {
        m.ev.delay = arr.delay;
        m.ev.has_delay = true;
      }
      m.skipped = skipped;
    }
    return true;
  }

  bool stopEvent(StopEvent& ev) {
    uint64_t len;
    if (!r_.varint(len)) return false;
    uint64_t end = r_.offset() + len;
    return fields(end, [&](uint32_t field, uint32_t wt, bool& handled) {
      handled = wt == 0 && (field == 1 || field == 2 || field == 4);
      if (!handled) return true;
      uint64_t v;
      if (!r_.varint(v)) return false;
      int64_t sv = static_cast<int64_t>(v);
      if (field == 1) {
        ev.delay = static_cast<int32_t>(sv);
        ev.has_delay = true;
      } else if (field == 2) {
        ev.time = sv;
        ev.has_time = sv > 0;
      } else {
        ev.scheduled = sv;
      }
      return true;
    });
  }

  Reader& r_;
  const Filter& f_;
  Result& out_;
  Trip trip_;
};

inline bool parse(ByteSource& src, const Filter& f, Result& out) {
  Reader r(src);
  Parser p(r, f, out);
  return p.parseFeed();
}

}  // namespace gtfsrt
