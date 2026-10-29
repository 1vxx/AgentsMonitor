"""Runs AgentsMonitor on made-up sessions: for trying the dashboard and the
displays without an agent at work, and for screenshots that show nobody's
real projects.

    python3 scripts/demo.py [--port 7790] [--bin path/to/agentsmon]

It starts a daemon of its own in a throwaway folder, with Claude Code's and
Codex's folders pointed at empty ones beside it, so nothing from this
machine's real sessions can show. It writes three weeks of token history,
then acts out a handful of sessions until Ctrl+C, which stops the daemon and
removes the folder. Point the dashboard at it:

    AGENTSMON_URL=http://127.0.0.1:7790 pnpm --dir web dev

and the review page's live mode at ws://127.0.0.1:7790/v1/ws.
"""

import argparse
import json
import os
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DAY = 86_400_000
HOUR = 3_600_000
MIN = 60_000

# The sessions on show: (agent, session id, project, title, model)
LIVE = [
    ("claude-code", "demo-web", "web-app", "Fix the flaky login test", "claude-opus-5-5"),
    ("claude-code", "demo-pay", "payments-api", "Refactor the auth middleware", "claude-opus-5-5"),
    ("claude-code", "demo-docs", "docs-site", "Write the v2 migration guide", "claude-sonnet-5-5"),
    ("codex", "demo-ml", "ml-pipeline", "Speed up the data loader", "gpt-5.5-codex"),
    ("claude-code", "demo-mob", "mobile-app", "Dark mode for settings", "claude-opus-5-5"),
    ("codex", "demo-infra", "infra", "Terraform drift check", "gpt-5.5-codex"),
]
PAST = [
    ("claude-code", "web-app", "Cache the session lookups"),
    ("claude-code", "payments-api", "Idempotency keys for refunds"),
    ("codex", "ml-pipeline", "Shard the feature store"),
    ("claude-code", "docs-site", "Search across versions"),
    ("claude-code", "mobile-app", "Offline sync conflicts"),
    ("codex", "infra", "Rotate the staging certificates"),
    ("claude-code", "web-app", "Upgrade the router"),
    ("claude-code", "payments-api", "Webhook retries with backoff"),
]


# As the agents write their logs: compact, which the daemon's quick scan for
# "type":"assistant" and the like relies on
def line(record):
    return json.dumps(record, separators=(",", ":")) + "\n"


