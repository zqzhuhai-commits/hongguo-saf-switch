from pathlib import Path
import hashlib,json
root=Path('dist/formal-1.0.0');record=json.loads((root/'verification.json').read_text());b=b''.join((root/p).read_bytes() for p in record['parts']);assert len(b)==record['bytes'] and hashlib.sha256(b).hexdigest()==record['sha256'];(root/'Hongguo-Switch-1.0.0.nsp').write_bytes(b);print('NSP assembled and SHA256 verified')
