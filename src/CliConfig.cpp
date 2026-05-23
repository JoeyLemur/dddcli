#include "CliConfig.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
std::string trim(std::string value)
{
    auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    return value;
}

std::string stripQuotes(std::string value)
{
    value = trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
    {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool parseBool(const std::string& value)
{
    auto lowered = lower(trim(value));
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") return true;
    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") return false;
    throw std::runtime_error("invalid boolean value: " + value);
}

size_t parseSize(const std::string& value)
{
    auto text = lower(trim(value));
    size_t multiplier = 1;
    if (text.ends_with("mib"))
    {
        multiplier = 1024 * 1024;
        text.resize(text.size() - 3);
    }
    else if (text.ends_with("mb"))
    {
        multiplier = 1000 * 1000;
        text.resize(text.size() - 2);
    }
    return (size_t)std::stoull(trim(text)) * multiplier;
}

CaptureFormatCli parseFormat(const std::string& value)
{
    auto text = lower(trim(value));
    if (text == "lds" || text == "ten-bit-packed" || text == "10bit") return CaptureFormatCli::Lds;
    if (text == "raw" || text == "sixteen-bit-signed" || text == "16bit") return CaptureFormatCli::Raw;
    if (text == "cds" || text == "ten-bit-cd-packed" || text == "cd") return CaptureFormatCli::Cds;
    throw std::runtime_error("invalid capture format: " + value);
}

SerialSpeedCli parseSerialSpeed(const std::string& value)
{
    auto text = lower(trim(value));
    if (text == "auto") return SerialSpeedCli::Auto;
    if (text == "9600") return SerialSpeedCli::Bps9600;
    if (text == "4800") return SerialSpeedCli::Bps4800;
    if (text == "2400") return SerialSpeedCli::Bps2400;
    if (text == "1200") return SerialSpeedCli::Bps1200;
    throw std::runtime_error("invalid serial speed: " + value);
}

DiscTypeCli parseDiscType(const std::string& value)
{
    auto text = lower(trim(value));
    if (text == "cav") return DiscTypeCli::Cav;
    if (text == "clv") return DiscTypeCli::Clv;
    if (text == "unknown" || text.empty()) return DiscTypeCli::Unknown;
    throw std::runtime_error("invalid disc type: " + value);
}

AutoCaptureModeCli parseAutoCaptureMode(const std::string& value)
{
    auto text = lower(trim(value));
    if (text == "whole-disc" || text == "whole") return AutoCaptureModeCli::WholeDisc;
    if (text == "lead-in" || text == "leadin") return AutoCaptureModeCli::LeadIn;
    if (text == "partial") return AutoCaptureModeCli::Partial;
    throw std::runtime_error("invalid auto-capture mode: " + value);
}

uint16_t parseU16(const std::string& value)
{
    int base = lower(value).starts_with("0x") ? 16 : 10;
    return (uint16_t)std::stoul(value, nullptr, base);
}

std::string requireValue(int& index, int argc, char* argv[], const std::string& option)
{
    if (index + 1 >= argc)
    {
        throw std::runtime_error(option + " requires a value");
    }
    return argv[++index];
}

void applyKeyValue(CliOptions& options, const std::string& key, const std::string& value)
{
    if (key == "usb.vid") options.usbVid = parseU16(stripQuotes(value));
    else if (key == "usb.pid") options.usbPid = parseU16(stripQuotes(value));
    else if (key == "usb.preferred_device") options.usbPreferredDevice = stripQuotes(value);
    else if (key == "usb.disk_buffer_queue_size") options.diskBufferQueueSize = parseSize(stripQuotes(value));
    else if (key == "usb.use_small_usb_transfer_queue") options.useSmallUsbTransferQueue = parseBool(value);
    else if (key == "usb.use_small_usb_transfers") options.useSmallUsbTransfers = parseBool(value);
    else if (key == "capture.output_dir") options.outputDir = stripQuotes(value);
    else if (key == "capture.json") options.jsonOutput = stripQuotes(value);
    else if (key == "capture.format") options.captureFormat = parseFormat(stripQuotes(value));
    else if (key == "capture.test_mode") options.testMode = parseBool(value);
    else if (key == "capture.duration_seconds") options.durationSeconds = std::stoi(stripQuotes(value));
    else if (key == "player.serial_device") options.serialDevice = stripQuotes(value);
    else if (key == "player.serial_speed") options.serialSpeed = parseSerialSpeed(stripQuotes(value));
    else if (key == "auto_capture.disc_type") options.discType = parseDiscType(stripQuotes(value));
    else if (key == "auto_capture.mode") options.autoCaptureMode = parseAutoCaptureMode(stripQuotes(value));
    else if (key == "auto_capture.start_address") options.startAddress = std::stoi(stripQuotes(value));
    else if (key == "auto_capture.end_address") options.endAddress = std::stoi(stripQuotes(value));
    else if (key == "auto_capture.key_lock") options.keyLock = parseBool(value);
}
}

std::filesystem::path defaultConfigPath()
{
    const char* xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfigHome != nullptr && *xdgConfigHome != '\0')
    {
        return std::filesystem::path(xdgConfigHome) / "domesday-duplicator" / "dddcli.toml";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0')
    {
        return std::filesystem::path(home) / ".config" / "domesday-duplicator" / "dddcli.toml";
    }
    return "dddcli.toml";
}

bool TomlConfig::load(const std::filesystem::path& path, std::string& error)
{
    values.clear();
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        return true;
    }

    std::string section;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(stream, line))
    {
        ++lineNumber;
        auto comment = line.find('#');
        if (comment != std::string::npos)
        {
            line.resize(comment);
        }
        line = trim(line);
        if (line.empty())
        {
            continue;
        }
        if (line.front() == '[' && line.back() == ']')
        {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        auto equals = line.find('=');
        if (equals == std::string::npos)
        {
            error = "invalid TOML line " + std::to_string(lineNumber);
            return false;
        }
        std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        values[section.empty() ? key : section + "." + key] = value;
    }
    return true;
}

