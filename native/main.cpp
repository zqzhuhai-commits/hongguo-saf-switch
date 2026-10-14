// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include "app_config.hpp"
#include "episode_groups.hpp"
#include "stream_request.hpp"
#include "poster.hpp"
#include "workers.hpp"
#include "playback_policy.hpp"
#include "playback_touch.hpp"
#include <chrono>
#include <cstdio>
#include <borealis/views/image.hpp>
#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
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
// One touch target for the entire card, rather than its decorative children.
class ClickableBox:public brls::Box {
public:
  explicit ClickableBox(brls::Axis axis):brls::Box(axis){setFocusable(true);addGestureRecognizer(new brls::TapGestureRecognizer(this));}
  brls::View* hitTest(brls::Point point)override{return brls::Box::hitTest(point)?this:nullptr;}
};
static brls::Box* quickButton(const std::string& name,const std::string& note,std::function<void()> fn){auto* box=new ClickableBox(brls::Axis::COLUMN);box->setWidth(220);box->setHeight(88);box->setPadding(12);box->setMarginRight(12);box->setBackgroundColor(nvgRGB(35,38,49));for(const auto& text:{name,note}){auto* label=new brls::Label();label->setText(text);label->setWidth(196);label->setFontSize(text==name?20:14);label->setHorizontalAlign(brls::HorizontalAlign::LEFT);box->addView(label);}box->registerClickAction([fn](brls::View*){fn();return true;});return box;}
static brls::DetailCell* cell(const std::string& name,const std::string& note,std::function<void()> fn){auto* v=new brls::DetailCell();v->setText(name);v->setDetailText(note);v->addGestureRecognizer(new brls::TapGestureRecognizer(v));v->registerClickAction([fn](brls::View*){fn();return true;});return v;}
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
    else {nvgFontFaceId(vg,brls::Application::getDefaultFont());nvgFontSize(vg,15);nvgFillColor(vg,nvgRGB(160,165,170));nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgText(vg,x+w/2,y+h/2,finished||url.empty()?"暂无封面":"封面加载中",nullptr);}
  }
};
static brls::Box* posterCell(const Json& row,std::function<void()> fn){auto* box=new ClickableBox(brls::Axis::ROW);box->setMinHeight(172);box->setPadding(8);auto* poster=new PosterImage(row.value("cover",std::string()));poster->setMarginRight(20);poster->setShrink(0);box->addView(poster);auto* text=new brls::Box(brls::Axis::COLUMN);text->setGrow(1);text->setShrink(1);auto* title=new brls::Label();title->setText(row.value("title",std::string()));title->setWidthPercentage(100);title->setSingleLine(false);title->setFontSize(22);title->setHorizontalAlign(brls::HorizontalAlign::LEFT);text->addView(title);auto* note=new brls::Label();note->setText(row.value("badge",std::string()));note->setFontSize(16);note->setWidthPercentage(100);note->setHorizontalAlign(brls::HorizontalAlign::LEFT);text->addView(note);box->addView(text);box->registerClickAction([fn](brls::View*){fn();return true;});return box;}

