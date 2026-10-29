// Host test for fermata/gtfsrt.h. Run tools/test_gtfsrt.sh.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../fermata/gtfsrt.h"

// Feeds bytes in small odd-sized chunks to exercise buffer refills.
class ChunkSource : public gtfsrt::ByteSource {
 public:
  ChunkSource(const std::vector<uint8_t>& d) : d_(d) {}
  size_t read(uint8_t* buf, size_t cap) override {
    size_t n = std::min(cap, std::min<size_t>(37, d_.size() - pos_));
    memcpy(buf, d_.data() + pos_, n);
    pos_ += n;
    return n;
  }
 private:
  const std::vector<uint8_t>& d_;
  size_t pos_ = 0;
};

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  FILE* f = fopen(argv[1], "rb");
  int64_t now = atoll(argv[2]);
  std::vector<uint8_t> data;
  int c;
  while ((c = fgetc(f)) != EOF) data.push_back(c);
  fclose(f);

  if (argc > 3) {  // feed built by gtfs_static.php from tools/make_test_gtfs.py
    gtfsrt::Filter f;
    f.setStops("S_MI,S_MI_1");
    f.now = now;
    ChunkSource src(data);
    gtfsrt::Result res;
    CHECK(gtfsrt::parse(src, f, res));
    CHECK(res.count == 3);
    if (res.count == 3) {
      CHECK(strcmp(res.deps[0].route, "S5") == 0 && strcmp(res.deps[0].headsign, "Treviglio") == 0);
      CHECK(res.deps[0].time == now + 420 && !res.deps[0].has_delay);
    }
    printf(failures ? "FAILED\n" : "PASSED (static)\n");
    return failures ? 1 : 0;
  }

  gtfsrt::Filter filter;
  filter.setStops(" 70017 , 70018");
  filter.now = now;
  CHECK(filter.count == 2);

  ChunkSource src(data);
  gtfsrt::Result res;
  bool ok = gtfsrt::parse(src, filter, res);
  printf("ok=%d entities=%u trips=%u deps=%d bytes=%llu ts=%lld\n", ok, res.entities,
         res.trip_updates, res.count, (unsigned long long)res.bytes, (long long)res.feed_timestamp);
  for (int i = 0; i < res.count; ++i) {
    auto& d = res.deps[i];
    printf("  %-4s +%5llds delay=%d(%d) skip=%d '%s' veh='%s' plate='%s'\n", d.route, (long long)(d.time - now),
           d.delay, d.has_delay, d.skipped, d.headsign, d.vehicle, d.plate);
  }
  CHECK(ok);
  CHECK(res.entities == 11);
  CHECK(res.feed_timestamp == now);
  CHECK(res.bytes == data.size());
  CHECK(res.count == 6);
  if (res.count == 6) {
    CHECK(strcmp(res.deps[0].route, "H") == 0 && res.deps[0].time == now + 60);
    CHECK(strcmp(res.deps[0].vehicle, "H-12") == 0 && res.deps[0].headsign[0] == 0);
    CHECK(strcmp(res.deps[1].route, "550") == 0 && res.deps[1].delay == 56);
    CHECK(strcmp(res.deps[1].vehicle, "K4153") == 0 && strcmp(res.deps[1].plate, "FZ758DN") == 0);
    CHECK(strcmp(res.deps[2].route, "40") == 0 && res.deps[2].skipped);
    CHECK(strcmp(res.deps[3].route, "64") == 0 && res.deps[3].delay == -60);
    CHECK(strcmp(res.deps[3].headsign, "San Pietro") == 0);
    CHECK(strcmp(res.deps[4].route, "C3") == 0 && res.deps[4].delay == 300);
    CHECK(strcmp(res.deps[5].route, "8") == 0 && !res.deps[5].has_delay);
  }

  // Route filter: only line 550 and 64.
  gtfsrt::Filter only;
  only.setStops("70017");
  only.setRoutes("550, 64");
  only.now = now;
  ChunkSource only_src(data);
  gtfsrt::Result only_res;
  CHECK(gtfsrt::parse(only_src, only, only_res));
  CHECK(only_res.count == 2);

  // A truncated download must be reported as a failure.
  std::vector<uint8_t> cut(data.begin(), data.begin() + data.size() / 2);
  ChunkSource cut_src(cut);
  gtfsrt::Result cut_res;
  CHECK(!gtfsrt::parse(cut_src, filter, cut_res));

  // Keeps only the earliest kMaxDepartures.
  gtfsrt::Result many;
  for (int i = 40; i > 0; --i) {
    gtfsrt::Departure d = {};
    d.time = i;
    many.add(d);
  }
  CHECK(many.count == gtfsrt::kMaxDepartures);
  CHECK(many.deps[0].time == 1 && many.deps[gtfsrt::kMaxDepartures - 1].time == gtfsrt::kMaxDepartures);

  printf(failures ? "FAILED\n" : "PASSED\n");
  return failures ? 1 : 0;
}
