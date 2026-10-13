// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char**){Backend backend;const Json r=backend.call("selftest",Json::array());
  assert(r.at("sm3")=="66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0");
  assert(r.at("md5")=="900150983cd24fb0d6963f7d28e17f72");
  assert(r.at("sha512")=="ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
  assert(r.at("cbc")=="6bc1bee22e409f96e93d7e117393172a");
  assert(r.at("ctr")=="6bc1bee22e409f96e93d7e117393172a");
  std::cout<<"QuickJS signatures and native digest/AES known-answer tests passed\n";
  if(argc>1){try{auto list=backend.call("home",Json::array({1,"short"}));std::cout<<"Live catalog rows: "<<list.at("items").size()<<"\n";if(!list.at("items").empty()){auto detail=backend.call("detail",Json::array({list["items"][0]["id"]}));std::cout<<"Live episode count: "<<detail.at("episodes").size()<<"\n";auto id=detail["episodes"][0]["id"];auto stream=backend.call("open",Json::array({id,480}));auto bytes=unhex(backend.call("read",Json::array({id,0,1023})).get<std::string>());assert(bytes.size()==1024);assert(std::string((char*)bytes.data()+4,4)=="ftyp");std::cout<<"Live stream header decoded successfully\n";}}catch(const std::exception& e){std::cout<<"Live source probe failed: "<<e.what()<<"; live playback is not verified\n";return 2;}}
}
