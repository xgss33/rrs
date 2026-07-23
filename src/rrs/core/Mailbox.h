#pragma once

#include <deque>
#include <functional>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace rrs {

template <typename T>
class Mailbox {
public:
    using NotifyCallback = std::function<void()>;

    explicit Mailbox(NotifyCallback notify_callback = {})
        : notify_callback_(std::move(notify_callback))
    {
    }

    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;

    void Push(T message)
    {
        bool should_notify = false;
        {
            std::scoped_lock lock(mutex_);
            should_notify = queue_.empty();
            queue_.push_back(std::move(message));
        }

        if (should_notify && notify_callback_) {
            notify_callback_();
        }
    }

    void PushBatch(std::vector<T>&& messages)
    {
        if (messages.empty()) {
            return;
        }

        bool should_notify = false;
        {
            std::scoped_lock lock(mutex_);
            should_notify = queue_.empty();
            queue_.insert(
                queue_.end(),
                std::make_move_iterator(messages.begin()),
                std::make_move_iterator(messages.end()));
        }

        if (should_notify && notify_callback_) {
            notify_callback_();
        }
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
    NotifyCallback notify_callback_;
    std::mutex mutex_;
    std::deque<T> queue_;
};

template <typename T>
class MailboxSender {
public:
    explicit MailboxSender(Mailbox<T>& mailbox) : mailbox_(&mailbox) {}

    void Push(T message) const
    {
        mailbox_->Push(std::move(message));
    }

    void PushBatch(std::vector<T>&& messages) const
    {
        mailbox_->PushBatch(std::move(messages));
    }

private:
    Mailbox<T>* mailbox_{nullptr};
};

} // namespace rrs
