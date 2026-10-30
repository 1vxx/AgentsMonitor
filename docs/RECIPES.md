# 🤖 Recipes: let your agent do it

You have a tireless engineer open in a terminal, and you installed this project so you could stop staring at them. So put them to work on it.

Each recipe below is a prompt: paste it into Claude Code or Codex and fill in the `<bits>`. The repository tells agents how it works in [AGENTS.md](../AGENTS.md), so the prompts can stay short. Then go watch your show. Your display will tell you when they need you, which is a little bit poetic.

English | [简体中文](RECIPES.zh-CN.md)

> [!IMPORTANT]
> **Three things stay yours.** Type your own Wi-Fi password into `secrets.h`, plug in the board (and hold BOOT if it asks), and read what you approve. A recipe that needs one of these says so, and your agent should stop and ask you.

**Contents:** [Get started](#-get-started) · [Make it yours](#-make-it-yours) · [When something is off](#-when-something-is-off) · [Write your own](#-write-your-own)

## 🚀 Get started

### Install it

```text
Clone https://github.com/1vxx/AgentsMonitor and read its AGENTS.md. Follow the README's
quick start: install the daemon, and connect whichever of Claude Code and Codex I have.
If Rust or anything else is missing, tell me before installing it. Prove it works with
`agentsmon status`, and tell me about any step only I can do, like trusting Codex's hooks.
```

### Look around without any hardware

```text
In AgentsMonitor, build the display's review page, serve it, and open it with the Her
theme. Run scripts/demo.py and point the page at it, so I can see the themes on made-up
sessions. Tell me what to click.
```

### Put it on my desk

```text
In AgentsMonitor, follow "Build the reference display" in devices/README.md for my
Waveshare 1.75C. Generate the token yourself, and set up the config and secrets.h. Stop
and let me type my Wi-Fi name and password into secrets.h: don't ask for them in chat.
Find the board, flash THEME=<halo, pets or her>, then read the serial log for 15 seconds
and tell me whether it connected and at what frame rate.
```

### Ask what is going on

```text
Run `agentsmon status --json` and tell me, in two lines, which of my sessions need me
and what each one wants.
```

## 🎨 Make it yours

### Dream up a theme

```text
Make a new AgentsMonitor display theme: <your idea, the weirder the better>.
Start with devices/tools/new_theme.sh and follow devices/themes/README.md, including its
list of states that must all show. Use `snap.mjs png` to look at what you draw and fix
what looks wrong before showing me. Then run `snap.mjs check` and open it on the review
page.
```

A good brief says what the screen should feel like, not how to draw it. *"A tiny aquarium where every session is a fish, and a waiting one taps on the glass"* beats a paragraph about coordinates.

### Give Her a new face

```text
Set up what devices/themes/her/README.md needs to make photo packs, unless it is already
there, and make a pack named <name> from <path to a photo>. First check the photo against
that guide and tell me if it will cause trouble. When it is done, open it on the review
page. Keep it out of git.
```

Don't have a face? The same guide has a prompt for generating one.

### Fix an expression

```text
In the Her theme, <expression> looks wrong: <what you feel, in plain words>.
Change it in devices/themes/her/faces.py, make the pack again with face_pack.py (only
what changed renders), and show me on the review page.
```

"The mouth looks odd" and "she seems dim" are perfectly good bug reports. That is how every one of her faces was tuned.

### Bring it to another board

```text
Port the AgentsMonitor display to <your board and screen>. Follow "Path A" in
devices/README.md, and find the pin mapping in the board's documentation rather than
guessing. Stop and tell me anything you could not verify.
```

### Make a display out of anything

```text
Using the summary from /v1/summary described in docs/PROTOCOL.md, make <a Raspberry Pi
with an e-ink panel / a menu bar item / a page for my old phone> show what my agents are
doing. Keep it small. Test it against scripts/demo.py.
```

### Teach it another agent

```text
Add support for <agent> to AgentsMonitor, following "Adding an agent" in
docs/ARCHITECTURE.md. Show me how you plan to read its state before you write the code.
```

## 🩺 When something is off

Most problems here are a log away, and your agent is better at reading logs than either of us. Give it the symptom, not a guess.

### My sessions don't show up

```text
AgentsMonitor isn't showing my sessions. Go through its troubleshooting in docs/USAGE.md:
check the daemon, the plugins and the logs, and find out why. Fix what you safely can, and
tell me what only I can do.
```

### The display won't connect

```text
My AgentsMonitor display is stuck on "connecting". Work through the troubleshooting in
devices/README.md. Read the board's serial log, and test the daemon's address and token
from this machine. Don't print the token.
```

### Something looks wrong on the screen

```text
On the <theme> theme, <what you saw, and when>. Reproduce it on the review page or with
snap.mjs, find the cause and fix it, then show me the before and after.
```

### The numbers look wrong

```text
AgentsMonitor says <what it says>, and I expected <what you expected>. Read "Token counts
and usage limits" in docs/USAGE.md, then compare with the daemon's data and tell me which
one is right and why.
```

## ✍️ Write your own

A good prompt here has three parts: what you want, the guide to follow, and how you will know it worked. If yours turns out well, [send it in](../CONTRIBUTING.md). Recipes are the easiest pull request in this repository, and your agent can write that too.
