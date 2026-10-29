#pragma once

#include <stdint.h>

// The daemon's summary (`GET /v1/summary`, the `summary` WebSocket message) as
// fixed-size structs. Times are epoch milliseconds, as the daemon sends them.
namespace model {

enum class Status : uint8_t { Idle, Thinking, Tool, Compacting, WaitingInput, WaitingPermission, Ended };
enum class Mood : uint8_t { Offline, Idle, Error, Working, Attention };
enum class Wait : uint8_t { None, Permission, Input, Idle };
enum class Outcome : uint8_t { None, Answer, Aborted, Refusal, Error };

constexpr int MAX = 8;

struct Session {
  char key[96];
  char agent[24];
  char project[64];
  char title[160];
  char tool[48];
  Status status;
  int64_t since;
  bool stale;
  Wait wait;
  int64_t waitSince;
  char waitTool[48];
  char waitText[400];  // the subject (a command, a path), else the message
  Outcome outcome;
  int64_t endedAt;
  uint64_t tokens;   // used so far, cache reads and writes included; 0 before any
  bool tokensExact;  // the agent's own counts rather than an estimate
  float context;     // percent of the context window in use, or -1 without a reading
};

struct Summary {
  int64_t ts;
  Mood mood;
  int count;
  Session sessions[MAX];
};

inline bool waiting(Status s) { return s == Status::WaitingInput || s == Status::WaitingPermission; }
inline bool working(Status s) { return s == Status::Thinking || s == Status::Tool || s == Status::Compacting; }

// Reads a summary, or a WebSocket message that carries one. False if neither.
bool parse(const char *json, Summary &out);

}  // namespace model
