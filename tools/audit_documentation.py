"""Check the pinned documentation packages against the OSV advisory database."""

import argparse
import json
from pathlib import Path
import re
import urllib.request


def locked_packages(paths):
    packages = set()
    for path in paths:
        for line in path.read_text(encoding="utf-8").splitlines():
            match = re.match(r"^([A-Za-z0-9_.-]+)(?:\[[^]]+\])?==([^\s;\\]+)", line)
            if match:
                packages.add(match.groups())
    if not packages:
        raise ValueError("No pinned documentation packages found")
    return sorted(packages)


def audit(paths):
    packages = locked_packages(paths)
    queries = [{"package": {"name": name, "ecosystem": "PyPI"}, "version": version}
               for name, version in packages]
    request = urllib.request.Request(
        "https://api.osv.dev/v1/querybatch",
        data=json.dumps({"queries": queries}).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        results = json.load(response)["results"]
    if len(results) != len(packages):
        raise ValueError("Incomplete OSV advisory response")
    findings = []
    for (name, version), result in zip(packages, results):
        if result.get("error") or result.get("next_page_token"):
            raise ValueError(f"Incomplete OSV advisory response for {name}")
        if result.get("vulns"):
            findings.append(f"{name}=={version}: " + ", ".join(v["id"] for v in result["vulns"]))
    if findings:
        raise ValueError("Known vulnerable documentation packages:\n" + "\n".join(findings))
    print(f"OSV: no known advisories for {len(packages)} pinned package versions")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("locks", type=Path, nargs="+")
    audit(parser.parse_args().locks)
