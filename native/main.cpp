// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include "poster.hpp"
#include "workers.hpp"
#include <borealis/views/image.hpp>
#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/core/thread.hpp>
#include <switch.h>
#include <curl/curl.h>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <mpv/stream_cb.h>
#include <fstream>
#include <memory>
#include <atomic>

#include <cstring>
#include <algorithm>
#include <sys/stat.h>

static Backend* backend=nullptr;
static Json library;
static const char* store="sdmc:/switch/hongguo-saf/library.json";
static void save(){try{std::ofstream out(std::string(store)+".tmp");out<<library.dump();out.close();if(out)std::rename((std::string(store)+".tmp").c_str(),store);}catch(...){}}
static void load(){mkdir("sdmc:/switch",0777);mkdir("sdmc:/switch/hongguo-saf",0777);try{std::ifstream in(store);library=Json::parse(in);if(!library.is_object()||!library.contains("favorites")||!library["favorites"].is_array()||!library.contains("history")||!library["history"].is_array())throw std::runtime_error("Invalid library");}catch(...){library={{"format","hongguotv.library"},{"version",1},{"favorites",Json::array()},{"history",Json::array()},{"later",Json::array()}};}}
static void record(const Json& series,const Json& episode,int index,double seconds,double duration){if(seconds<0||duration<0)return;auto& rows=library["history"];rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const Json& r){return r.value("series",Json::object()).value("id",std::string())==series.value("id",std::string());}),rows.end());rows.insert(rows.begin(),Json{{"series",series},{"episodeId",episode.value("id",std::string())},{"episodeIndex",index},{"position",(int64_t)(seconds*1000)},{"duration",(int64_t)(duration*1000)},{"completed",duration>0&&seconds>=duration-3},{"updatedAt",(int64_t)time(nullptr)*1000}});while(rows.size()>100)rows.erase(rows.end()-1);save();}
static void message(const std::string& text){auto* dialog=new brls::Dialog(text);dialog->addButton("确定",[]{});dialog->open();}
static void back(brls::View* view){view->registerAction("返回",brls::BUTTON_B,[](brls::View*){brls::Application::popActivity();return true;});}
static brls::DetailCell* cell(const std::string& name,const std::string& note,std::function<void()> fn){auto* v=new brls::DetailCell();v->setText(name);v->setDetailText(note);v->registerClickAction([fn](brls::View*){fn();return true;});return v;}
static std::atomic<int> posterJobs{0};
class PosterImage:public brls::Image {
  std::string url;bool started=false,finished=false;
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
public:
  explicit PosterImage(std::string url,float width=104,float height=156):url(std::move(url)){setWidth(width);setHeight(height);setScalingType(brls::ImageScalingType::FIT);setBackgroundColor(nvgRGB(40,43,47));}
  ~PosterImage(){alive->store(false);}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style style,brls::FrameContext* ctx)override {
    if(!started && !url.empty() && x+w>=0 && x<=1280 && y+h>=0 && y<=720 && posterJobs.load()<2){started=true;posterJobs++;auto flag=alive;auto address=url;
      posterTask([this,flag,address]{PosterPixels image;try{image=fetchPoster(address,*flag);}catch(...){}posterJobs--;brls::sync([this,flag,image=std::move(image)]{if(!flag->load())return;finished=true;if(!image.rgba.empty()){trace("poster:upload_begin");innerSetImage(nvgCreateImageRGBA(brls::Application::getNVGContext(),image.width,image.height,0,image.rgba.data()));trace("poster:upload_end");}});});
    }
    if(getTexture())brls::Image::draw(vg,x,y,w,h,style,ctx);
    else {nvgFontFace(vg,"regular");nvgFontSize(vg,15);nvgFillColor(vg,nvgRGB(160,165,170));nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgText(vg,x+w/2,y+h/2,finished||url.empty()?"暂无封面":"封面加载中",nullptr);}
  }
};
static brls::Box* posterCell(const Json& row,std::function<void()> fn){auto* box=new brls::Box(brls::Axis::ROW);box->setHeight(172);box->setPadding(8);box->addView(new PosterImage(row.value("cover",std::string())));auto* text=cell(row.value("title",std::string()),row.value("badge",std::string()),std::move(fn));text->setGrow(1);box->addView(text);return box;}
static std::mutex playbackErrorMutex;
static std::string playbackErrorId,playbackError;
class SceneFrame:public brls::AppletFrame {public:explicit SceneFrame(brls::View* root):brls::AppletFrame(root){}std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);~SceneFrame(){*alive=false;}};
static SceneFrame* frame(brls::Box* list,const std::string& title){auto* scroll=new brls::ScrollingFrame();scroll->setGrow(1);list->setPadding(20,50,30,50);scroll->setContentView(list);auto* f=new SceneFrame(scroll);f->setTitle(title);return f;}
struct Reader{std::string id;int64_t total=0,pos=0,cacheAt=-1;std::vector<unsigned char> cache;std::atomic<bool> cancelled{false};};
static int64_t readStream(void* data,char* out,uint64_t size){auto* r=(Reader*)data;if(r->cancelled)return -1;if(r->pos>=r->total)return 0;try{
  const int64_t start=(r->pos/(1024*1024))*(1024*1024);
  if(r->cacheAt!=start){auto value=backend->call("read",Json::array({r->id,start,std::min(start+1024*1024-1,r->total-1)}));r->cache=unhex(value.get<std::string>());r->cacheAt=start;}
  size_t offset=(size_t)(r->pos-r->cacheAt);if(offset>=r->cache.size())return -1;size_t take=std::min<size_t>(size,r->cache.size()-offset);if(r->cancelled)return -1;std::memcpy(out,r->cache.data()+offset,take);r->pos+=take;return take;
}catch(...){return -1;}}
static int64_t seekStream(void* data,int64_t at){auto* r=(Reader*)data;if(at<0||at>r->total)return -1;r->pos=at;return at;}
static int64_t sizeStream(void* data){return ((Reader*)data)->total;}
static void closeStream(void* data){delete (Reader*)data;}
static void cancelStream(void* data){((Reader*)data)->cancelled=true;}
static int openStream(void*,char* uri,mpv_stream_cb_info* info){try{
  const std::string value(uri);if(value.rfind("saf://",0)!=0)return MPV_ERROR_LOADING_FAILED;const std::string id=value.substr(6);if(id.empty()||id.size()>30||id.find_first_not_of("0123456789")!=std::string::npos)return MPV_ERROR_LOADING_FAILED;
  const Json meta=backend->call("open",Json::array({id,480}));auto* r=new Reader();r->id=id;r->total=meta.at("total").get<int64_t>();if(r->total<=0){delete r;return MPV_ERROR_LOADING_FAILED;}
  info->cookie=r;info->read_fn=readStream;info->seek_fn=seekStream;info->size_fn=sizeStream;info->close_fn=closeStream;info->cancel_fn=cancelStream;return 0;
}catch(const std::exception& e){std::lock_guard<std::mutex> guard(playbackErrorMutex);const std::string uriValue(uri);playbackErrorId=uriValue.rfind("saf://",0)==0?uriValue.substr(6):"";playbackError=e.what();return MPV_ERROR_LOADING_FAILED;}catch(...){return MPV_ERROR_LOADING_FAILED;}}
static void* getProc(void*,const char* name){return (void*)glfwGetProcAddress(name);}
class Player:public brls::Box {
  mpv_handle* mpv=nullptr;mpv_render_context* renderer=nullptr;
  Json series,episodes;int index=0,speedIndex=1;bool failed=false,nextQueued=false,paused=false;
  double position=0,duration=0,resumeTarget=0;bool opened=false;uint64_t lastSave=0;std::string status="正在连接播放源…";
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
  void persist(){if(opened&&(position>0||duration>0)&&index>=0&&index<(int)episodes.size())record(series,episodes[index],index,position,duration);}
  void startEpisode(int target){if(!mpv||target<0||target>=(int)episodes.size())return;persist();index=target;position=0;duration=0;opened=true;resumeTarget=0;for(const auto& row:library["history"])if(row.value("episodeId",std::string())==episodes[index].value("id",std::string()))resumeTarget=row.value("position",0.0)/1000;nextQueued=false;paused=false;int unpause=0;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&unpause);status="正在连接播放源…";const auto url="saf://"+episodes[index].at("id").get<std::string>();const char* command[]={"loadfile",url.c_str(),"replace",nullptr};if(mpv_command(mpv,command)<0){failed=true;status="无法打开分集";}}
