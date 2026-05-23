#include "CliConfig.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
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

std::string stripComment(std::string line)
{
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        char ch = line[i];
        if (ch == '\'' && !inDoubleQuote)
        {
            inSingleQuote = !inSingleQuote;
        }
        else if (ch == '"' && !inSingleQuote)
        {
            inDoubleQuote = !inDoubleQuote;
        }
        else if (ch == '#' && !inSingleQuote && !inDoubleQuote)
        {
            line.resize(i);
            break;
        }
    }
    return line;
}

bool parseBool(const std::string& value)
{
    auto lowered = lower(trim(value));
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") return true;
    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") return false;
    throw std::runtime_error("invalid boolean value: " + value);
}

int parseSignedInt(const std::string& value, const std::string& description)
{
    auto text = trim(value);
    if (text.empty())
    {
        throw std::runtime_error("invalid " + description + ": " + value);
    }

    size_t parsedLength = 0;
    try
    {
        int parsed = std::stoi(text, &parsedLength);
        if (parsedLength != text.size())
        {
            throw std::runtime_error("invalid " + description + ": " + value);
        }
        return parsed;
    }
    catch (const std::invalid_argument&)
    {
        throw std::runtime_error("invalid " + description + ": " + value);
    }
    catch (const std::out_of_range&)
    {
        throw std::runtime_error(description + " out of range: " + value);
    }
}

int parsePositiveInt(const std::string& value, const std::string& description)
{
    int parsed = parseSignedInt(value, description);
    if (parsed <= 0)
    {
        throw std::runtime_error(description + " must be greater than zero: " + value);
    }
    return parsed;
}

int parseNonNegativeInt(const std::string& value, const std::string& description)
{
    int parsed = parseSignedInt(value, description);
    if (parsed < 0)
    {
        throw std::runtime_error(description + " must be non-negative: " + value);
    }
    return parsed;
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

    text = trim(text);
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch); }))
    {
        throw std::runtime_error("invalid size: " + value);
    }

    unsigned long long parsed = 0;
    try
    {
        parsed = std::stoull(text);
    }
    catch (const std::out_of_range&)
    {
        throw std::runtime_error("size out of range: " + value);
    }

    if (parsed > std::numeric_limits<size_t>::max() / multiplier)
    {
        throw std::runtime_error("size out of range: " + value);
    }
    return (size_t)parsed * multiplier;
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

PlayerProfileCli parsePlayerProfile(const std::string& value)
{
    auto text = lower(trim(value));
    if (text == "auto") return PlayerProfileCli::Auto;
    if (text == "generic-level3" || text == "generic-level-3" || text == "generic") return PlayerProfileCli::GenericLevel3;
    if (text == "pioneer-ld-v4300d" || text == "ld-v4300d" || text == "ldv4300d") return PlayerProfileCli::PioneerLdV4300D;
    if (text == "pioneer-ld-v2200" || text == "ld-v2200" || text == "ldv2200") return PlayerProfileCli::PioneerLdV2200;
    throw std::runtime_error("invalid player profile: " + value);
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
    auto text = trim(value);
    int base = 10;
    std::string digits = text;
    if (lower(text).starts_with("0x"))
    {
        base = 16;
        digits = text.substr(2);
    }
    if (digits.empty())
    {
        throw std::runtime_error("invalid USB ID: " + value);
    }
    auto validDigit = [base](unsigned char ch)
    {
        return base == 16 ? std::isxdigit(ch) : std::isdigit(ch);
    };
    if (!std::all_of(digits.begin(), digits.end(), validDigit))
    {
        throw std::runtime_error("invalid USB ID: " + value);
    }

    unsigned long parsed = 0;
    try
    {
        parsed = std::stoul(digits, nullptr, base);
    }
    catch (const std::out_of_range&)
    {
        throw std::runtime_error("USB ID out of range: " + value);
    }
    if (parsed > 0xFFFF)
    {
        throw std::runtime_error("USB ID out of range: " + value);
    }
    return (uint16_t)parsed;
}