static void addGrid(brls::Box* list,const std::vector<brls::View*>& tiles,size_t columns,float gap,float rowGap);
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
  StreamRequest request;if(!parseStreamRequest(uri,request))return MPV_ERROR_LOADING_FAILED;const auto& id=request.id;
  const Json meta=backend->call("open",Json::array({id,480,request.fresh,request.tier}));auto* r=new Reader();r->id=id;r->total=meta.at("total").get<int64_t>();if(r->total<=0){delete r;return MPV_ERROR_LOADING_FAILED;}
  info->cookie=r;info->read_fn=readStream;info->seek_fn=seekStream;info->size_fn=sizeStream;info->close_fn=closeStream;info->cancel_fn=cancelStream;return 0;
}catch(const std::exception& e){std::lock_guard<std::mutex> guard(playbackErrorMutex);const std::string uriValue(uri);StreamRequest failedRequest;playbackErrorId=parseStreamRequest(uriValue,failedRequest)?failedRequest.id:"";playbackError=e.what();return MPV_ERROR_LOADING_FAILED;}catch(...){return MPV_ERROR_LOADING_FAILED;}}
static void* getProc(void*,const char* name){return (void*)glfwGetProcAddress(name);}
// Continuous touch tracking is needed for release-to-restore, including cancellation.
class PlaybackTouchGesture:public brls::GestureRecognizer {
  std::function<void(brls::GestureState,brls::Point)> callback;bool exclusive,active=false;int finger=0;
public:
  PlaybackTouchGesture(std::function<void(brls::GestureState,brls::Point)> fn,bool claim=false):callback(fn),exclusive(claim){}
  brls::GestureState recognitionLoop(brls::TouchState touch,brls::MouseState mouse,brls::View*,brls::Sound* sound)override{
    auto phase=touch.phase;auto point=touch.position;if(phase==brls::TouchPhase::NONE){phase=mouse.leftButton;point=mouse.position;}*sound=brls::SOUND_NONE;
    if(phase==brls::TouchPhase::START){active=true;finger=touch.fingerId;state=exclusive?brls::GestureState::START:brls::GestureState::UNSURE;callback(state,point);return state;}
    if(!active)return brls::GestureState::FAILED;
    if(state==brls::GestureState::INTERRUPTED||phase==brls::TouchPhase::NONE||finger!=touch.fingerId){active=false;callback(brls::GestureState::INTERRUPTED,point);return state=brls::GestureState::FAILED;}
    if(phase==brls::TouchPhase::END){active=false;callback(brls::GestureState::END,point);return state=brls::GestureState::END;}
    callback(brls::GestureState::STAY,point);return state=exclusive?brls::GestureState::STAY:brls::GestureState::UNSURE;
  }
};
class PlaybackTimeline:public brls::View {
  double current=0,total=0,preview=0;bool dragging=false,available=false;
  std::function<void(double)> seek;
  void updateTouch(brls::Point p){preview=playbackSeekAt(p.x,getX()+16,getWidth()-32,total);}
public:
  explicit PlaybackTimeline(std::function<void(double)> fn):seek(fn){setFocusable(true);setHideHighlight(false);setHeight(64);
    addGestureRecognizer(new PlaybackTouchGesture([this](brls::GestureState state,brls::Point point){if(state==brls::GestureState::START){dragging=available;updateTouch(point);brls::Application::giveFocus(this);}else if(state==brls::GestureState::STAY&&dragging)updateTouch(point);else if(state==brls::GestureState::END){if(dragging){updateTouch(point);seek(preview);}dragging=false;}else if(state==brls::GestureState::INTERRUPTED)dragging=false;},true));
    registerAction("后退5秒",brls::BUTTON_LEFT,[this](brls::View*){if(available)seek(std::max(0.0,current-5));return true;});registerAction("前进5秒",brls::BUTTON_RIGHT,[this](brls::View*){if(available)seek(std::min(total,current+5));return true;});
  }
  void update(double at,double length,bool enabled){current=at;total=length;available=enabled&&length>0;if(!available)dragging=false;}
  void reset(){dragging=false;available=false;current=total=preview=0;}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style,brls::FrameContext*)override{
    const double shown=dragging?preview:current;const float left=x+16,width=std::max(1.0f,w-32),at=left+(float)playbackSeekAt(shown,0,total,1)*width;
    nvgBeginPath(vg);nvgRoundedRect(vg,x,y,w,h,8);nvgFillColor(vg,nvgRGBA(15,17,23,215));nvgFill(vg);
    nvgFontFaceId(vg,brls::Application::getDefaultFont());nvgFontSize(vg,16);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgFillColor(vg,nvgRGB(245,245,245));
    auto clock=[](double t){int s=std::max(0,(int)t);char b[32];std::snprintf(b,sizeof(b),"%d:%02d",s/60,s%60);return std::string(b);};const std::string label=clock(shown)+" / "+clock(total)+(dragging?"  松开跳转":available?"  拖动进度条跳转":"  等待视频加载");nvgText(vg,left,y+7,label.c_str(),nullptr);
    nvgBeginPath(vg);nvgRoundedRect(vg,left,y+40,width,6,3);nvgFillColor(vg,nvgRGB(105,110,122));nvgFill(vg);
    nvgBeginPath(vg);nvgRoundedRect(vg,left,y+40,std::max(0.0f,at-left),6,3);nvgFillColor(vg,nvgRGB(242,85,103));nvgFill(vg);
    nvgBeginPath(vg);nvgCircle(vg,at,y+43,dragging?11:8);nvgFillColor(vg,nvgRGB(255,255,255));nvgFill(vg);
  }
};

