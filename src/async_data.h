#pragma once

#include <future>


/**
 * Class template based on std::async and std::future<> to help in fetching data asynchronously.
 */
template <typename T>
struct async_data {

    bool ready() {
        if (_done) return true;
        _check_future();
        return _done;
    }

    bool busy() {
        return _future.valid();
    }

    template <typename Func, typename... Args>
    void obtain(Func fn, Args... args) {
        assert(!_done && "Do not call obtain() again once the data is ready - did you forget to check with ready() ?");
        if (!busy()) {
            _future = std::async(std::launch::async, fn, args...);
        }
    }

    auto& value() {
        assert(_done && "Do not try to access the value before ready() reports true!");
        return _result;
    }

private:

    bool _done = false;
    std::future<T> _future;
    T _result;

    void _check_future() {
        if (_future.valid() && _future.wait_for(std::chrono::nanoseconds(1)) == std::future_status::ready) {
            _done = true;
            _result = _future.get();
        }
    }
};
