from pathlib import Path
import hashlib,json
root=Path('dist/full-0.1.9');record=json.loads((root/'verification.json').read_text())
parts=record['parts'];name='Hongguo-Switch-0.1.9-test.1.nsp';b=b''.join((root/p).read_bytes() for p in parts)
assert len(b)==record['bytes'] and hashlib.sha256(b).hexdigest()==record['sha256']
(root/name).write_bytes(b)
print('NSP assembled and SHA256 verified:',name)
