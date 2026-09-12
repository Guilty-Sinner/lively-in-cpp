#include <catch2/catch_test_macros.hpp>

#include <lively/events.h>
#include <lively/task.h>

#include <string>
#include <thread>
#include <vector>

using namespace lively;

TEST_CASE("event mirrors C# multicast semantics", "[events]") {
    struct Args {
        int value;
    };

    event<Args> e;

    int sum = 0;
    auto h1 = [&sum](const Args& a) { sum += a.value; };

    e.subscribe(h1);
    e.raise(Args{5});
    REQUIRE(sum == 5);

    // Unsubscribe of a non-subscribed handler is a no-op.
    auto h2 = [](const Args&) {};
    e.unsubscribe(h2);
    e.raise(Args{5});
    REQUIRE(sum == 10);

    // Two subscribers both fire in subscription order.
    int other = 0;
    e.subscribe([&other](const Args&) { other = 1; });
    e.raise(Args{1});
    REQUIRE(sum == 11);
    REQUIRE(other == 1);
}

TEST_CASE("event supports recursion and mutation during raise", "[events]") {
    struct Args {
        int value;
    };
    event<Args> e;
    std::vector<int> order;

    event<Args>::handler_type self;
    self = [&](const Args& a) {
        order.push_back(a.value);
        if (a.value < 3) e.raise(Args{a.value + 1}); // recursive raise: snapshot semantics
    };
    e.subscribe(self);
    e.raise(Args{1});
    REQUIRE((order == std::vector<int>{1, 2, 3}));
}

TEST_CASE("void event maps event EventHandler", "[events]") {
    event<void> e;
    int count = 0;
    e.subscribe([&count] { ++count; });
    e.raise();
    e.raise();
    REQUIRE(count == 2);
}

TEST_CASE("task_completion_source completes awaiting coroutine", "[task]") {
    task_completion_source<int> tcs;

    bool done = false;
    Task<int> waiter = [](::lively::task_completion_source<int>& src,
                          bool* finished) -> Task<int> {
        const int v = co_await src.task();
        *finished = true;
        co_return v * 2;
    }(tcs, &done);

    waiter.start();
    // Let the waiter suspend.
    std::this_thread::yield();

    tcs.set_result(21);
    // Continuation runs on setter thread; poll briefly.
    for (int i = 0; i < 1000 && !done; ++i) std::this_thread::yield();
    REQUIRE(done);
    REQUIRE(waiter.get() == 42);
}

TEST_CASE("cancellation token maps CancellationToken", "[task]") {
    cancellation_token_source cts;
    REQUIRE_FALSE(cts.token().is_cancellation_requested());
    cts.cancel();
    REQUIRE(cts.token().is_cancellation_requested());
    REQUIRE_THROWS_AS(cts.token().throw_if_cancellation_requested(),
                      operation_canceled_exception);
}
