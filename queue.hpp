#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

namespace messaging {

// Base class for queue entries.
struct message_base
{
    virtual ~message_base()
    {
    }
};


// Type erasure idiom to handle any message type.
// Derives from message_base but contents can be any type Msg_T.
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
        c.notify_all();
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
      typename PreviousDispatcher
    , typename Msg
    , typename Func
    >
class TemplateDispatcher
{
public:

    TemplateDispatcher(TemplateDispatcher && other)
        : q_{other.q_}
        , prev_{other.prev_}
        , f_{std::move(other.f_)}
        , chained_{other.chained_}
    {
        other.chained = true;
    }

    TemplateDispatcher(
          queue * q
        , PreviousDispatcher * prev
        , Func && f
        )
        : q_{q}
        , prev_{prev}
        , f_{std::forward<Func>(f)}
        , chained_{false}
    {
        prev_->chained = true;
    }

    // Additional handlers can be chained.
    template <typename OtherMsg, typename OtherFunc>
    TemplateDispatcher<
          TemplateDispatcher
        , OtherMsg
        , OtherFunc
        >
    handle(OtherFunc && f)
    {
        return TemplateDispatcher<TemplateDispatcher, OtherMsg, OtherFunc>{q_, this, std::forward<OtherFunc>(f)};
    }

    ~TemplateDispatcher() noexcept(false)
    {
        if (not chained_)
        {
            wait_and_dispatch();
        }
    }

protected:
    // Not copyable.
    TemplateDispatcher(TemplateDispatcher const &) = delete;
    TemplateDispatcher & operator=(TemplateDispatcher const &) = delete;

    // TemplateDispatcher instantiations are friends of each other.
    template<
          typename OtherDispatcher
        , typename OtherMsg
        , typename OtherFunc
        >
    friend class TemplateDispatcher;

    void wait_and_dispatch()
    {
        while (true)
        {
            auto msg = q_->wait_and_pop();
            if (dispatch(msg))
            {
                // Stop if this dispatcher handled the message.
                break;
            }
        }
    }

    bool dispatch(std::shared_ptr<message_base> const & msg)
    {
        // Check the message type and call the function.
        if (wrapped_message<Msg> wrapper = dynamic_cast<wrapped_message<Msg> *>(msg.get()))
        {
            f_(wrapper->contents);
            return true;
        }
        else
        {
            // Not our message, so chain to the previous dispatcher.
            return prev_->dispatch(msg);
        }
    }

private:
    queue * q_ = nullptr;
    PreviousDispatcher * prev_ = nullptr;
    Func f_;
    bool chained_ = false;
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

    // The destructor does the work of waiting for a message dispatching it.
    ~dispatcher() noexcept(false)
    {
        if (not chained_)
        {
            wait_and_dispatch();
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
    void wait_and_dispatch()
    {
        while (true)
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
    // ~dispatcher does the work of waiting for a message dispatching it.
    dispatcher wait()
    {
        return dispatcher{&q_};
    }

private:
    // Receive owns the queue.
    queue q_;
};


struct withdraw
{
    std::string account;
};


}
