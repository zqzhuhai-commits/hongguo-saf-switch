#!/usr/bin/env python3
"""Resource-only patch for the user's build158, not a feature implementation.
Input main must be decompressed by hactool. Never accepts or writes keys.
"""
import argparse, ctypes, ctypes.util, hashlib, json, shutil, struct
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser()
p.add_argument('extracted',type=Path)
p.add_argument('uncompressed_main',type=Path)
p.add_argument('icon',type=Path)
p.add_argument('config',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args()
assert hashlib.sha256((a.extracted/'exefs/main').read_bytes()).hexdigest() == 'ca2d817d8275c6712feb23b34093e89c37696e98f5464cebef3f08294b0c7d01'
shutil.copytree(a.extracted/'exefs',a.output/'exefs',dirs_exist_ok=True)
shutil.copytree(a.extracted/'romfs',a.output/'romfs',dirs_exist_ok=True)
shutil.copytree(a.extracted/'control',a.output/'control',dirs_exist_ok=True)
b=bytearray(a.uncompressed_main.read_bytes()); original=bytes(b)
assert b[:4]==b'NSO0' and not (struct.unpack_from('<I',b,12)[0]&7)
old=b'sdmc:/hongguo.cfg\0';new=b'romfs:/hongguo.cfg\0'
assert b.count(old)==1
at=b.index(old)
# Use verified zero padding, without changing offsets or neighboring data.
assert b[at+len(old):at+len(new)]==bytes(len(new)-len(old))
b[at:at+len(new)]=new
assert b.count(b'sdmc:/switch/hongguo.cfg\0')==1
sections=[]
for i in range(3):
 off,mem,size,extra=struct.unpack_from('<IIII',b,0x10+16*i)
 sections.append(bytes(b[off:off+size]))
 if i==0:assert original[off:off+size]==b[off:off+size]
 b[0xa0+32*i:0xc0+32*i]=hashlib.sha256(sections[-1]).digest()
# Recompress all sections. Section addresses and sizes stay unchanged.
lz4=ctypes.CDLL(ctypes.util.find_library('lz4'))
lz4.LZ4_compressBound.argtypes=[ctypes.c_int]
lz4.LZ4_compress_default.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int]
header=bytearray(b[:0x100]);payload=bytearray()
struct.pack_into('<I',header,12,struct.unpack_from('<I',header,12)[0]|7)
for i,section in enumerate(sections):
 src=ctypes.create_string_buffer(section);cap=lz4.LZ4_compressBound(len(section));dest=ctypes.create_string_buffer(cap)
 size=lz4.LZ4_compress_default(src,dest,len(section),cap);assert size>0
 struct.pack_into('<I',header,0x10+16*i,0x100+len(payload))
 struct.pack_into('<I',header,0x60+4*i,size)
 payload+=dest.raw[:size]
(a.output/'exefs/main').write_bytes(header+payload)
n=bytearray((a.output/'control/control.nacp').read_bytes());assert len(n)==0x4000
for i in range(16):
 name='红果短剧'.encode()+b'\0';n[i*0x300:i*0x300+0x200]=name.ljust(0x200,b'\0')
(a.output/'control/control.nacp').write_bytes(n)
Image.open(a.icon).convert('RGB').resize((256,256),Image.Resampling.LANCZOS).save(a.output/'control/icon_AmericanEnglish.dat',format='JPEG',quality=95)
config=a.config.read_bytes()
# The native build supports the language option; resource fallback selects Chinese.
config+=b'\nlanguage=zh\n'
(a.output/'romfs/hongguo.cfg').write_bytes(config)
(a.output/'resource-patch.json').write_text(json.dumps({'inputBuild':'build158','patchType':'resources-only','codeSegmentUnchanged':True,'name':'红果短剧','configFallback':'romfs:/hongguo.cfg','externalConfig':'sdmc:/switch/hongguo.cfg','hardwareTested':False,'newPlaybackFeaturesImplemented':False,'note':'Bundled fallback is read-only. Use external config to persist settings.'},ensure_ascii=False,indent=2)+'\n')
print('Resource patch prepared; executable code unchanged; hardware untested.')
