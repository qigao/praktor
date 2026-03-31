#pragma once

#include <queue>
#include <string>
#include <mutex>
#include <vector>

namespace btdsl {

// Thread-safe event queue for behavior tree events
class EventQueue {
public:
  // Push an event to the queue
  void push(const std::string& event);

  // Check if event exists and consume it (removes from queue)
  bool poll(const std::string& event);

  // Check if event exists without consuming
  bool peek(const std::string& event) const;

  // Get all pending events
  std::vector<std::string> getAll() const;

  // Clear all events
  void clear();

  // Get queue size
  size_t size() const;

private:
  mutable std::mutex mutex_;
  std::queue<std::string> events_;
};

} // namespace btdsl
