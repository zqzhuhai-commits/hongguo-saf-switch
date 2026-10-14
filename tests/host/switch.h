#pragma once
#include <chrono>
#include <thread>
#include <cstdint>
inline void svcSleepThread(int64_t ns){std::this_thread::sleep_for(std::chrono::nanoseconds(ns));}
inline uint64_t armGetSystemTick(){return std::chrono::steady_clock::now().time_since_epoch().count();}