def iso(ms):
    return datetime.fromtimestamp(ms / 1000, timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


class Demo:
    def __init__(self, port, binary):
        self.port = port
        self.binary = binary
        # One folder per port, cleared at the start: a run stopped too hard
        # to tidy up after itself leaves nothing behind for long
        self.dir = Path(tempfile.gettempdir()) / f"agentsmon-demo-{port}"
        shutil.rmtree(self.dir, ignore_errors=True)
        self.home = self.dir / "home"
        self.claude = self.dir / "claude"
        self.codex = self.dir / "codex"
        self.rng = random.Random(7)
        self.requests = 0
        self.daemon = None

    # ---------- History, as the agents' own logs ----------

    def transcript(self, project, sid):
        path = self.claude / "projects" / f"-home-demo-{project}" / f"{sid}.jsonl"
        path.parent.mkdir(parents=True, exist_ok=True)
        return path

    def claude_requests(self, path, sid, title, start, count, model):
        lines = [{"type": "ai-title", "aiTitle": title, "sessionId": sid}]
        for k in range(count):
            self.requests += 1
            sub = self.rng.random() < 0.25
            lines.append({
                "type": "assistant", "sessionId": sid, "isSidechain": sub,
                "requestId": f"req_demo_{self.requests}", "timestamp": iso(start + k * self.rng.randint(20_000, 90_000)),
                "message": {
                    "model": "claude-haiku-5-5" if sub else model, "id": f"msg_demo_{self.requests}",
                    "usage": {"input_tokens": self.rng.randint(2, 60), "output_tokens": self.rng.randint(150, 2500),
                              "cache_read_input_tokens": self.rng.randint(20_000, 140_000),
                              "cache_creation_input_tokens": self.rng.randint(500, 9_000)},
                },
            })
        with path.open("a") as f:
            f.writelines(line(l) for l in lines)

    def codex_requests(self, sid, start, count, model):
        when = datetime.fromtimestamp(start / 1000, timezone.utc)
        path = self.codex / "sessions" / when.strftime("%Y/%m/%d") / f"rollout-{when.strftime('%Y-%m-%dT%H-%M-%S')}-{sid}.jsonl"
        path.parent.mkdir(parents=True, exist_ok=True)
        lines = [
            {"timestamp": iso(start), "type": "session_meta", "payload": {"id": sid, "session_id": sid, "source": "cli"}},
            {"timestamp": iso(start), "type": "turn_context", "payload": {"model": model}},
        ]
        for k in range(count):
            self.requests += 1
            inp = self.rng.randint(30_000, 120_000)
            cached = int(inp * self.rng.uniform(0.6, 0.9))
            lines.append({"timestamp": iso(start + k * 45_000), "type": "token_usage_record", "payload": {
                "thread_id": sid, "session_id": sid, "response_id": f"resp_demo_{self.requests}",
                "usage": {"input_tokens": inp, "cached_input_tokens": cached, "cache_write_input_tokens": 0,
                          "output_tokens": self.rng.randint(200, 3000)}}})
        with path.open("w") as f:
            f.writelines(line(l) for l in lines)

    def history(self, now):
        # Three weeks of work, busier on weekdays, as ended sessions
        for d in range(21, 0, -1):
            day = now - d * DAY
            busy = 0.35 if datetime.fromtimestamp(day / 1000).weekday() >= 5 else 1.0
            for _ in range(int(self.rng.randint(2, 5) * busy) or 1):
                agent, project, title = self.rng.choice(PAST)
                start = day + self.rng.randint(9, 18) * HOUR + self.rng.randint(0, 59) * MIN
                sid = f"past-{d}-{self.rng.randint(1000, 9999)}"
                n = self.rng.randint(15, 70)
                if agent == "codex":
                    self.codex_requests(sid, start, n, "gpt-5.5-codex")
                else:
                    self.claude_requests(self.transcript(project, sid), sid, title, start, n, "claude-opus-5-5")
        # Today's work so far in the live sessions
        for agent, sid, project, title, model in LIVE:
            start = now - self.rng.randint(25, 140) * MIN
            n = self.rng.randint(20, 60)
            if agent == "codex":
                self.codex_requests(sid, start, n, model)
            else:
                self.claude_requests(self.transcript(project, sid), sid, title, start, n, model)

    # ---------- The daemon ----------

    def start(self):
        self.home.mkdir(parents=True)
        self.claude.mkdir()
        self.codex.mkdir()
        (self.home / "config.toml").write_text(
            f'socket = "{self.home / "agentsmon.sock"}"\n\n[http]\nlisten = "127.0.0.1:{self.port}"\n\n'
            "[claude]\nusage_refresh_minutes = 0\n")
        env = dict(os.environ, AGENTSMON_HOME=str(self.home), AGENTSMON_SOCKET=str(self.home / "agentsmon.sock"),
                   CLAUDE_CONFIG_DIR=str(self.claude), CODEX_HOME=str(self.codex), AGENTSMON_LOG="warn")
        self.history(int(time.time() * 1000))
        self.daemon = subprocess.Popen([self.binary, "serve"], env=env)
        for _ in range(100):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{self.port}/v1/health", timeout=1)
                return
            except OSError:
                if self.daemon.poll() is not None:
                    sys.exit(f"agentsmon stopped at once; is port {self.port} free?")
                time.sleep(0.1)
        sys.exit("agentsmon did not answer in 10 s")

    def stop(self):
        if self.daemon and self.daemon.poll() is None:
            self.daemon.send_signal(signal.SIGTERM)
            try:
                self.daemon.wait(5)
            except subprocess.TimeoutExpired:
                self.daemon.kill()
        shutil.rmtree(self.dir, ignore_errors=True)

    def send(self, *events):
        body = json.dumps(list(events)).encode()
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}/v1/ingest", body, {"content-type": "application/json"})
        urllib.request.urlopen(req, timeout=3).read()

    # ---------- The sessions ----------

    def ev(self, agent, sid, event, **fields):
        return {"agent": agent, "sid": sid, "type": event, **fields}

    def open(self):
        now = int(time.time() * 1000)
        for agent, sid, project, title, model in LIVE:
            meta = {"cwd": f"/home/demo/code/{project}", "project": project, "model": model, "title": title,
                    "permission_mode": "default", "interactive": True, "surface": "terminal"}
            if agent == "claude-code":
                meta["transcript_path"] = str(self.transcript(project, sid))
            self.send(self.ev(agent, sid, "session.start", source="startup", **meta))
        limits = {
            "claude-code": [{"kind": "five_hour", "percent_used": 34, "resets_at": iso(now + 2 * HOUR + 14 * MIN)},
                            {"kind": "seven_day", "percent_used": 21, "resets_at": iso(now + 4 * DAY)}],
            "codex": [{"kind": "five_hour", "percent_used": 12, "resets_at": iso(now + 3 * HOUR + 41 * MIN)},
                      {"kind": "seven_day", "percent_used": 46, "resets_at": iso(now + 2 * DAY + 5 * HOUR)}],
        }
        for agent, sid, *_ in LIVE:
            self.send(self.ev(agent, sid, "usage", context={"percent": self.rng.randint(18, 64)}, rate_limits=limits[agent]))
        # Two that finished a while ago, one well and one not
        for sid, outcome, ago in (("demo-ml", "answer", 4 * MIN), ("demo-infra", "error", 9 * MIN)):
            self.send(self.ev("codex", sid, "prompt", ts=now - ago - 3 * MIN, preview="Profile the loader and fix the slowest step"),
                      self.ev("codex", sid, "turn.start", ts=now - ago - 3 * MIN),
                      self.ev("codex", sid, "turn.end", ts=now - ago, outcome=outcome, duration_ms=3 * MIN))

    # A loop of small changes, so the dashboard and displays have something to show
    def act(self):
        steps = [
            lambda: self.send(self.ev("claude-code", "demo-web", "prompt", preview="The login test fails one run in five"),
                              self.ev("claude-code", "demo-web", "turn.start"),
                              self.ev("claude-code", "demo-web", "tool.start", id="t1", tool="Bash", subject="pnpm test -- auth")),
            lambda: self.send(self.ev("claude-code", "demo-pay", "prompt", preview="Split the auth middleware"),
                              self.ev("claude-code", "demo-pay", "turn.start"),
                              self.ev("claude-code", "demo-pay", "tool.start", id="p1", tool="Edit", subject="src/middleware/auth.ts")),
            lambda: self.send(self.ev("claude-code", "demo-docs", "prompt", preview="Draft the migration guide"),
                              self.ev("claude-code", "demo-docs", "turn.start")),
            lambda: self.send(self.ev("claude-code", "demo-web", "attention", kind="permission", tool="Bash",
                                      subject="pnpm test -- auth --repeat 20")),
            lambda: self.send(self.ev("claude-code", "demo-mob", "prompt", preview="Add dark mode to settings"),
                              self.ev("claude-code", "demo-mob", "turn.start"),
                              self.ev("claude-code", "demo-mob", "attention", kind="input", tool="AskUserQuestion",
                                      message="Follow the system theme, or add a toggle?")),
        ]
        for step in steps:
            step()
            time.sleep(1.5)
        # The two at work move from tool to tool, and the docs session between
        # thinking and writing
        work = {
            "demo-pay": [("Read", "src/middleware/session.ts"), ("Edit", "src/middleware/auth.ts"),
                         ("Grep", "verifyToken"), ("Bash", "cargo test -p payments")],
            "demo-docs": [("Read", "docs/v1/config.md"), ("Write", "docs/migrate/v2.md"), ("WebFetch", "semver.org")],
        }
        running = {"demo-pay": ("p1", "Edit"), "demo-docs": None}
        beat = n = 0
        while True:
            time.sleep(5)
            beat += 5
            # Claude Code sessions beat every 20 s, as the plugin does
            if beat % 20 == 0:
                self.send(*[self.ev(a, s, "heartbeat", busy=True) for a, s, *_ in LIVE if a == "claude-code"])
            sid = "demo-pay" if beat % 10 == 0 else "demo-docs"
            events = []
            if running[sid]:
                tid, tool = running[sid]
                events.append(self.ev("claude-code", sid, "tool.end", id=tid, tool=tool, ok=True))
                running[sid] = None
            if sid == "demo-pay" or self.rng.random() < 0.6:
                n += 1
                tool, subject = self.rng.choice(work[sid])
                running[sid] = (f"w{n}", tool)
                events.append(self.ev("claude-code", sid, "tool.start", id=f"w{n}", tool=tool, subject=subject))
            self.send(*events)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--port", type=int, default=7790)
    parser.add_argument("--bin", help="the agentsmon to run (default: on PATH, else this checkout's release build)")
    args = parser.parse_args()
    binary = args.bin or shutil.which("agentsmon") or str(ROOT / "target" / "release" / "agentsmon")
    if not Path(binary).exists() and not shutil.which(binary):
        sys.exit("No agentsmon found: install it (see the README) or build it with cargo build --release")

    demo = Demo(args.port, binary)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        demo.start()
        demo.open()
        print(f"Demo daemon on http://127.0.0.1:{args.port}, with made-up sessions. Ctrl+C stops it.\n\n"
              f"  Dashboard:    AGENTSMON_URL=http://127.0.0.1:{args.port} pnpm --dir web dev\n"
              f"  Review page:  live mode, ws://127.0.0.1:{args.port}/v1/ws\n", flush=True)
        demo.act()
    except KeyboardInterrupt:
        pass
    finally:
        demo.stop()


if __name__ == "__main__":
    main()
