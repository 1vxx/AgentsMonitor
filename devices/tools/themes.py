"""Checks every theme's theme.json and lists the themes for the review page.

    python3 -I devices/tools/themes.py <themes dir> <out.json>

Each devices/themes/<name>/ needs a theme.json:

    {
      "name": {"en": "Halo", "zh": "光环"},
      "about": {"en": "One sentence on what it shows.", "zh": "……"},
      "author": "Who made it"
    }

The list puts halo, the default, first and the rest by name. Folders whose
name starts with "_" (the template) are checked but not listed.
"""

import json
import sys
from pathlib import Path

LANGS = ("en", "zh")
DEFAULT = "halo"


def check(folder):
    path = folder / "theme.json"
    if not path.is_file():
        return None, f"{folder.name}: no theme.json"
    try:
        meta = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        return None, f"{path}: {e}"
    problems = []
    for field in ("name", "about"):
        value = meta.get(field)
        if not isinstance(value, dict) or any(not isinstance(value.get(l), str) or not value[l].strip() for l in LANGS):
            problems.append(f'"{field}" needs text for each of {", ".join(LANGS)}')
    if not isinstance(meta.get("author"), str) or not meta["author"].strip():
        problems.append('"author" needs a name')
    if not any(folder.glob("*.cpp")):
        problems.append("no .cpp sources")
    if problems:
        return None, f"{path}: " + "; ".join(problems)
    return {"id": folder.name, "name": meta["name"], "about": meta["about"], "author": meta["author"]}, None


def main(themes_dir, out):
    listed, errors = [], []
    for folder in sorted(Path(themes_dir).iterdir()):
        if not folder.is_dir():
            continue
        theme, error = check(folder)
        if error:
            errors.append(error)
        elif not folder.name.startswith("_"):
            listed.append(theme)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        sys.exit(1)
    listed.sort(key=lambda t: (t["id"] != DEFAULT, t["id"]))
    Path(out).write_text(json.dumps(listed, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