void TomlConfig::applyTo(CliOptions& options) const
{
    for (const auto& [key, value] : values)
    {
        applyKeyValue(options, key, value);
    }
}

ParsedCommandLine parseCommandLine(int argc, char* argv[])
{
    CliOptions options;
    options.configPath = defaultConfigPath();
    return parseCommandLine(argc, argv, options);
}

ParsedCommandLine parseCommandLine(int argc, char* argv[], const CliOptions& baseOptions)
{
    ParsedCommandLine parsed;
    parsed.options = baseOptions;
    if (parsed.options.configPath.empty())
    {
        parsed.options.configPath = defaultConfigPath();
    }

    if (argc < 2)
    {
        parsed.command = "help";
        return parsed;
    }

    parsed.command = argv[1];
    if (parsed.command == "--help" || parsed.command == "-h")
    {
        parsed.command = "help";
        return parsed;
    }
    int startIndex = 2;
    if (parsed.command == "player" && argc > 2 && argv[2][0] != '-')
    {
        parsed.playerAction = argv[2];
        startIndex = 3;
    }

    for (int i = startIndex; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") parsed.command = "help";
        else if (arg == "--config") parsed.options.configPath = requireValue(i, argc, argv, arg);
        else if (arg == "--debug") parsed.options.debug = true;
        else if (arg == "--quiet") parsed.options.quiet = true;
        else if (arg == "--vid") parsed.options.usbVid = parseU16(requireValue(i, argc, argv, arg));
        else if (arg == "--pid") parsed.options.usbPid = parseU16(requireValue(i, argc, argv, arg));
        else if (arg == "--usb-device") parsed.options.usbPreferredDevice = requireValue(i, argc, argv, arg);
        else if (arg == "--disk-buffer-queue-size") parsed.options.diskBufferQueueSize = parseSize(requireValue(i, argc, argv, arg));
        else if (arg == "--small-usb-transfer-queue") parsed.options.useSmallUsbTransferQueue = true;
        else if (arg == "--large-usb-transfer-queue") parsed.options.useSmallUsbTransferQueue = false;
        else if (arg == "--small-usb-transfers") parsed.options.useSmallUsbTransfers = true;
        else if (arg == "--large-usb-transfers") parsed.options.useSmallUsbTransfers = false;
        else if (arg == "--output") parsed.options.output = requireValue(i, argc, argv, arg);
        else if (arg == "--json") parsed.options.jsonOutput = requireValue(i, argc, argv, arg);
        else if (arg == "--output-dir") parsed.options.outputDir = requireValue(i, argc, argv, arg);
        else if (arg == "--format") parsed.options.captureFormat = parseFormat(requireValue(i, argc, argv, arg));
        else if (arg == "--test-mode") parsed.options.testMode = true;
        else if (arg == "--duration") parsed.options.durationSeconds = std::stoi(requireValue(i, argc, argv, arg));
        else if (arg == "--serial-device") parsed.options.serialDevice = requireValue(i, argc, argv, arg);
        else if (arg == "--serial-speed") parsed.options.serialSpeed = parseSerialSpeed(requireValue(i, argc, argv, arg));
        else if (arg == "--disc-type") parsed.options.discType = parseDiscType(requireValue(i, argc, argv, arg));
        else if (arg == "--mode") parsed.options.autoCaptureMode = parseAutoCaptureMode(requireValue(i, argc, argv, arg));
        else if (arg == "--start-address") parsed.options.startAddress = std::stoi(requireValue(i, argc, argv, arg));
        else if (arg == "--end-address") parsed.options.endAddress = std::stoi(requireValue(i, argc, argv, arg));
        else if (arg == "--key-lock") parsed.options.keyLock = true;
        else throw std::runtime_error("unknown option: " + arg);
    }
    return parsed;
}

