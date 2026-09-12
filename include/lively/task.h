#pragma once
// C# Task/async-await mapping onto C++20 coroutines.
//   lively::Task<T>                    ~ System.Threading.Tasks.Task<T>
//   lively::task_completion_source<T>  ~ TaskCompletionSource<T>
//   lively::cancellation_token(_source)~ CancellationToken / CancellationTokenSource
// See prompt.txt: "Port async/await logic to a custom C++ coroutine framework."

#include <coroutine>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <atomic>

namespace lively {

class operation_canceled_exception : public std::runtime_error {
public:
    explicit operation_canceled_exception(const char* msg = "The operation was canceled.")
        : std::runtime_error(msg) {}
};

class cancellation_token_source;

class cancellation_token {
public:
    cancellation_token() : state_(std::make_shared<State>()) {}
    bool is_cancellation_requested() const noexcept { return state_->canceled; }
    void throw_if_cancellation_requested() const {
        if (is_cancellation_requested()) throw operation_canceled_exception();
    }
private:
    friend class cancellation_token_source;
    struct State { bool canceled = false; };
    explicit cancellation_token(std::shared_ptr<State> s) : state_(std::move(s)) {}
    std::shared_ptr<State> state_;
};

class cancellation_token_source {
public:
    cancellation_token_source() : state_(std::make_shared<cancellation_token::State>()) {}
    cancellation_token token() const noexcept { return cancellation_token(state_); }
    void cancel() noexcept { state_->canceled = true; }
private:
    std::shared_ptr<cancellation_token::State> state_;
};

template <typename T = void> class Task;

namespace detail {

template <typename T> struct task_promise;

template <typename T>
struct task_final_awaiter {
    bool await_ready() const noexcept { return false; }
    template <typename P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
        // Release-store: everything the body wrote (return value / captured
        // exception) is visible to any thread that acquire-loads `finished`.
        // Task::get() spins on this flag from a foreign thread — without this
        // fence it could observe done()==true but a stale null exception.
        h.promise().finished.store(true, std::memory_order_release);
        auto cont = h.promise().continuation;
        return cont ? cont : std::noop_coroutine();
    }
    void await_resume() noexcept {}
};

template <typename T>
struct task_promise_base {
    std::coroutine_handle<> continuation;
    std::exception_ptr exception;
    std::atomic<bool> finished{false};
    std::suspend_always initial_suspend() noexcept { return {}; }
    task_final_awaiter<T> final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept { exception = std::current_exception(); }
};

template <typename T>
struct task_promise : task_promise_base<T> {
    std::optional<T> value;
    Task<T> get_return_object() noexcept;
    template <typename U> requires std::convertible_to<U&&, T>
    void return_value(U&& v) { value.emplace(std::forward<U>(v)); }
};

template <>
struct task_promise<void> : task_promise_base<void> {
    Task<> get_return_object() noexcept;
    void return_void() noexcept {}
};

} // namespace detail

template <typename T>
class [[nodiscard]] Task {
public:
    using promise_type = detail::task_promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;
    explicit Task(handle_type coro) noexcept : coro_(coro) {}
    Task(Task&& o) noexcept : coro_(std::exchange(o.coro_, nullptr)) {}
    Task& operator=(Task&& o) noexcept {
        if (this != &o) { if (coro_) coro_.destroy(); coro_ = std::exchange(o.coro_, nullptr); }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { if (coro_) coro_.destroy(); }

    auto operator co_await() const noexcept {
        struct awaiter {
            handle_type coro;
            bool await_ready() const noexcept { return !coro || coro.done(); }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
                coro.promise().continuation = awaiting;
                return coro;
            }
            T await_resume() {
                auto& p = coro.promise();
                if (p.exception) std::rethrow_exception(p.exception);
                if constexpr (!std::is_void_v<T>) return std::move(*p.value);
            }
        };
        return awaiter{coro_};
    }

    void start() { if (coro_ && !coro_.done()) coro_.resume(); }
    bool is_completed() const noexcept { return !coro_ || coro_.done(); }

    // Blocking wait; maps task.Wait()/task.Result (console/test bridges only).
    // Spins on the release/acquire `finished` fence rather than the coroutine's
    // done() bit, so return values and exceptions written by a completing
    // foreign thread (gRPC callback) are guaranteed visible after the wait.
    T get() {
        start();
        while (coro_ && !coro_.promise().finished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        auto& p = coro_.promise();
        if (p.exception) std::rethrow_exception(p.exception);
        if constexpr (!std::is_void_v<T>) return std::move(*p.value);
    }

private:
    handle_type coro_ = nullptr;
};

namespace detail {

template <typename T>
Task<T> task_promise<T>::get_return_object() noexcept {
    return Task<T>{std::coroutine_handle<task_promise<T>>::from_promise(*this)};
}

inline Task<> task_promise<void>::get_return_object() noexcept {
    return Task<>{std::coroutine_handle<task_promise<void>>::from_promise(*this)};
}

} // namespace detail

template <typename T = void>
class task_completion_source {
public:
    task_completion_source() : state_(std::make_shared<State>()) {}