class Player:public brls::Box {
  mpv_handle* mpv=nullptr;mpv_render_context* renderer=nullptr;
  Json series,episodes;int index=0,speedIndex=1,rotation=0;
  bool failed=false,nextQueued=false,paused=false,errorState=false,menuVisible=false,loaded=false,holdApplied=false;
  double position=0,duration=0,resumeTarget=0;bool opened=false;uint64_t lastSave=0;size_t retryDelay=0;
  std::string status="正在连接播放源…",lastProgress,selectedTier,selectedQuality="自动";
  bool warmScheduled=false,prefetchEnabled=true,resumePaused=false;uint64_t loadStarted=0;
  std::shared_ptr<std::atomic<uint64_t>> warmGeneration=std::make_shared<std::atomic<uint64_t>>(0);
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);
  PlaybackHold hold;PlaybackTimeline* timeline=nullptr;
  PlaybackPolicy policy;brls::Box* controls=nullptr;brls::View* firstControl=nullptr;brls::Label* progress=nullptr;
  static std::string clockText(double value){int seconds=std::max(0,(int)value);char text[32];std::snprintf(text,sizeof(text),"%d:%02d",seconds/60,seconds%60);return text;}
  static uint64_t touchTime(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
  void stopHold(){if(holdApplied&&mpv){double rate=hold.previousSpeed;mpv_set_property(mpv,"speed",MPV_FORMAT_DOUBLE,&rate);}holdApplied=false;hold.cancel();}
  void seekTo(double at){if(!mpv||failed||errorState||!loaded||duration<=0)return;stopHold();const std::string value=std::to_string(std::clamp(at,0.0,duration));const char* cmd[]={"seek",value.c_str(),"absolute",nullptr};if(mpv_command(mpv,cmd)>=0){position=std::clamp(at,0.0,duration);persist();}}
  void screenTouch(brls::GestureState state,brls::Point point){if(state==brls::GestureState::UNSURE){stopHold();const bool excluded=menuVisible&&(controls->getFrame().pointInside(point)||timeline->getFrame().pointInside(point));hold.begin(!excluded,touchTime(),point.x,point.y);}else if(state==brls::GestureState::STAY){if(!getFrame().pointInside(point)){stopHold();return;}if(hold.tick(touchTime(),point.x,point.y)&&mpv&&!failed&&!errorState&&loaded&&!paused){hold.previousSpeed=1;mpv_get_property(mpv,"speed",MPV_FORMAT_DOUBLE,&hold.previousSpeed);double rate=2;holdApplied=mpv_set_property(mpv,"speed",MPV_FORMAT_DOUBLE,&rate)>=0;}}else if(state==brls::GestureState::END){const bool tap=hold.isTap();stopHold();if(tap)showMenu(!menuVisible);}else if(state==brls::GestureState::INTERRUPTED)stopHold();}
  void persist(){if(opened&&(position>0||duration>0)&&index>=0&&index<(int)episodes.size())record(series,episodes[index],index,position,duration);}
  void showMenu(bool visible){stopHold();menuVisible=visible;if(!visible)brls::Application::giveFocus(this);controls->setVisibility(visible?brls::Visibility::VISIBLE:brls::Visibility::GONE);timeline->setVisibility(visible?brls::Visibility::VISIBLE:brls::Visibility::GONE);if(visible&&firstControl)brls::Application::giveFocus(firstControl);}
  void togglePause(){stopHold();if(errorState){retryNow();return;}if(!mpv||failed)return;paused=!paused;int value=paused;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&value);}
  void seek(int seconds){if(!mpv||failed||errorState)return;const std::string value=std::to_string(seconds);const char* cmd[]={"seek",value.c_str(),"relative",nullptr};mpv_command(mpv,cmd);}
  void cycleSpeed(){stopHold();if(!mpv||failed)return;const double rates[]={0.75,1,1.25,1.5,2};speedIndex=(speedIndex+1)%5;double rate=rates[speedIndex];mpv_set_property(mpv,"speed",MPV_FORMAT_DOUBLE,&rate);}
  void rotateVideo(){stopHold();if(!mpv||failed)return;int64_t angle=rotation==0?90:0;if(mpv_set_property(mpv,"video-rotate",MPV_FORMAT_INT64,&angle)>=0)rotation=(int)angle;else status="当前视频暂不支持方向切换";}
  void retryNow(){if(mpv&&renderer&&!failed)startEpisode(index,false,true);}
  void scheduleRetry(){stopHold();timeline->reset();errorState=true;if(policy.pending)return;auto wait=policy.scheduleRetry();if(!wait){status="本集暂时无法播放，可重试或选择其他分集。";showMenu(true);return;}trace("playback:retry_scheduled:"+std::to_string(policy.retries));status="正在重新获取播放源（重试 "+std::to_string(policy.retries)+"/3）…";auto flag=alive;const auto token=policy.generation;retryDelay=brls::delay(*wait,[this,flag,token]{if(!flag->load())return;if(policy.consumeRetry(token)){retryDelay=0;startEpisode(index,true);}});}
  void startEpisode(int target,bool retry=false,bool fresh=false,bool preservePause=false){if(!mpv||!renderer||failed||target<0||target>=(int)episodes.size())return;stopHold();timeline->reset();if(retryDelay){brls::cancelDelay(retryDelay);retryDelay=0;}resumePaused=preservePause?paused:(retry?resumePaused:false);if(target!=index){selectedTier.clear();selectedQuality="自动";}policy.begin(retry);warmGeneration->store(policy.generation);warmScheduled=false;loadStarted=touchTime();persist();index=target;position=0;duration=0;opened=true;resumeTarget=0;for(const auto& row:library["history"])if(row.value("episodeId",std::string())==episodes[index].value("id",std::string()))resumeTarget=row.value("position",0.0)/1000;nextQueued=false;paused=false;errorState=false;loaded=false;int unpause=0;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&unpause);status=retry?"正在重新连接（重试 "+std::to_string(policy.retries)+"/3）…":"正在连接播放源…";{std::lock_guard<std::mutex> lock(playbackErrorMutex);playbackErrorId.clear();playbackError.clear();}const auto url="saf://"+episodes[index].at("id").get<std::string>()+"/"+selectedTier+"/"+((retry||fresh)?"1":"0");const char* command[]={"loadfile",url.c_str(),"replace",nullptr};trace("playback:load_begin");if(mpv_command(mpv,command)<0)scheduleRetry();}
  void showQualities(){
    if(!mpv||failed)return;stopHold();const auto id=episodes[index].at("id").get<std::string>();auto flag=alive;
    auto* list=new brls::Box(brls::Axis::COLUMN);auto* picker=::frame(list,"选择清晰度");back(picker);auto pickerFlag=picker->alive;
    auto select=[this,flag,id](const std::string& tier,const std::string& label){if(!*flag)return;const bool current=episodes[index].at("id").get<std::string>()==id;brls::Application::popActivity();if(current&&tier!=selectedTier){selectedTier=tier;selectedQuality=label;startEpisode(index,false,false,true);}};
    auto* automatic=cell("自动","优先使用兼容画质",[select]{select("","自动");});list->addView(automatic);auto* waiting=new brls::Label();waiting->setText("正在读取片源可用画质…");list->addView(waiting);brls::Application::pushActivity(new brls::Activity(picker));
    apiTask([flag,pickerFlag,list,id,select]{Json choices=Json::array();bool ok=false;try{choices=backend->call("qualities",Json::array({id}));ok=choices.is_array();}catch(...){}brls::sync([flag,pickerFlag,list,choices,ok,select]{if(!*flag||!*pickerFlag)return;list->clearViews();auto* automatic=cell("自动","优先使用兼容画质",[select]{select("","自动");});list->addView(automatic);
      if(ok)for(const auto& row:choices){auto tier=row.value("key",std::string()),label=row.value("label",std::string());if(tier.empty()||!validTier(tier))continue;const auto codec=row.value("codec",std::string());list->addView(cell(label,"切换后继续当前进度",[select,tier,label]{select(tier,label);}));}
      if(!ok||choices.empty()){auto* note=new brls::Label();note->setText(ok?"本片源暂未提供其他清晰度":"清晰度读取失败，请返回后重试");list->addView(note);}brls::Application::giveFocus(automatic);
    });});
  }
  void buildMenu(){controls=new brls::Box(brls::Axis::COLUMN);controls->detach();controls->setDetachedPosition(24,346);controls->setSize(brls::Size(1232,286));controls->setPadding(16);controls->setBackgroundColor(nvgRGBA(15,17,23,235));progress=new brls::Label();progress->setWidth(1200);progress->setHeight(24);progress->setFontSize(18);progress->setHorizontalAlign(brls::HorizontalAlign::LEFT);progress->setMarginBottom(12);controls->addView(progress);std::vector<brls::View*> buttons;
    auto add=[&](const std::string& title,std::function<void()> fn){auto* button=new ClickableBox(brls::Axis::COLUMN);button->setWidth(284);button->setHeight(56);button->setPadding(10);button->setBackgroundColor(nvgRGB(42,46,58));auto* label=new brls::Label();label->setText(title);label->setFontSize(18);label->setHorizontalAlign(brls::HorizontalAlign::CENTER);button->addView(label);button->registerClickAction([fn](brls::View*){fn();return true;});buttons.push_back(button);};
    add("播放 / 暂停",[this]{togglePause();});add("后退 10 秒",[this]{seek(-10);});add("前进 10 秒",[this]{seek(10);});add("横竖切换",[this]{rotateVideo();});
    add("上一集",[this]{startEpisode(index-1);});add("下一集",[this]{startEpisode(index+1);});add("倍速",[this]{cycleSpeed();});add("自动连播 开 / 关",[this]{policy.continuous=!policy.continuous;library["autoplay"]=policy.continuous;save();});
    add("清晰度",[this]{showQualities();});add("重试本集",[this]{retryNow();});add("隐藏菜单",[this]{showMenu(false);});add("返回列表",[]{brls::Application::popActivity();});addGrid(controls,buttons,4,12,8);firstControl=buttons.front();controls->setVisibility(brls::Visibility::GONE);addView(controls);timeline=new PlaybackTimeline([this](double at){seekTo(at);});timeline->detach();timeline->setDetachedPosition(24,644);timeline->setSize(brls::Size(1232,64));timeline->setVisibility(brls::Visibility::GONE);timeline->registerAction("控制菜单",brls::BUTTON_UP,[this](brls::View*){showMenu(true);return true;});addView(timeline);
  }
