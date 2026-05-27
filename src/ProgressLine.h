#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <iosfwd>
#include <string>

struct CaptureProgressSnapshot
{
    int64_t elapsedSeconds = 0;
    uint64_t bytesWritten = 0;
    uint64_t transfers = 0;
    uint64_t samples = 0;
    std::string playerPosition;
};

std::string formatCaptureProgressLine(const CaptureProgressSnapshot& snapshot);

class ProgressLine
{
public:
    explicit ProgressLine(bool quiet);
    ProgressLine(std::ostream& output, bool quiet, bool live);

    void update(const CaptureProgressSnapshot& snapshot);
    void update(const CaptureProgressSnapshot& snapshot, const std::chrono::steady_clock::time_point& now);
    void clear();
    void finish();

private:
    enum class OutputMode
    {
        Live,
        Periodic,
    };

    void updateLive(const CaptureProgressSnapshot& snapshot);
    void updatePeriodic(const CaptureProgressSnapshot& snapshot, const std::chrono::steady_clock::time_point& now);

    std::ostream& output;
    bool quiet = false;
    OutputMode mode = OutputMode::Periodic;
    bool active = false;
    size_t lastWidth = 0;
    std::chrono::steady_clock::time_point lastPeriodicUpdate{};
};
