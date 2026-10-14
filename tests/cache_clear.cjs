const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync('romfs/adapter.js','utf8'),ctx={Buffer,Date,Math,console,nativeCrypto:require('crypto')};
for(const n of source.match(/^const api=\{([^}]+)\}/)[1].split(','))ctx[n]=()=>{};
for(const n of ['streamCache','videoInfoCache','pageCache','appSearchCache'])ctx[n]=new Map([['entry',{}]]);
vm.createContext(ctx);vm.runInContext(source,ctx);
(async()=>{assert.equal(ctx.SwitchAPI.clearCache().entries,2);assert.equal(ctx.streamCache.size,0);assert.equal(ctx.videoInfoCache.size,0);assert.equal(ctx.pageCache.size,1);assert.equal(ctx.appSearchCache.size,1);await assert.rejects(ctx.SwitchAPI.read('1',0,2),/Range/);assert.equal(ctx.SwitchAPI.clearCache().entries,0);console.log('PASS: clear video caches, stale readers invalidated, catalog caches retained, repeat safe');})().catch(e=>{console.error(e);process.exitCode=1;});