public:
  Player(Json detail,int episode):brls::Box(brls::Axis::COLUMN),series(detail),episodes(detail.at("episodes")),index(episode){
    setGrow(1);setBackgroundColor(nvgRGB(0,0,0));setFocusable(true);setHideHighlight(true);appletSetMediaPlaybackState(true);if(library.contains("autoplay")&&library["autoplay"].is_boolean())policy.continuous=library["autoplay"].get<bool>();buildMenu();
    addGestureRecognizer(new PlaybackTouchGesture([this](brls::GestureState state,brls::Point point){screenTouch(state,point);}));
    registerAction("返回",brls::BUTTON_B,[this](brls::View*){if(menuVisible)showMenu(false);else brls::Application::popActivity();return true;});
    registerAction("播放/暂停",brls::BUTTON_A,[this](brls::View*){togglePause();return true;});registerAction("菜单",brls::BUTTON_X,[this](brls::View*){showMenu(!menuVisible);return true;});
    registerAction("后退10秒",brls::BUTTON_LEFT,[this](brls::View*){if(menuVisible)return false;seek(-10);return true;});registerAction("前进10秒",brls::BUTTON_RIGHT,[this](brls::View*){if(menuVisible)return false;seek(10);return true;});
    registerAction("下一集",brls::BUTTON_RB,[this](brls::View*){startEpisode(index+1);return true;});registerAction("上一集",brls::BUTTON_LB,[this](brls::View*){startEpisode(index-1);return true;});registerAction("倍速",brls::BUTTON_Y,[this](brls::View*){cycleSpeed();return true;});registerAction("横竖切换",brls::BUTTON_RT,[this](brls::View*){rotateVideo();return true;},false,false);
    mpv=mpv_create();if(!mpv){failed=true;status="播放器初始化失败";return;}
    mpv_set_option_string(mpv,"config","no");mpv_set_option_string(mpv,"terminal","no");mpv_set_option_string(mpv,"vo","libmpv");auto config=AppConfig::load();prefetchEnabled=config.get("playback.prefetch","yes")=="yes";auto hwdec=config.get("hwdec","auto");if(hwdec!="auto"&&hwdec!="no"&&hwdec!="nvdec"&&hwdec!="nvdec-copy")hwdec="auto";mpv_set_option_string(mpv,"hwdec",hwdec.c_str());mpv_set_option_string(mpv,"audio-channels","stereo");mpv_set_option_string(mpv,"cache","yes");mpv_set_option_string(mpv,"demuxer-max-bytes","16MiB");mpv_set_option_string(mpv,"opengl-glfinish","yes");mpv_set_option_string(mpv,"vd-lavc-dr","yes");
    if(mpv_stream_cb_add_ro(mpv,"saf",nullptr,openStream)<0||mpv_initialize(mpv)<0){failed=true;status="播放器初始化失败";return;}
    mpv_opengl_init_params gl{getProc,nullptr};mpv_render_param params[]={{MPV_RENDER_PARAM_API_TYPE,(void*)MPV_RENDER_API_TYPE_OPENGL},{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS,&gl},{MPV_RENDER_PARAM_INVALID,nullptr}};
    if(mpv_render_context_create(&renderer,mpv,params)<0){failed=true;status="视频渲染初始化失败";return;}startEpisode(index);
  }
  ~Player(){stopHold();*alive=false;if(retryDelay)brls::cancelDelay(retryDelay);persist();if(renderer)mpv_render_context_free(renderer);if(mpv)mpv_terminate_destroy(mpv);appletSetMediaPlaybackState(false);}
  void draw(NVGcontext* vg,float x,float y,float w,float h,brls::Style style,brls::FrameContext* ctx)override{
    if(mpv&&!failed){for(int count=0;count<30;count++){mpv_event* event=mpv_wait_event(mpv,0);if(event->event_id==MPV_EVENT_NONE)break;if(event->event_id==MPV_EVENT_FILE_LOADED){status="";errorState=false;loaded=true;trace("playback:loaded:ms="+std::to_string(touchTime()-loadStarted));if(resumeTarget>0){const std::string at=std::to_string(resumeTarget);const char* seekCommand[]={"seek",at.c_str(),"absolute",nullptr};mpv_command(mpv,seekCommand);resumeTarget=0;}if(resumePaused){int p=1;mpv_set_property(mpv,"pause",MPV_FORMAT_FLAG,&p);paused=true;resumePaused=false;}}
      if(event->event_id==MPV_EVENT_END_FILE){auto* end=(mpv_event_end_file*)event->data;if(end&&end->reason==MPV_END_FILE_REASON_ERROR){trace("playback:failed");scheduleRetry();}else if(end&&end->reason==MPV_END_FILE_REASON_EOF&&!nextQueued){nextQueued=true;if(duration>0)position=duration;persist();const int next=policy.nextEpisode(index,(int)episodes.size());if(next>=0){auto flag=alive;const auto generation=policy.generation;brls::sync([this,flag,next,generation]{if(flag->load()&&generation==policy.generation)startEpisode(next);});}else{status=index+1==(int)episodes.size()?"本剧已播放完毕":"本集已结束，自动连播已关闭";showMenu(true);}}}
    }mpv_get_property(mpv,"time-pos",MPV_FORMAT_DOUBLE,&position);mpv_get_property(mpv,"duration",MPV_FORMAT_DOUBLE,&duration);if(armGetSystemTick()-lastSave>armNsToTicks(10000000000ULL)){lastSave=armGetSystemTick();persist();}}
    if(prefetchEnabled&&loaded&&!errorState&&!warmScheduled&&duration-position>12&&position>2&&index+1<(int)episodes.size()){
      double buffered=0;mpv_get_property(mpv,"demuxer-cache-duration",MPV_FORMAT_DOUBLE,&buffered);
      if(buffered>=10){auto flag=alive;auto generation=warmGeneration;const auto token=policy.generation;const auto id=episodes[index+1].at("id").get<std::string>();const std::string tier;
        warmScheduled=apiBackgroundTask([flag,generation,token,id,tier]{if(!*flag||generation->load()!=token)return;try{const auto r=backend->call("prefetch",Json::array({id,480,tier}));trace(r.value("ready",false)?"prefetch:ready":"prefetch:deferred");}catch(...){trace("prefetch:deferred");}});
      }
    }
    nvgEndFrame(vg);GLint fbo=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,brls::Application::windowWidth,brls::Application::windowHeight);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if(renderer&&!failed){mpv_opengl_fbo surface{fbo,(int)brls::Application::windowWidth,(int)brls::Application::windowHeight,0};int flip=1;mpv_render_param params[]={{MPV_RENDER_PARAM_OPENGL_FBO,&surface},{MPV_RENDER_PARAM_FLIP_Y,&flip},{MPV_RENDER_PARAM_INVALID,nullptr}};mpv_render_context_render(renderer,params);mpv_render_context_report_swap(renderer);}else{glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);}
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    const auto factor=brls::Application::getPlatform()->getVideoContext()->getScaleFactor();nvgBeginFrame(vg,brls::Application::windowWidth,brls::Application::windowHeight,factor);nvgScale(vg,brls::Application::windowScale,brls::Application::windowScale);
    nvgFontFaceId(vg,brls::Application::getDefaultFont());nvgFontSize(vg,22);nvgFillColor(vg,nvgRGBA(255,255,255,240));nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
    const std::string title=series.value("title",std::string())+" · 第 "+std::to_string(index+1)+" 集";nvgTextBox(vg,28,20,1224,title.c_str(),nullptr);if(!status.empty())nvgTextBox(vg,60,180,1160,status.c_str(),nullptr);
    timeline->update(position,duration,loaded&&!errorState&&!failed);if(holdApplied){nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_TOP);nvgText(vg,640,90,"2倍速播放中 · 松开恢复",nullptr);}
    if(menuVisible){const double rates[]={0.75,1,1.25,1.5,2};const std::string text="第 "+std::to_string(index+1)+" / "+std::to_string(episodes.size())+" 集  ·  "+clockText(position)+" / "+clockText(duration)+"  ·  "+(errorState?"播放失败":!loaded?"连接中":paused?"已暂停":"播放中")+"  ·  连播"+(policy.continuous?"开":"关")+"  ·  倍速 "+std::to_string(rates[speedIndex]).substr(0,4)+"  ·  清晰度 "+selectedQuality+"  ·  方向 "+std::to_string(rotation)+"°";if(text!=lastProgress){lastProgress=text;progress->setText(text);}}else{nvgFontSize(vg,16);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_BOTTOM);nvgText(vg,28,brls::Application::contentHeight-4,"点屏幕 / X 菜单  ·  长按画面 2倍速  ·  A 暂停  ·  L/R 选集  ·  ZR 切屏  ·  B 返回",nullptr);}
    brls::Box::draw(vg,x,y,w,h,style,ctx);
  }
};

