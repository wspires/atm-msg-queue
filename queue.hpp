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


// Interface through which to send a message.
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
        other.chained_ = true;
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
        prev_->chained_ = true;
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
        if (wrapped_message<Msg> * wrapper = dynamic_cast<wrapped_message<Msg> *>(msg.get()))
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


// ATM-specific message types.

struct withdraw
{
    std::string account = "";
    unsigned amount = 0;
    mutable sender atm_queue;
};

struct withdraw_ok
{
};

struct withdraw_denied
{
};

struct cancel_withdrawal
{
    std::string account = "";
    unsigned amount = 0;
};

struct withdrawal_processed
{
    std::string account = "";
    unsigned amount = 0;
};

struct card_inserted
{
    std::string account = "";
};

struct digit_pressed
{
    char digit;
};

struct clear_last_pressed
{
};

struct eject_card
{
};

struct withdraw_pressed
{
    unsigned amount;
};

struct cancel_pressed
{
};

struct issue_money
{
    unsigned amount;
};

struct verify_pin
{
    std::string account;
    std::string pin;
    mutable sender atm_queue;
};

struct pin_verified
{
};

struct pin_incorrect
{
};

struct display_enter_pin
{
};

struct display_enter_card
{
};

struct display_insufficient_funds
{
};

struct display_withdrawal_cancelled
{
};

struct display_pin_incorrect_message
{
};

struct display_withdrawal_options
{
};

struct get_balance
{
    std::string account;
    mutable sender atm_queue;
};

struct balance
{
    unsigned amount;
};

struct display_balance
{
    unsigned amount;
};

struct balance_pressed
{
};


// ATM state machine
class atm
{
public:
    atm(sender bank, sender interface_hardware)
        : bank_{bank}
        , interface_hardware_{interface_hardware}
    {
    }

    void done()
    {
        get_sender().send(close_queue{});
    }

    void run()
    {
        state_ = &atm::waiting_for_card;
        try
        {
            while (true)
            {
                (this->*state_)();
            }
        }
        catch (close_queue const &)
        {
        }
    }

    sender get_sender()
    {
        // Converts receiver to sender.
        return incoming_;
    }

protected:
    void process_withdrawal()
    {
        incoming_.wait()
            .handle<withdraw_ok>(
                [&](withdraw_ok const & msg)
                {
                    interface_hardware_.send(issue_money{withdrawal_amount_});
                    bank_.send(withdrawal_processed{account_, withdrawal_amount_});
                    state_ = &atm::done_processing;
                })
            .handle<withdraw_denied>(
                [&](withdraw_denied const & msg)
                {
                    interface_hardware_.send(display_insufficient_funds{});
                    state_ = &atm::done_processing;
                })
            .handle<cancel_pressed>(
                [&](cancel_pressed const & msg)
                {
                    bank_.send(cancel_withdrawal{account_, withdrawal_amount_});
                    interface_hardware_.send(display_withdrawal_cancelled{});
                    state_ = &atm::done_processing;
                })
            ;
    }

    void process_balance()
    {
        incoming_.wait()
            .handle<balance>(
                [&](balance const & msg)
                {
                    interface_hardware_.send(display_balance{msg.amount});
                    state_ = &atm::wait_for_action;
                })
            .handle<cancel_pressed>(
                [&](cancel_pressed const & msg)
                {
                    state_ = &atm::done_processing;
                })
            ;
    }

    void wait_for_action()
    {
        interface_hardware_.send(display_withdrawal_options{});
        incoming_.wait()
            .handle<withdraw_pressed>(
                [&](withdraw_pressed const & msg)
                {
                    withdrawal_amount_ = msg.amount;
                    bank_.send(withdraw{account_, msg.amount, incoming_});
                    state_ = &atm::process_withdrawal;
                })
            .handle<balance_pressed>(
                [&](balance_pressed const & msg)
                {
                    bank_.send(get_balance{account_, incoming_});
                    state_ = &atm::process_balance;
                })
            .handle<cancel_pressed>(
                [&](cancel_pressed const & msg)
                {
                    state_ = &atm::done_processing;
                })
            ;
    }

    void verifying_pin()
    {
        incoming_.wait()
            .handle<pin_verified>(
                [&](pin_verified const & msg)
                {
                    state_ = &atm::wait_for_action;
                })
            .handle<pin_incorrect>(
                [&](pin_incorrect const & msg)
                {
                    interface_hardware_.send(display_pin_incorrect_message{});
                    state_ = &atm::done_processing;
                })
            .handle<cancel_pressed>(
                [&](cancel_pressed const & msg)
                {
                    state_ = &atm::done_processing;
                })
            ;
    }

    void getting_pin()
    {
        incoming_.wait()
            .handle<digit_pressed>(
                [&](digit_pressed const & msg)
                {
                    unsigned const pin_length = 4;
                    pin_ += msg.digit;
                    if (pin_.length() == pin_length)
                    {
                        bank_.send(verify_pin{account_, pin_, incoming_});
                        state_ = &atm::verifying_pin;
                    }
                })
            .handle<clear_last_pressed>(
                [&](clear_last_pressed const & msg)
                {
                    if (not pin_.empty())
                    {
                        pin_.pop_back();
                    }
                })
            .handle<cancel_pressed>(
                [&](cancel_pressed const & msg)
                {
                    state_ = &atm::done_processing;
                })
            ;
    }

    void waiting_for_card()
    {
        interface_hardware_.send(display_enter_card{});
        incoming_.wait()
            .handle<card_inserted>(
                [&](card_inserted const & msg)
                {
                    account_ = msg.account;
                    pin_ = "";
                    interface_hardware_.send(display_enter_pin{});
                    state_ = &atm::getting_pin;
                })
            ;
    }

    void done_processing()
    {
        interface_hardware_.send(eject_card{});
        state_ = &atm::waiting_for_card;
    }

    atm(atm const &) = delete;
    atm & operator=(atm const &) = delete;

private:
    receiver incoming_;

    // Bank to send messages as represents authority/backend storage of account data.
    sender bank_;

    // Hardware device that handles the display and mechanical actions.
    sender interface_hardware_;

    // Function pointer to track state, called by run() and changed in message handlers.
    void (atm::*state_)();

    std::string account_;
    unsigned withdrawal_amount_ = 0;

    // Currently entered PIN.
    std::string pin_;
};

}
