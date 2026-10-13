"""Read-only live check of the user's public first-episode example. No private keys."""
import json, re, ssl, urllib.request
from pathlib import Path
from html.parser import HTMLParser
SITE='https://hongguoduanju.com'
SERIES='7686713195246930968'
EPISODE='7686715217652223038'
PAGE=f'{SITE}/player/{SERIES}'
class Schemas(HTMLParser):
    def __init__(self):super().__init__();self.active=False;self.chunks=[];self.nodes=[]
    def handle_starttag(self,tag,attrs):
        if tag=='script' and dict(attrs).get('type')=='application/ld+json':self.active=True;self.chunks=[]
    def handle_data(self,data):
        if self.active:self.chunks.append(data)
    def handle_endtag(self,tag):
        if tag=='script' and self.active:
            self.active=False
            try:self.nodes.append(json.loads(''.join(self.chunks)))
            except ValueError:pass
report={'seriesId':SERIES,'episodeId':EPISODE,'hardwareTested':False,'liveProbePassed':False}
headers={'User-Agent':'Mozilla/5.0','Referer':PAGE,'Accept-Encoding':'identity'}
try:
    with urllib.request.urlopen(urllib.request.Request(PAGE,headers=headers),timeout=25,context=ssl.create_default_context()) as response:
        html=response.read(2*1024*1024).decode('utf-8')
    parser=Schemas();parser.feed(html)
    video=next(n for n in parser.nodes if n.get('@type')=='VideoObject' and n.get('contentUrl','').startswith(f'{SITE}/video/public/{EPISODE}/'))
    report['canonicalSchemaUsesSeriesId']=video['about']['@id'].split('#')[0]==PAGE
    checks=[]
    for start,end in [(0,262143),(1048576,1049599)]:
        with urllib.request.urlopen(urllib.request.Request(video['contentUrl'],headers={**headers,'Range':f'bytes={start}-{end}'}),timeout=25,context=ssl.create_default_context()) as response:
            body=response.read(1048577);status=response.status;content_range=response.headers.get('Content-Range','')
        match=re.fullmatch(r'bytes (\d+)-(\d+)/(\d+)',content_range)
        assert status==206 and match,'Public video did not return a valid partial response'
        a,b,total=map(int,match.groups());assert a==start and b==min(end,total-1) and len(body)==b-a+1
        if start==0:
            assert body[4:8]==b'ftyp'
            assert b'encv' not in body and b'enca' not in body
        checks.append({'start':start,'bytes':len(body),'total':total,'status':status,'rangePassed':True})
    report.update(liveProbePassed=True,ranges=checks,publicMP4HeaderPassed=True)
except Exception as error:
    # Do not expose server response bodies or signed/redirect URLs in logs.
    report['errorType']=type(error).__name__
Path('build').mkdir(exist_ok=True)
Path('build/official-player-probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(report,ensure_ascii=False))
