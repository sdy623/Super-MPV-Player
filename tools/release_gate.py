from pathlib import Path
import json,re,sys,subprocess
DENIED={'.exe','.dll','.pdb','.lib','.obj','.zip','.7z','.gz','.tar','.icc','.icm','.mkv','.m2ts','.mp4','.iso','.pfx','.p12','.pem','.key','.pyc'}
def inspect_tree(root, tracked=False):
 issues=[]
 paths=[root/p for p in subprocess.check_output(['git','-C',str(root),'ls-files','-z']).decode().split('\0') if p] if tracked else root.rglob('*')
 for p in sorted(paths):
  rel=p.relative_to(root)
  if '.git' in rel.parts or (not tracked and '__pycache__' in rel.parts):continue
  if p.is_symlink():issues.append([str(rel),'symlink']);continue
  if not p.is_file():continue
  low=p.name.lower();data=p.read_bytes()
  if p.suffix.lower() in DENIED or low in {'keydb.cfg','cookies.txt','.env'} or low.endswith('.live.yaml'):
   issues.append([str(rel),'excluded artifact']);continue
  if data[:2]==b'MZ' or data[:4] in (b'PK\x03\x04',b'\x7fELF'):
   issues.append([str(rel),'executable/archive magic']);continue
  text=data.decode('utf-8',errors='replace')
  if re.search(r'[A-Za-z]:[\\/]Users[\\/][^\s\\/]+',text,re.I):issues.append([str(rel),'personal absolute path'])
  if re.search(r'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|-----BEGIN [A-Z ]*PRIVATE KEY-----[\r\n]+[A-Za-z0-9+/=]{16,}',text):issues.append([str(rel),'credential pattern'])
 return issues
if __name__=='__main__':
 findings=inspect_tree(Path(sys.argv[1]),'--tracked' in sys.argv);print(json.dumps(findings,ensure_ascii=False,indent=2));sys.exit(bool(findings))
