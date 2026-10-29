#!/bin/sh
# Starts a new theme from the template: devices/tools/new_theme.sh <name>
# The name is the theme's folder and its C++ namespace: lowercase letters,
# digits and underscores, starting with a letter.
set -e
cd "$(dirname "$0")/.."
NAME="$1"

case "$NAME" in
  "" | *[!a-z0-9_]* | [!a-z]*)
    echo "usage: devices/tools/new_theme.sh <name>, the name in lowercase letters, digits and _" >&2
    exit 1
    ;;
esac
if [ -e "themes/$NAME" ]; then
  echo "themes/$NAME already exists" >&2
  exit 1
fi

cp -R themes/_template "themes/$NAME"
# Its own namespace and class, so a reader always knows which theme they are in
CLASS=$(printf '%s' "$NAME" | awk -F_ '{for (i = 1; i <= NF; i++) printf "%s%s", toupper(substr($i, 1, 1)), substr($i, 2)}')
sed -e "s/Starter/$CLASS/g" -e "s/starter/$NAME/g" "themes/$NAME/ui.cpp" > "themes/$NAME/ui.cpp.new"
mv "themes/$NAME/ui.cpp.new" "themes/$NAME/ui.cpp"
cat > "themes/$NAME/theme.json" <<EOF
{
  "name": { "en": "$CLASS", "zh": "$CLASS" },
  "about": {
    "en": "One sentence on what this theme shows.",
    "zh": "用一句话说说这个主题显示什么。"
  },
  "author": "$(git config user.name 2>/dev/null || echo "Your name")"
}
EOF

cat <<EOF
Made devices/themes/$NAME from the template.

  1. Describe it in devices/themes/$NAME/theme.json
  2. Try it in the browser:  THEME=$NAME devices/sim/build.sh
  3. Flash it:               THEME=$NAME devices/boards/<board>/build.sh --upload

What a theme implements: devices/core/theme.h
EOF
