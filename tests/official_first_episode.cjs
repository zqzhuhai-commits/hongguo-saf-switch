// SPDX-License-Identifier: GPL-3.0-only
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('romfs/adapter.js','utf8');
const series='7686713195246930968',episode='7686715217652223038',site='https://hongguoduanju.com';
const bytes=Buffer.alloc(24);bytes.writeUInt32BE(16);bytes.write('ftyp',4);bytes.write('isom',8);bytes.writeUInt32BE(8,16);bytes.write('moov',20);
const ctx={Buffer,Date,Math,console,SITE:site,VIDEO_UA:'Android app UA',nativeCrypto:require('node:crypto')};
for(const name of source.match(/^const api=\{([^}]+)\}/)[1].split(','))ctx[name]=()=>{throw Error('unexpected '+name);};
for(const name of ['streamCache','videoInfoCache','pageCache','appSearchCache'])ctx[name]=new Map();
ctx.itemToCard=item=>({url:item.series_id,title:item.series_name});
ctx.routerData=async()=>({loaderData:{detail_page:{seriesDetail:{series_id:series,series_name:'首辅娇娘',vid_list:[episode,'7686715221565508670']}}}});
let wrongEpisode=false,requests=0;
ctx.getText=async url=>{assert.equal(url,site+'/player/'+series,'first episode uses canonical series page');return '<script type="application/ld+json">'+JSON.stringify({'@type':'VideoObject','@id':site+'/player/'+series+'#video',about:{'@id':site+'/player/'+series+'#episode'},contentUrl:site+'/video/public/'+(wrongEpisode?'7686715221565508670':episode)+'/360p.mp4'})+'</script>';};
ctx.axios=async(url,opts)=>{requests++;assert.equal(url,site+'/video/public/'+episode+'/360p.mp4');assert.equal(opts.headers['User-Agent'],'Mozilla/5.0');assert.equal(opts.headers.Referer,site+'/player/'+series);const [a,b]=opts.headers.Range.slice(6).split('-').map(Number),end=Math.min(b,bytes.length-1);return {status:206,headers:{'content-range':`bytes ${a}-${end}/${bytes.length}`},data:bytes.subarray(a,end+1)};};
ctx.fetchVideoInfo=async()=>{throw Error('APP video model unavailable');};ctx.decryptPartial=data=>data;
ctx.SwitchConfig={source:'web'};vm.createContext(ctx);vm.runInContext(source,ctx);
(async()=>{const detail=await ctx.SwitchAPI.detail(series);assert.equal(detail.episodes[0].id,episode);const meta=await ctx.SwitchAPI.open(episode);assert.equal(meta.source,'web');assert(requests>0);assert.equal(await ctx.SwitchAPI.read(episode,0,23),bytes.toString('hex'));wrongEpisode=true;const previous=requests;await assert.rejects(ctx.SwitchAPI.open(episode),/APP video model unavailable/);assert.equal(requests,previous,'unrelated episode URL is rejected');console.log('PASS: 首辅娇娘 first canonical schema resolves exact official video ID with website headers, without APP API; unrelated episode rejected');})().catch(e=>{console.error(e);process.exitCode=1;});
