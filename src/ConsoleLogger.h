#pragma once

#include "ILogger.h"
#include <mutex>
#include <memory>

class ConsoleLogger final : public ILogger
{
public:
    using unique_ptr = std::unique_ptr<ConsoleLogger, ILogger::Deleter>;

    static unique_ptr Create(SeverityFilter filter, bool quiet);

    void Delete() override;

private:
    ConsoleLogger(SeverityFilter filter, bool quiet);

    bool IsLogSeverityEnabledInternal(Severity severity) const override;
    void ProcessLogMessage(Severity severity, const wchar_t* message, size_t messageLength) const override;

    SeverityFilter filter;
    bool quiet;
    mutable std::mutex outputMutex;
};

