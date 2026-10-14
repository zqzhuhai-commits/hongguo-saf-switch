#!/usr/bin/env python3
"""Assemble the explicitly incomplete, untested resource patch and verify it."""
from pathlib import Path
import hashlib
root=Path(__file__).resolve().parents[2]/'dist/build158'
name='Hongguo-build158-resources-only-untested.nsp'
parts=[root/(name+'.part01'),root/(name+'.part02')]
content=b''.join(p.read_bytes() for p in parts)
expected=(root/'SHA256SUMS').read_text().split()[0]
assert hashlib.sha256(content).hexdigest()==expected, 'NSP checksum mismatch'
(root/name).write_bytes(content)
print('Verified resource-only NSP:', root/name)
print('Playback features pending; no Switch hardware test.')