public:
  Player(Json detail,int episode):brls::Box(brls::Axis::COLUMN),series(detail),episodes(detail.at("episodes")),index(episode){
    this->setGrow(1);this->setBackgroundColor(nvgRGB(0,0,0));this->setFocusable(true);this->setHideHighlight(true);appletSetMediaPlaybackState(true);
    this->registerAction("返回",brls::BUTTON_B,[](brls::View*){brls::Application::popActivity();return true;});
    this->registerAction("播放/暂停",brls::BUTTON_A,[this](brls::View*){if(!mpv)return true;paused=!paused;int v=paused;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&v);return true;});
    this->registerAction("后退10秒",brls::BUTTON_LEFT,[this](brls::View*){if(!mpv)return true;const char* cmd[]={"seek","-10","relative",nullptr};mpv_command(mpv,cmd);return true;});
    this->registerAction("前进10秒",brls::BUTTON_RIGHT,[this](brls::View*){if(!mpv)return true;const char* cmd[]={"seek","10","relative",nullptr};mpv_command(mpv,cmd);return true;});
    this->registerAction("下一集",brls::BUTTON_RB,[this](brls::View*){startEpisode(index+1);return true;});
    this->registerAction("上一集",brls::BUTTON_LB,[this](brls::View*){startEpisode(index-1);return true;});
    this->registerAction("倍速",brls::BUTTON_Y,[this](brls::View*){if(!mpv)return true;const double rates[]={0.75,1,1.25,1.5,2};speedIndex=(speedIndex+1)%5;double rate=rates[speedIndex];mpv_set_property(mpv,"speed",MPV_FORMAT_DOUBLE,&rate);return true;});
    mpv=mpv_create();if(!mpv){failed=true;status="播放器初始化失败";return;}
    mpv_set_option_string(mpv,"config","no");mpv_set_option_string(mpv,"terminal","no");mpv_set_option_string(mpv,"vo","libmpv");mpv_set_option_string(mpv,"hwdec","auto");mpv_set_option_string(mpv,"audio-channels","stereo");mpv_set_option_string(mpv,"cache","yes");mpv_set_option_string(mpv,"demuxer-max-bytes","16MiB");mpv_set_option_string(mpv,"opengl-glfinish","yes");mpv_set_option_string(mpv,"vd-lavc-dr","yes");
    if(mpv_stream_cb_add_ro(mpv,"saf",nullptr,openStream)<0||mpv_initialize(mpv)<0){failed=true;status="播放器初始化失败";return;}
    mpv_opengl_init_params gl{getProc,nullptr};mpv_render_param params[]={{MPV_RENDER_PARAM_API_TYPE,(void*)MPV_RENDER_API_TYPE_OPENGL},{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,&gl},{MPV_RENDER_PARAM_INVALID,nullptr}};
    if(mpv_render_context_create(&renderer,mpv,params)<0){failed=true;status="视频渲染初始化失败";return;}
    startEpisode(index);
  }
  ~Player(){*alive=false;persist();if(renderer)mpv_render_context_free(renderer);if(mpv)mpv_terminate_destroy(mpv);appletSetMediaPlaybackState(false);}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style style,brls::FrameContext* ctx)override{
    if(mpv&&!failed){for(int count=0;count<30;count++){mpv_event* event=mpv_wait_event(mpv,0);if(event->event_id==MPV_EVENT_NONE)break;if(event->event_id==MPV_EVENT_FILE_LOADED){status="";if(resumeTarget>0){const std::string at=std::to_string(resumeTarget);const char* seek[]={"seek",at.c_str(),"absolute",nullptr};mpv_command(mpv,seek);resumeTarget=0;}}
      if(event->event_id==MPV_EVENT_END_FILE){auto* end=(mpv_event_end_file*)event->data;if(end&&end->reason==MPV_END_FILE_REASON_ERROR){status="内容源或视频播放暂不可用";std::lock_guard<std::mutex> guard(playbackErrorMutex);if(playbackErrorId==episodes[index].value("id",std::string())&&!playbackError.empty())status=playbackError;}else if(end&&end->reason==MPV_END_FILE_REASON_EOF&&!nextQueued){nextQueued=true;auto flag=alive;int next=index+1;brls::sync([this,flag,next]{if(*flag)startEpisode(next);});}}
    }mpv_get_property(mpv,"time-pos",MPV_FORMAT_DOUBLE,&position);mpv_get_property(mpv,"duration",MPV_FORMAT_DOUBLE,&duration);if(armGetSystemTick()-lastSave>armNsToTicks(10000000000ULL)){lastSave=armGetSystemTick();persist();}}
    nvgEndFrame(vg);GLint fbo=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,brls::Application::windowWidth,brls::Application::windowHeight);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if(renderer&&!failed){mpv_opengl_fbo surface{fbo,(int)brls::Application::windowWidth,(int)brls::Application::windowHeight,0};int flip=1;mpv_render_param params[]={{MPV_RENDER_PARAM_OPENGL_FBO,&surface},{MPV_RENDER_PARAM_FLIP_Y,&flip},{MPV_RENDER_PARAM_INVALID,nullptr}};mpv_render_context_render(renderer,params);mpv_render_context_report_swap(renderer);}else{glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);}
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const auto factor=brls::Application::getPlatform()->getVideoContext()->getScaleFactor();nvgBeginFrame(vg,brls::Application::windowWidth,brls::Application::windowHeight,factor);nvgScale(vg,brls::Application::windowScale,brls::Application::windowScale);
    nvgFontFace(vg,"regular");nvgFontSize(vg,22);nvgFillColor(vg,nvgRGBA(255,255,255,240));nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
    const std::string title=series.value("title",std::string())+" · 第 "+std::to_string(index+1)+" 集";nvgText(vg,28,20,title.c_str(),nullptr);if(!status.empty())nvgText(vg,60,180,status.c_str(),nullptr);
    if(paused||!status.empty()){nvgText(vg,28,660,"A 播放/暂停  ←/→ 跳转  L/R 选集  Y 倍速  B 返回",nullptr);}
  }
};
static void showDetail(const Json& series);
class Page:public brls::Box {
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);brls::Box* list;
public:
  Page(const std::string& method,const Json& args):brls::Box(brls::Axis::COLUMN){this->setGrow(1);list=new brls::Box(brls::Axis::COLUMN);list->setPadding(20,50,30,50);auto* label=new brls::Label();label->setText("正在加载…");list->addView(label);auto* scroll=new brls::ScrollingFrame();scroll->setGrow(1);scroll->setContentView(list);this->addView(scroll);back(this);auto flag=alive;
    apiTask([this,flag,method,args]{Json response;bool ok=false;try{response=backend->call(method,args);ok=true;}catch(...){}brls::sync([this,flag,ok,response,method,args]{if(!*flag)return;list->clearViews();if(!ok){list->addView(cell("内容源或网络暂不可用","按 B 返回后重试",[]{}));return;}for(const auto& row:response["items"]){if(!row.is_object()||!row.contains("id")||!row.contains("title"))continue;list->addView(posterCell(row,[row]{showDetail(row);}));}if(response.value("hasMore",false)){list->addView(cell("下一页","继续加载",[method,args]{Json next=args;const int at=method=="home"?0:1;next[at]=next[at].get<int>()+1;brls::Application::pushActivity(new brls::Activity(new Page(method,next)));}));}});});
  }
  ~Page(){*alive=false;}
};
static void showDetail(const Json& series){auto* list=new brls::Box(brls::Axis::COLUMN);auto* title=new brls::Label();title->setText("正在加载分集…");list->addView(title);auto* f=frame(list,series.value("title",std::string()));brls::Application::pushActivity(new brls::Activity(f));
  // The frame owns the flag; work never touches a destroyed view.
  auto flag=f->alive;
  apiTask([list,flag,series]{Json detail;bool ok=false;try{detail=backend->call("detail",Json::array({series.at("id")}));ok=true;}catch(...){}brls::sync([list,flag,ok,detail]{if(!*flag)return;list->clearViews();if(!ok){list->addView(cell("分集暂不可用","按 B 返回",[]{}));return;}
    list->addView(posterCell(detail,[]{}));
    list->addView(cell("收藏 / 取消收藏","保存在当前主机",[detail]{auto& rows=library["favorites"];auto it=std::find_if(rows.begin(),rows.end(),[&](const Json& row){return row.value("id",std::string())==detail.value("id",std::string());});if(it==rows.end()){Json row=detail;row.erase("episodes");rows.push_back(row);message("已收藏");}else{rows.erase(it);message("已取消收藏");}save();}));
    for(size_t i=0;i<detail["episodes"].size();i++)list->addView(cell("第 "+std::to_string(i+1)+" 集","A 播放",[detail,i]{brls::Application::pushActivity(new brls::Activity(new Player(detail,(int)i)));}));
  });});
}
static void openSearch(){SwkbdConfig keyboard;if(R_FAILED(swkbdCreate(&keyboard,0)))return;swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetStringLenMax(&keyboard,80);swkbdConfigSetGuideText(&keyboard,"输入剧名");char text[512]={};Result result=swkbdShow(&keyboard,text,sizeof(text));swkbdClose(&keyboard);if(R_SUCCEEDED(result)&&text[0])brls::Application::pushActivity(new brls::Activity(new Page("search",Json::array({std::string(text),1,"short"}))));}
static void openLibrary(bool favorites){auto* box=new brls::Box(brls::Axis::COLUMN);if(favorites){for(const auto& row:library["favorites"])box->addView(posterCell(row,[row]{showDetail(row);}));}else{for(const auto& row:library["history"]){const auto series=row.value("series",Json::object());box->addView(posterCell(series,[series]{showDetail(series);}));}}brls::Application::pushActivity(new brls::Activity(frame(box,favorites?"我的收藏":"最近观看")));}
static brls::View* posterTile(const Json& row){auto* tile=new brls::Box(brls::Axis::COLUMN);tile->setWidth(166);tile->setPadding(4);tile->setFocusable(true);tile->addView(new PosterImage(row.value("cover",std::string()),158,210));auto* title=new brls::Label();title->setText(row.value("title",std::string("红果短剧")));title->setFontSize(17);title->setTextColor(nvgRGB(245,245,247));title->setHorizontalAlign(brls::HorizontalAlign::LEFT);title->setHeight(50);tile->addView(title);const auto badge=row.value("badge",std::string());if(!badge.empty()){auto* note=new brls::Label();note->setText(badge);note->setFontSize(13);note->setTextColor(nvgRGB(156,164,178));note->setHorizontalAlign(brls::HorizontalAlign::LEFT);tile->addView(note);}tile->registerClickAction([row](brls::View*){showDetail(row);return true;});return tile;}
static brls::Label* sectionTitle(const std::string& text){auto* label=new brls::Label();label->setText(text);label->setFontSize(25);label->setTextColor(nvgRGB(255,255,255));label->setHorizontalAlign(brls::HorizontalAlign::LEFT);label->setMarginTop(20);label->setMarginBottom(10);return label;}
class HomeDashboard:public brls::Box{
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);brls::Box* content;
  void addCatalog(brls::Box* shelf,const std::string& title,const Json& result,bool ok){trace("home:render_begin");shelf->clearViews();shelf->addView(sectionTitle(title));if(!ok){shelf->addView(cell("暂时无法加载","按 B 返回后重试",[]{}));trace("home:render_error");return;}auto* scroller=new brls::HScrollingFrame();scroller->setHeight(288);auto* row=new brls::Box(brls::Axis::ROW);for(const auto& item:result.value("items",Json::array())){if(row->getChildren().size()>=12)break;if(item.is_object()&&item.contains("id")&&item.contains("title"))row->addView(posterTile(item));}if(row->getChildren().empty())row->addView(cell("暂时没有内容","请稍后重试",[]{}));scroller->setContentView(row);shelf->addView(scroller);trace("home:render_complete");}
