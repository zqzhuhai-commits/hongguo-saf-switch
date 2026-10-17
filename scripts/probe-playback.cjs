// SPDX-License-Identifier: GPL-3.0-only
const fs=require('fs'),vm=require('vm'),cp=require('child_process'),crypto=require('crypto');
let seq=0;
async function request(url,opts={}){
 const headerFile=require('path').join(require('os').tmpdir(),'hg-header-'+process.pid+'-'+seq++);
 const args=['-sS','-L','--max-time','25','-D',headerFile,'-w','\n%{http_code}',url];
 for(const [k,v]of Object.entries(opts.headers||{}))args.push('-H',k+': '+v);
 if(opts.body)args.push('--data-binary','@-');
 const r=cp.spawnSync('curl',args,{input:opts.body,maxBuffer:20*1024*1024});
 if(r.status!==0)throw Error('network '+r.status);
 const i=r.stdout.lastIndexOf(10),status=Number(r.stdout.subarray(i+1).toString()),bytes=r.stdout.subarray(0,i),headers={};
 for(const line of fs.readFileSync(headerFile,'utf8').split(/\r?\n/)){const p=line.indexOf(':');if(p>0)headers[line.slice(0,p).toLowerCase()]=line.slice(p+1).trim();}fs.unlinkSync(headerFile);
 if(status<200||status>=300)throw Error('HTTP '+status);
 return {status,headers,data:opts.responseType==='arraybuffer'?bytes:bytes.toString('utf8')};
}
request.post=(u,b,o)=>request(u,{...o,body:b});
const ctx={require,Buffer,URL,URLSearchParams,axios:request,console,nativeCrypto:crypto,SwitchConfig:{source:'phone'}};vm.createContext(ctx);
for(const f of ['hongguo.cjs','phone_source.js','adapter.js'])vm.runInContext(fs.readFileSync('romfs/'+f,'utf8'),ctx);
const http=require('http');
(async()=>{let server;try{
 const detail=await ctx.SwitchAPI.detail('7686713195246930968'),id=detail.episodes[Number(process.argv[2]||1)].id;
 const metadata=await ctx.SwitchAPI.open(id,480,true);let delivered=0;
 server=http.createServer(async(req,res)=>{try{
   const match=String(req.headers.range||'bytes=0-').match(/^bytes=(\d+)-(\d*)$/);if(!match){res.writeHead(416);res.end();return;}
   const start=Number(match[1]),end=Math.min(match[2]?Number(match[2]):metadata.total-1,metadata.total-1);
   res.writeHead(206,{'Content-Type':'video/mp4','Accept-Ranges':'bytes','Content-Length':end-start+1,'Content-Range':`bytes ${start}-${end}/${metadata.total}`});
   for(let at=start;at<=end&&!res.destroyed;at+=1048576){const hex=await ctx.SwitchAPI.read(id,at,Math.min(at+1048575,end));const bytes=Buffer.from(hex,'hex');delivered+=bytes.length;if(!res.write(bytes))await new Promise(resolve=>{res.once('drain',resolve);res.once('close',resolve);});}res.end();
 }catch(e){res.destroy(e);}});
 await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;
 const result=await new Promise((resolve,reject)=>{const child=cp.spawn('ffmpeg',['-v','error','-xerror','-i',`http://127.0.0.1:${port}/video.mp4`,'-t','15','-map','0:v:0','-map','0:a:0','-progress','pipe:1','-f','null','-']);let stdout='',stderr='';child.stdout.on('data',b=>stdout+=b);child.stderr.on('data',b=>stderr+=b);child.on('error',reject);child.on('close',code=>resolve({code,stdout,stderr}));});
 const frames=Number(Array.from(result.stdout.matchAll(/^frame=(\d+)/gm)).at(-1)?.[1]||0);
 const decodedSeconds=Number(Array.from(result.stdout.matchAll(/^out_time_us=(\d+)/gm)).at(-1)?.[1]||0)/1000000;const report={episode:id,source:metadata.source,total:metadata.total,delivered,frames,decodedSeconds,audioAndVideo:true,decodePassed:result.code===0&&frames>0&&decodedSeconds>=14,decoderExit:result.code,hardwareTested:false,createdAt:new Date().toISOString(),commit:cp.execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim()};fs.mkdirSync('build',{recursive:true});fs.writeFileSync('build/playback-probe.json',JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
 if(result.code!==0){console.log(result.stderr.replace(/https?:\/\/[^ ]+/g,'[URL]').slice(0,600));process.exitCode=1;}
}catch(e){console.log(String(e.message).replace(/https?:\/\/[^ ]+/g,'[URL]'));process.exitCode=1;}finally{server?.closeAllConnections();server?.close();}})();
