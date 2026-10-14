#include "workers.hpp"
#include <cassert>
#include <future>
#include <vector>
#include <iostream>
int main(){
  assert(startWorkers());
  std::promise<void> entered,release,foreground,finished;
  auto gate=release.get_future().share();std::vector<int> order;
  assert(apiBackgroundTask([&]{assert(inApiBackground());entered.set_value();gate.wait();assert(apiForegroundPending());order.push_back(1);}));
  assert(entered.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready);
  assert(apiBackgroundTask([&]{assert(inApiBackground());order.push_back(3);finished.set_value();}));
  assert(apiTask([&]{assert(inApiWorker());assert(!inApiBackground());order.push_back(2);foreground.set_value();}));
  release.set_value();
  assert(foreground.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready);
  assert(finished.get_future().wait_for(std::chrono::seconds(3))==std::future_status::ready);
  stopWorkers();assert((order==std::vector<int>{1,2,3}));assert(!apiBackgroundTask([]{}));
  std::cout<<"PASS: background detects pending foreground; foreground runs ahead of queued prefetch\n";
}
