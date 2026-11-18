"""Package the generated HTML portal without its separate PDF or build tools."""

import argparse
from pathlib import Path
import stat
import zipfile


def package_portal(site, output):
    site = Path(site).resolve()
    output = Path(output).absolute()
    if output.suffix.lower() != ".zip" or output.resolve().is_relative_to(site):
        raise ValueError("Portal ZIP must be outside the generated site")
    if not (site / "index.html").is_file():
        raise ValueError("Generated portal is missing index.html")
    files = []
    names = set()
    for path in sorted(site.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Symlinks are not allowed in the portal: {path}")
        if path.is_dir():
            continue
        name = path.relative_to(site).as_posix()
        if (not stat.S_ISREG(path.stat().st_mode) or "\\" in name or ":" in name
                or name.casefold() in names):
            raise ValueError(f"Unsafe or duplicate portal file: {name}")
        if path.suffix.lower() == ".pdf":
            raise ValueError("The PDF must be published separately, not inside the portal ZIP")
        names.add(name.casefold())
        files.append((path, name))
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation also refuses dangling output symlinks and preserves any
    # artifact already present at the destination.
    with output.open("xb") as stream:
        try:
            with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
                for path, name in files:
                    archive.write(path, name)
        except BaseException:
            output.unlink()
            raise
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            output.unlink()
            raise ValueError("Portal ZIP CRC validation failed")
    print(f"Validated portal ZIP: {output} ({len(files)} files, {output.stat().st_size:,} bytes)")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("site", type=Path)
    parser.add_argument("output", type=Path)
    arguments = parser.parse_args()
    package_portal(arguments.site, arguments.output)
