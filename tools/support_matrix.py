#!/usr/bin/env python3
"""Validate support.yaml and render the support table into README.md.

  --check   fail if support.yaml is invalid or README.md is out of date (CI)
  --write   rewrite the table in README.md
  (none)    print the table
"""
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
START = "<!-- support-matrix:start -->"
END = "<!-- support-matrix:end -->"
STATUSES = {
    "verified": "✅",
    "works-in-ci": "🧪",
    "in-progress": "🚧",
    "planned": "⬜",
    "blocked": "⛔",
    "post-v1": "—",
}
LEGEND = ("✅ verified on hardware · 🧪 builds and passes tests, not yet verified · "
          "🚧 in progress · ⬜ planned for v1 · ⛔ blocked · — after v1")


def load():
    data = yaml.safe_load((ROOT / "support.yaml").read_text(encoding="utf-8"))
    errors = []
    consoles = {c["id"]: c for c in data["consoles"]}
    controllers = {c["id"]: c for c in data["controllers"]}
    cells = {}
    for i, cell in enumerate(data.get("cells") or []):
        where = f"cells[{i}]"
        if cell.get("console") not in consoles:
            errors.append(f"{where}: unknown console {cell.get('console')!r}")
        if cell.get("controller") not in controllers:
            errors.append(f"{where}: unknown controller {cell.get('controller')!r}")
        status = cell.get("status")
        if status not in STATUSES:
            errors.append(f"{where}: unknown status {status!r}")
        if status == "verified" and not (cell.get("verified_by") and cell.get("firmware")):
            errors.append(f"{where}: verified needs verified_by and firmware")
        key = (cell.get("controller"), cell.get("console"))
        if key in cells:
            errors.append(f"{where}: duplicate cell {key}")
        cells[key] = cell
    return data, cells, errors


def render(data, cells):
    consoles = data["consoles"]
    lines = [
        "| Controller | " + " | ".join(c["name"] for c in consoles) + " |",
        "| --- |" + " :---: |" * len(consoles),
    ]
    for ctrl in data["controllers"]:
        row = []
        for con in consoles:
            cell = cells.get((ctrl["id"], con["id"]))
            default = "planned" if con["milestone"] == "v1" else "post-v1"
            row.append(STATUSES[cell["status"] if cell else default])
        lines.append(f"| {ctrl['name']} | " + " | ".join(row) + " |")
    lines += ["", LEGEND]
    return "\n".join(lines)


def splice(readme, table):
    head, rest = readme.split(START, 1)
    _, tail = rest.split(END, 1)
    return f"{head}{START}\n{table}\n{END}{tail}"


def main():
    data, cells, errors = load()
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    table = render(data, cells)
    readme_path = ROOT / "README.md"
    readme = readme_path.read_text(encoding="utf-8")
    if "--check" in sys.argv:
        if splice(readme, table) != readme:
            print("README.md support table is out of date: run tools/support_matrix.py --write",
                  file=sys.stderr)
            return 1
        return 0
    if "--write" in sys.argv:
        readme_path.write_text(splice(readme, table), encoding="utf-8")
        return 0
    print(table)
    return 0


if __name__ == "__main__":
    sys.exit(main())