void applyAddressFields(CliOptions& options)
{
    if (options.discType != DiscTypeCli::Clv)
    {
        if (!options.startAddressText.empty()) options.startAddress = parseNonNegativeInt(options.startAddressText, "CAV address");
        if (!options.endAddressText.empty()) options.endAddress = parseNonNegativeInt(options.endAddressText, "CAV address");
        return;
    }

    if (!options.startAddressText.empty()) options.startAddress = parseClvAddressSeconds(options.startAddressText);
    if (!options.endAddressText.empty()) options.endAddress = parseClvAddressSeconds(options.endAddressText);
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
    else if (key == "capture.duration_seconds") options.durationSeconds = parsePositiveInt(stripQuotes(value), "duration");
    else if (key == "player.serial_device") options.serialDevice = stripQuotes(value);
    else if (key == "player.serial_speed") options.serialSpeed = parseSerialSpeed(stripQuotes(value));
    else if (key == "player.profile") options.playerProfile = parsePlayerProfile(stripQuotes(value));
    else if (key == "auto_capture.disc_type") options.discType = parseDiscType(stripQuotes(value));
    else if (key == "auto_capture.mode") options.autoCaptureMode = parseAutoCaptureMode(stripQuotes(value));
    else if (key == "auto_capture.start_address") options.startAddressText = stripQuotes(value);
    else if (key == "auto_capture.end_address") options.endAddressText = stripQuotes(value);
    else if (key == "auto_capture.key_lock") options.keyLock = parseBool(value);
    else throw std::runtime_error("unknown config key: " + key);
}

ParsedCommandLine parseCommandLineImpl(int argc, char* argv[], const CliOptions& baseOptions, bool validate)
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
        if (parsed.playerAction == "raw-command" && argc > 3)
        {
            parsed.playerRawCommand = argv[3];
            startIndex = 4;
        }
    }

    for (int i = startIndex; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") parsed.command = "help";
        else if (arg == "--config")
        {
            parsed.options.configPath = requireValue(i, argc, argv, arg);
            parsed.options.configPathExplicit = true;
        }
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
        else if (arg == "--duration") parsed.options.durationSeconds = parsePositiveInt(requireValue(i, argc, argv, arg), "duration");
        else if (arg == "--serial-device") parsed.options.serialDevice = requireValue(i, argc, argv, arg);
        else if (arg == "--serial-speed") parsed.options.serialSpeed = parseSerialSpeed(requireValue(i, argc, argv, arg));
        else if (arg == "--player-profile") parsed.options.playerProfile = parsePlayerProfile(requireValue(i, argc, argv, arg));
        else if (arg == "--disc-type") parsed.options.discType = parseDiscType(requireValue(i, argc, argv, arg));
        else if (arg == "--mode") parsed.options.autoCaptureMode = parseAutoCaptureMode(requireValue(i, argc, argv, arg));
        else if (arg == "--start-address") parsed.options.startAddressText = requireValue(i, argc, argv, arg);
        else if (arg == "--end-address") parsed.options.endAddressText = requireValue(i, argc, argv, arg);
        else if (arg == "--key-lock") parsed.options.keyLock = true;
        else throw std::runtime_error("unknown option: " + arg);
    }
    applyAddressFields(parsed.options);
    if (validate)
    {
        validateAutoCaptureOptions(parsed);
    }
    return parsed;
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

bool TomlConfig::load(const std::filesystem::path& path, std::string& error, bool allowMissing)
{
    values.clear();
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        std::error_code existsError;
        bool exists = std::filesystem::exists(path, existsError);
        if (allowMissing && !exists && !existsError)
        {
            return true;
        }
        error = "failed to open " + path.string();
        return false;
    }

    std::string section;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(stream, line))
    {
        ++lineNumber;
        line = trim(stripComment(line));
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
    applyAddressFields(options);
}

ParsedCommandLine parseCommandLine(int argc, char* argv[])
{
    CliOptions options;
    options.configPath = defaultConfigPath();
    return parseCommandLineImpl(argc, argv, options, false);
}

ParsedCommandLine parseCommandLine(int argc, char* argv[], const CliOptions& baseOptions)
{
    return parseCommandLineImpl(argc, argv, baseOptions, true);
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

int parseClvAddressSeconds(const std::string& value)
{
    auto text = trim(value);
    if (text.empty())
    {
        throw std::runtime_error("invalid CLV address: empty value");
    }
    if (!std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch); }))
    {
        throw std::runtime_error("invalid CLV address: " + value);
    }

    if (text.size() < 5)
    {
        return std::stoi(text);
    }
    if (text.size() == 5 || text.size() == 7)
    {
        int hours = std::stoi(text.substr(0, 1));
        int minutes = std::stoi(text.substr(1, 2));
        int seconds = std::stoi(text.substr(3, 2));
        if (minutes >= 60 || seconds >= 60)
        {
            throw std::runtime_error("invalid CLV time code: " + value);
        }
        return (hours * 60 * 60) + (minutes * 60) + seconds;
    }

    throw std::runtime_error("invalid CLV address length: " + value);
}

