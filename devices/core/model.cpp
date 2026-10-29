#include "model.h"

#include <stdlib.h>
#include <string.h>

#include <string>
#include <utility>
#include <vector>

namespace model {
namespace {

// Just enough JSON for the summary: a tree of values, \u escapes included
struct Value {
  enum Type { NONE, BOOL, NUM, STR, ARR, OBJ } type = NONE;
  double num = 0;
  bool truth = false;
  std::string str;
  std::vector<Value> items;
  std::vector<std::pair<std::string, Value>> fields;

  const Value &operator[](const char *name) const {
    static const Value none;
    if (type == OBJ)
      for (auto &f : fields)
        if (f.first == name) return f.second;
    return none;
  }
};

struct Parser {
  const char *p;

  void space() {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
  }

  bool literal(const char *word) {
    size_t n = strlen(word);
    if (strncmp(p, word, n)) return false;
    p += n;
    return true;
  }

  static void utf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
      out += char(cp);
    } else if (cp < 0x800) {
      out += char(0xC0 | cp >> 6), out += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      out += char(0xE0 | cp >> 12), out += char(0x80 | (cp >> 6 & 63)), out += char(0x80 | (cp & 63));
    } else {
      out += char(0xF0 | cp >> 18), out += char(0x80 | (cp >> 12 & 63)), out += char(0x80 | (cp >> 6 & 63));
      out += char(0x80 | (cp & 63));
    }
  }

  bool hex4(uint32_t &v) {
    v = 0;
    for (int k = 0; k < 4; k++) {
      char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9')
        v |= c - '0';
      else if (c >= 'a' && c <= 'f')
        v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        v |= c - 'A' + 10;
      else
        return false;
    }
    return true;
  }

  bool string(std::string &out) {
    if (*p != '"') return false;
    p++;
    while (*p && *p != '"') {
      if (*p != '\\') {
        out += *p++;
        continue;
      }
      p++;
      char e = *p++;
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          uint32_t cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            uint32_t lo;
            if (!hex4(lo)) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8(out, cp);
          break;
        }
        default: out += e;
      }
    }
    if (*p != '"') return false;
    p++;
    return true;
  }

  bool value(Value &v) {
    space();
    if (*p == '{') {
      v.type = Value::OBJ;
      p++;
      space();
      if (*p == '}') return p++, true;
      for (;;) {
        space();
        std::pair<std::string, Value> f;
        if (!string(f.first)) return false;
        space();
        if (*p++ != ':' || !value(f.second)) return false;
        v.fields.push_back(std::move(f));
        space();
        if (*p == ',') {
          p++;
          continue;
        }
        if (*p == '}') return p++, true;
        return false;
      }
    }
    if (*p == '[') {
      v.type = Value::ARR;
      p++;
      space();
      if (*p == ']') return p++, true;
      for (;;) {
        v.items.emplace_back();
        if (!value(v.items.back())) return false;
        space();
        if (*p == ',') {
          p++;
          continue;
        }
        if (*p == ']') return p++, true;
        return false;
      }
    }
    if (*p == '"') return v.type = Value::STR, string(v.str);
    if (literal("true")) return v.type = Value::BOOL, v.truth = true, true;
    if (literal("false")) return v.type = Value::BOOL, true;
    if (literal("null")) return true;
    char *end;
    v.num = strtod(p, &end);
    if (end == p) return false;
    p = end;
    v.type = Value::NUM;
    return true;
  }
};

void copy(char *dst, size_t n, const Value &v) {
  size_t len = v.str.size() < n - 1 ? v.str.size() : n - 1;
  // Never cut a UTF-8 sequence in half
  while (len && (v.str[len] & 0xC0) == 0x80) len--;
  memcpy(dst, v.str.data(), len);
  dst[len] = 0;
}

Status status(const std::string &s) {
  if (s == "thinking") return Status::Thinking;
  if (s == "tool") return Status::Tool;
  if (s == "compacting") return Status::Compacting;
  if (s == "waiting_input") return Status::WaitingInput;
  if (s == "waiting_permission") return Status::WaitingPermission;
  if (s == "ended") return Status::Ended;
  return Status::Idle;
}

Mood mood(const std::string &s) {
  if (s == "attention") return Mood::Attention;
  if (s == "working") return Mood::Working;
  if (s == "error") return Mood::Error;
  if (s == "idle") return Mood::Idle;
  return Mood::Offline;
}

Outcome outcome(const std::string &s) {
  if (s == "answer") return Outcome::Answer;
  if (s == "aborted") return Outcome::Aborted;
  if (s == "refusal") return Outcome::Refusal;
  if (s == "error") return Outcome::Error;
  return Outcome::None;
}

}  // namespace

bool parse(const char *json, Summary &out) {
  Value root;
  Parser parser{json};
  if (!parser.value(root) || root.type != Value::OBJ) return false;
  const Value *sum = &root;
  if (root["summary"].type == Value::OBJ) sum = &root["summary"];
  if ((*sum)["sessions"].type != Value::ARR) return false;

  memset(&out, 0, sizeof out);
  out.ts = int64_t((*sum)["ts"].num);
  out.mood = mood((*sum)["mood"].str);
  for (const Value &item : (*sum)["sessions"].items) {
    if (out.count == MAX) break;
    Session &s = out.sessions[out.count++];
    copy(s.key, sizeof s.key, item["key"]);
    copy(s.agent, sizeof s.agent, item["agent"]);
    copy(s.project, sizeof s.project, item["project"]);
    copy(s.title, sizeof s.title, item["title"]);
    copy(s.tool, sizeof s.tool, item["tool"]);
    s.status = status(item["status"].str);
    s.since = int64_t(item["since"].num);
    s.turnSince = int64_t(item["turn_since"].num);
    s.stale = item["stale"].truth;
    const Value &wait = item["attention"];
    if (wait.type == Value::OBJ) {
      const std::string &kind = wait["kind"].str;
      s.wait = kind == "permission" ? Wait::Permission : kind == "input" ? Wait::Input : Wait::Idle;
      s.waitSince = int64_t(wait["since"].num);
      copy(s.waitTool, sizeof s.waitTool, wait["tool"]);
      copy(s.waitText, sizeof s.waitText, wait["subject"].type == Value::STR ? wait["subject"] : wait["message"]);
    }
    const Value &last = item["last_turn"];
    if (last.type == Value::OBJ) {
      s.outcome = outcome(last["outcome"].str);
      s.endedAt = int64_t(last["ended_at"].num);
      s.seen = last["seen"].truth;
    }
    s.tokens = uint64_t(item["tokens"].num);
    s.tokensExact = item["tokens_exact"].truth;
    const Value &ctx = item["context"];
    s.context = -1;
    if (ctx["percent"].type == Value::NUM)
      s.context = float(ctx["percent"].num);
    else if (ctx["tokens"].type == Value::NUM && ctx["window"].num > 0)
      s.context = float(ctx["tokens"].num / ctx["window"].num * 100);
  }
  return true;
}

}  // namespace model
