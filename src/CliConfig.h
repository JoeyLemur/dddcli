#pragma once

#include "UsbDeviceBase.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

enum class CaptureFormatCli
{
    Lds,
    Raw,
    Cds,
};

enum class SerialSpeedCli
{
    Auto,
    Bps9600,
    Bps4800,
    Bps2400,
    Bps1200,
};

enum class PlayerProfileCli
{
    Auto,
    GenericLevel3,
    PioneerLdV4300D,
    PioneerLdV2200,
};

enum class DiscTypeCli
{
    Unknown,
    Cav,
    Clv,
};

enum class AutoCaptureModeCli
{
    WholeDisc,
    LeadIn,
    Partial,
};

enum class PlayerStateCli
{
    Unknown,
    Stop,
    Play,
    PlayWithStopCodesDisabled,
    Pause,
    StillFrame,
};

struct CliOptions
{
    std::filesystem::path configPath;
    bool debug = false;
    bool quiet = false;

    uint16_t usbVid = 0x1D50;
    uint16_t usbPid = 0x603B;
    std::string usbPreferredDevice;
    size_t diskBufferQueueSize = 256 * 1024 * 1024;
    bool useSmallUsbTransferQueue = false;
    bool useSmallUsbTransfers = true;
    bool useAsyncFileIo = false;

    std::filesystem::path output;
    std::filesystem::path jsonOutput;
    std::filesystem::path outputDir = ".";
    CaptureFormatCli captureFormat = CaptureFormatCli::Lds;
    bool testMode = false;
    std::optional<int> durationSeconds;

    std::string serialDevice;
    SerialSpeedCli serialSpeed = SerialSpeedCli::Auto;
    PlayerProfileCli playerProfile = PlayerProfileCli::Auto;
    DiscTypeCli discType = DiscTypeCli::Unknown;
    AutoCaptureModeCli autoCaptureMode = AutoCaptureModeCli::WholeDisc;
    int startAddress = 0;
    int endAddress = 0;
    std::string startAddressText;
    std::string endAddressText;
    bool keyLock = false;
};

struct ParsedCommandLine
{
    std::string command;
    std::string playerAction;
    std::string playerRawCommand;
    CliOptions options;
};

struct AutoCaptureStopState
{
    bool clvPostRollStarted = false;
    std::chrono::steady_clock::time_point clvPostRollStart{};
};

class TomlConfig
{
public:
    bool load(const std::filesystem::path& path, std::string& error);
    void applyTo(CliOptions& options) const;

private:
    std::map<std::string, std::string> values;
};

std::filesystem::path defaultConfigPath();
ParsedCommandLine parseCommandLine(int argc, char* argv[]);
ParsedCommandLine parseCommandLine(int argc, char* argv[], const CliOptions& baseOptions);
std::filesystem::path buildOutputPath(const CliOptions& options);
UsbDeviceBase::CaptureFormat toUsbCaptureFormat(CaptureFormatCli format);
std::string captureFormatExtension(CaptureFormatCli format);
int parseClvAddressSeconds(const std::string& value);
bool shouldStopAutoCaptureAtAddress(
    DiscTypeCli discType,
    int address,
    int endAddress,
    const std::chrono::steady_clock::time_point& now,
    AutoCaptureStopState& state);
bool shouldStopAutoCaptureForPlayerState(
    DiscTypeCli discType,
    const AutoCaptureStopState& state,
    PlayerStateCli playerState);
std::string playerProfileToString(PlayerProfileCli profile);
std::string transferResultToString(UsbDeviceBase::TransferResult result);
void printUsage();
