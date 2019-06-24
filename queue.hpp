#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

namespace messaging {

// Base class for queue entries
struct message_base
{
    virtual ~message_base()
    {
    }
};

// Type erasure idiom to handle any message type.
template <typename Msg_T>
struct wrapped_message
    : message_base
{
    Msg_T contents;

    explicit wrapped_message(Msg_T const & msg)
        : contents{msg}
    {
    }
};

class queue
{
public:
    template <typename Msg_T>
    void push(Msg_T const & msg)
    {
        std::lock_guard<std::mutex> lock{m};
        q.push(std::make_shared<wrapped_message<Msg_T> >(msg));
        c. notify_all();
    }

    std::shared_ptr<message_base> wait_and_pop()
    {
        std::unique_lock<std::mutex> lock{m};
        c.wait(lock,
            [this]()
            {
                // Block until queue is not empty.
                return not q.empty();
            });
        auto msg = q.front();
        q.pop();
        return msg;
    }

private:
    std::mutex m;
    std::condition_variable c;
    std::queue< std::shared_ptr<message_base> > q;
};

class sender
{
public:
    sender() = default;

    explicit sender(queue * q)
        : q_{q}
    {
    }

    template <typename Msg_T>
    void send(Msg_T const & msg)
    {
        if (q_)
        {
            q_->push(msg);
        }
    }

private:
    queue * q_ = nullptr;
};

template<
      typename Dispatcher
    , typename Msg
    , typename Func
    >
class TemplateDispatcher
{
public:
private:
};

// Message to close queue.
struct close_queue
{
};

class dispatcher
{
public:
    explicit dispatcher(queue * q)
        : q_{q}
    {
    }

    dispatcher(dispatcher && other)
        : q_{other.q_}
        , chained_{other.chained_}
    {
        // Source dispatcher must not now wait for messages.
        other.chained_ = false;
    }

    // Not copyable
    dispatcher(dispatcher const &) = delete;
    dispatcher & operator=(dispatcher const &) = delete;

    ~dispatcher() noexcept(false)
    {
        if (not chained_)
        {
            wait_and_dispatcher();
        }
    }

    // Handle a specific type of message with a TemplateDispatcher.
    template <typename Msg_T, typename Func>
    TemplateDispatcher<dispatcher, Msg_T, Func>
    handle(Func && f)
    {
        return TemplateDispatcher<dispatcher, Msg_T, Func>{q_, this, std::forward<Func>(f)};
    }

protected:
    // TemplateDispatcher can access internals.
    template<
          typename Dispatcher
        , typename Msg
        , typename Func
        >
    friend class TemplateDispatcher;

    // Infinitely loop and dispatch messages.
    void wait_and_dispatcher()
    {
        while (1)
        {
            auto msg = q_->wait_and_pop();
            dispatch(msg);
        }
    }

    // Checks for close_queue message and throws if so.
    bool dispatch(std::shared_ptr<message_base> const & msg)
    {
        if (dynamic_cast<wrapped_message<close_queue> *>(msg.get()))
        {
            throw close_queue{};
        }
        return false;
    }

private:
    queue * q_ = nullptr;
    bool chained_ = false;
};


class receiver
{
public:
    // Implicit conversion to a sender with pointer to queue.
    operator sender()
    {
        return sender(&q_);
    }

    // Waiting for queue creates dispatcher.
    dispatcher wait()
    {
        return dispatcher(&q_);
    }

private:
    // Receive owns the queue.
    queue q_;
};

}
