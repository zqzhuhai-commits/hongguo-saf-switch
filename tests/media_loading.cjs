// SPDX-License-Identifier: GPL-3.0-only
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('romfs/adapter.js','utf8');
function box(type,length=8){const b=Buffer.alloc(length);b.writeUInt32BE(length);b.write(type,4);return b;}
const media=Buffer.concat([box('ftyp',24),box('free',32),box('mdat',300000),box('moov',32)]);
function context(){
  const ctx={Buffer,Date,Math,console,SITE:'https://example.test',VIDEO_UA:'test',nativeCrypto:require('node:crypto')};
  for(const name of source.match(/^const api=\{([^}]+)\}/)[1].split(','))ctx[name]=()=>{throw Error('unexpected '+name);};
  for(const name of ['streamCache','videoInfoCache','pageCache','appSearchCache'])ctx[name]=new Map();
  ctx.getText=async()=>`<script type="application/ld+json">${JSON.stringify({'@type':'VideoObject',about:{'@id':'https://example.test/player/0/1'},contentUrl:'https://example.test/video.mp4'})}</script>`;
  ctx.decryptPartial=(data)=>data;ctx.qualityNumber=Number;ctx.qualityLabel=String;
  ctx.axios=async(_url,opts)=>{const [a,b]=opts.headers.Range.slice(6).split('-').map(Number),end=Math.min(b,media.length-1);return {status:206,headers:{'content-range':`bytes ${a}-${end}/${media.length}`},data:media.subarray(a,end+1)};};
  ctx.SwitchConfig={source:'web'};vm.createContext(ctx);vm.runInContext(source,ctx);return ctx;
}
(async()=>{
  let ctx=context(),ranges=[];const transport=ctx.axios;
  ctx.axios=async(u,o)=>{ranges.push(o.headers.Range);return transport(u,o);};
  const meta=await ctx.SwitchAPI.open('1');assert.equal(meta.total,media.length);assert.equal(meta.source,'web');
  assert(ranges.some(r=>r.startsWith('bytes=300056-')),'must seek directly past mdat');
  const tail=await ctx.SwitchAPI.read('1',media.length-10,media.length+99);assert.equal(tail,media.subarray(-10).toString('hex'));
  ctx=context();let calls=0;const original=ctx.axios;
  ctx.axios=async(u,o)=>{if(++calls===1)throw Error('disconnect');return original(u,o);};
  assert.equal((await ctx.SwitchAPI.open('1')).source,'web');assert(calls>1);
  calls=0;ctx.axios=async(u,o)=>{calls++;const r=await original(u,o);r.headers['content-range']='bytes 0-9/'+media.length;return r;};
  await assert.rejects(ctx.SwitchAPI.read('1',264000,264009),/位置异常/);assert.equal(calls,2,'bounded range retries');
  ctx=context();ctx.getText=async()=>{throw Error('website unavailable');};
  ctx.fetchVideoInfo=async()=>({info:{},rows:[{quality:'480',item:{codec_type:'avc',main_url:'https://example.test/broken',backup_url:'https://example.test/backup'}}]});
  const base=ctx.axios;ctx.axios=async(u,o)=>{if(u.endsWith('/broken'))throw Error('CDN down');return base(u,o);};
  vm.runInContext('api.getText=getText;api.fetchVideoInfo=fetchVideoInfo;',ctx);assert.equal((await ctx.SwitchAPI.open('1')).quality,'480');assert.equal(ctx.streamCache.get('1|desktop-480').value.url,'https://example.test/backup');
  ctx=context();ctx.getText=async()=>`<script type="application/ld+json">${JSON.stringify({'@type':'VideoObject',about:{'@id':'https://example.test/player/0/21'},contentUrl:'https://example.test/wrong.mp4'})}</script>`;
  ctx.fetchVideoInfo=async()=>{throw Error('no source');};vm.runInContext('api.getText=getText;api.fetchVideoInfo=fetchVideoInfo;',ctx);await assert.rejects(ctx.SwitchAPI.open('1'),/no source/);
  ctx=context();const encrypted=Buffer.concat([box('ftyp',24),box('moov',32)]);encrypted.write('encv',40);
  ctx.axios=async(_u,o)=>{const [a,b]=o.headers.Range.slice(6).split('-').map(Number),end=Math.min(b,encrypted.length-1);return {status:206,headers:{'content-range':`bytes ${a}-${end}/${encrypted.length}`},data:encrypted.subarray(a,end+1)};};
  ctx.fetchVideoInfo=async()=>{throw Error('encrypted public resource rejected');};vm.runInContext('api.fetchVideoInfo=fetchVideoInfo;',ctx);await assert.rejects(ctx.SwitchAPI.open('1'),/encrypted public resource rejected/);
  console.log('PASS: tail moov/free boxes, short final range, bounded segment retry, invalid offset rejection, backup CDN, exact episode matching, encrypted public resource rejection');
})().catch(e=>{console.error(e);process.exitCode=1;});
