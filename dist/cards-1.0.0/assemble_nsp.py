from pathlib import Path
import hashlib,json
root=Path('dist/cards-1.0.0');record=json.loads((root/'verification.json').read_text());data=b''.join((root/p).read_bytes() for p in record['parts']);assert len(data)==record['bytes'] and hashlib.sha256(data).hexdigest()==record['sha256'];(root/'Hongguo-Switch-1.0.0-cards.nsp').write_bytes(data);print('NSP assembled and SHA256 verified')
