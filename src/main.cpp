#include "CliConfig.h"
#include "ConsoleLogger.h"
#include "CaptureMetadata.h"
#include "PlayerSerial.h"
#include "ProgressLine.h"
#include "UsbDeviceLibUsb.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

namespace
{
std::atomic<bool> stopRequested = false;

void signalHandler(int)
{
    stopRequested = true;
}

size_t usbQueueSize(const CliOptions& options)
{
    const size_t smallUsbTransferQueueSize = 12 * 1024 * 1024;
    return options.useSmallUsbTransferQueue ? smallUsbTransferQueueSize : options.diskBufferQueueSize;
}

bool initializeUsb(UsbDeviceLibUsb& usb, const CliOptions& options)
{
    return usb.Initialize(options.usbVid, options.usbPid);
}

CaptureProgressSnapshot makeCaptureProgressSnapshot(UsbDeviceBase& usb, const std::chrono::steady_clock::time_point& start)
{
    return {
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count(),
        static_cast<uint64_t>(usb.GetFileSizeWrittenInBytes()),
        static_cast<uint64_t>(usb.GetNumberOfTransfers()),
        static_cast<uint64_t>(usb.GetProcessedSampleCount())
    };
}

bool startCapture(UsbDeviceBase& usb, const CliOptions& options, const std::filesystem::path& outputPath)
{
    std::filesystem::create_directories(outputPath.parent_path().empty() ? "." : outputPath.parent_path());
    usb.SendConfigurationCommand(options.usbPreferredDevice, options.testMode);
    return usb.StartCapture(
        outputPath,
        toUsbCaptureFormat(options.captureFormat),
        options.usbPreferredDevice,
        options.testMode,
        options.useSmallUsbTransfers,
        options.useAsyncFileIo,
        usbQueueSize(options),
        options.diskBufferQueueSize);
}

class AutoCaptureCleanupGuard
{
public:
    AutoCaptureCleanupGuard(UsbDeviceBase& usbDevice, PlayerSerial& serialPlayer, bool& started, bool& locked)
        : usb(usbDevice), player(serialPlayer), captureStarted(started), keyLocked(locked)
    {
    }

    ~AutoCaptureCleanupGuard()
    {
        cleanup();
    }

    void disarm()
    {
        active = false;
    }

private:
    void cleanup()
    {
        if (!active)
        {
            return;
        }
        if (captureStarted && usb.GetTransferInProgress())
        {
            usb.StopCapture();
        }
        if (keyLocked)
        {
            if (!player.setKeyLock(false))
            {
                std::cerr << "Failed to release player key lock during cleanup\n";
            }
            keyLocked = false;
        }
    }