    Task<T> task() const {
        std::shared_ptr<State> st = state_;
        // Captureless lambda: state is passed as a parameter, which the
        // coroutine copies into its frame. (A capturing lambda coroutine would
        // dangle — the closure temporary dies while the lazy task is suspended.)
        // co_return forwards the awaited value into the promise.
        return [](std::shared_ptr<State> st) -> Task<T> {
            co_return co_await Awaiter{st};
        }(std::move(st));
    }

    void set_result(T value) const {
        { std::lock_guard<std::mutex> lk(state_->mutex);
          if (state_->completed) return; // TrySet* semantics: first wins.
          state_->value = std::move(value); state_->completed = true; }
        resume_waiters();
    }
    void set_exception(std::exception_ptr e) const {
        { std::lock_guard<std::mutex> lk(state_->mutex);
          if (state_->completed) return;
          state_->exception = std::move(e); state_->completed = true; }
        resume_waiters();
    }
    void set_canceled() const {
        set_exception(std::make_exception_ptr(operation_canceled_exception()));
    }

private:
    struct State {
        mutable std::mutex mutex;
        std::vector<std::coroutine_handle<>> waiters;
        std::optional<T> value;
        std::exception_ptr exception;
        bool completed = false;
    };
    struct Awaiter {
        std::shared_ptr<State> st;
        bool await_ready() const noexcept {
            std::lock_guard<std::mutex> lk(st->mutex);
            return st->completed;
        }
        bool await_suspend(std::coroutine_handle<> h) {
            std::lock_guard<std::mutex> lk(st->mutex);
            if (st->completed) return false;
            st->waiters.push_back(h);
            return true;
        }
        T await_resume() {
            if (st->exception) std::rethrow_exception(st->exception);
            if constexpr (!std::is_void_v<T>) return std::move(*st->value);
        }
    };
    std::shared_ptr<State> state_;

    void resume_waiters() const {
        std::vector<std::coroutine_handle<>> to_resume;
        { std::lock_guard<std::mutex> lk(state_->mutex);
          to_resume = std::move(state_->waiters); state_->waiters.clear(); }
        for (auto h : to_resume) h.resume();
    }
};

// Void specialization: C# TaskCompletionSource<object> used for
// Task (non-generic) completions — gRPC unary calls resolve this way.
template <>
class task_completion_source<void> {
public:
    task_completion_source() : state_(std::make_shared<State>()) {}

    Task<> task() const {
        std::shared_ptr<State> st = state_;
        return [](std::shared_ptr<State> st) -> Task<> {
            co_return co_await Awaiter{st};
        }(std::move(st));
    }

    void set_result() const {
        { std::lock_guard<std::mutex> lk(state_->mutex);
          if (state_->completed) return;
          state_->completed = true; }
        resume_waiters();
    }
    void set_exception(std::exception_ptr e) const {
        { std::lock_guard<std::mutex> lk(state_->mutex);
          if (state_->completed) return;
          state_->exception = std::move(e); state_->completed = true; }
        resume_waiters();
    }
    void set_canceled() const {
        set_exception(std::make_exception_ptr(operation_canceled_exception()));
    }

private:
    struct State {
        mutable std::mutex mutex;
        std::vector<std::coroutine_handle<>> waiters;
        std::exception_ptr exception;
        bool completed = false;
    };
    struct Awaiter {
        std::shared_ptr<State> st;
        bool await_ready() const noexcept {
            std::lock_guard<std::mutex> lk(st->mutex);
            return st->completed;
        }
        bool await_suspend(std::coroutine_handle<> h) {
            std::lock_guard<std::mutex> lk(st->mutex);
            if (st->completed) return false;
            st->waiters.push_back(h);
            return true;
        }
        void await_resume() {
            if (st->exception) std::rethrow_exception(st->exception);
        }
    };
    std::shared_ptr<State> state_;

    void resume_waiters() const {
        std::vector<std::coroutine_handle<>> to_resume;
        { std::lock_guard<std::mutex> lk(state_->mutex);
          to_resume = std::move(state_->waiters); state_->waiters.clear(); }
        for (auto h : to_resume) h.resume();
    }
};

} // namespace lively
