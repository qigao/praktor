#include "actions/event_queue.hpp"
#include <algorithm>

namespace actions {

void EventQueue::push(const std::string& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  events_.push(event);
}

bool EventQueue::poll(const std::string& event) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Search through queue
  std::queue<std::string> temp;
  bool found = false;

  while (!events_.empty()) {
    std::string current = events_.front();
    events_.pop();

    if (current == event && !found) {
      found = true;
      // Don't add back to queue (consume it)
    } else {
      temp.push(current);
    }
  }

  // Restore remaining events
  events_ = std::move(temp);
  return found;
}

bool EventQueue::peek(const std::string& event) const {
  std::lock_guard<std::mutex> lock(mutex_);

  // Create a copy to search
  std::queue<std::string> temp = events_;

  while (!temp.empty()) {
    if (temp.front() == event) {
      return true;
    }
    temp.pop();
  }

  return false;
}

std::vector<std::string> EventQueue::getAll() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::string> result;
  std::queue<std::string> temp = events_;

  while (!temp.empty()) {
    result.push_back(temp.front());
    temp.pop();
  }

  return result;
}

void EventQueue::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  events_ = std::queue<std::string>();
}

size_t EventQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return events_.size();
}

} // namespace actions
