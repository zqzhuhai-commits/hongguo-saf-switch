// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <borealis/extern/nlohmann/json.hpp>
extern "C" {
#include "quickjs.h"
}
using Json=nlohmann::json;
class Backend {
  JSRuntime* runtime=nullptr;
  JSContext* context=nullptr;
  std::mutex lock;
  void evaluate(const std::string&,const char*);
public:
  Backend();
  ~Backend();
  Json call(const std::string& function,const Json& args);
};
std::vector<unsigned char> unhex(const std::string&);
std::string hex(const unsigned char*,size_t);
