#pragma once
// C#-style thread-safe multi-cast events.
//
// Maps `event EventHandler<TArgs>` (see prompt.txt: "Map single-cast delegates to
// std::function<Signature>. Create a new C++ class to replicate the
// subscription/unsubscription and invocation logic of C# events.").
//
// Semantics mirrored from C# MulticastDelegate:
//  * += / -= add/remove handlers; removal removes the LAST matching entry.
//  * Raising copies the handler list under lock, then invokes outside the lock —
//    so handlers may (un)subscribe or raise recursively, exactly like C#.
//  * The event can be raised from any thread; a handler added while a raise is in
//    flight is not invoked by that in-flight raise (same as C# invocation-list
//    snapshotting).
//  * An exception thrown by a handler propagates out of raise() — matching C#
//    behaviour where a handler exception surfaces at the raise site.

#include <algorithm>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace lively {

template <typename TArgs>
class event {
public:
    using handler_type = std::function<void(const TArgs&)>;

    void subscribe(handler_type h) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.push_back(std::move(h));
    }

    // Removal matches by the callable's closure type (std::type_index via
    // target_type()). This is exact for the canonical C# pattern
    // (`handler` stored once, `event -= handler`): copies of the same
    // std::function share a closure type. C# compares delegate instances
    // (target+method); per-type matching is the accepted std::function
    // approximation and removes the LAST matching entry like Delegate.Remove.
    void unsubscribe(const handler_type& h) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!h) return;
        const auto it = std::find_if(
            handlers_.rbegin(), handlers_.rend(),
            [&h](const handler_type& stored) {
                return stored && stored.target_type() == h.target_type();
            });
        if (it != handlers_.rend()) {
            // Convert reverse iterator to forward iterator before erase.
            handlers_.erase(std::next(it).base());
        }
    }

    template <typename... TConstruct>
    void raise(TConstruct&&... args) {
        // Snapshot under lock; invoke outside the lock (C# invocation list semantics).
        std::vector<handler_type> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = handlers_;
        }
        TArgs args_tuple{std::forward<TConstruct>(args)...};
        for (const auto& h : snapshot) {
            h(args_tuple);
        }
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return handlers_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::vector<handler_type> handlers_;
};

// C# `event EventHandler` (no payload).
template <>
class event<void> {
public:
    using handler_type = std::function<void()>;

    void subscribe(handler_type h) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.push_back(std::move(h));
    }

    void unsubscribe(const handler_type& h) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!h) return;
        const auto it = std::find_if(
            handlers_.rbegin(), handlers_.rend(),
            [&h](const handler_type& stored) {
                return stored && stored.target_type() == h.target_type();
            });
        if (it != handlers_.rend()) {
            handlers_.erase(std::next(it).base());
        }
    }

    void raise() {
        std::vector<handler_type> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = handlers_;
        }
        for (const auto& h : snapshot) {
            h();
        }
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return handlers_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::vector<handler_type> handlers_;
};

} // namespace lively
