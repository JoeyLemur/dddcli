// SPDX-FileCopyrightText: Copyright (C) 2026 Ed Powell
// SPDX-License-Identifier: GPL-3.0-only

#include "ConsoleLogger.h"
#include <iostream>

ConsoleLogger::ConsoleLogger(SeverityFilter filter, bool quiet)
: filter(filter), quiet(quiet)
{ }

ConsoleLogger::unique_ptr ConsoleLogger::Create(SeverityFilter filter, bool quiet)
{
    return ConsoleLogger::unique_ptr(new ConsoleLogger(filter, quiet));
}

void ConsoleLogger::Delete()
{
    delete this;
}

bool ConsoleLogger::IsLogSeverityEnabledInternal(Severity severity) const
{
    if (quiet && severity != Severity::Critical && severity != Severity::Error)
    {
        return false;
    }
    return ((unsigned int)filter & (unsigned int)severity) != 0;
}

void ConsoleLogger::ProcessLogMessage(Severity severity, const wchar_t* message, size_t messageLength) const
{
    if (quiet && severity != Severity::Critical && severity != Severity::Error)
    {
        return;
    }

    const char* prefix = "info";
    switch (severity)
    {
    case Severity::Critical: prefix = "critical"; break;
    case Severity::Error: prefix = "error"; break;
    case Severity::Warning: prefix = "warning"; break;
    case Severity::Info: prefix = "info"; break;
    case Severity::Debug: prefix = "debug"; break;
    case Severity::Trace: prefix = "trace"; break;
    }

    std::lock_guard<std::mutex> lock(outputMutex);
    std::cerr << prefix << ": " << WStringToUtf8String(std::wstring(message, messageLength)) << '\n';
}
