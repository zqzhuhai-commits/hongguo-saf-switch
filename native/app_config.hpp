// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <fstream>
#include <map>
#include <string>
struct AppConfig {
  std::map<std::string,std::string> values;
  std::string get(const std::string& key,const std::string& fallback)const{auto i=values.find(key);return i==values.end()?fallback:i->second;}
  void read(const std::string& file){std::ifstream in(file);std::string line;while(std::getline(in,line)){auto comment=line.find('#');if(comment!=std::string::npos)line.resize(comment);auto equals=line.find('=');if(equals==std::string::npos)continue;auto trim=[](std::string s){auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return std::string();auto b=s.find_last_not_of(" \t\r");return s.substr(a,b-a+1);};auto key=trim(line.substr(0,equals)),value=trim(line.substr(equals+1));if(!key.empty()&&!value.empty())values[key]=value;}}
  static AppConfig load(){AppConfig c;c.read("romfs:/hongguo.cfg");c.read("sdmc:/switch/hongguo.cfg");return c;}
};