AutoCaptureEndAddress resolveAutoCaptureEndAddress(
    AutoCaptureModeCli mode,
    int requestedEndAddress,
    int startAddress,
    int discEndAddress)
{
    AutoCaptureEndAddress result;
    if (mode == AutoCaptureModeCli::WholeDisc)
    {
        result.endAddress = discEndAddress;
        return result;
    }

    result.endAddress = requestedEndAddress > 0 ? std::min(requestedEndAddress, discEndAddress) : discEndAddress;
    result.cappedToDiscEnd = requestedEndAddress > discEndAddress;
    if (mode == AutoCaptureModeCli::Partial && result.endAddress <= startAddress)
    {
        result.validRange = false;
    }
    return result;
}

bool shouldStopAutoCaptureAtAddress(
    DiscTypeCli discType,
    int address,
    int endAddress,
    const std::chrono::steady_clock::time_point& now,
    AutoCaptureStopState& state)
{
    if (discType != DiscTypeCli::Clv)
    {
        return address >= endAddress;
    }

    if (address > endAddress)
    {
        return true;
    }
    if (address < endAddress)
    {
        state.clvPostRollStarted = false;
        return false;
    }
    if (!state.clvPostRollStarted)
    {
        state.clvPostRollStarted = true;
        state.clvPostRollStart = now;
        return false;
    }

    constexpr auto clvEndAddressPostRoll = std::chrono::milliseconds(1500);
    return now - state.clvPostRollStart >= clvEndAddressPostRoll;
}

bool shouldStopAutoCaptureForPlayerState(
    DiscTypeCli discType,
    const AutoCaptureStopState& state,
    PlayerStateCli playerState)
{
    if (discType != DiscTypeCli::Clv || !state.clvPostRollStarted)
    {
        return false;
    }

    return playerState == PlayerStateCli::Stop ||
        playerState == PlayerStateCli::Pause ||
        playerState == PlayerStateCli::StillFrame;
}

void validateAutoCaptureOptions(const ParsedCommandLine& parsed)
{
    if (parsed.command != "auto-capture")
    {
        return;
    }
    if (parsed.options.startAddress < 0)
    {
        throw std::runtime_error("auto-capture requires --start-address to be non-negative");
    }
    if (parsed.options.endAddress < 0)
    {
        throw std::runtime_error("auto-capture requires --end-address to be non-negative");
    }
    if (parsed.options.autoCaptureMode != AutoCaptureModeCli::Partial)
    {
        return;
    }
    if (parsed.options.endAddress <= 0)
    {
        throw std::runtime_error("partial auto-capture requires --end-address");
    }
    if (parsed.options.endAddress <= parsed.options.startAddress)
    {
        throw std::runtime_error("partial auto-capture requires --end-address greater than --start-address");
    }
}

std::string playerProfileToString(PlayerProfileCli profile)
{
    switch (profile)
    {
    case PlayerProfileCli::Auto: return "auto";
    case PlayerProfileCli::GenericLevel3: return "generic-level3";
    case PlayerProfileCli::PioneerLdV4300D: return "pioneer-ld-v4300d";
    case PlayerProfileCli::PioneerLdV2200: return "pioneer-ld-v2200";
    }
    return "auto";
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
        "  player status|play|pause|stop|still|read-user-codes|raw-command <command>\n"
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
        "  --disk-buffer-queue-size <size>  disk buffer queue size, bytes/mb/mib\n"
        "  --small-usb-transfer-queue       use reduced USB transfer queue\n"
        "  --large-usb-transfer-queue       use configured USB transfer queue\n"
        "  --small-usb-transfers            use small USB transfers\n"
        "  --large-usb-transfers            use large USB transfers\n"
        "\n"
        "Player/automatic options:\n"
        "  --serial-device <path>           serial device, e.g. /dev/ttyUSB0\n"
        "  --serial-speed auto|9600|4800|2400|1200\n"
        "  --player-profile auto|generic-level3|pioneer-ld-v4300d|pioneer-ld-v2200\n"
        "  --disc-type cav|clv              required for auto-capture\n"
        "  --mode whole-disc|lead-in|partial\n"
        "  --start-address <n> --end-address <n>\n"
        "  --key-lock\n";
}
