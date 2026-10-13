// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
struct PlaybackPolicy {
  static constexpr unsigned maxRetries=3;
  unsigned retries=0;uint64_t generation=0;bool pending=false,continuous=true;
  void begin(bool retry=false){++generation;pending=false;if(!retry)retries=0;}
  std::optional<unsigned> scheduleRetry(){if(pending||retries>=maxRetries)return {};pending=true;return 2000*++retries;}
  bool consumeRetry(uint64_t token){if(!pending||token!=generation)return false;pending=false;return true;}
  int nextEpisode(int current,int count)const{return continuous&&current>=0&&current+1<count?current+1:-1;}
};
