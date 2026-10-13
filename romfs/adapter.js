const api={categoryPage,itemToCard,routerData,getText,appSearchPage,appSearchParams,signedAppGet,appSearchCells,appSearchVod,fetchVideoInfo,qualityNumber,qualityLabel,decryptSpadeUrl,deriveContentKey,remoteRange,prepareCencHeader,decryptPartial,streamCache,videoInfoCache,pageCache,appSearchCache};
const validId=id=>typeof id==='string'&&/^\d{1,30}$/.test(id);
const card=row=>({id:String(row.url),title:String(row.title||''),cover:String(row.img||''),description:String(row.content||''),badge:String(row.desc||''),tags:String(row.tname||'')});
const cards=rows=>[...new Map(Array.from(rows,card).filter(r=>validId(r.id)&&r.title).map(r=>[r.id,r])).values()];
const episodeSeries = new Map();
async function publicComicPage(page) {
  const html=await getText(`${SITE}/rank/hot-comic-drama?page=${page}`);
  for(const match of html.matchAll(/<script\b[^>]*data-fn-name="([^"]+)"[^>]*data-fn-args="([^"]+)"[^>]*>/g)) {
    let args;try{args=JSON.parse(decodeHtml(match[2]));}catch(_){continue;}
    if(args[0]!=='rank_hot-comic-drama/page')continue;
    let value;
    if(match[1]==='r' && args[1]==='content')value=args[2];
    if(match[1]==='mergeLoaderData' && Array.isArray(args[1]))value=args[1].find(v=>v.key==='content' && v.routerDataFnName==='p')?.routerDataFnArgs?.[0];
    if(typeof value==='string')try{value=JSON.parse(value);}catch(_){continue;}
    if(!value?.isSuccess || !Array.isArray(value.rankList))continue;
    const pages=Math.min(Number(value.pagination?.totalPages || 0),100);
    if(pages<1 || Number(value.pagination.pageNum || page)!==page)throw new Error('漫剧分页信息异常');
    return {items:cards(value.rankList.map(rankItemToCard)),page,hasMore:page<pages};
  }
  throw new Error('漫剧榜单数据暂不可用');
}
async function publicWebStream(id) {
  const sid = episodeSeries.get(id) || '_';
  const html = await api.getText(`${SITE}/player/${sid}/${id}`);
  // The requested episode's public VideoObject is server rendered. Never take
  // a URL from an unrelated recommendation or an arbitrary HTML substring.
  for (const match of html.matchAll(/<script\b[^>]*type=["']application\/ld\+json["'][^>]*>([\s\S]*?)<\/script>/gi)) {
    let node; try { node=JSON.parse(match[1]); } catch (_) { continue; }
    for (const item of Array.isArray(node) ? node : node['@graph'] || [node]) {
      if (item['@type'] !== 'VideoObject' || !String(item.about?.['@id'] || item['@id'] || '').includes(id)) continue;
      const url = String(item.contentUrl || '').replace(/&amp;/g,'&');
      if (!/^https:\/\//.test(url)) continue;
      const stream = {url,key:null,quality:'官网',codec:'',source:'web'};
      const first=await api.remoteRange(stream,0,256*1024-1);
      const bytes=Buffer.from(first.data);
      if (bytes.indexOf(Buffer.from('encv')) >= 0 || bytes.indexOf(Buffer.from('enca')) >= 0) throw new Error('官网资源不是明文 MP4');
      stream.initial = first;
      return stream;
    }
  }
  throw new Error('官网暂未提供本集的公开视频');
}
function repairVideoEntries(raw, header) {
  let pos = 0;
  while ((pos = raw.indexOf(Buffer.from('encv'), pos)) >= 0) {
    if (pos >= 4) {
      const end = pos - 4 + raw.readUInt32BE(pos - 4);
      if (end > pos + 8 && end <= raw.length) {
        const frma = raw.indexOf(Buffer.from('frma'),pos + 4);
        const declared = frma >= 0 && frma + 8 <= end ? raw.toString('ascii',frma+4,frma+8) : '';
        const avc = raw.indexOf(Buffer.from('avcC'),pos+4);
        const format = ['avc1','avc3','hvc1','hev1'].includes(declared) ? declared : avc >= 0 && avc < end ? 'avc1' : 'hvc1';
        header.write(format,pos,'ascii');
      }
    }
    pos += 4;
  }
  return header;
}

async function prepare(stream) {
  if (stream.prepared) return stream.prepared;
  if (!stream.preparePromise) stream.preparePromise = (async () => {
    const first = stream.initial || await api.remoteRange(stream,0,256*1024-1);
    delete stream.initial;
    let head = Buffer.from(first.data); const total = first.total;
    if (!Number.isSafeInteger(total) || total < head.length || head.length < 16 || head.toString('ascii',4,8) !== 'ftyp') throw new Error('视频头格式异常');
    const ftypSize = head.readUInt32BE(0);
    if (ftypSize < 8 || ftypSize + 8 > head.length || head.toString('ascii',ftypSize+4,ftypSize+8) !== 'moov') throw new Error('视频索引格式异常');
    const moovSize = head.readUInt32BE(ftypSize), end = ftypSize + moovSize;
    if (moovSize < 8 || end > total || end > 8*1024*1024) throw new Error('视频索引超出限制');
    if (end > head.length) head = Buffer.concat([head,Buffer.from((await api.remoteRange(stream,head.length,end-1)).data)]);
    head = head.subarray(0,end);
    const p = stream.key ? api.prepareCencHeader(head,stream.key) : {header:head,samples:[]};
    if (p.samples.length > 300000) throw new Error('视频索引过大');
    repairVideoEntries(head,p.header);
    return stream.prepared = {total,header:p.header,samples:p.samples};
  })();
  try {return await stream.preparePromise;} finally {delete stream.preparePromise;}
}

const provider = {
  async home(page = 1, type = 'comic') {
    if(type==='comic'){
      try {const r=await appSearchPage('热门',19,page,24);return {items:cards(r.list),page,hasMore:r.hasMore};}
      catch (_) {return publicComicPage(page);}
    }
    const data = await api.categoryPage('tab=1&sort_type=1',page);
    const section = data?.loaderData?.category_page || data?.loaderData?.['category_$'];
    const rows = section?.recommendList || section?.categoryData?.recommendList;
    if (!Array.isArray(rows)) throw new Error('首页数据结构已变化');
    const items = cards(rows.map(api.itemToCard));
    const totalPages = Math.min(Number(section.pagination?.totalPages || 100),100);
    return {items,page,totalPages,hasMore:items.length>0 && page<totalPages};
  },
  async search(keyword, page = 1, type = 'comic') {
    const tabType = type === 'comic' ? 19 : 11;
    // Inspect the response rather than treating a missing search tab as an empty success.
    let cursor = {};
    const key = `${tabType}|${keyword}|${page}`;
    if (page === 1) for (const k of api.appSearchCache.keys()) if (k.startsWith(`${tabType}|${keyword}|`)) api.appSearchCache.delete(k);
    const cached = api.appSearchCache.get(key);
    if (cached && Date.now()-cached.time<300000) return {items:cards(cached.value.list),page,hasMore:cached.value.hasMore};
    if (page > 1) {
      const previous = api.appSearchCache.get(`${tabType}|${keyword}|${page-1}`);
      if (!previous || Date.now()-previous.time>=300000) throw new Error('搜索结果已过期，请从第 1 页重新搜索');
      cursor = previous.value.cursor;
    }
    try {
      const data = await api.signedAppGet(api.appSearchParams(keyword,tabType,20,cursor,page));
      const tab = (data.search_tabs || data.data?.search_tabs || []).find(row => Number(row.tab_type) === tabType);
      if (!tab || !Array.isArray(tab.data)) throw new Error('搜索接口暂不可用');
      const list = Array.from(tab.data).flatMap(cell => Array.from(api.appSearchCells(cell))).map(api.appSearchVod).filter(Boolean);
      const value = {list,hasMore:!!tab.has_more,cursor:{search_id:String(tab.search_id || ''),passback:String(tab.passback || ''),next_offset:String(tab.next_offset || '')}};
      api.appSearchCache.set(key,{time:Date.now(),value});
      return {items:cards(list),page,hasMore:value.hasMore && page<100};
    } catch (error) {
      if (type === 'comic') throw error;
      const data = await api.routerData(`${SITE}/search/${encodeURIComponent(keyword)}?page=${page}`);
      const section = data?.loaderData?.['search_(keyword)/page'] || data?.loaderData?.search_page;
      if (!Array.isArray(section?.searchList)) throw new Error('搜索数据不可用');
      const items = cards(section.searchList.map(api.itemToCard));
      return {items,page,hasMore:items.length>0 && page<100};
    }
  },
  async detail(id) {
    if (!validId(id)) throw new Error('剧集 ID 无效');
    const data = await api.routerData(`${SITE}/detail?series_id=${id}`);
    const series = data?.loaderData?.detail_page?.seriesDetail;
    if (!series || !Array.isArray(series.vid_list) || !series.vid_list.length || series.vid_list.length>2000) throw new Error('分集暂不可用');
    const episodes = Array.from(series.vid_list,(vid,index) => ({id:String(vid),number:index+1,title:`第 ${index+1} 集`}));
    if (episodes.some(e => !validId(e.id)) || new Set(episodes.map(e => e.id)).size !== episodes.length) throw new Error('分集数据异常');
    for (const episode of episodes) episodeSeries.set(episode.id,id);
    while(episodeSeries.size>6000)episodeSeries.delete(episodeSeries.keys().next().value);
    return {...card(api.itemToCard(series)),id,episodes};
  },
  async stream(id, quality = 720, fresh = false) {
    if (!validId(id)) throw new Error('分集 ID 无效');
    const key = `${id}|desktop-${quality}`;
    if (fresh) {api.streamCache.delete(key); api.videoInfoCache.delete(id);}
    let stream = api.streamCache.get(key);
    if (!stream || Date.now()-stream.time>=240000) {
      let web;
      try { web=await publicWebStream(id); } catch (_) {}
      if (web) {
        stream={time:Date.now(),value:web};
      } else {
      const {info,rows} = await api.fetchVideoInfo(id);
      const compatible = Array.from(rows).filter(row => !['bytevc2','bvc2'].includes(String(row.item.codec_type || '').toLowerCase()));
      const avc = compatible.filter(row => /^(avc|h264|h\.264|avc1)$/i.test(String(row.item.codec_type || '')));
      const pool = avc.length ? avc : compatible;
      const selected = pool.find(row => api.qualityNumber(row.quality)<=quality) || pool.at(-1);
      if (!selected) throw new Error('没有兼容的播放资源');
      let url = String(selected.item.main_url);
      const seed = Buffer.from(String(info.key_seed || ''),'base64');
      if (seed.length) url = api.decryptSpadeUrl(url,seed);
      if (!/^https?:\/\//.test(url)) throw new Error('播放地址无效');
      stream = {time:Date.now(),value:{url,key:selected.item.spade_a ? api.deriveContentKey(selected.item.spade_a) : null,quality:api.qualityLabel(selected.quality),codec:String(selected.item.codec_type || '')}};
      }
      api.streamCache.set(key,stream);
      while(api.streamCache.size>4)api.streamCache.delete(api.streamCache.keys().next().value);
    }
    const value = stream.value, prepared = await prepare(value);
    return {total:prepared.total,quality:value.quality,codec:value.codec,source:value.source || 'app',
      chunk:async (start,end) => api.decryptPartial((await api.remoteRange(value,start,end)).data,start,value.key,prepared)};
  }
};


const active=new Map();globalThis.SwitchAPI={home:provider.home,search:provider.search,detail:provider.detail,open:async(id,q=480)=>{const s=await provider.stream(id,q);active.set(id,s);while(active.size>2)active.delete(active.keys().next().value);return {total:s.total,quality:s.quality,codec:s.codec,source:s.source};},read:async(id,a,b)=>{const s=active.get(id);if(!s||a<0||b<a||b-a>=1048576)throw new Error('Range');return Buffer.from(await s.chunk(a,Math.min(b,s.total-1))).toString('hex');},selftest:()=>({protocol:protocolVector(),ctr:nativeCrypto.createDecipheriv('aes-128-ctr',Buffer.from('2b7e151628aed2a6abf7158809cf4f3c','hex'),Buffer.from('f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff','hex')).update(Buffer.from('874d6191b620e3261bef6864990db6ce','hex')).toString('hex'),cbc:nativeCrypto.createDecipheriv('aes-128-cbc',Buffer.from('2b7e151628aed2a6abf7158809cf4f3c','hex'),Buffer.from('000102030405060708090a0b0c0d0e0f','hex')).setAutoPadding(false).update(Buffer.from('7649abac8119b246cee98e9b12e9197d','hex')).toString('hex'),sm3:nativeCrypto.createHash('sm3').update('abc').digest('hex'),md5:nativeCrypto.createHash('md5').update('abc').digest('hex'),sha512:nativeCrypto.createHash('sha512').update('abc').digest('hex')})};

globalThis.protocolVector=()=>{const d=Date.now,r=Math.random;Date.now=()=>1791875000123;Math.random=()=>0.123456789;try{return nativeCrypto.createHash('md5').update(JSON.stringify(signedVideoRequest(JSON.stringify({mixed_video_id_map:{'1004':['1234567890123456789']}})).headers)).digest('hex');}finally{Date.now=d;Math.random=r;}};
