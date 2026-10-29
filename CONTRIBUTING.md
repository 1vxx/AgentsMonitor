# 🤝 Contributing

Thanks for helping make AgentsMonitor better! Bug reports, new themes, boards, agent adapters, docs and translations are all welcome.

> 中文贡献者：用中文提 Issue 和 Pull Request 完全没问题。

## Ways to help

| You'd like to… | Start here |
| --- | --- |
| 🐛 Report a bug | [Open a bug report](https://github.com/1vxx/AgentsMonitor/issues/new?template=bug_report.yml). The output of `agentsmon status` and `AGENTSMON_LOG=debug agentsmon serve` helps a lot |
| 🔌 Support another board or screen | [Make it yours](devices/README.md#-make-it-yours) in the devices guide |
| 🎨 Make a theme for the display | The [theme guide](devices/themes/README.md) |
| 🤖 Support another coding agent | [Adding an agent](docs/ARCHITECTURE.md#adding-an-agent) |
| 🐾 Show off your build | [Share it](https://github.com/1vxx/AgentsMonitor/issues/new?template=show_your_build.yml) |
| 💡 Suggest something bigger | [Open a feature request](https://github.com/1vxx/AgentsMonitor/issues/new?template=feature_request.yml) first, so we can agree on the shape before you write code |

## Set up

```bash
git clone https://github.com/1vxx/AgentsMonitor.git && cd AgentsMonitor
cargo run --release -p agentsmon -- serve       # the daemon, built from your checkout
pnpm --dir web install && pnpm --dir web dev    # the dashboard, reloading as you edit
```

The Claude Code plugin is read straight from the cloned folder once its marketplace is added ([quick start](README.md#-quick-start), step 2), so plugin edits apply to the next session you start. For the device code, `devices/sim/build.sh` builds the review page: see [Try it in your browser](devices/README.md#-try-it-in-your-browser). To work on the dashboard or a theme without agents at hand, [`scripts/demo.py`](docs/USAGE.md#demo-mode) runs a daemon on made-up sessions.

In Claude Desktop, `.claude/launch.json` has entries for all of these: `dashboard`, `device` (the review page), and `demo` with `dashboard-demo` (the dashboard on made-up sessions).

## Before you send a pull request

Run the checks for what you changed:

| Changed | Run |
| --- | --- |
| Rust (`crates/`) | `cargo test` and `cargo clippy --all-targets` |
| Claude Code plugin | `claude plugin validate plugins/claude-code` and `claude plugin test plugins/claude-code` |
| Dashboard (`web/`) | `pnpm --dir web typecheck` |
| Device code (`devices/`) | `devices/sim/build.sh` (every theme), then `node devices/sim/snap.mjs check <theme>` for each theme you touched, plus the board's `build.sh` if you have the toolchain |
| A change that shouldn't alter the display | `snap.mjs record` before and after, then `snap.mjs compare` (see [Test it](devices/themes/README.md#-test-it)) |

Then:

- **Keep pull requests focused.** One change per pull request is easier to review and to revert.
- **Match the surrounding code.** Follow its naming, comment style and idioms rather than introducing new ones.
- **Write commit messages as a short sentence** in the imperative, describing the effect: `Keep pets in their places when swiping`, not `fix: pets bug`.
- **Update the docs** when behaviour or setup changes. User-facing guides come in English and Chinese, so update both if you can (or say so in the pull request, and someone will help).
- **Never commit secrets or private data.** `secrets.h` and `web/.env.local` are ignored by git; keep it that way. Screenshots should show made-up sessions, not your real projects.

## Adding a board

A new board is a folder `devices/boards/<board>/` with:

- [ ] `<board>.ino` and its drivers (screen, and optionally touch, buttons, audio)
- [ ] `secrets.example.h` with placeholders, never real values
- [ ] `build.sh` that copies `core/` and the chosen app into the sketch and compiles
- [ ] A row in the [devices guide](devices/README.md), with where to buy the board and anything unusual about its wiring
- [ ] A photo or short video in the pull request, so we can all admire it 📸

## Adding a theme

Start with `devices/tools/new_theme.sh <name>`, and follow the [theme guide](devices/themes/README.md). A theme pull request has:

- [ ] `devices/themes/<name>/` with its sources and a `theme.json` (name and description in English and Chinese, author)
- [ ] Only portable C++ that builds for the review page and the firmware, with no board or platform headers
- [ ] Every state shown: waiting, working, finished, failed, idle, no signal, no sessions, link lost
- [ ] `node devices/sim/snap.mjs check <name>` passing
- [ ] Its frame rate on a board, if you have one (the serial log prints it)
- [ ] A picture or two from `snap.mjs png` in the pull request

## License

By contributing, you agree that your contributions are licensed under the [MIT License](LICENSE).
