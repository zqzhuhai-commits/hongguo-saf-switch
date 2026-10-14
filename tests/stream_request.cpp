#include "stream_request.hpp"
#include <cassert>
int main(){StreamRequest r;assert(parseStreamRequest("saf://123",r)&&r.id=="123"&&!r.fresh&&r.tier.empty());assert(parseStreamRequest("saf://123/video_1/0",r)&&r.tier=="video_1"&&!r.fresh);assert(parseStreamRequest("saf://123//1",r)&&r.fresh&&r.tier.empty());for(const auto& s:{"https://123","saf://x","saf://123/key/2","saf://123/key/0/x","saf://123/../0","saf://123/invalid%20/0","saf://123/key"})assert(!parseStreamRequest(s,r));}