public:
  HomeDashboard():brls::Box(brls::Axis::COLUMN){setGrow(1);setPadding(24,40,30,40);auto* hero=new brls::Box(brls::Axis::COLUMN);hero->setPadding(20);hero->setBackgroundColor(nvgRGB(35,38,49));auto* eyebrow=new brls::Label();eyebrow->setText("DISCOVER YOUR NEXT STORY");eyebrow->setFontSize(14);eyebrow->setTextColor(nvgRGB(255,112,100));eyebrow->setHorizontalAlign(brls::HorizontalAlign::LEFT);hero->addView(eyebrow);auto* heading=new brls::Label();heading->setText("好剧上新，随时开看");heading->setFontSize(31);heading->setTextColor(nvgRGB(255,255,255));heading->setHorizontalAlign(brls::HorizontalAlign::LEFT);hero->addView(heading);auto* subtitle=new brls::Label();subtitle->setText("短剧与漫剧精选 · 为你发现下一段故事");subtitle->setTextColor(nvgRGB(183,190,204));subtitle->setHorizontalAlign(brls::HorizontalAlign::LEFT);hero->addView(subtitle);addView(hero);
    auto* nav=new brls::Box(brls::Axis::ROW);nav->setPaddingTop(12);nav->addView(cell("短剧","热播推荐",[]{brls::Application::pushActivity(new brls::Activity(new Page("home",Json::array({1,"short"}))));}));nav->addView(cell("漫剧","人气榜单",[]{brls::Application::pushActivity(new brls::Activity(new Page("home",Json::array({1,"comic"}))));}));nav->addView(cell("搜索","查找剧名",openSearch));nav->addView(cell("收藏","我的片单",[]{openLibrary(true);}));nav->addView(cell("最近观看","接着看",[]{openLibrary(false);}));addView(nav);
    content=new brls::Box(brls::Axis::COLUMN);addView(content);if(!library["history"].empty()){content->addView(sectionTitle("接着看"));auto* recent=new brls::Box(brls::Axis::COLUMN);const size_t count=std::min<size_t>(3,library["history"].size());for(size_t i=0;i<count;i++){const auto series=library["history"][i].value("series",Json::object());recent->addView(posterCell(series,[series]{showDetail(series);}));}content->addView(recent);}
    auto flag=alive;for(const auto& category: {std::string("short"),std::string("comic")}){auto* shelf=new brls::Box(brls::Axis::COLUMN);const std::string heading=category=="short"?"热播短剧":"人气漫剧";shelf->addView(sectionTitle(heading));auto* loading=new brls::Label();loading->setText("正在加载海报与剧集…");shelf->addView(loading);content->addView(shelf);apiTask([this,flag,shelf,category,heading]{if(!*flag)return;Json result;bool ok=false;try{result=backend->call("home",Json::array({1,category}));ok=true;}catch(...){trace("home:"+category+":error");}brls::sync([this,flag,shelf,heading,ok,result=std::move(result)]{if(!*flag)return;addCatalog(shelf,heading,result,ok);});});}trace("startup:home_ready");}

  ~HomeDashboard(){*alive=false;}
};
static brls::View* home(){return frame(new HomeDashboard(),"红果短剧");}
int main(int,char**){curl_global_init(CURL_GLOBAL_DEFAULT);if(!brls::Application::init())return 1;brls::Application::createWindow("红果短剧");brls::Application::setGlobalQuit(true);load();resetTrace();trace("startup:ui_ready");
  try{if(!startWorkers())throw std::runtime_error("Worker initialization failed");backend=new Backend();trace("startup:backend_ready");const auto result=backend->call("selftest",Json::array());if(result.value("sm3",std::string())!="66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0")throw std::runtime_error("Self test");brls::Application::pushActivity(new brls::Activity(home()));}
  catch(...){auto* text=new brls::Label();text->setText("应用初始化失败，请确认安装包完整。");brls::Application::pushActivity(new brls::Activity(text));}
  while(brls::Application::mainLoop()){}trace("shutdown:begin");stopWorkers();save();return 0;
}