    UsbDeviceBase& usb;
    PlayerSerial& player;
    bool& captureStarted;
    bool& keyLocked;
    bool active = true;
};

int runListDevices(UsbDeviceLibUsb& usb, const CliOptions& options)
{
    if (!initializeUsb(usb, options))
    {
        std::cerr << "Failed to initialize USB backend\n";
        return 1;
    }
    std::vector<std::string> paths;
    if (!usb.GetPresentDevicePaths(paths) || paths.empty())
    {
        std::cout << "No Domesday Duplicator USB devices found\n";
        return 1;
    }
    for (const auto& path : paths)
    {
        std::cout << path << '\n';
    }
    return 0;
}

void writeMetadataIfRequested(const CliOptions& options, const CaptureMetadata& metadata, const UsbDeviceBase& usb)
{
    if (options.jsonOutput.empty())
    {
        return;
    }

    std::string error;
    if (!writeCaptureMetadata(options.jsonOutput, metadata, usb, error))
    {
        std::cerr << "Failed to write JSON metadata: " << error << "\n";
    }
    else if (!options.quiet)
    {
        std::cerr << "Wrote JSON metadata to " << options.jsonOutput << "\n";
    }
}

int finishCapture(UsbDeviceBase& usb)
{
    if (usb.GetTransferInProgress())
    {
        usb.StopCapture();
    }
    auto result = usb.GetTransferResult();
    if (result != UsbDeviceBase::TransferResult::Success && result != UsbDeviceBase::TransferResult::ForcedAbort)
    {
        std::cerr << "Capture ended with " << transferResultToString(result) << "\n";
        return 1;
    }
    std::cerr << "Capture complete: " << transferResultToString(result) << "\n";
    return 0;
}

int runCapture(UsbDeviceLibUsb& usb, const CliOptions& options)
{
    if (!initializeUsb(usb, options))
    {
        std::cerr << "Failed to initialize USB backend\n";
        return 1;
    }

    auto outputPath = buildOutputPath(options);
    if (!options.quiet)
    {
        std::cerr << "Capturing to " << outputPath << "\n";
    }
    if (!startCapture(usb, options, outputPath))
    {
        std::cerr << "Failed to start capture: " << transferResultToString(usb.GetTransferResult()) << "\n";
        return 1;
    }

    CaptureMetadata metadata;
    metadata.captureFilePath = outputPath;
    metadata.captureFormat = options.captureFormat;
    metadata.testMode = options.testMode;
    metadata.creationTimeUtc = std::chrono::system_clock::now();

    auto start = std::chrono::steady_clock::now();
    ProgressLine progress(options.quiet);
    while (!stopRequested && usb.GetTransferInProgress())
    {
        progress.update(makeCaptureProgressSnapshot(usb, start));
        if (options.durationSeconds.has_value() &&
            std::chrono::steady_clock::now() - start >= std::chrono::seconds(options.durationSeconds.value()))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    progress.finish();
    int result = finishCapture(usb);
    metadata.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    writeMetadataIfRequested(options, metadata, usb);
    return result;
}

bool connectPlayer(PlayerSerial& player, const CliOptions& options)
{
    std::string error;
    if (!player.connect(options.serialDevice, options.serialSpeed, options.playerProfile, error))
    {
        std::cerr << "Failed to connect to player: " << error << "\n";
        return false;
    }
    if (!options.quiet)
    {
        std::cerr << "Connected to " << player.modelName() << " version " << player.versionNumber()
                  << " @ " << serialSpeedToString(player.detectedSpeed()) << " bps"
                  << " using " << playerProfileToString(player.activeProfile()) << " profile\n";
    }
    return true;
}

int runPlayerTransportAction(PlayerSerial& player, const std::string& action, PlayerStateCli state)
{
    if (!player.setPlayerState(state))
    {
        std::cerr << "Failed to " << action << " player\n";
        return 1;
    }
    return 0;
}

int runPlayer(const ParsedCommandLine& parsed)
{
    const auto& action = parsed.playerAction.empty() ? std::string("status") : parsed.playerAction;
    if (action == "raw-command" && !parsed.playerRawCommand.empty() && !playerRawCommandFits(parsed.playerRawCommand))
    {
        std::cerr << "raw-command exceeds " << MaxPlayerSerialCommandBytes << " bytes including carriage return\n";
        return 1;
    }

    PlayerSerial player;
    if (!connectPlayer(player, parsed.options))
    {
        return 1;
    }

    if (action == "status")
    {
        std::cout << "model=" << player.modelName() << "\n";
        std::cout << "playerProfile=" << playerProfileToString(player.activeProfile()) << "\n";
        std::cout << "state=" << playerStateToString(player.getPlayerState()) << "\n";
        std::cout << "discType=" << discTypeToString(player.getDiscType()) << "\n";
        std::cout << "discStatus=" << player.getDiscStatus() << "\n";
    }
    else if (action == "play") return runPlayerTransportAction(player, action, PlayerStateCli::Play);
    else if (action == "pause") return runPlayerTransportAction(player, action, PlayerStateCli::Pause);
    else if (action == "stop") return runPlayerTransportAction(player, action, PlayerStateCli::Stop);
    else if (action == "still") return runPlayerTransportAction(player, action, PlayerStateCli::StillFrame);
    else if (action == "read-user-codes")
    {
        std::cout << "standardUserCode=" << player.getStandardUserCode() << "\n";
        std::cout << "pioneerUserCode=" << player.getPioneerUserCode() << "\n";
    }
    else if (action == "raw-command")
    {
        if (parsed.playerRawCommand.empty())
        {
            std::cerr << "raw-command requires a command string\n";
            return 1;
        }
        std::cout << escapedSerialResponse(player.rawCommand(parsed.playerRawCommand)) << "\n";
    }
    else
    {
        std::cerr << "Unknown player action: " << action << "\n";
        return 1;
    }
    return 0;
}

int currentAutoAddress(PlayerSerial& player, DiscTypeCli discType)
{
    return discType == DiscTypeCli::Cav ? player.getCurrentFrame().address : player.getCurrentTimeCode().address;
}

void recordAutoAddress(CaptureMetadata& metadata, DiscTypeCli discType, int address)
{
    if (address < 0)
    {
        return;
    }
    if (discType == DiscTypeCli::Cav)
    {
        metadata.minFrameNumber = metadata.minFrameNumber.has_value() ? std::min(metadata.minFrameNumber.value(), address) : address;
        metadata.maxFrameNumber = metadata.maxFrameNumber.has_value() ? std::max(metadata.maxFrameNumber.value(), address) : address;
    }
    else if (discType == DiscTypeCli::Clv)
    {
        metadata.minTimeCode = metadata.minTimeCode.has_value() ? std::min(metadata.minTimeCode.value(), address) : address;
        metadata.maxTimeCode = metadata.maxTimeCode.has_value() ? std::max(metadata.maxTimeCode.value(), address) : address;
    }
}

int runAutoCapture(UsbDeviceLibUsb& usb, const CliOptions& options)
{
    if (options.discType == DiscTypeCli::Unknown)
    {
        std::cerr << "auto-capture requires --disc-type cav|clv\n";
        return 1;
    }
    if (options.autoCaptureMode == AutoCaptureModeCli::Partial && options.endAddress <= 0)
    {
        std::cerr << "partial auto-capture requires --end-address\n";
        return 1;
    }
    if (options.autoCaptureMode == AutoCaptureModeCli::Partial && options.endAddress <= options.startAddress)
    {
        std::cerr << "partial auto-capture requires --end-address greater than --start-address\n";
        return 1;
    }
    if (!initializeUsb(usb, options))
    {
        std::cerr << "Failed to initialize USB backend\n";
        return 1;
    }

    PlayerSerial player;
    if (!connectPlayer(player, options))
    {
        return 1;
    }

    bool keyLocked = false;
    bool captureStarted = false;
    auto outputPath = buildOutputPath(options);
    CaptureMetadata metadata;
    metadata.captureFilePath = outputPath;
    metadata.captureFormat = options.captureFormat;
    metadata.testMode = options.testMode;
    metadata.creationTimeUtc = std::chrono::system_clock::now();
    metadata.playerModelCode = player.modelCode();
    metadata.playerModelName = player.modelName();
    metadata.playerVersionNumber = player.versionNumber();
    metadata.serialSpeed = serialSpeedToString(player.detectedSpeed());
    AutoCaptureCleanupGuard cleanupGuard(usb, player, captureStarted, keyLocked);

    if (options.keyLock)
    {
        if (!player.setKeyLock(true))
        {
            std::cerr << "Could not enable player key lock\n";
            return 1;
        }
        keyLocked = true;
    }

    PlayerStateCli state = player.getPlayerState();
    if (state == PlayerStateCli::Unknown)
    {
        std::cerr << "The player's state could not be determined\n";
        return 1;
    }
    if (state == PlayerStateCli::Stop && !player.setPlayerState(PlayerStateCli::Play))
    {
        std::cerr << "Could not spin up player\n";
        return 1;
    }
    auto detectedDiscType = player.getDiscType();
    metadata.discType = discTypeToString(detectedDiscType);
    metadata.discStatus = player.getDiscStatus();
    if (detectedDiscType != options.discType)
    {
        std::cerr << "The disc in the player does not match --disc-type\n";
        return 1;
    }

    int endAddress = options.endAddress;
    if (options.discType == DiscTypeCli::Cav)
    {
        if (!player.setPositionFrame(60000))
        {
            std::cerr << "Could not determine CAV disc length\n";
            return 1;
        }
        int discEnd = player.getCurrentFrame().address;
        recordAutoAddress(metadata, options.discType, discEnd);
        endAddress = options.autoCaptureMode == AutoCaptureModeCli::WholeDisc ? discEnd : (endAddress > 0 ? std::min(endAddress, discEnd) : discEnd);
    }
    else
    {
        constexpr int latestClvAddressToProbeSeconds = (1 * 60 * 60) + (59 * 60) + 59;
        if (!player.setPositionTimeCode(latestClvAddressToProbeSeconds))
        {
            std::cerr << "Could not determine CLV disc length\n";
            return 1;
        }
        int discEnd = player.getCurrentTimeCode().address;
        recordAutoAddress(metadata, options.discType, discEnd);
        endAddress = options.autoCaptureMode == AutoCaptureModeCli::WholeDisc ? discEnd : (endAddress > 0 ? std::min(endAddress, discEnd) : discEnd);
    }

    bool captureFromLeadIn = options.autoCaptureMode == AutoCaptureModeCli::WholeDisc || options.autoCaptureMode == AutoCaptureModeCli::LeadIn;
    if (captureFromLeadIn)
    {
        if (!player.setPlayerState(PlayerStateCli::Stop))
        {
            std::cerr << "Could not spin-down disc\n";
            return 1;
        }
    }
    else if (options.discType == DiscTypeCli::Cav)
    {
        if (!player.setPositionFrame(options.startAddress))
        {
            std::cerr << "Could not position player on start frame\n";
            return 1;
        }
    }
    else
    {
        if (!player.setPositionTimeCode(options.startAddress))
        {
            std::cerr << "Could not position player on start time code\n";
            return 1;
        }
    }

    if (!options.quiet)
    {
        std::cerr << "Capturing to " << outputPath << "\n";
    }
    if (!startCapture(usb, options, outputPath))
    {
        std::cerr << "Failed to start capture: " << transferResultToString(usb.GetTransferResult()) << "\n";
        return 1;
    }
    captureStarted = true;

    PlayerStateCli playState = options.discType == DiscTypeCli::Cav && captureFromLeadIn
        ? PlayerStateCli::PlayWithStopCodesDisabled
        : PlayerStateCli::Play;
    if (!player.setPlayerState(playState))
    {
        std::cerr << "Could not start player playback\n";
        return 1;
    }

    int lastAddress = -1;
    int consecutiveAddressReadFailures = 0;
    constexpr int maxConsecutiveAddressReadFailures = 3;
    bool autoCaptureError = false;
    std::string autoCaptureErrorMessage;
    AutoCaptureStopState stopState;
    auto start = std::chrono::steady_clock::now();
    ProgressLine progress(options.quiet);
    while (!stopRequested && usb.GetTransferInProgress())
    {
        auto now = std::chrono::steady_clock::now();
        if (options.discType == DiscTypeCli::Cav && player.getPlayerState() == PlayerStateCli::StillFrame)
        {
            player.setPlayerState(PlayerStateCli::Play);
        }

        int address = currentAutoAddress(player, options.discType);
        if (address >= 0)
        {
            consecutiveAddressReadFailures = 0;
            recordAutoAddress(metadata, options.discType, address);
            if (shouldStopAutoCaptureAtAddress(options.discType, address, endAddress, now, stopState))
            {
                break;
            }
            if (stopState.clvPostRollStarted)
            {
                auto playerState = player.getPlayerState();
                if (shouldStopAutoCaptureForPlayerState(options.discType, stopState, playerState))
                {
                    if (!options.quiet)
                    {
                        progress.clear();
                        std::cerr << "CLV post-roll ended because player state is "
                                  << playerStateToString(playerState) << "\n";
                    }
                    break;
                }
            }
            if (lastAddress >= 0 && (lastAddress - 1000) > address)
            {
                autoCaptureError = true;
                autoCaptureErrorMessage = options.discType == DiscTypeCli::Cav
                    ? "CAV disc frames were not sequential - player skipped back more than 1000 frames during capture"
                    : "CLV disc frames were not sequential - player skipped back more than 1000 frames during capture";
                progress.clear();
                std::cerr << autoCaptureErrorMessage << "\n";
                break;
            }
            lastAddress = address;
        }
        else
        {
            ++consecutiveAddressReadFailures;
            progress.clear();
            std::cerr << "Could not read current player address ("
                      << consecutiveAddressReadFailures << "/"
                      << maxConsecutiveAddressReadFailures << ")\n";
            if (consecutiveAddressReadFailures >= maxConsecutiveAddressReadFailures)
            {
                autoCaptureError = true;
                autoCaptureErrorMessage = options.discType == DiscTypeCli::Cav
                    ? "Could not read current CAV frame number from player during capture"
                    : "Could not read current CLV time code from player during capture";
                progress.clear();
                std::cerr << autoCaptureErrorMessage << "\n";
                break;
            }
        }

        progress.update(makeCaptureProgressSnapshot(usb, start));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    progress.finish();
    int captureResult = finishCapture(usb);
    metadata.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    captureStarted = false;
    bool cleanupFailed = false;
    if (autoCaptureError)
    {
        if (!player.setPlayerState(PlayerStateCli::StillFrame))
        {
            std::cerr << "Failed to set player to still-frame during cleanup\n";
            cleanupFailed = true;
        }
    }
    else if (options.discType == DiscTypeCli::Cav)
    {
        if (!player.setPlayerState(PlayerStateCli::Stop))
        {
            std::cerr << "Failed to stop player during cleanup\n";
            cleanupFailed = true;
        }
    }
    else
    {
        if (!player.setPlayerState(PlayerStateCli::Pause))
        {
            std::cerr << "Failed to pause player during cleanup\n";
            cleanupFailed = true;
        }
    }
    if (keyLocked)
    {
        if (!player.setKeyLock(false))
        {
            std::cerr << "Failed to release player key lock during cleanup\n";
            cleanupFailed = true;
        }
        keyLocked = false;
    }
    cleanupGuard.disarm();
    writeMetadataIfRequested(options, metadata, usb);
    if (autoCaptureError)
    {
        return 1;
    }
    if (captureResult != 0)
    {
        return captureResult;
    }
    return cleanupFailed ? 1 : 0;
}
}

int main(int argc, char* argv[])
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try
    {
        auto preliminary = parseCommandLine(argc, argv);
        if (preliminary.command == "help")
        {
            printUsage();
            return 0;
        }

        TomlConfig config;
        std::string configError;
        CliOptions configured;
        configured.configPath = preliminary.options.configPath;
        configured.configPathExplicit = preliminary.options.configPathExplicit;
        if (!config.load(preliminary.options.configPath, configError, !preliminary.options.configPathExplicit))
        {
            std::cerr << "Failed to read config: " << configError << "\n";
            return 1;
        }
        config.applyTo(configured);
        auto parsed = parseCommandLine(argc, argv, configured);

        auto log = ConsoleLogger::Create(
            parsed.options.debug ? ILogger::SeverityFilter::DebugOrHigher : ILogger::SeverityFilter::InfoOrHigher,
            parsed.options.quiet);
        UsbDeviceLibUsb usb(*log.get());

        if (parsed.command == "list-devices") return runListDevices(usb, parsed.options);
        if (parsed.command == "capture") return runCapture(usb, parsed.options);
        if (parsed.command == "auto-capture") return runAutoCapture(usb, parsed.options);
        if (parsed.command == "player") return runPlayer(parsed);

        std::cerr << "Unknown command: " << parsed.command << "\n";
        printUsage();
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "error: " << ex.what() << "\n";
        return 1;
    }
}
