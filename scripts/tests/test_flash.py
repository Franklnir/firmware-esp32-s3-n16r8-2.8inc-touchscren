import contextlib
import io
import json
from pathlib import Path
import runpy
import shutil
import sys
import tempfile
import types
import unittest
from unittest.mock import patch, Mock


class FlashPackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.script = self.root / "flash.py"
        shutil.copyfile(Path(__file__).resolve().parents[2] / "flash.py", self.script)
        self.build = self.root / "build"
        self.build.mkdir()
        self.manifest = {
            "extra_esptool_args": {"chip": "esp32s3"},
            "write_flash_args": ["--flash_size", "16MB"],
            "flash_files": {"0x20000": "xiaozhi.bin", "0x800000": "assets.bin"},
            "assets": {"offset": "0x800000", "file": "assets.bin"},
        }
        for name in ("xiaozhi.bin", "assets.bin"):
            (self.build / name).write_bytes(b"test")

    def run_flash(self, *args):
        (self.build / "flasher_args.json").write_text(json.dumps(self.manifest))
        serial = types.ModuleType("serial")
        serial.tools = types.ModuleType("serial.tools")
        serial.tools.list_ports = types.ModuleType("serial.tools.list_ports")
        serial_modules = {
            "serial": serial,
            "serial.tools": serial.tools,
            "serial.tools.list_ports": serial.tools.list_ports,
        }
        with patch.object(sys, "argv", [str(self.script), *args]), \
                patch.dict(sys.modules, serial_modules), \
                contextlib.redirect_stdout(io.StringIO()):
            runpy.run_path(str(self.script), run_name="__main__")

    def test_full_flash_includes_assets_and_uses_requested_port(self):
        with patch("subprocess.run", return_value=Mock(returncode=0, stdout="OK")) as run:
            self.run_flash("COM9")
        command = run.call_args.args[0]
        self.assertIn("COM9", command)
        self.assertIn("0x800000", command)
        self.assertIn(str(self.build / "assets.bin"), command)
        self.assertIn(str(self.build / "xiaozhi.bin"), command)
        run.assert_called_once()

    def test_dry_run_never_flashes(self):
        with patch("subprocess.run") as run, self.assertRaises(SystemExit) as result:
            self.run_flash("--dry-run")
        self.assertEqual(result.exception.code, 0)
        run.assert_not_called()

    def test_missing_assets_fails_before_flash(self):
        (self.build / "assets.bin").unlink()
        with patch("subprocess.run") as run, self.assertRaises(SystemExit):
            self.run_flash("COM9")
        run.assert_not_called()

    def test_manifest_without_assets_is_rejected(self):
        del self.manifest["assets"]
        with patch("subprocess.run") as run, self.assertRaises(SystemExit):
            self.run_flash("COM9")
        run.assert_not_called()
