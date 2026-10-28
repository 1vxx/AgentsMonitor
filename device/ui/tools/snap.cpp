// Plays a short script through the UI on the host and writes the frames it
// names as PPM files, for checking screens without a browser or a device.
//   snap <cjk10.bin> <out dir>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../gfx.h"
#include "../synth.h"
#include "../ui.h"

namespace {

std::vector<uint8_t> load(const char *path) {
  std::vector<uint8_t> data;
  FILE *f = fopen(path, "rb");
  if (!f) return data;
  fseek(f, 0, SEEK_END);
  data.resize(ftell(f));
  fseek(f, 0, SEEK_SET);
  fread(data.data(), 1, data.size(), f);
  fclose(f);
  return data;
}

const int64_t BASE = 1793000000000;  // epoch ms the script starts at

struct Item {
  const char *key, *project, *title, *status;
  int64_t since;  // seconds before now
  const char *tool, *extra;
};

std::string item(const Item &i, int64_t now) {
  char buf[1024];
  snprintf(buf, sizeof buf, R"({"key":"%s","agent":"claude-code","project":"%s","title":"%s","status":"%s","since":%lld%s%s%s%s})",
           i.key, i.project, i.title, i.status, (long long)(now - i.since * 1000), i.tool ? R"(,"tool":")" : "",
           i.tool ? i.tool : "", i.tool ? "\"" : "", i.extra ? i.extra : "");
  return buf;
}

std::string summary(const char *mood, const std::vector<std::string> &items, int64_t now) {
  std::string s = R"({"ts":)" + std::to_string(now) + R"(,"mood":")" + mood + R"(","counts":{},"quotas":[],"sessions":[)";
  for (size_t k = 0; k < items.size(); k++) s += (k ? "," : "") + items[k];
  return s + "]}";
}

uint16_t fb[gfx::W * gfx::H];

void save(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", gfx::W, gfx::H);
  for (uint16_t q : fb) {
    uint16_t p = __builtin_bswap16(q);
    int r = p >> 11, g = p >> 5 & 63, b = p & 31;
    unsigned char px[3] = {(unsigned char)(r << 3 | r >> 2), (unsigned char)(g << 2 | g >> 4), (unsigned char)(b << 3 | b >> 2)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 3) return fprintf(stderr, "usage: snap cjk10.bin outdir\n"), 1;
  static std::vector<uint8_t> cjk = load(argv[1]);
  if (!ui::init(cjk.data(), cjk.size())) return fprintf(stderr, "init failed\n"), 1;
  const char *out = argv[2];

  int64_t ms = 1000;
  auto run = [&](double seconds) {
    for (int64_t end = ms + int64_t(seconds * 1000); ms < end; ms += 33) ui::frame(fb, ms, BASE + ms);
  };
  auto now = [&] { return BASE + ms; };

  const Item thinking = {"claude-code:a", "AgentsMonitor", "Daemon DB growth and event retention", "thinking", 40, nullptr, nullptr};
  const Item tool = {"claude-code:b", "AgentsMonitor", "Theme switch polish", "tool", 12, "Edit", nullptr};
  const Item done = {"claude-code:c", "AgentsMonitor", "Live session ordering", "idle", 90, nullptr,
                     R"(,"last_turn":{"outcome":"answer","ended_at":)"};
  const Item failed = {"codex:d", "device-fw", "Device UI prototype", "idle", 400, nullptr, R"(,"last_turn":{"outcome":"error","ended_at":)"};
  const Item quiet = {"claude-code:e", "notes", "", "idle", 900, nullptr, R"(,"stale":true)"};

  // An item whose last turn ended at `at`, epoch ms; the same time every call,
  // so repeating it in a later summary is not a new ending
  auto withEnd = [&](Item i, int64_t at) {
    static std::string extra[16];
    static int n;
    extra[n % 16] = std::string(i.extra) + std::to_string(at) + "}";
    i.extra = extra[n++ % 16].c_str();
    return i;
  };
  const int64_t doneAt = BASE - 90000, failedAt = BASE - 400000;

  ui::link(ui::ONLINE);
  run(0.5);
  ui::summary(summary("working",
                      {item(thinking, now()), item(tool, now()), item(withEnd(done, doneAt), now()), item(withEnd(failed, failedAt), now()),
                       item(quiet, now())},
                      now())
                  .c_str());
  run(3);
  save(out, "1_home_working");

  // A turn finishes: the orb celebrates
  Item finished = thinking;
  finished.status = "idle";
  finished.extra = R"(,"last_turn":{"outcome":"answer","ended_at":)";
  const int64_t finishedAt = now();
  ui::summary(summary("working",
                      {item(tool, now()), item(withEnd(finished, finishedAt), now()), item(withEnd(done, doneAt), now()),
                       item(withEnd(failed, failedAt), now()), item(quiet, now())},
                      now())
                  .c_str());
  run(0.35);
  save(out, "1b_done_cheer");
  run(3);

  // A turn fails: the orb winces
  Item broke = tool;
  broke.status = "idle";
  broke.tool = nullptr;
  broke.extra = R"(,"last_turn":{"outcome":"error","ended_at":)";
  const int64_t brokeAt = now();
  ui::summary(summary("error",
                      {item(withEnd(broke, brokeAt), now()), item(withEnd(finished, finishedAt), now()), item(withEnd(done, doneAt), now()),
                       item(withEnd(failed, failedAt), now()), item(quiet, now())},
                      now())
                  .c_str());
  run(0.12);
  save(out, "1c_fail_wince");
  run(3);

  // Someone asks for approval
  Item asking = {"claude-code:f", "AgentsMonitor", "卡顿问题排查", "waiting_permission", 0, "Bash", nullptr};
  std::string wait = R"(,"attention":{"kind":"permission","since":)" + std::to_string(now()) +
                     R"(,"tool":"Bash","subject":"npm run build && npm test -- --watch=false"})";
  asking.extra = wait.c_str();
  ui::summary(summary("attention",
                      {item(asking, now()), item(thinking, now()), item(tool, now()), item(withEnd(done, doneAt), now()),
                       item(withEnd(failed, failedAt), now()), item(quiet, now())},
                      now())
                  .c_str());
  run(1.5);
  save(out, "2_alert");
  printf("cues:");
  for (int c; (c = ui::cue()) >= 0;) printf(" %d", c);
  printf("\n");
  run(10);
  save(out, "3_home_attention");

  // Swipe up to the list, then open a row
  ui::touch(ui::DOWN, 233, 380, ms);
  ui::touch(ui::MOVE, 233, 300, ms + 60);
  ui::touch(ui::UP, 233, 260, ms + 120);
  run(1);
  save(out, "4_list");
  ui::touch(ui::DOWN, 233, 245, ms);
  ui::touch(ui::UP, 233, 245, ms + 80);
  run(1);
  save(out, "5_detail");

  ui::link(ui::CONNECTING);
  run(1.5);
  save(out, "6_connecting");
  ui::link(ui::ONLINE);
  ui::summary(summary("offline", {}, now()).c_str());
  run(2);
  save(out, "7_empty");
  printf("screen %s\n", ui::screen());
  return 0;
}
