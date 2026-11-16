// Cooperative cancellation for long running analysis jobs.
#pragma once

#include <atomic>
#include <memory>

namespace pkg {

class CancelToken {
public:
    CancelToken() : m_cancelled(std::make_shared<std::atomic_bool>(false)) {}

    void cancel() const { m_cancelled->store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool isCancelled() const
    {
        return m_cancelled->load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

}  // namespace pkg
