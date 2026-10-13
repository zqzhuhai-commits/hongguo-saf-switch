// SPDX-License-Identifier: GPL-3.0-only
#include "../native/playback_touch.hpp"
#include <cassert>
#include <iostream>
int main(){PlaybackHold hold;
 hold.begin(true,1000,200,200);assert(hold.isTap());assert(!hold.tick(1399,200,200));assert(hold.tick(1400,200,200));assert(hold.boosting&&!hold.isTap());assert(!hold.tick(1800,205,200));hold.cancel();assert(!hold.boosting&&!hold.isTap());
 hold.begin(false,2000,50,680);assert(!hold.tick(9999,50,680)&&!hold.isTap()); // timeline or menu origin
 hold.begin(true,3000,200,200);assert(!hold.tick(3100,240,200));assert(!hold.tick(4000,200,200)&&!hold.isTap()); // moved finger is not a tap or hold
 hold.begin(true,5000,200,200);hold.cancel();assert(!hold.tick(6000,200,200)); // release, retry or episode switch cancels
 assert(playbackSeekAt(616,16,1200,200)==100);assert(playbackSeekAt(-40,16,1200,200)==0);assert(playbackSeekAt(1400,16,1200,200)==200);assert(playbackSeekAt(300,16,0,200)==0);assert(playbackSeekAt(300,16,1200,0)==0);
 std::cout<<"PASS: hold threshold, one activation, release/cancellation, excluded controls, movement rejection, seek coordinate clamping and unknown duration\n";
}
