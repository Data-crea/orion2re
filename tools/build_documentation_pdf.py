"""Build and validate a single PDF without fetching remote rendering resources."""

import argparse
import logging
import os
from pathlib import Path
import shutil
import subprocess
import sys
from urllib.parse import urlsplit
from urllib.request import url2pathname


def check_resource(url, site_directory):
    parsed = urlsplit(url)
    if parsed.scheme == "data":
        return
    if parsed.scheme == "file" and parsed.netloc in ("", "localhost"):
        path = Path(url2pathname(parsed.path)).resolve()
        if path.is_relative_to(site_directory.resolve()):
            return
    raise ValueError(f"PDF resource outside the generated site: {url}")


def validate_pdf(path):
    from pypdf import PdfReader

    reader = PdfReader(path, strict=True)
    if reader.is_encrypted or not reader.pages or not reader.outline:
        raise ValueError("Expected an unencrypted PDF with pages and bookmarks")
    names = reader.trailer["/Root"].get("/Names", {})
    if hasattr(names, "get_object"):
        names = names.get_object()
    if "/EmbeddedFiles" in names or "/JavaScript" in names:
        raise ValueError("PDF must not contain attachments or JavaScript")
    if reader.trailer["/Root"].get("/OpenAction"):
        raise ValueError("PDF must not contain an automatic open action")
    return len(reader.pages)


def build(config_file, site_directory, output):
    from mkdocs.commands.build import build as mkdocs_build
    from mkdocs.config import load_config

    config = load_config(config_file=str(config_file), site_dir=str(site_directory), strict=True)
    options = config.plugins["to-pdf"].config
    if options["render_js"] or options.get("theme_handler_path"):
        raise ValueError("PDF JavaScript rendering and custom theme handlers are disabled")
    # These guards constrain resource fetching, not arbitrary Python hooks.
    # The entire documentation checkout must still be treated as executable code.
    blocked = []

    def audit(event, arguments):
        try:
            if event == "urllib.Request":
                check_resource(arguments[0], site_directory)
            elif event == "socket.connect":
                raise ValueError("Network access is disabled during the PDF build")
        except ValueError as error:
            blocked.append(str(error))
            raise

    sys.addaudithook(audit)
    if sys.platform.startswith("linux"):
        import resource

        resource.setrlimit(resource.RLIMIT_AS, (4 * 1024**3, 4 * 1024**3))
    mkdocs_build(config)
    if blocked:
        raise ValueError("Blocked PDF resources:\n" + "\n".join(blocked))
    pdf = site_directory / options["output_path"]
    pages = validate_pdf(pdf)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".pdf.tmp")
    shutil.copyfile(pdf, temporary)
    temporary.replace(output)
    print(f"Validated PDF: {output} ({pages} pages, {output.stat().st_size:,} bytes)")


def main():
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--site-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.worker:
        os.environ.pop("ORION2RE_CONFIG_DOCS_BUILD_DIR", None)
        build(arguments.config.resolve(), arguments.site_dir.resolve(), arguments.output.resolve())
    else:
        subprocess.run([sys.executable, str(Path(__file__).resolve()),
                        *sys.argv[1:], "--worker"], check=True, timeout=600)


if __name__ == "__main__":
    main()
