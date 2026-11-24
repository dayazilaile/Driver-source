"""Rebuild the offline VID/PID candidate index from the bundled OTD configurations."""
import json
import os
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
brands = {
    "Wacom": "wacom", "Huion": "huion", "XP-Pen": "xppen", "UGEE": "ugee",
    "Gaomon": "gaomon", "VEIKK": "veikk", "XenceLabs": "xencelabs",
    "Parblo": "parblo", "Artisul": "artisul", "Bosto": "bosto",
}
records = set()
models = set()
for path in sorted((ROOT / "database/tablet-configs").rglob("*.json")):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    name = data["Name"]
    if any(c in name for c in "\t\r\n"):
        raise ValueError(f"Invalid model name in {path}")
    models.add(name)
    for identifier in data.get("DigitizerIdentifiers", []):
        vid, pid = identifier.get("VendorID"), identifier.get("ProductID")
        if not isinstance(vid, int) or not isinstance(pid, int) or not (0 <= vid <= 65535 and 0 <= pid <= 65535):
            raise ValueError(f"Invalid USB ID in {path}")
        brand = path.relative_to(ROOT / "database/tablet-configs").parts[0]
        records.add((vid, pid, brands.get(brand, "-"), name))
lines = ["# VID\tPID\tmanufacturer source key\tmodel candidate",
         "# Derived from OpenTabletDriver configurations. See database/README.md and licenses.",
         "# A VID/PID match identifies candidates. Additional device-string/report probing may be required."]
lines.extend(f"{vid:04X}\t{pid:04X}\t{brand}\t{name}" for vid, pid, brand, name in sorted(records))
target = ROOT / "database/tablets.tsv"
with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=target.parent, suffix=".tmp", delete=False) as stream:
    temporary = Path(stream.name)
    stream.write("\n".join(lines) + "\n")
try:
    os.replace(temporary, target)
finally:
    temporary.unlink(missing_ok=True)
print(f"{len(models)} model names, {len(records)} model/VID/PID mappings")
