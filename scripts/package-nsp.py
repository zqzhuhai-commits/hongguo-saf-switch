"""Package only a successfully decoded build. Never print or copy key contents."""
import argparse,json,pathlib,subprocess,shutil,tempfile
p=argparse.ArgumentParser()
p.add_argument('--package',required=True,type=pathlib.Path)
p.add_argument('--probe',required=True,type=pathlib.Path)
p.add_argument('--keyset',required=True,type=pathlib.Path)
p.add_argument('--tool',default='hacbrewpack')
p.add_argument('--output',required=True,type=pathlib.Path)
a=p.parse_args()
r=json.loads(a.probe.read_text())
commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
if not (r.get('decodePassed') and r.get('audioAndVideo') and r.get('frames',0)>0 and r.get('decodedSeconds',0)>=14 and r.get('commit')==commit):
    raise SystemExit('Playback decode probe did not pass for this exact commit; NSP was not generated.')
if not a.keyset.is_file():raise SystemExit('NSP requires a local keyset file containing header_key and key_area_key_application_00.')
for path in ['exefs/main','exefs/main.npdm','control/control.nacp']:
    if not (a.package/path).is_file():raise SystemExit('Incomplete Switch package inputs: '+path)
if not (a.package/'romfs/phone_source.js').is_file():raise SystemExit('New playback adapter missing from package inputs.')
a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='hongguo-nsp-') as temp:
    result=subprocess.run([a.tool,'--keyset',str(a.keyset.resolve()),'--exefsdir',str((a.package/'exefs').resolve()),'--romfsdir',str((a.package/'romfs').resolve()),'--controldir',str((a.package/'control').resolve()),'--nologo','--nspdir',str(a.output.resolve()),'--tempdir',temp+'/temp','--ncadir',temp+'/nca'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if result.returncode:raise SystemExit('NSP packaging failed; check local keyset and hacbrewpack. Key and tool output were not logged.')
    files=list(a.output.glob('*.nsp'))
    if not files or not all(f.stat().st_size>0 for f in files):raise SystemExit('Packaging returned no NSP file.')
print('NSP generated after successful audio/video decode probe. Switch hardware playback remains unverified.')
