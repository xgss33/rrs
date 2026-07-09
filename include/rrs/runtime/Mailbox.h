#pragma once

#include <deque>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace rrs {

template <typename T>
class Mailbox {
public:
    Mailbox() = default;

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    [[nodiscard]] bool Push(T message)
    {
        std::scoped_lock lock(mutex_);
        queue_.push_back(std::move(message));
        return true;
    }

    [[nodiscard]] std::vector<T> Drain()
    {
        std::deque<T> drained;
        {
            std::scoped_lock lock(mutex_);
            drained.swap(queue_);
        }

        return std::vector<T>(
            std::make_move_iterator(drained.begin()),
            std::make_move_iterator(drained.end()));
    }

private:
    std::mutex mutex_;
    std::deque<T> queue_;
};

template <typename T>
class MailboxSender {
public:
    MailboxSender() = default;
    explicit MailboxSender(Mailbox<T>& mailbox) noexcept : mailbox_(&mailbox) {}

    [[nodiscard]] bool IsValid() const noexcept { return mailbox_ != nullptr; }

    [[nodiscard]] bool Push(T message) const
    {
        if (mailbox_ == nullptr) {
            return false;
        }

        return mailbox_->Push(std::move(message));
    }

private:
    Mailbox<T>* mailbox_{nullptr};
};

} // namespace rrs
