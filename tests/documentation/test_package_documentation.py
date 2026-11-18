import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "package_documentation", ROOT / "tools/package_documentation.py"
)
packaging = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packaging)


class PortalFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.site = self.root / "site"
        self.output = self.root / "dist/orion2re-documentation-main.zip"
        self.files = {
            "index.html": b"<a href='manual/index.html'>Manual</a>",
            "manual/index.html": b"Manual",
            "generated/configuration/index.html": b"Generated reference",
            "search/search_index.json": b'{"docs": []}',
            "assets/stylesheets/main.css": b"body { color: black; }",
            "assets/javascripts/search.js": b"// offline search",
        }
        for name, content in self.files.items():
            path = self.site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)


class PortalPackagingTests(PortalFixture):
    def test_complete_portal_at_archive_root_and_separate_pdf(self):
        self.output.parent.mkdir()
        pdf = self.output.with_suffix(".pdf")
        pdf.write_bytes(b"%PDF-1.7 separate artifact")
        packaging.package_portal(self.site, self.output)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(set(archive.namelist()), set(self.files))
            self.assertIsNone(archive.testzip())
            for name, content in self.files.items():
                self.assertEqual(archive.read(name), content)
            self.assertFalse(any(name.endswith(".pdf") for name in archive.namelist()))
        self.assertEqual(pdf.read_bytes(), b"%PDF-1.7 separate artifact")

    def test_missing_index_is_rejected(self):
        (self.site / "index.html").unlink()
        with self.assertRaisesRegex(ValueError, "index.html"):
            packaging.package_portal(self.site, self.output)
        self.assertFalse(self.output.exists())

    def test_pdf_in_portal_is_rejected(self):
        (self.site / "documentation.pdf").write_bytes(b"%PDF-")
        with self.assertRaisesRegex(ValueError, "separately"):
            packaging.package_portal(self.site, self.output)
        self.assertFalse(self.output.exists())

    def test_symlink_is_rejected(self):
        (self.site / "secret").symlink_to(self.root / "private")
        with self.assertRaisesRegex(ValueError, "Symlinks"):
            packaging.package_portal(self.site, self.output)
        self.assertFalse(self.output.exists())

    def test_output_cannot_be_inside_portal(self):
        with self.assertRaises(ValueError):
            packaging.package_portal(self.site, self.site / "portal.zip")

    def test_existing_archive_is_not_overwritten(self):
        packaging.package_portal(self.site, self.output)
        before = self.output.read_bytes()
        with self.assertRaises(FileExistsError):
            packaging.package_portal(self.site, self.output)
        self.assertEqual(self.output.read_bytes(), before)

    def test_dangling_output_symlink_is_not_followed(self):
        self.output.parent.mkdir()
        outside = self.root / "outside"
        self.output.symlink_to(outside)
        with self.assertRaises(FileExistsError):
            packaging.package_portal(self.site, self.output)
        self.assertFalse(outside.exists())


class ReleaseAssetTests(PortalFixture):
    def setUp(self):
        super().setUp()
        import yaml

        workflow = yaml.safe_load((ROOT / ".github/workflows/ci.yml").read_text())
        steps = workflow["jobs"]["publish-release"]["steps"]
        self.validation = next(step["run"] for step in steps
                               if step.get("name") == "Validate complete asset set")
        packaging.package_portal(self.site, self.output)
        for platform in ("win-win32", "win-win64", "osx-arm64"):
            (self.output.parent / f"orion2re-{platform}-main.zip").write_bytes(b"platform fixture")
        self.pdf = self.output.with_suffix(".pdf")
        self.pdf.write_bytes(b"%PDF-1.7 standalone fixture")

    def validate_assets(self):
        return subprocess.run(
            ["bash", "-e", "-o", "pipefail", "-c", self.validation], cwd=self.root,
            env={**os.environ, "RELEASE_TAG": "main"}, capture_output=True, text=True,
        )

    def test_complete_five_asset_set_is_accepted(self):
        result = self.validate_assets()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_portal_is_rejected(self):
        self.output.unlink()
        self.assertNotEqual(self.validate_assets().returncode, 0)

    def test_missing_pdf_is_rejected(self):
        self.pdf.unlink()
        self.assertNotEqual(self.validate_assets().returncode, 0)

    def test_extra_asset_is_rejected(self):
        (self.output.parent / "unexpected.txt").write_text("extra")
        self.assertNotEqual(self.validate_assets().returncode, 0)

    def test_wrong_pdf_signature_is_rejected(self):
        self.pdf.write_bytes(b"not a PDF")
        self.assertNotEqual(self.validate_assets().returncode, 0)

    def test_corrupt_portal_is_rejected(self):
        self.output.write_bytes(b"not a ZIP")
        self.assertNotEqual(self.validate_assets().returncode, 0)


if __name__ == "__main__":
    unittest.main()
