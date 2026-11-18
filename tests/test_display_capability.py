import json
import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "windows" / "native-dv" / "Detect-Dependencies.ps1"
POWERSHELL = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"


class DisplayCapabilityProbeTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "DXGI display capability probe is Windows-only")
    def test_active_output_exposes_dxgi_desc1_luminance_and_colour_volume(self):
        display = subprocess.run(
            [
                str(POWERSHELL),
                "-NoProfile",
                "-NonInteractive",
                "-Command",
                "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::PrimaryScreen.DeviceName",
            ],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        if not display.startswith(r"\\.\DISPLAY"):
            self.skipTest("no active Windows desktop output")

        result = subprocess.run(
            [
                str(POWERSHELL),
                "-NoProfile",
                "-NonInteractive",
                "-File",
                str(PROBE),
                "-DisplayName",
                display,
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        data = json.loads(result.stdout)
        dxgi = data.get("dxgi_output")
        if not dxgi:
            self.skipTest("active output does not expose IDXGIOutput6")
        self.assertEqual(dxgi["source"], "dxgi-output-desc1")
        self.assertEqual(dxgi["device_name"].lower(), display.lower())
        for key in (
            "min_luminance_nits",
            "max_luminance_nits",
            "max_full_frame_luminance_nits",
            "bits_per_color",
            "color_space",
            "red_primary",
            "green_primary",
            "blue_primary",
            "white_point",
        ):
            self.assertIn(key, dxgi)
        self.assertIn("display_full_frame_nits", data)


if __name__ == "__main__":
    unittest.main()
