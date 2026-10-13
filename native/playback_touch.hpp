// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
struct PlaybackHold {
  bool tracking=false,boosting=false,moved=false;uint64_t started=0;double x=0,y=0,previousSpeed=1;
  void begin(bool eligible,uint64_t now,double px,double py){tracking=eligible;boosting=false;moved=false;started=now;x=px;y=py;}
  bool tick(uint64_t now,double px,double py){if(!tracking)return false;if(!boosting&&std::hypot(px-x,py-y)>24){moved=true;tracking=false;return false;}if(!boosting&&now-started>=400){boosting=true;return true;}return false;}
  bool isTap()const{return tracking&&!boosting&&!moved;}
  void cancel(){tracking=false;boosting=false;moved=false;}
};
inline double playbackSeekAt(double x,double left,double width,double duration){return width>0&&duration>0?std::clamp((x-left)/width,0.0,1.0)*duration:0;}