static void showDetail(const Json& series);
static brls::View* posterTile(const Json& row,float width=180);
static void addGrid(brls::Box* list,const std::vector<brls::View*>& tiles,size_t columns,float gap,float rowGap){brls::Box* row=nullptr;for(size_t i=0;i<tiles.size();i++){if(i%columns==0){row=new brls::Box(brls::Axis::ROW);row->setMarginBottom(rowGap);list->addView(row);}auto* tile=tiles[i];if(i%columns!=columns-1)tile->setMarginRight(gap);row->addView(tile);if(i>=columns)tile->setCustomNavigationRoute(brls::FocusDirection::UP,tiles[i-columns]);const size_t nextRow=(i/columns+1)*columns;if(nextRow<tiles.size())tile->setCustomNavigationRoute(brls::FocusDirection::DOWN,tiles[std::min(i+columns,tiles.size()-1)]);}}
class Page:public brls::Box {
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);brls::Box* list;
public:
  Page(const std::string& method,const Json& args):brls::Box(brls::Axis::COLUMN){this->setGrow(1);list=new brls::Box(brls::Axis::COLUMN);list->setPadding(20,50,30,50);auto* label=new brls::Label();label->setText("正在加载…");list->addView(label);auto* scroll=new brls::ScrollingFrame();scroll->setGrow(1);scroll->setContentView(list);this->addView(scroll);back(this);auto flag=alive;
    apiTask([this,flag,method,args]{Json response;bool ok=false;try{response=backend->call(method,args);ok=true;}catch(...){}brls::sync([this,flag,ok,response,method,args]{if(!*flag)return;list->clearViews();if(!ok){list->addView(cell("内容源或网络暂不可用","按 B 返回后重试",[]{}));return;}auto* heading=new brls::Label();heading->setFontSize(25);heading->setText(method=="search"?"搜索结果":args[1]=="comic"?"人气漫剧":"热播短剧");heading->setMarginBottom(18);list->addView(heading);std::vector<brls::View*> tiles;for(const auto& row:response["items"]){if(!row.is_object()||!row.contains("id")||!row.contains("title"))continue;tiles.push_back(posterTile(row));}addGrid(list,tiles,6,16,20);if(tiles.empty())list->addView(cell("暂无内容","按 B 返回",[]{}));else brls::Application::giveFocus(tiles.front());if(response.value("hasMore",false)){list->addView(cell("下一页","继续加载",[method,args]{Json next=args;const int at=method=="home"?0:1;next[at]=next[at].get<int>()+1;brls::Application::pushActivity(new brls::Activity(new Page(method,next)));}));}});});
  }
  ~Page(){*alive=false;}
};
static void showDetail(const Json& series){auto* list=new brls::Box(brls::Axis::COLUMN);auto* title=new brls::Label();title->setText("正在加载分集…");list->addView(title);auto* f=frame(list,series.value("title",std::string()));brls::Application::pushActivity(new brls::Activity(f));
  // The frame owns the flag; work never touches a destroyed view.
  auto flag=f->alive;
  apiTask([list,flag,series]{Json detail;bool ok=false;try{detail=backend->call("detail",Json::array({series.at("id")}));ok=true;}catch(...){}brls::sync([list,flag,ok,detail]{if(!*flag)return;list->clearViews();if(!ok){list->addView(cell("分集暂不可用","按 B 返回",[]{}));return;}
    list->addView(posterCell(detail,[]{}));
    list->addView(cell("收藏 / 取消收藏","保存在当前主机",[detail]{auto& rows=library["favorites"];auto it=std::find_if(rows.begin(),rows.end(),[&](const Json& row){return row.value("id",std::string())==detail.value("id",std::string());});if(it==rows.end()){Json row=detail;row.erase("episodes");rows.push_back(row);message("已收藏");}else{rows.erase(it);message("已取消收藏");}save();}));
    auto sharedDetail=std::make_shared<const Json>(detail);
    const size_t total=detail["episodes"].size(),groups=episodeGroupCount(total);
    auto* label=new brls::Label();label->setFontSize(24);label->setMarginTop(20);label->setMarginBottom(16);list->addView(label);
    auto ranges=std::make_shared<std::vector<brls::View*>>();
    auto* grid=new brls::Box(brls::Axis::COLUMN);
    auto render=[grid,label,sharedDetail,total,ranges](size_t group){
      const auto bounds=episodeGroupBounds(total,group);
      label->setText("选择分集 · 共 "+std::to_string(total)+" 集"+(total>100?" · 当前 "+std::to_string(bounds.first+1)+"-"+std::to_string(bounds.second)+" 集":""));
      grid->clearViews();std::vector<brls::View*> buttons;
      for(size_t i=bounds.first;i<bounds.second;i++){
        auto* button=new ClickableBox(brls::Axis::COLUMN);button->setWidth(106);button->setHeight(56);button->setPadding(12);button->setBackgroundColor(nvgRGB(35,38,49));
        auto* number=new brls::Label();number->setText(std::to_string(i+1));number->setFontSize(22);number->setHorizontalAlign(brls::HorizontalAlign::CENTER);button->addView(number);
        button->registerClickAction([sharedDetail,i](brls::View*){brls::Application::pushActivity(new brls::Activity(new Player(*sharedDetail,(int)i)));return true;});buttons.push_back(button);
      }
      addGrid(grid,buttons,10,10,12);
      for(size_t i=0;i<ranges->size();i++){(*ranges)[i]->setBackgroundColor(i==group?nvgRGB(180,50,70):nvgRGB(35,38,49));if(!buttons.empty())(*ranges)[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN,buttons.front());}
      if(group<ranges->size())for(size_t i=0;i<std::min<size_t>(10,buttons.size());i++)buttons[i]->setCustomNavigationRoute(brls::FocusDirection::UP,(*ranges)[group]);
      if(!buttons.empty())brls::Application::giveFocus(buttons.front());
    };
    if(groups>1){
      auto* scroll=new brls::HScrollingFrame();scroll->setHeight(72);auto* row=new brls::Box(brls::Axis::ROW);
      for(size_t group=0;group<groups;group++){
        const auto bounds=episodeGroupBounds(total,group);auto* button=new ClickableBox(brls::Axis::COLUMN);button->setWidth(150);button->setHeight(56);button->setPadding(12);button->setMarginRight(12);button->setShrink(0);
        auto* text=new brls::Label();text->setText(std::to_string(bounds.first+1)+"-"+std::to_string(bounds.second)+" 集");text->setFontSize(18);text->setHorizontalAlign(brls::HorizontalAlign::CENTER);button->addView(text);
        button->registerClickAction([render,group](brls::View*){render(group);return true;});row->addView(button);ranges->push_back(button);
        if(group){button->setCustomNavigationRoute(brls::FocusDirection::LEFT,(*ranges)[group-1]);(*ranges)[group-1]->setCustomNavigationRoute(brls::FocusDirection::RIGHT,button);}
      }
      scroll->setContentView(row);list->addView(scroll);
    }
    list->addView(grid);render(0);

  });});
}
static void openSearch(){SwkbdConfig keyboard;if(R_FAILED(swkbdCreate(&keyboard,0)))return;swkbdConfigMakePresetDefault(&keyboard);swkbdConfigSetStringLenMax(&keyboard,80);swkbdConfigSetGuideText(&keyboard,"输入剧名");char text[512]={};Result result=swkbdShow(&keyboard,text,sizeof(text));swkbdClose(&keyboard);if(R_SUCCEEDED(result)&&text[0])brls::Application::pushActivity(new brls::Activity(new Page("search",Json::array({std::string(text),1,"short"}))));}
static void openLibrary(bool favorites){auto* box=new brls::Box(brls::Axis::COLUMN);if(favorites){for(const auto& row:library["favorites"])box->addView(posterCell(row,[row]{showDetail(row);}));}else{for(const auto& row:library["history"]){const auto series=row.value("series",Json::object());box->addView(posterCell(series,[series]{showDetail(series);}));}}brls::Application::pushActivity(new brls::Activity(frame(box,favorites?"我的收藏":"最近观看")));}
static brls::View* posterTile(const Json& row,float width){
  constexpr float padding=8,titleHeight=72,badgeHeight=24;
  const float innerWidth=width-2*padding,coverHeight=innerWidth*1.5f;
  auto* tile=new ClickableBox(brls::Axis::COLUMN);
  tile->setWidth(width);tile->setHeight(2*padding+coverHeight+12+titleHeight+8+badgeHeight);
  tile->setPadding(padding);tile->setShrink(0);tile->setCornerRadius(14);
  tile->setBackgroundColor(nvgRGB(35,38,49));
  auto* cover=new PosterImage(row.value("cover",std::string()),innerWidth,coverHeight);
  cover->setShrink(0);cover->setCornerRadius(10);cover->setScalingType(brls::ImageScalingType::FILL);tile->addView(cover);
  auto* title=new brls::Label();title->setText(row.value("title",std::string("红果短剧")));
  title->setWidth(innerWidth);title->setHeight(titleHeight);title->setShrink(0);title->setMarginTop(12);
  title->setFontSize(18);title->setLineHeight(1.2f);title->setSingleLine(false);title->setClipsToBounds(true);
  title->setTextColor(nvgRGB(245,245,247));title->setHorizontalAlign(brls::HorizontalAlign::LEFT);
  title->setVerticalAlign(brls::VerticalAlign::TOP);tile->addView(title);
  auto* note=new brls::Label();note->setText(row.value("badge",std::string()));
  note->setWidth(innerWidth);note->setHeight(badgeHeight);note->setShrink(0);note->setMarginTop(8);
  note->setSingleLine(true);note->setFontSize(14);note->setTextColor(nvgRGB(156,164,178));
  note->setHorizontalAlign(brls::HorizontalAlign::LEFT);note->setVerticalAlign(brls::VerticalAlign::TOP);tile->addView(note);
  tile->registerClickAction([row](brls::View*){showDetail(row);return true;});
  tile->registerAction("完整剧名",brls::BUTTON_X,[row](brls::View*){message(row.value("title",std::string()));return true;});return tile;
}

