// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <utility>
struct StreamRequest {std::string id,tier;bool fresh=false;};
inline bool validTier(const std::string& tier){return tier!="."&&tier!=".."&&tier.size()<=64&&tier.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")==std::string::npos;}
inline bool parseStreamRequest(const std::string& uri,StreamRequest& out){
 if(uri.rfind("saf://",0)!=0)return false;auto value=uri.substr(6);auto first=value.find('/');StreamRequest r;r.id=value.substr(0,first);
 if(r.id.empty()||r.id.size()>30||r.id.find_first_not_of("0123456789")!=std::string::npos)return false;
 if(first!=std::string::npos){auto last=value.find('/',first+1);if(last==std::string::npos)return false;r.tier=value.substr(first+1,last-first-1);auto fresh=value.substr(last+1);if(!validTier(r.tier)||(fresh!="0"&&fresh!="1"))return false;r.fresh=fresh=="1";}
 out=std::move(r);return true;
}
