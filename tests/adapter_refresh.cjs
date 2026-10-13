// SPDX-License-Identifier: GPL-3.0-only
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('romfs/adapter.js','utf8');
let fail=false,requests=0;
const bytes=Buffer.alloc(16);bytes.writeUInt32BE(8,0);bytes.write('ftyp',4);bytes.writeUInt32BE(8,8);bytes.write('moov',12);
const ctx={Buffer,Date,Math,console,SITE:'https://example.test',nativeCrypto:require('node:crypto')};
for(const name of source.match(/^const api=\{([^}]+)\}/)[1].split(','))ctx[name]=()=>{throw Error('unexpected '+name);};
for(const name of ['streamCache','videoInfoCache','pageCache','appSearchCache'])ctx[name]=new Map();
ctx.getText=async()=>{requests++;if(fail)throw Error('temporary network error');return `<script type="application/ld+json">${JSON.stringify({'@type':'VideoObject',about:{'@id':'https://example.test/player/0/1'},contentUrl:'https://example.test/fresh.mp4'})}</script>`;};
ctx.remoteRange=async()=>({data:bytes,total:bytes.length});
ctx.fetchVideoInfo=async()=>{throw Error('video_model failed: 未知异常');};
ctx.streamCache.set('1|desktop-480',{time:Date.now(),value:{url:'https://example.test/stale.mp4',quality:'cached',prepared:{total:16,header:bytes,samples:[]}}});
vm.createContext(ctx);vm.runInContext(source,ctx);
(async()=>{await ctx.SwitchAPI.open('1',480,false);assert.equal(requests,0);const fresh=await ctx.SwitchAPI.open('1',480);assert.equal(requests,1);assert.equal(fresh.source,'web');assert.equal(ctx.streamCache.get('1|desktop-480').value.url,'https://example.test/fresh.mp4');fail=true;await assert.rejects(ctx.SwitchAPI.open('1',480),/video_model failed/);assert.equal(requests,2);await assert.rejects(ctx.SwitchAPI.read('1',0,1),/Range/);console.log('PASS: retry refreshes cached URL; failed refresh cannot serve a previous active stream');})().catch(e=>{console.error(e);process.exitCode=1;});
