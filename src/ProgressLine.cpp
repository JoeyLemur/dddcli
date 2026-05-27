#include "ProgressLine.h"

#include <cstdio>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace
{
constexpr auto PeriodicProgressInterval = std::chrono::seconds(10);
}

std::string formatCaptureProgressLine(const CaptureProgressSnapshot& snapshot)
{
    std::ostringstream stream;
    stream << "elapsed=" << snapshot.elapsedSeconds << "s "
           << "written=" << snapshot.bytesWritten / (1024 * 1024) << "MiB "
           << "transfers=" << snapshot.transfers
           << " samples=" << snapshot.samples;
    if (!snapshot.playerPosition.empty())
    {
        stream << ' ' << snapshot.playerPosition;
    }
    return stream.str();
}

ProgressLine::ProgressLine(bool quiet)
    : ProgressLine(std::cerr, quiet, isatty(fileno(stderr)) != 0)
{
}

ProgressLine::ProgressLine(std::ostream& output, bool quiet, bool live)
    : output(output), quiet(quiet), mode(live ? OutputMode::Live : OutputMode::Periodic)
{
}

void ProgressLine::update(const CaptureProgressSnapshot& snapshot)
{
    update(snapshot, std::chrono::steady_clock::now());
}

void ProgressLine::update(const CaptureProgressSnapshot& snapshot, const std::chrono::steady_clock::time_point& now)
{
    if (quiet)
    {
        return;
    }

    if (mode == OutputMode::Live)
    {
        updateLive(snapshot);
    }
    else
    {
        updatePeriodic(snapshot, now);
    }
}

void ProgressLine::updateLive(const CaptureProgressSnapshot& snapshot)
{
    std::string line = formatCaptureProgressLine(snapshot);
    output << '\r' << line;
    if (lastWidth > line.size())
    {
        output << std::string(lastWidth - line.size(), ' ');
    }
    output.flush();

    lastWidth = line.size();
    active = true;
}

void ProgressLine::updatePeriodic(const CaptureProgressSnapshot& snapshot, const std::chrono::steady_clock::time_point& now)
{
    if (active && now - lastPeriodicUpdate < PeriodicProgressInterval)
    {
        return;
    }

    output << formatCaptureProgressLine(snapshot) << '\n';
    output.flush();

    lastPeriodicUpdate = now;
    active = true;
}

void ProgressLine::clear()
{
    if (quiet || mode != OutputMode::Live || !active)
    {
        return;
    }

    output << '\r' << std::string(lastWidth, ' ') << '\r';
    output.flush();
    active = false;
}

void ProgressLine::finish()
{
    if (quiet || mode != OutputMode::Live || !active)
    {
        return;
    }

    output << '\n';
    output.flush();
    active = false;
}
