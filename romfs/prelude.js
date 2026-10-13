// SPDX-License-Identifier: GPL-3.0-only
globalThis.Buffer = SwitchDeps.Buffer;
globalThis.console = {log(){},warn(){},error(){}};
globalThis.log = ()=>{};
class URLSearchParams {
  constructor(value='') {
    this.rows=[];
    if(typeof value==='object')this.rows=Object.entries(value).map(([k,v])=>[String(k),String(v)]);
    else for(const pair of String(value).replace(/^\?/,'').split('&')) {
      if(!pair)continue;const pos=pair.indexOf('=');
      const dec=s=>decodeURIComponent(s.replace(/\+/g,' '));
      this.rows.push([dec(pos<0?pair:pair.slice(0,pos)),dec(pos<0?'':pair.slice(pos+1))]);
    }
  }
  get(key){const row=this.rows.find(r=>r[0]===key);return row?row[1]:null;}
  set(key,value){this.rows=this.rows.filter(r=>r[0]!==key);this.rows.push([String(key),String(value)]);}
  entries(){return this.rows[Symbol.iterator]();}
  [Symbol.iterator](){return this.entries();}
  toString(){const enc=s=>encodeURIComponent(s).replace(/%20/g,'+');return this.rows.map(([k,v])=>enc(k)+'='+enc(v)).join('&');}
}
globalThis.URLSearchParams=URLSearchParams;
const nativeCrypto={
  createHash(algorithm){let parts=[];return {
    update(data,encoding){parts.push(Buffer.from(data,encoding));return this;},
    digest(encoding){const bytes=Buffer.concat(parts);const result=algorithm==='sm3'?Buffer.from(SwitchDeps.sm3(Array.from(bytes)),'hex'):Buffer.from(__hash(algorithm,bytes.toString('hex')),'hex');return encoding?result.toString(encoding):result;}
  };},
  createDecipheriv(algorithm,key,iv){let automatic=true;return {
    setAutoPadding(value){automatic=value;return this;},
    update(data){let out=Buffer.from(__aes(algorithm,Buffer.from(key).toString('hex'),Buffer.from(iv).toString('hex'),Buffer.from(data).toString('hex')),'hex');
      if(automatic && algorithm.endsWith('-cbc') && out.length){const n=out[out.length-1];if(n<1||n>16||n>out.length||!out.subarray(out.length-n).every(b=>b===n))throw new Error('Invalid AES padding');out=out.subarray(0,out.length-n);}return out;},
    final(){return Buffer.alloc(0);}
  };}
};
globalThis.require=name=>{if(name==='node:crypto')return nativeCrypto;throw new Error('Unsupported module');};
async function request(url,options={}){
  const response=JSON.parse(__http(String(url),options.method||'GET',JSON.stringify(options.headers||{}),String(options.body||'')));
  const bytes=Buffer.from(response.hex,'hex');
  if(response.status<200||response.status>=300)throw new Error('内容源 HTTP '+response.status);
  return {status:response.status,headers:response.headers,data:options.responseType==='arraybuffer'?bytes:bytes.toString('utf8')};
}
globalThis.axios=request;
axios.post=(url,body,options={})=>request(url,{...options,method:'POST',body:typeof body==='string'?body:JSON.stringify(body)});
