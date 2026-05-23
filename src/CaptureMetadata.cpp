#include "CaptureMetadata.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::string jsonEscape(const std::string& value)
{
    std::ostringstream out;
    for (unsigned char ch : value)
    {
        switch (ch)
        {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20)
            {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)ch;
            }
            else
            {
                out << ch;
            }
            break;
        }
    }
    return out.str();
}

std::string quoted(const std::string& value)
{
    return "\"" + jsonEscape(value) + "\"";
}

std::string isoUtc(std::chrono::system_clock::time_point timePoint)
{
    auto time = std::chrono::system_clock::to_time_t(timePoint);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string captureFormatName(CaptureFormatCli format)
{
    switch (format)
    {
    case CaptureFormatCli::Lds: return "lds";
    case CaptureFormatCli::Raw: return "raw";
    case CaptureFormatCli::Cds: return "cds";
    }
    return "lds";
}

template<typename T>
void optionalNumber(std::ostringstream& out, const char* name, const std::optional<T>& value, bool& first)
{
    if (!value.has_value())
    {
        return;
    }
    if (!first) out << ",\n";
    out << "    " << quoted(name) << ": " << value.value();
    first = false;
}

void optionalString(std::ostringstream& out, const char* name, const std::string& value, bool& first)
{
    if (value.empty())
    {
        return;
    }
    if (!first) out << ",\n";
    out << "    " << quoted(name) << ": " << quoted(value);
    first = false;
}
}

bool writeCaptureMetadata(
    const std::filesystem::path& jsonPath,
    const CaptureMetadata& metadata,
    const UsbDeviceBase& usb,
    std::string& error)
{
    auto parentPath = jsonPath.parent_path().empty() ? std::filesystem::path(".") : jsonPath.parent_path();
    std::error_code createDirectoriesError;
    std::filesystem::create_directories(parentPath, createDirectoriesError);
    if (createDirectoriesError)
    {
        error = "failed to create directory " + parentPath.string() + ": " + createDirectoriesError.message();
        return false;
    }

    std::ostringstream out;
    out << "{\n";

    out << "  " << quoted("serialInfo") << ": {\n";
    bool firstSerial = true;
    optionalString(out, "playerModelCode", metadata.playerModelCode, firstSerial);
    optionalString(out, "playerModelName", metadata.playerModelName, firstSerial);
    optionalString(out, "playerVersionNumber", metadata.playerVersionNumber, firstSerial);
    optionalString(out, "serialSpeed", metadata.serialSpeed, firstSerial);
    optionalString(out, "discType", metadata.discType, firstSerial);
    optionalString(out, "discStatus", metadata.discStatus, firstSerial);
    optionalString(out, "discStandardUserCode", metadata.discStandardUserCode, firstSerial);
    optionalString(out, "discPioneerUserCode", metadata.discPioneerUserCode, firstSerial);
    optionalNumber(out, "minFrameNumber", metadata.minFrameNumber, firstSerial);
    optionalNumber(out, "maxFrameNumber", metadata.maxFrameNumber, firstSerial);
    optionalNumber(out, "minTimeCode", metadata.minTimeCode, firstSerial);
    optionalNumber(out, "maxTimeCode", metadata.maxTimeCode, firstSerial);
    if (!firstSerial) out << "\n";
    out << "  },\n";

    out << "  " << quoted("captureInfo") << ": {\n";
    out << "    " << quoted("captureFilePath") << ": " << quoted(metadata.captureFilePath.string()) << ",\n";
    out << "    " << quoted("captureFormat") << ": " << quoted(captureFormatName(metadata.captureFormat)) << ",\n";
    out << "    " << quoted("testMode") << ": " << (metadata.testMode ? "true" : "false") << ",\n";
    out << "    " << quoted("transferResult") << ": " << (int)usb.GetTransferResult() << ",\n";
    out << "    " << quoted("transferResultString") << ": " << quoted(transferResultToString(usb.GetTransferResult())) << ",\n";
    out << "    " << quoted("durationInMilliseconds") << ": " << metadata.duration.count() << ",\n";
    out << "    " << quoted("transferCount") << ": " << usb.GetNumberOfTransfers() << ",\n";
    out << "    " << quoted("numberOfDiskBuffersWritten") << ": " << usb.GetNumberOfDiskBuffersWritten() << ",\n";
    out << "    " << quoted("fileSizeWrittenInBytes") << ": " << usb.GetFileSizeWrittenInBytes() << ",\n";
    out << "    " << quoted("sampleCount") << ": " << usb.GetProcessedSampleCount() << ",\n";
    out << "    " << quoted("minSampleValue") << ": " << usb.GetMinSampleValue() << ",\n";
    out << "    " << quoted("maxSampleValue") << ": " << usb.GetMaxSampleValue() << ",\n";
    out << "    " << quoted("clippedMinSampleCount") << ": " << usb.GetClippedMinSampleCount() << ",\n";
    out << "    " << quoted("clippedMaxSampleCount") << ": " << usb.GetClippedMaxSampleCount() << ",\n";
    out << "    " << quoted("sequenceMarkersPresent") << ": " << (usb.GetTransferHadSequenceNumbers() ? "true" : "false") << ",\n";
    out << "    " << quoted("creationTimestamp") << ": " << quoted(isoUtc(metadata.creationTimeUtc)) << "\n";
    out << "  }\n";
    out << "}\n";

    std::ofstream file(jsonPath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open())
    {
        error = "failed to open " + jsonPath.string();
        return false;
    }
    auto json = out.str();
    file.write(json.data(), (std::streamsize)json.size());
    if (!file.good())
    {
        error = "failed to write " + jsonPath.string();
        return false;
    }
    return true;
}
