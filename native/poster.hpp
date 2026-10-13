// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <string>
#include <vector>
struct PosterPixels { int width=104,height=156; std::vector<unsigned char> rgba; };
PosterPixels fetchPoster(const std::string& url,const std::atomic<bool>& alive);
