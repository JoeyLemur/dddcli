#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>

struct CaptureProgressSnapshot
{
    int64_t elapsedSeconds = 0;
    uint64_t bytesWritten = 0;
    uint64_t transfers = 0;
    uint64_t samples = 0;
};

std::string formatCaptureProgressLine(const CaptureProgressSnapshot& snapshot);

class ProgressLine
{
public:
    explicit ProgressLine(bool quiet);
    ProgressLine(std::ostream& output, bool quiet, bool live);

    void update(const CaptureProgressSnapshot& snapshot);
    void clear();
    void finish();

private:
    std::ostream& output;
    bool quiet = false;
    bool live = false;
    bool active = false;
    size_t lastWidth = 0;
};
