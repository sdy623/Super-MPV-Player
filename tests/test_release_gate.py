import unittest
from pathlib import Path
import sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from release_gate import inspect_tree
class ReleaseGateTests(unittest.TestCase):
 def check(self,name,body):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(body)
   return inspect_tree(Path(d))
 def test_source_allowed(self): self.assertEqual(self.check('src/player.c',b'/* public source */'),[])
 def test_binary_rejected_even_renamed(self): self.assertTrue(self.check('src/image.txt',b'MZ\x00\x00test'))
 def test_dll_rejected(self): self.assertTrue(self.check('runtime/Dolby.dll',b'anything'))
 def test_credentials_rejected(self): self.assertTrue(self.check('KEYDB.cfg',b'key'))
 def test_profile_rejected(self): self.assertTrue(self.check('display.icc',b'profile'))
 def test_private_path_rejected(self): self.assertTrue(self.check('config.conf',b'C:\\Users\\Someone\\cookies.txt'))
 def test_archive_rejected(self): self.assertTrue(self.check('old.zip',b'PK\x03\x04'))
if __name__=='__main__': unittest.main()
