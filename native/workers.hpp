// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <functional>
#include <string>
void trace(const std::string& stage);
void resetTrace();
bool startWorkers();
void stopWorkers();
bool apiTask(std::function<void()> task);
bool posterTask(std::function<void()> task);
bool inApiWorker();
