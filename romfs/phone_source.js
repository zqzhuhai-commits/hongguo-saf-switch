// SPDX-License-Identifier: GPL-3.0-only
// Protocol compatibility reference: qq294716498/HongguoTV-Phone-TV-NoAds,
// native/core/provider_hongguo_{app,sign,native_media,playback}.go (c356be4).
const PhoneSource = (() => {
  const ua='com.phoenix.read/73532 (Linux; U; Android 16; zh_CN; 25053RT47C; Build/BP2A.250605.031.A3; Cronet/TTNetVersion:04657795 2026-01-23 QuicVersion:c67e9834 2025-09-08)';
  const identity=()=>String(1000000000000000000n+BigInt(Math.floor(Math.random()*Number.MAX_SAFE_INTEGER))*1000n);
  const deviceId=identity(),installId=identity();
  const valid=id=>typeof id==='string'&&/^\d{1,30}$/.test(id);
  const httpUrl=value=>typeof value==='string'&&value.length<=8192&&/^https?:\/\//.test(value);
  function sign(query,body,now){
    const hash=value=>nativeCrypto.createHash('md5').update(value).digest();
    const payload=Buffer.alloc(20);hash(query).copy(payload,0,0,4);hash(body).copy(payload,4,0,4);
    Buffer.from([0,6,11,28]).copy(payload,12);payload.writeUInt32BE(Math.floor(now/1000),16);
    const key=Buffer.from('44b9b9d9a4aef9fca493aa757ca3c2c4a496938f','hex');
    for(let i=0;i<20;i++)payload[i]^=key[i];
    for(let i=0;i<20;i++){const x=(((payload[i]<<4)|(payload[i]>>4))&255)^payload[(i+1)%20];let reverse=0;for(let bit=0;bit<8;bit++)reverse|=((x>>bit)&1)<<(7-bit);payload[i]=reverse^255^20;}
    return {'X-Khronos':String(Math.floor(now/1000)),'X-Gorgon':'8404401c0000'+payload.toString('hex'),'X-SS-Req-Ticket':String(now),'X-SS-STUB':hash(body).toString('hex').toUpperCase()};
  }
  function contentKey(value){
    if(typeof value!=='string'||value.length>1024)throw Error('媒体密钥编码无效');
    const raw=Buffer.from(value,'base64');if(raw.length<3)throw Error('媒体密钥编码无效');
    const tagLength=(raw[0]^raw[1]^raw[2])-48,n=raw.length-tagLength-1;
    if(tagLength<1||n<33||n>=raw.length)throw Error('媒体密钥结构无效');
    const seed=raw[raw.length-tagLength-2]^raw[raw.length-tagLength-1];
    const tag=Buffer.from(raw.subarray(raw.length-tagLength).map(x=>x^seed)).toString();
    if(tag==='app_v2'||tag==='web_v2')throw Error('媒体密钥版本暂不支持');
    const out=Buffer.alloc(n);let even=250,odd=85;
    for(let i=0;i<n;i++){const c=raw[i+1],previous=i%2?odd:even;if(i%2)odd=c;else even=c;const pop=i.toString(2).split('1').length-1;out[i]=((previous^c)-21-pop)&255;}
    if(n-parseInt(out.toString('ascii',0,1),36)-1!==32||!/[0-9a-fA-F]{32}/.test(out.toString('ascii',1,33)))throw Error('媒体密钥内容无效');
    return Buffer.from(out.toString('ascii',1,33),'hex');
  }
  function decode(body){
    const text=String(body).trim();if(!text.startsWith('v2.'))return JSON.parse(text);
    const parts=text.split('.');if(parts.length!==3||parts[1].length<=4||parts[1].length>1028||!/^[0-9a-fA-F]+$/.test(parts[1].slice(4))||parts[1].length%2)throw Error('备用接口响应密钥无效');
    const encoded=Buffer.from(parts[1].slice(4),'hex');if(encoded.length<32)throw Error('备用接口响应密钥无效');
    const mask=[104,64,70,166,190,168,143,130,225,254,251,217,196,34,45,60,29,20,103,105],material=Buffer.alloc(encoded.length);
    for(let i=0;i<encoded.length;i++){const slot=i%20,x=(encoded[i]+215-11*i)&255;material[i]=(i?encoded[i-1]:109)^mask[slot]^((90+13*slot)&255)^85^(((x<<3)|(x>>5))&255);}
    const ciphertext=Buffer.from(parts[2],'base64');if(!ciphertext.length||ciphertext.length%16)throw Error('备用接口加密响应无效');
    const decipher=nativeCrypto.createDecipheriv('aes-128-cbc',material.subarray(0,16),material.subarray(16,32));
    return JSON.parse(Buffer.concat([decipher.update(ciphertext),decipher.final()]).toString());
  }
  function addresses(row){
    const result=[];const add=value=>{if(Array.isArray(value)){value.forEach(add);return;}if(typeof value!=='string')return;let url=value.trim();if(!httpUrl(url))url=Buffer.from(url,'base64').toString().trim();if(httpUrl(url)&&!result.includes(url))result.push(url);};
    for(const k of ['main_url','backup_url','backup_url_1','backup_url_2','backup_urls','url_list'])add(row[k]);return result;
  }
  function rowsFromModel(model){
    const rows=[];
    for(const [index,row] of Object.entries(model?.video_list||{})){
      if(!row||typeof row!=='object')continue;const meta=row.video_meta||{},codec=String(meta.codec_type||'');
      if(/bytevc2|bvc2/i.test(codec+' '+String(row.gear_des_key||'')))continue;
      const urls=addresses(row);if(!urls.length)continue;
      const encryption=row.encrypt_info||{};let key=null;
      try{if(encryption.spade_a||encryption.encrypt===true||encryption.encryption_method==='cenc-aes-ctr')key=contentKey(encryption.spade_a);}catch(_){continue;}
      const dims=[Number(meta.vheight),Number(meta.vwidth)].filter(n=>n>0);
      const quality=qualityNumber(meta.definition)|| (dims.length?Math.min(...dims):0);
      rows.push({key:'phone-'+index,quality:String(quality),item:{main_url:urls[0],backup_url:urls[1],codec_type:codec,phone_key:key,phone:true}});
    }
    if(!rows.length)throw Error('新版 App 未返回兼容片源');
    return {info:{},rows:rows.sort((a,b)=>Number(b.quality)-Number(a.quality))};
  }
  const cache=new Map();
  async function videoInfo(id){
    if(!valid(id))throw Error('分集 ID 无效');const hit=cache.get(id);if(hit&&Date.now()-hit.time<240000)return hit.value;
    const now=Date.now(),values={aid:'8662',app_name:'novelread',version_code:'73532',version_name:'7.3.5.32',manifest_version_code:'73532',update_version_code:'73532',channel:'update_64',device_platform:'android',os:'android',ssmix:'a',device_type:'25053RT47C',device_brand:'Redmi',language:'zh',os_api:'36',os_version:'16',resolution:'1280*2772',dpi:'520',ac:'wifi',device_id:deviceId,iid:installId,_rticket:String(now)};
    const query=pythonUrlEncode(Object.fromEntries(Object.entries(values).sort(([a],[b])=>a<b?-1:a>b?1:0))).replace(/\*/g,'%2A');
    const body=JSON.stringify({biz_param:{need_all_video_definition:true,video_platform:3},content_type:1,video_id:id});
    const response=await axios.post('https://api5-normal-sinfonlineb.fqnovel.com/novel/player/video_model/v1/?'+query,body,{headers:{'User-Agent':ua,Accept:'application/json','X-XS-From-Web':'0','Sdk-Version':'2','Content-Type':'application/json; charset=utf-8',...sign(query,body,now)},responseType:'text'});
    const data=JSON.parse(response.data);if(response.status!==200||Number(data.code??data.Code??data.status_code??data.BaseResp?.StatusCode??0)!==0)throw Error('新版 App 播放接口暂不可用');
    const raw=data.data?.video_model,model=typeof raw==='string'?JSON.parse(raw):raw,value=rowsFromModel(model);
    cache.set(id,{time:Date.now(),value});while(cache.size>4)cache.delete(cache.keys().next().value);return value;
  }
  async function backup(series,id){
    if(!valid(series)||!valid(id))throw Error('备用取流缺少剧集 ID');
    const reference={content_type:1004,from_video_id:'',series_id:series,vid:id,video_platform:3};
    const query=new URLSearchParams({id:Buffer.from(JSON.stringify(reference)).toString('base64')});
    const response=await axios('https://djapi.999888456.xyz/api/hongguo/play?'+query,{headers:{'User-Agent':'Mozilla/5.0',Referer:'https://hongguoduanju.com/'},responseType:'text'});
    const data=decode(response.data);for(const flag of [data.parse,data.jx])if(![undefined,null,false,0,'0',''].includes(flag))throw Error('备用接口未返回直接媒体');
    const rows=[];
    for(const [i,row]of (Array.isArray(data.key_urls)?data.key_urls:[]).entries())try{
      if(!httpUrl(row.src)||!/^[0-9a-fA-F]{32}$/.test(String(row.kid)))continue;
      rows.push({key:'backup-'+i,quality:String(qualityNumber(row.name)),item:{main_url:row.src,phone_key:contentKey(row.spade_a),phone:true,codec_type:''}});
    }catch(_){}
    if(!rows.length)throw Error('备用接口没有可用片源');return {info:{},rows:rows.sort((a,b)=>Number(b.quality)-Number(a.quality))};
  }
  return {videoInfo,backup,clear:id=>id?cache.delete(id):cache.clear(),sign,decode,contentKey,rowsFromModel};
})();
