// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>
constexpr std::size_t episodeGroupSize=100;
inline std::size_t episodeGroupCount(std::size_t total){return total/episodeGroupSize+(total%episodeGroupSize?1:0);}
inline std::pair<std::size_t,std::size_t> episodeGroupBounds(std::size_t total,std::size_t group){if(group>=episodeGroupCount(total))return {total,total};const auto start=group*episodeGroupSize;return {start,start+std::min(episodeGroupSize,total-start)};}