std::string captureFormatExtension(CaptureFormatCli format)
{
    switch (format)
    {
    case CaptureFormatCli::Lds: return ".lds";
    case CaptureFormatCli::Raw: return ".raw";
    case CaptureFormatCli::Cds: return ".cds";
    }
    return ".lds";
}

std::filesystem::path buildOutputPath(const CliOptions& options)
{
    if (!options.output.empty())
    {
        auto output = options.output;
        if (output.extension().empty())
        {
            output += captureFormatExtension(options.captureFormat);
        }
        return output;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_r(&time, &localTime);

    std::ostringstream name;
    name << (options.testMode ? "TestData" : "RF-Sample") << "_";
    name << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S");
    name << captureFormatExtension(options.captureFormat);
    return options.outputDir / name.str();
}

UsbDeviceBase::CaptureFormat toUsbCaptureFormat(CaptureFormatCli format)
{
    switch (format)
    {
    case CaptureFormatCli::Lds: return UsbDeviceBase::CaptureFormat::Unsigned10Bit;
    case CaptureFormatCli::Raw: return UsbDeviceBase::CaptureFormat::Signed16Bit;
    case CaptureFormatCli::Cds: return UsbDeviceBase::CaptureFormat::Unsigned10Bit4to1Decimation;
    }
    return UsbDeviceBase::CaptureFormat::Unsigned10Bit;
}

std::string transferResultToString(UsbDeviceBase::TransferResult result)
{
    switch (result)
    {
    case UsbDeviceBase::TransferResult::Running: return "running";
    case UsbDeviceBase::TransferResult::Success: return "success";
    case UsbDeviceBase::TransferResult::FileCreationError: return "file creation error";
    case UsbDeviceBase::TransferResult::BufferUnderflow: return "buffer underflow";
    case UsbDeviceBase::TransferResult::ConnectionFailure: return "connection failure";
    case UsbDeviceBase::TransferResult::UsbMemoryLimit: return "USB memory limit";
    case UsbDeviceBase::TransferResult::UsbTransferFailure: return "USB transfer failure";
    case UsbDeviceBase::TransferResult::FileWriteError: return "file write error";
    case UsbDeviceBase::TransferResult::SequenceMismatch: return "sequence mismatch";
    case UsbDeviceBase::TransferResult::VerificationError: return "verification error";
    case UsbDeviceBase::TransferResult::ProgramError: return "program error";
    case UsbDeviceBase::TransferResult::ForcedAbort: return "forced abort";
    }
    return "unknown";
}

void printUsage()
{
    std::cout <<
        "Usage: dddcli <command> [options]\n"
        "\n"
        "Commands:\n"
        "  list-devices\n"
        "  capture\n"
        "  auto-capture\n"
        "  player status|play|pause|stop|still|read-user-codes\n"
        "\n"
        "Common options:\n"
        "  --config <file>                  TOML config path\n"
        "  --debug                          show debug logging\n"
        "  --quiet                          suppress non-error status\n"
        "\n"
        "Capture options:\n"
        "  --output <file>                  output capture path\n"
        "  --json <file>                    write JSON sidecar metadata\n"
        "  --output-dir <dir>               directory for generated output\n"
        "  --format lds|raw|cds             capture format\n"
        "  --test-mode                      capture test pattern\n"
        "  --duration <seconds>             stop manual capture after duration\n"
        "  --usb-device <path>              preferred USB device path\n"
        "  --vid <id> --pid <id>            USB IDs, decimal or 0x-prefixed\n"
        "\n"
        "Player/automatic options:\n"
        "  --serial-device <path>           serial device, e.g. /dev/ttyUSB0\n"
        "  --serial-speed auto|9600|4800|2400|1200\n"
        "  --disc-type cav|clv              required for auto-capture\n"
        "  --mode whole-disc|lead-in|partial\n"
        "  --start-address <n> --end-address <n>\n"
        "  --key-lock\n";
}
