#include "episode_groups.hpp"
#include <cassert>
#include <iostream>
int main(){
 for(std::size_t total=0;total<=2000;total++){
  std::size_t next=0;
  for(std::size_t group=0;group<episodeGroupCount(total);group++){
   auto bounds=episodeGroupBounds(total,group);assert(bounds.first==next);assert(bounds.second>bounds.first&&bounds.second<=total);assert(bounds.second-bounds.first<=100);next=bounds.second;
  }
  assert(next==total);assert(episodeGroupBounds(total,episodeGroupCount(total))==std::make_pair(total,total));
 }
 assert(episodeGroupCount(100)==1);assert(episodeGroupBounds(101,1)==std::make_pair(std::size_t(100),std::size_t(101)));
 assert(episodeGroupBounds(201,2)==std::make_pair(std::size_t(200),std::size_t(201)));
 std::cout<<"PASS: groups cover 0-2000 episodes without gaps, overlaps or wrong absolute indices; 100/101/201 boundaries valid\n";
}
