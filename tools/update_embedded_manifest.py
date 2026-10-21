"""Refresh the resource hash manifest after intentional changes to bundled assets."""
import hashlib
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
manifest = root / "data/embedded_assets.manifest.json"
document = json.loads(manifest.read_text(encoding="utf-8"))
assets = []
inventory = (root / "data/embedded_assets.inc").read_text(encoding="utf-8")
for resource_id, name, native in re.findall(r'EMBED_ASSET\((\d+),\s*"([^"]+)",\s*(true|false)\)', inventory):
    data = (root / name).read_bytes()
    assets.append({"id": int(resource_id), "path": name, "native": native == "true",
                   "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
document["assets"] = assets
manifest.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
print(f"Recorded {len(assets)} embedded asset hashes")
