#include "ProgressLine.h"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

std::string formatCaptureProgressLine(const CaptureProgressSnapshot& snapshot)
{
    std::ostringstream stream;
    stream << "elapsed=" << snapshot.elapsedSeconds << "s "
           << "written=" << snapshot.bytesWritten / (1024 * 1024) << "MiB "
           << "transfers=" << snapshot.transfers
           << " samples=" << snapshot.samples;
    return stream.str();
}

ProgressLine::ProgressLine(bool quiet)
    : ProgressLine(std::cerr, quiet, isatty(fileno(stderr)) != 0)
{
}

ProgressLine::ProgressLine(std::ostream& output, bool quiet, bool live)
    : output(output), quiet(quiet), live(live)
{
}

void ProgressLine::update(const CaptureProgressSnapshot& snapshot)
{
    if (quiet || !live)
    {
        return;
    }

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

void ProgressLine::clear()
{
    if (quiet || !live || !active)
    {
        return;
    }

    output << '\r' << std::string(lastWidth, ' ') << '\r';
    output.flush();
    active = false;
}

void ProgressLine::finish()
{
    if (quiet || !live || !active)
    {
        return;
    }

    output << '\n';
    output.flush();
    active = false;
}
