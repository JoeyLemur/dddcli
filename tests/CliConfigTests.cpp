#include "CliConfig.h"
#include "PlayerSerial.h"
#include "ProgressLine.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
void assertParseThrows(const char* const* argv, int argc)
{
    bool threw = false;
    try
    {
        parseCommandLine(argc, const_cast<char**>(argv));
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void assertParseThrows(const char* const* argv, int argc, const CliOptions& base)
{
    bool threw = false;
    try
    {
        parseCommandLine(argc, const_cast<char**>(argv), base);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void assertConfigApplyThrows(const std::filesystem::path& path, const std::string& contents)
{
    {
        std::ofstream file(path);
        file << contents;
    }

    TomlConfig config;
    std::string error;
    assert(config.load(path, error));
    bool threw = false;
    try
    {
        CliOptions options;
        config.applyTo(options);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
    std::filesystem::remove(path);
}

void assertClvAddressThrows(const std::string& value)
{
    bool threw = false;
    try
    {
        parseClvAddressSeconds(value);
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}
}

int main()
{
    assert(captureFormatExtension(CaptureFormatCli::Lds) == ".lds");
    assert(captureFormatExtension(CaptureFormatCli::Raw) == ".raw");
    assert(captureFormatExtension(CaptureFormatCli::Cds) == ".cds");

    CaptureProgressSnapshot progressSnapshot;
    progressSnapshot.elapsedSeconds = 42;
    progressSnapshot.bytesWritten = 5 * 1024 * 1024;
    progressSnapshot.transfers = 123;
    progressSnapshot.samples = 456;
    assert(formatCaptureProgressLine(progressSnapshot) == "elapsed=42s written=5MiB transfers=123 samples=456");

    std::ostringstream liveOutput;
    ProgressLine liveProgress(liveOutput, false, true);
    liveProgress.update({ 100, 12 * 1024 * 1024, 99999, 88888 });
    liveProgress.update({ 1, 0, 1, 2 });
    std::string liveText = liveOutput.str();
    assert(liveText.starts_with("\relapsed=100s written=12MiB transfers=99999 samples=88888"));
    assert(liveText.find("\relapsed=1s written=0MiB transfers=1 samples=2 ") != std::string::npos);
    liveProgress.clear();
    std::string clearedText = liveOutput.str();
    assert(clearedText.ends_with("\r"));
    liveProgress.finish();
    assert(liveOutput.str() == clearedText);

    std::ostringstream finishOutput;
    ProgressLine finishingProgress(finishOutput, false, true);
    finishingProgress.update({ 1, 0, 1, 2 });
    finishingProgress.finish();
    assert(finishOutput.str().ends_with("\n"));

    std::ostringstream quietOutput;
    ProgressLine quietProgress(quietOutput, true, true);
    quietProgress.update(progressSnapshot);
    quietProgress.clear();
    quietProgress.finish();
    assert(quietOutput.str().empty());

    std::ostringstream nonLiveOutput;
    ProgressLine nonLiveProgress(nonLiveOutput, false, false);
    nonLiveProgress.update(progressSnapshot);
    nonLiveProgress.clear();
    nonLiveProgress.finish();
    assert(nonLiveOutput.str().empty());

    CliOptions base;
    base.configPath = "custom.toml";
    base.outputDir = "/tmp/from-config";
    base.captureFormat = CaptureFormatCli::Raw;
    const char* argv[] = {
        "dddcli",
        "capture",
        "--output-dir",
        "/tmp/from-cli",
        "--format",
        "cds",
        "--json",
        "/tmp/capture.json",
        "--duration",
        "1",
    };
    auto parsed = parseCommandLine(10, const_cast<char**>(argv), base);
    assert(parsed.command == "capture");
    assert(parsed.options.outputDir == "/tmp/from-cli");
    assert(parsed.options.captureFormat == CaptureFormatCli::Cds);
    assert(parsed.options.jsonOutput == "/tmp/capture.json");
    assert(parsed.options.durationSeconds.has_value());
    assert(parsed.options.durationSeconds.value() == 1);

    const char* invalidDurationArgv[] = { "dddcli", "capture", "--duration", "10oops" };
    assertParseThrows(invalidDurationArgv, 4);
    const char* invalidDiskBufferSizeArgv[] = { "dddcli", "capture", "--disk-buffer-queue-size", "128MiBtypo" };
    assertParseThrows(invalidDiskBufferSizeArgv, 4);

    const char* configArgv[] = {
        "dddcli",
        "capture",
        "--config",
        "/tmp/missing-explicit-dddcli.toml",
    };
    auto configParsed = parseCommandLine(4, const_cast<char**>(configArgv));
    assert(configParsed.options.configPath == "/tmp/missing-explicit-dddcli.toml");
    assert(configParsed.options.configPathExplicit);

    const char* usbIdArgv[] = {
        "dddcli",
        "capture",
        "--vid",
        "7504",
        "--pid",
        "0x603B",
    };
    auto usbIdParsed = parseCommandLine(6, const_cast<char**>(usbIdArgv));
    assert(usbIdParsed.options.usbVid == 7504);
    assert(usbIdParsed.options.usbPid == 0x603B);

    const char* upperHexUsbIdArgv[] = {
        "dddcli",
        "capture",
        "--pid",
        "0X603B",
    };
    auto upperHexUsbIdParsed = parseCommandLine(4, const_cast<char**>(upperHexUsbIdArgv));
    assert(upperHexUsbIdParsed.options.usbPid == 0x603B);

    const char* outOfRangeVidArgv[] = { "dddcli", "capture", "--vid", "70000" };
    assertParseThrows(outOfRangeVidArgv, 4);
    const char* hugeVidArgv[] = { "dddcli", "capture", "--vid", "999999999999999999999999" };
    assertParseThrows(hugeVidArgv, 4);
    const char* outOfRangePidArgv[] = { "dddcli", "capture", "--pid", "0x10000" };
    assertParseThrows(outOfRangePidArgv, 4);
    const char* negativeVidArgv[] = { "dddcli", "capture", "--vid", "-1" };
    assertParseThrows(negativeVidArgv, 4);
    const char* partialVidArgv[] = { "dddcli", "capture", "--vid", "123abc" };
    assertParseThrows(partialVidArgv, 4);
    const char* malformedHexPidArgv[] = { "dddcli", "capture", "--pid", "0x" };
    assertParseThrows(malformedHexPidArgv, 4);
    const char* invalidPidArgv[] = { "dddcli", "capture", "--pid", "xyz" };
    assertParseThrows(invalidPidArgv, 4);

    const char* profileArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "clv",
        "--player-profile",
        "pioneer-ld-v2200",
        "--start-address",
        "01234",
        "--end-address",
        "0123400",
    };
    auto profileParsed = parseCommandLine(10, const_cast<char**>(profileArgv));
    assert(profileParsed.options.playerProfile == PlayerProfileCli::PioneerLdV2200);
    assert(profileParsed.options.startAddress == 754);
    assert(profileParsed.options.endAddress == 754);

    const char* validPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "--start-address",
        "1300",
        "--end-address",
        "1301",
    };
    CliOptions validationBase;
    auto validPartialParsed = parseCommandLine(10, const_cast<char**>(validPartialArgv), validationBase);
    assert(validPartialParsed.options.startAddress == 1300);
    assert(validPartialParsed.options.endAddress == 1301);

    const char* invalidCavStartArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "--start-address",
        "1300abc",
        "--end-address",
        "1301",
    };
    assertParseThrows(invalidCavStartArgv, 10, validationBase);

    const char* invalidCavEndArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "--start-address",
        "1300",
        "--end-address",
        "1301abc",
    };
    assertParseThrows(invalidCavEndArgv, 10, validationBase);

    const char* preliminaryPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--mode",
        "partial",
    };
    auto preliminaryPartialParsed = parseCommandLine(4, const_cast<char**>(preliminaryPartialArgv));
    assert(preliminaryPartialParsed.command == "auto-capture");
    assert(preliminaryPartialParsed.options.autoCaptureMode == AutoCaptureModeCli::Partial);

    const char* equalCavPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "--start-address",
        "1300",
        "--end-address",
        "1300",
    };
    assertParseThrows(equalCavPartialArgv, 10, validationBase);

    const char* reversedCavPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "--start-address",
        "1301",
        "--end-address",
        "1300",
    };
    assertParseThrows(reversedCavPartialArgv, 10, validationBase);

    const char* equalClvPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "clv",
        "--mode",
        "partial",
        "--start-address",
        "01234",
        "--end-address",
        "0123400",
    };
    assertParseThrows(equalClvPartialArgv, 10, validationBase);

    const char* invalidSixDigitClvPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "clv",
        "--mode",
        "partial",
        "--start-address",
        "123456",
        "--end-address",
        "123457",
    };
    assertParseThrows(invalidSixDigitClvPartialArgv, 10, validationBase);

    const char* invalidEightDigitClvPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "clv",
        "--mode",
        "partial",
        "--start-address",
        "01234",
        "--end-address",
        "01234000",
    };
    assertParseThrows(invalidEightDigitClvPartialArgv, 10, validationBase);

    CliOptions invalidConfiguredPartial;
    invalidConfiguredPartial.discType = DiscTypeCli::Cav;
    invalidConfiguredPartial.autoCaptureMode = AutoCaptureModeCli::Partial;
    invalidConfiguredPartial.startAddress = 1300;
    invalidConfiguredPartial.endAddress = 1300;
    const char* configuredPartialArgv[] = {
        "dddcli",
        "auto-capture",
    };
    assertParseThrows(configuredPartialArgv, 2, invalidConfiguredPartial);

    const char* rawArgv[] = {
        "dddcli",
        "player",
        "raw-command",
        "?T",
    };
    auto rawParsed = parseCommandLine(4, const_cast<char**>(rawArgv));
    assert(rawParsed.command == "player");
    assert(rawParsed.playerAction == "raw-command");
    assert(rawParsed.playerRawCommand == "?T");
    assert(playerRawCommandFits(std::string(19, 'A')));
    assert(playerRawCommandFits(std::string(19, 'A') + "\r"));
    assert(!playerRawCommandFits(std::string(20, 'A')));
    assert(!playerRawCommandFits(std::string(21, 'A') + "\r"));

    auto path = std::filesystem::temp_directory_path() / "dddcli-test.toml";
    {
        std::ofstream file(path);
        file << "[capture]\n";
        file << "output_dir = \"/tmp/cfg\"\n";
        file << "format = \"raw\"\n";
        file << "test_mode = true\n";
        file << "[usb]\n";
        file << "vid = \"0x1D50\"\n";
        file << "disk_buffer_queue_size = \"128MiB\"\n";
        file << "[player]\n";
        file << "serial_device = \"/dev/ttyUSB0\"\n";
        file << "serial_speed = \"9600\"\n";
        file << "profile = \"pioneer-ld-v4300d\"\n";
        file << "[auto_capture]\n";
        file << "disc_type = \"clv\"\n";
        file << "start_address = \"01234\"\n";
        file << "end_address = \"0123400\"\n";
    }

    TomlConfig config;
    std::string error;
    CliOptions options;
    assert(config.load(path, error));
    config.applyTo(options);
    assert(options.outputDir == "/tmp/cfg");
    assert(options.captureFormat == CaptureFormatCli::Raw);
    assert(options.testMode);
    assert(options.usbVid == 0x1D50);
    assert(options.diskBufferQueueSize == 128 * 1024 * 1024);
    assert(options.serialDevice == "/dev/ttyUSB0");
    assert(options.serialSpeed == SerialSpeedCli::Bps9600);
    assert(options.playerProfile == PlayerProfileCli::PioneerLdV4300D);
    assert(options.discType == DiscTypeCli::Clv);
    assert(options.startAddress == 754);
    assert(options.endAddress == 754);

    auto quotedHashPath = std::filesystem::temp_directory_path() / "dddcli-quoted-hash-test.toml";
    {
        std::ofstream file(quotedHashPath);
        file << "[capture]\n";
        file << "output_dir = \"/tmp/capture#1\"\n";
    }
    assert(config.load(quotedHashPath, error));
    CliOptions quotedHashOptions;
    config.applyTo(quotedHashOptions);
    assert(quotedHashOptions.outputDir == "/tmp/capture#1");

    auto quotedHashCommentPath = std::filesystem::temp_directory_path() / "dddcli-quoted-hash-comment-test.toml";
    {
        std::ofstream file(quotedHashCommentPath);
        file << "[capture]\n";
        file << "output_dir = \"/tmp/capture#1\" # comment\n";
    }
    assert(config.load(quotedHashCommentPath, error));
    CliOptions quotedHashCommentOptions;
    config.applyTo(quotedHashCommentOptions);
    assert(quotedHashCommentOptions.outputDir == "/tmp/capture#1");

    auto missingPath = std::filesystem::temp_directory_path() / "dddcli-missing-test.toml";
    std::filesystem::remove(missingPath);
    error.clear();
    assert(config.load(missingPath, error));
    error.clear();
    assert(!config.load(missingPath, error, false));
    assert(!error.empty());

    auto invalidUsbIdPath = std::filesystem::temp_directory_path() / "dddcli-invalid-usb-id-test.toml";
    {
        std::ofstream file(invalidUsbIdPath);
        file << "[usb]\n";
        file << "vid = \"0x10000\"\n";
    }
    assert(config.load(invalidUsbIdPath, error));
    bool invalidConfigThrew = false;
    try
    {
        CliOptions invalidUsbOptions;
        config.applyTo(invalidUsbOptions);
    }
    catch (const std::runtime_error&)
    {
        invalidConfigThrew = true;
    }
    assert(invalidConfigThrew);

    auto invalidDurationPath = std::filesystem::temp_directory_path() / "dddcli-invalid-duration-test.toml";
    assertConfigApplyThrows(invalidDurationPath, "[capture]\nduration_seconds = \"10oops\"\n");

    auto invalidSizePath = std::filesystem::temp_directory_path() / "dddcli-invalid-size-test.toml";
    assertConfigApplyThrows(invalidSizePath, "[usb]\ndisk_buffer_queue_size = \"128MiBtypo\"\n");

    auto invalidClvAddressPath = std::filesystem::temp_directory_path() / "dddcli-invalid-clv-address-test.toml";
    assertConfigApplyThrows(invalidClvAddressPath, "[auto_capture]\ndisc_type = \"clv\"\nstart_address = \"123456\"\n");

    auto unknownConfigKeyPath = std::filesystem::temp_directory_path() / "dddcli-unknown-key-test.toml";
    assertConfigApplyThrows(unknownConfigKeyPath, "[capture]\nduration_second = 10\n");

    assert(parseClvAddressSeconds("754") == 754);
    assert(parseClvAddressSeconds("01234") == 754);
    assert(parseClvAddressSeconds("0123400") == 754);
    assertClvAddressThrows("123456");
    assertClvAddressThrows("01234000");
    assertClvAddressThrows("999999999999999999999999");
    assert(playerProfileForModelCode("15", PlayerProfileCli::Auto) == PlayerProfileCli::PioneerLdV4300D);
    assert(playerProfileForModelCode("07", PlayerProfileCli::Auto) == PlayerProfileCli::PioneerLdV2200);
    assert(playerProfileForModelCode("42", PlayerProfileCli::Auto) == PlayerProfileCli::GenericLevel3);
    assert(playerProfileForModelCode("07", PlayerProfileCli::PioneerLdV4300D) == PlayerProfileCli::PioneerLdV4300D);

    auto timeCode5 = parsePlayerTimeCodeResponse("01234\r");
    assert(timeCode5.address == 754);
    assert(!timeCode5.inLeadIn);
    assert(!timeCode5.inLeadOut);
    auto timeCode7 = parsePlayerTimeCodeResponse(">0123400\r");
    assert(timeCode7.address == 754);
    assert(timeCode7.inLeadOut);
    auto frame = parsePlayerFrameResponse("<12345\r");
    assert(frame.address == 12345);
    assert(frame.inLeadIn);
    assert(parsePlayerFrameResponse("E04\r").address == -1);
    assert(parsePlayerFrameResponse("abc\r").address == -1);
    assert(parsePlayerFrameResponse("12x45\r").address == -1);
    assert(parsePlayerFrameResponse("<E04\r").address == -1);
    auto emptyLeadOutFrame = parsePlayerFrameResponse(">\r");
    assert(emptyLeadOutFrame.address == -1);
    assert(emptyLeadOutFrame.inLeadOut);
    assert(parsePlayerTimeCodeResponse("E04\r").address == -1);
    assert(parsePlayerTimeCodeResponse("abc\r").address == -1);
    assert(parsePlayerTimeCodeResponse("0126A\r").address == -1);
    assert(parsePlayerTimeCodeResponse("<bad\r").address == -1);
    auto emptyLeadOutTimeCode = parsePlayerTimeCodeResponse(">\r");
    assert(emptyLeadOutTimeCode.address == -1);
    assert(emptyLeadOutTimeCode.inLeadOut);
    assert(escapedSerialResponse("A\r\n\t\\\x01") == "A\\r\\n\\t\\\\\\x01");

    auto now = std::chrono::steady_clock::now();
    AutoCaptureStopState cavStop;
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Cav, 1299, 1300, now, cavStop));
    assert(shouldStopAutoCaptureAtAddress(DiscTypeCli::Cav, 1300, 1300, now, cavStop));

    AutoCaptureStopState clvStop;
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 89, 90, now, clvStop));
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 90, 90, now, clvStop));
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 90, 90, now + std::chrono::milliseconds(1499), clvStop));
    assert(shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 90, 90, now + std::chrono::milliseconds(1500), clvStop));

    AutoCaptureStopState clvAdvanceStop;
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 90, 90, now, clvAdvanceStop));
    assert(shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 91, 90, now + std::chrono::milliseconds(500), clvAdvanceStop));

    AutoCaptureStopState inactivePostRoll;
    assert(!shouldStopAutoCaptureForPlayerState(DiscTypeCli::Clv, inactivePostRoll, PlayerStateCli::Stop));

    AutoCaptureStopState activePostRoll;
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 90, 90, now, activePostRoll));
    assert(shouldStopAutoCaptureForPlayerState(DiscTypeCli::Clv, activePostRoll, PlayerStateCli::Stop));
    assert(shouldStopAutoCaptureForPlayerState(DiscTypeCli::Clv, activePostRoll, PlayerStateCli::Pause));
    assert(shouldStopAutoCaptureForPlayerState(DiscTypeCli::Clv, activePostRoll, PlayerStateCli::StillFrame));
    assert(!shouldStopAutoCaptureForPlayerState(DiscTypeCli::Clv, activePostRoll, PlayerStateCli::Play));
    assert(!shouldStopAutoCaptureForPlayerState(DiscTypeCli::Clv, activePostRoll, PlayerStateCli::Unknown));
    assert(!shouldStopAutoCaptureForPlayerState(DiscTypeCli::Cav, activePostRoll, PlayerStateCli::Stop));

    std::filesystem::remove(path);
    std::filesystem::remove(quotedHashPath);
    std::filesystem::remove(quotedHashCommentPath);
    std::filesystem::remove(invalidUsbIdPath);
    return 0;
}
