#include "app_config.hpp"
#include <cassert>
#include <cstdio>
int main(){const char* p="/tmp/hongguo-config-test.cfg";{std::ofstream f(p);f<<"source=app158\nhwdec=auto\n# ignored\n";}AppConfig c;c.read(p);assert(c.get("source","")=="app158");{std::ofstream f(p);f<<" hwdec = no # comment\ninvalid\nsource=\n";}c.read(p);assert(c.get("hwdec","")=="no");assert(c.get("source","")=="app158");assert(c.get("missing","fallback")=="fallback");std::remove(p);}
