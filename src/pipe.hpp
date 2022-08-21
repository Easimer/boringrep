#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

#include "Tracy.hpp"

template <typename T>
struct Pipe {
  TracyLockable(std::mutex, mtx);
  std::condition_variable_any cv;
  std::queue<std::optional<T>> queue;

  auto lock() { return std::unique_lock(mtx); }
  auto empty() const { return queue.empty(); }
  auto wait(std::unique_lock<LockableBase(std::mutex)> &L) { cv.wait(L); }
  auto &front() { return queue.front(); }
  auto pop() { queue.pop(); }
  auto notify_one() { cv.notify_one(); }
  auto push(const T &t) { queue.push(t); }
  auto push(T &&t) { queue.push(std::move(t)); }
  auto push() { queue.push(std::nullopt); }
  auto notify_all() { cv.notify_all(); }
  template <typename Duration>
  auto wait_for(std::unique_lock<LockableBase(std::mutex)> &L, Duration dur) {
    cv.wait_for(L, dur);
  }
};
