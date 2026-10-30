# AgentsMonitor, for coding agents

Read this before you change or set up anything here. It is short on purpose. People usually arrive with a prompt from [docs/RECIPES.md](docs/RECIPES.md).

AgentsMonitor watches Claude Code and Codex sessions. A small Rust daemon keeps their state, and a round desk display shows it. The display is the product; the web dashboard is for the numbers.

## Where things are

| Path | What it is |
| --- | --- |
| `crates/agentsmon` | The daemon, hook forwarder and CLI |
| `crates/agentsmon-protocol` | Wire types shared with everything else |
| `plugins/` | The Claude Code and Codex integrations |
| `devices/core/` | Portable C++17 shared by every theme and board |
| `devices/themes/<name>/` | One folder per display theme. Start from `devices/tools/new_theme.sh` |
| `devices/boards/<board>/` | Drivers, the only hardware-specific code |
| `devices/sim/` | The review page (the display in WebAssembly) and `snap.mjs` |
| `devices/tools/face_pack.py` | Makes Her's photo packs with LivePortrait |
| `web/` | The dashboard |
| `scripts/demo.py` | A daemon on made-up sessions, on port 7790 |

## Check your work

| You changed | Run |
| --- | --- |
| Rust | `cargo test` and `cargo clippy --all-targets` |
| Claude Code plugin | `claude plugin validate plugins/claude-code` and `claude plugin test plugins/claude-code` |
| Dashboard | `pnpm --dir web typecheck` |
| Device code | `devices/sim/build.sh`, then `node devices/sim/snap.mjs check <theme>` for each theme you touched |
| Something that must not change the display | `snap.mjs record` before and after, then `snap.mjs compare` |
| Firmware, if the toolchain is here | `THEME=<theme> devices/boards/waveshare_amoled_175c/build.sh` |

`node devices/sim/snap.mjs png <theme> <scenario> <seconds> out.png` gives you a picture of any moment, so look at what you drew. Her needs a real browser for that: photos are decoded by the review page, not in Node.

Say what you checked and what you could not.

## Rules

- **The person's secrets stay theirs.** Never print or commit `secrets.h`, or the token in `~/.agentsmon/config.toml`. Generate tokens yourself; let the person type their Wi-Fi password into `secrets.h` themselves. Don't ask for it in chat.
- **No real sessions in pictures.** Images come from the review page's scripts or `scripts/demo.py`, never the live daemon: those show private project names and prompts.
- **No real faces in the repo.** Packs and frames made from someone's photo belong in `devices/sim/build/`, which git ignores. Only a face made for publishing goes in `devices/themes/her/demo/`.
- **Keep the display code portable.** Themes and `core/` use no Arduino or ESP-IDF headers. The same files build for the browser and every board.
- **Say before you flash.** Flashing replaces what is on the person's board.
- **Commit only when asked, and never push on your own.** Commit messages are a short imperative sentence about the effect: `Keep pets in their places when swiping`.
- **Guides come in pairs.** `README`, `docs/USAGE`, `docs/RECIPES`, `devices/README`, `devices/themes/README` and `devices/themes/her/README` each have a `.zh-CN.md`. Change both, and keep them in step.

## Going further

| For | Read |
| --- | --- |
| Setting up, the CLI, config, troubleshooting | [docs/USAGE.md](docs/USAGE.md) |
| Flashing the board, porting to other hardware | [devices/README.md](devices/README.md) |
| Writing a theme | [devices/themes/README.md](devices/themes/README.md) |
| Her's photo packs and expressions | [devices/themes/her/README.md](devices/themes/her/README.md) |
| The daemon's insides, adding an agent | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| Events and the API | [docs/PROTOCOL.md](docs/PROTOCOL.md) |
| How changes are made and sent | [CONTRIBUTING.md](CONTRIBUTING.md) |
