#!/bin/sh
# Hands the hook payload on stdin to `agentsmon hook`, which sends it to the
# daemon and answers Codex with `{}`. The Codex app started from the Dock has
# a short PATH, so the usual install places are tried as well.
for bin in "$AGENTSMON_BIN" "$(command -v agentsmon)" "$HOME/.cargo/bin/agentsmon" /opt/homebrew/bin/agentsmon /usr/local/bin/agentsmon; do
  [ -n "$bin" ] && [ -x "$bin" ] && exec "$bin" hook --agent codex
done
# Not installed: answer as it would, so the session is never held up.
cat >/dev/null
echo '{}'
