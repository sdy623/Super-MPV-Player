from pathlib import Path
import unittest,json
R=Path(__file__).resolve().parents[1]
class ReleaseUiTests(unittest.TestCase):
 def test_version_sources(self):
  self.assertEqual((R/'VERSION').read_text().strip(),'2.1.1')
  self.assertEqual(json.loads((R/'package.json').read_text())['version'],'2.1.1')
  self.assertEqual(json.loads((R/'sources.lock.json').read_text())['runtime_candidate'],'2.1.1')
  self.assertIn("readFileSync",(R/'build.js').read_text())
  self.assertNotIn('git describe',(R/'build.js').read_text())
 def test_font_defaults(self):
  self.assertIn("o.font = 'Segoe UI'",(R/'src/common/scripts/menu/menu.lua').read_text())
  self.assertIn('osd-font=Segoe UI',(R/'src/mpv.conf').read_text())
  self.assertIn('ui-font-defaults',(R/'src/common/scripts/ui-font-defaults.lua').read_text())
  self.assertIn("mp.observe_property('osd-font'",(R/'src/uosc/scripts/uosc/main.lua').read_text())
 def test_visible_version(self):
  self.assertIn('show-config-version',(R/'src/common/scripts/config-update.lua').read_text())
  self.assertIn('show-config-version',(R/'src/input.conf').read_text())
  self.assertIn('config-version',(R/'packaging/windows/make-files.py').read_text())
if __name__=='__main__':unittest.main()
