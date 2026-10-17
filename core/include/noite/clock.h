#pragma once

#include <QtGlobal>

#include <atomic>

namespace noite {

inline constexpr qint64 kDayMs = 86'400'000;

// Milliseconds since the Unix epoch, injected so retention and ordering are testable.
struct Clock {
    virtual ~Clock() = default;
    virtual qint64 nowMs() const = 0;
};

struct SystemClock final : Clock {
    qint64 nowMs() const override;
};

class FixedClock final : public Clock {
public:
    explicit FixedClock(qint64 ms)
        : ms_(ms)
    {
    }
    qint64 nowMs() const override { return ms_.load(); }
    void set(qint64 ms) { ms_.store(ms); }
    void advance(qint64 ms) { ms_.fetch_add(ms); }

private:
    std::atomic<qint64> ms_;
};

} // namespace noite