static brls::Label* sectionTitle(const std::string& text){auto* label=new brls::Label();label->setText(text);label->setFontSize(25);label->setTextColor(nvgRGB(255,255,255));label->setHorizontalAlign(brls::HorizontalAlign::LEFT);label->setMarginTop(20);label->setMarginBottom(10);return label;}
class HomeDashboard:public brls::Box{
  std::shared_ptr<std::atomic<bool>> alive=std::make_shared<std::atomic<bool>>(true);brls::Box* content;
  void addCatalog(brls::Box* shelf,const std::string& title,const Json& result,bool ok){trace("home:render_begin");shelf->clearViews();auto* header=new brls::Box(brls::Axis::ROW);auto* heading=sectionTitle(title);heading->setGrow(1);header->addView(heading);if(ok&&result.value("items",Json::array()).size()>5){auto* hint=new brls::Label();hint->setText("向右滑动 →");hint->setFontSize(16);hint->setWidth(160);hint->setMarginTop(20);hint->setHorizontalAlign(brls::HorizontalAlign::RIGHT);hint->setTextColor(nvgRGB(183,190,204));header->addView(hint);}shelf->addView(header);if(!ok){shelf->addView(cell("暂时无法加载","按 B 返回后重试",[]{}));trace("home:render_error");return;}auto* scroller=new brls::HScrollingFrame();scroller->setHeight(432);auto* row=new brls::Box(brls::Axis::ROW);for(const auto& item:result.value("items",Json::array())){if(row->getChildren().size()>=12)break;if(item.is_object()&&item.contains("id")&&item.contains("title")){auto* tile=posterTile(item,200);tile->setMarginRight(16);row->addView(tile);}}if(row->getChildren().empty())row->addView(cell("暂时没有内容","请稍后重试",[]{}));scroller->setContentView(row);shelf->addView(scroller);trace("home:render_complete");}
public:
  HomeDashboard():brls::Box(brls::Axis::COLUMN){setGrow(1);setPadding(24,40,30,40);auto* cleanup=new brls::Box(brls::Axis::COLUMN);cleanup->setPaddingBottom(12);cleanup->addView(cell("一键清除视频缓存","清理播放源缓存，保留收藏和观看记录",[this]{auto flag=alive;apiTask([flag]{bool ok=false;try{backend->call("clearCache",Json::array());ok=true;}catch(...){}brls::sync([flag,ok]{if(!*flag)return;message(ok?"视频缓存已清除":"缓存清理失败，请重试");});});}));addView(cleanup);
    auto* hero=new brls::Box(brls::Axis::COLUMN);hero->setPadding(20);hero->setBackgroundColor(nvgRGB(35,38,49));auto* eyebrow=new brls::Label();eyebrow->setText("发现下一部好剧");eyebrow->setFontSize(14);eyebrow->setTextColor(nvgRGB(255,112,100));eyebrow->setHorizontalAlign(brls::HorizontalAlign::LEFT);hero->addView(eyebrow);auto* heading=new brls::Label();heading->setText("好剧上新，随时开看");heading->setFontSize(31);heading->setTextColor(nvgRGB(255,255,255));heading->setHorizontalAlign(brls::HorizontalAlign::LEFT);hero->addView(heading);auto* subtitle=new brls::Label();subtitle->setText("短剧与漫剧精选 · 为你发现下一段故事");subtitle->setTextColor(nvgRGB(183,190,204));subtitle->setHorizontalAlign(brls::HorizontalAlign::LEFT);hero->addView(subtitle);addView(hero);
    auto* nav=new brls::Box(brls::Axis::ROW);nav->setPaddingTop(12);nav->addView(quickButton("短剧","热播推荐",[]{brls::Application::pushActivity(new brls::Activity(new Page("home",Json::array({1,"short"}))));}));nav->addView(quickButton("漫剧","人气榜单",[]{brls::Application::pushActivity(new brls::Activity(new Page("home",Json::array({1,"comic"}))));}));nav->addView(quickButton("搜索","查找剧名",openSearch));nav->addView(quickButton("收藏","我的片单",[]{openLibrary(true);}));nav->addView(quickButton("最近观看","接着看",[]{openLibrary(false);}));addView(nav);
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
