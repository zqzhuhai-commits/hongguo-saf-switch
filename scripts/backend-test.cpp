// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include <cassert>
#include <iostream>
int main(){Backend backend;const Json r=backend.call("selftest",Json::array());
  assert(r.at("sm3")=="66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0");
  assert(r.at("md5")=="900150983cd24fb0d6963f7d28e17f72");
  assert(r.at("sha512")=="ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
  assert(r.at("cbc")=="6bc1bee22e409f96e93d7e117393172a");
  assert(r.at("ctr")=="6bc1bee22e409f96e93d7e117393172a");
  std::cout<<"QuickJS signatures and native digest/AES known-answer tests passed\n";
}
