// SPDX-FileCopyrightText: Copyright (C) 2026 Ed Powell
// SPDX-License-Identifier: GPL-3.0-only

#include "AutoCaptureOrchestration.h"
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
    assert(formatCaptureProgressLine(progressSnapshot) == "elapsed=42s written=5MiB transfers=123");
    progressSnapshot.playerPosition = "timecode=0:01:35";
    assert(formatCaptureProgressLine(progressSnapshot) == "elapsed=42s written=5MiB transfers=123 timecode=0:01:35");

    std::ostringstream liveOutput;
    ProgressLine liveProgress(liveOutput, false, true);
    liveProgress.update({ 100, 12 * 1024 * 1024, 99999, 88888 });
    liveProgress.update({ 1, 0, 1, 2 });
    std::string liveText = liveOutput.str();
    assert(liveText.starts_with("\relapsed=100s written=12MiB transfers=99999"));
    assert(liveText.find("\relapsed=1s written=0MiB transfers=1 ") != std::string::npos);
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

    std::ostringstream periodicOutput;
    ProgressLine periodicProgress(periodicOutput, false, false);
    auto periodicStart = std::chrono::steady_clock::now();
    periodicProgress.update({ 1, 1 * 1024 * 1024, 1, 2, "frame=12345" }, periodicStart);
    assert(periodicOutput.str() == "elapsed=1s written=1MiB transfers=1 frame=12345\n");
    periodicProgress.update({ 2, 2 * 1024 * 1024, 3, 4 }, periodicStart + std::chrono::seconds(9));
    assert(periodicOutput.str() == "elapsed=1s written=1MiB transfers=1 frame=12345\n");
    periodicProgress.update({ 11, 3 * 1024 * 1024, 5, 6 }, periodicStart + std::chrono::seconds(10));
    assert(periodicOutput.str().ends_with("elapsed=11s written=3MiB transfers=5\n"));
    std::string periodicText = periodicOutput.str();
    periodicProgress.clear();
    periodicProgress.finish();
    assert(periodicOutput.str() == periodicText);

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

    const char* flagBeforeCaptureArgv[] = {
        "dddcli",
        "--output",
        "out.lds",
        "capture",
    };
    auto flagBeforeCaptureParsed = parseCommandLine(4, const_cast<char**>(flagBeforeCaptureArgv));
    assert(flagBeforeCaptureParsed.command == "capture");
    assert(flagBeforeCaptureParsed.options.output == "out.lds");

    const char* flagBeforeAutoCaptureArgv[] = {
        "dddcli",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "auto-capture",
        "--end-address",
        "70",
    };
    auto flagBeforeAutoCaptureParsed = parseCommandLine(8, const_cast<char**>(flagBeforeAutoCaptureArgv));
    assert(flagBeforeAutoCaptureParsed.command == "auto-capture");
    assert(flagBeforeAutoCaptureParsed.options.discType == DiscTypeCli::Cav);
    assert(flagBeforeAutoCaptureParsed.options.autoCaptureMode == AutoCaptureModeCli::Partial);
    assert(flagBeforeAutoCaptureParsed.options.endAddress == 70);

    const char* captureExtraArgv[] = { "dddcli", "capture", "extra" };
    assertParseThrows(captureExtraArgv, 3);

    const char* invalidDurationArgv[] = { "dddcli", "capture", "--duration", "10oops" };
    assertParseThrows(invalidDurationArgv, 4);
    const char* zeroDurationArgv[] = { "dddcli", "capture", "--duration", "0" };
    assertParseThrows(zeroDurationArgv, 4);
    const char* negativeDurationArgv[] = { "dddcli", "capture", "--duration", "-1" };
    assertParseThrows(negativeDurationArgv, 4);
    const char* invalidDiskBufferSizeArgv[] = { "dddcli", "capture", "--disk-buffer-queue-size", "128MiBtypo" };
    assertParseThrows(invalidDiskBufferSizeArgv, 4);
    const char* zeroDiskBufferSizeArgv[] = { "dddcli", "capture", "--disk-buffer-queue-size", "0" };
    assertParseThrows(zeroDiskBufferSizeArgv, 4);
    const char* smallDiskBufferSizeArgv[] = { "dddcli", "capture", "--disk-buffer-queue-size", "1MiB" };
    assertParseThrows(smallDiskBufferSizeArgv, 4);
    const char* decimalSmallDiskBufferSizeArgv[] = { "dddcli", "capture", "--disk-buffer-queue-size", "6MB" };
    assertParseThrows(decimalSmallDiskBufferSizeArgv, 4);
    const char* minimumDiskBufferSizeArgv[] = { "dddcli", "capture", "--disk-buffer-queue-size", "6MiB" };
    auto minimumDiskBufferSizeParsed = parseCommandLine(4, const_cast<char**>(minimumDiskBufferSizeArgv));
    assert(minimumDiskBufferSizeParsed.options.diskBufferQueueSize == 6 * 1024 * 1024);

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

    const char* noOsdArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "clv",
        "--no-on-screen-display",
    };
    auto noOsdParsed = parseCommandLine(5, const_cast<char**>(noOsdArgv));
    assert(!noOsdParsed.options.onScreenDisplay);
    CliOptions disabledOsdBase;
    disabledOsdBase.onScreenDisplay = false;
    const char* forceOsdArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "clv",
        "--on-screen-display",
    };
    auto forceOsdParsed = parseCommandLine(5, const_cast<char**>(forceOsdArgv), disabledOsdBase);
    assert(forceOsdParsed.options.onScreenDisplay);

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

    const char* negativeCavStartArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "--start-address",
        "-1",
        "--end-address",
        "1301",
    };
    assertParseThrows(negativeCavStartArgv, 10, validationBase);

    const char* negativeCavEndArgv[] = {
        "dddcli",
        "auto-capture",
        "--disc-type",
        "cav",
        "--mode",
        "partial",
        "--start-address",
        "1300",
        "--end-address",
        "-1",
    };
    assertParseThrows(negativeCavEndArgv, 10, validationBase);

    const char* preliminaryPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--mode",
        "partial",
    };
    auto preliminaryPartialParsed = parseCommandLine(4, const_cast<char**>(preliminaryPartialArgv));
    assert(preliminaryPartialParsed.command == "auto-capture");
    assert(preliminaryPartialParsed.options.autoCaptureMode == AutoCaptureModeCli::Partial);

    const char* detectedClvPartialArgv[] = {
        "dddcli",
        "auto-capture",
        "--mode",
        "partial",
        "--start-address",
        "00100",
        "--end-address",
        "00130",
    };
    auto detectedClvPartialParsed = parseCommandLine(8, const_cast<char**>(detectedClvPartialArgv));
    assert(detectedClvPartialParsed.options.discType == DiscTypeCli::Unknown);
    assert(detectedClvPartialParsed.options.startAddress == 100);
    assert(detectedClvPartialParsed.options.endAddress == 130);
    applyDetectedDiscType(detectedClvPartialParsed.options, DiscTypeCli::Clv);
    assert(detectedClvPartialParsed.options.discType == DiscTypeCli::Clv);
    assert(detectedClvPartialParsed.options.startAddress == 60);
    assert(detectedClvPartialParsed.options.endAddress == 90);

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

    const char* flagBeforePlayerArgv[] = {
        "dddcli",
        "--serial-device",
        "/dev/ttyUSB0",
        "--serial-speed",
        "auto",
        "player",
        "stop",
    };
    auto flagBeforePlayerParsed = parseCommandLine(7, const_cast<char**>(flagBeforePlayerArgv));
    assert(flagBeforePlayerParsed.command == "player");
    assert(flagBeforePlayerParsed.playerAction == "stop");
    assert(flagBeforePlayerParsed.options.serialDevice == "/dev/ttyUSB0");
    assert(flagBeforePlayerParsed.options.serialSpeed == SerialSpeedCli::Auto);

    const char* flagBetweenPlayerArgv[] = {
        "dddcli",
        "player",
        "--serial-device",
        "/dev/ttyUSB0",
        "stop",
    };
    auto flagBetweenPlayerParsed = parseCommandLine(5, const_cast<char**>(flagBetweenPlayerArgv));
    assert(flagBetweenPlayerParsed.command == "player");
    assert(flagBetweenPlayerParsed.playerAction == "stop");
    assert(flagBetweenPlayerParsed.options.serialDevice == "/dev/ttyUSB0");

    const char* flagBetweenRawCommandArgv[] = {
        "dddcli",
        "player",
        "raw-command",
        "--serial-device",
        "/dev/ttyUSB0",
        "?T",
    };
    auto flagBetweenRawCommandParsed = parseCommandLine(6, const_cast<char**>(flagBetweenRawCommandArgv));
    assert(flagBetweenRawCommandParsed.command == "player");
    assert(flagBetweenRawCommandParsed.playerAction == "raw-command");
    assert(flagBetweenRawCommandParsed.playerRawCommand == "?T");
    assert(flagBetweenRawCommandParsed.options.serialDevice == "/dev/ttyUSB0");

    const char* literalRawCommandArgv[] = {
        "dddcli",
        "player",
        "raw-command",
        "--",
        "--serial-device",
    };
    auto literalRawCommandParsed = parseCommandLine(5, const_cast<char**>(literalRawCommandArgv));
    assert(literalRawCommandParsed.command == "player");
    assert(literalRawCommandParsed.playerAction == "raw-command");
    assert(literalRawCommandParsed.playerRawCommand == "--serial-device");

    const char* playerExtraArgv[] = { "dddcli", "player", "stop", "extra" };
    assertParseThrows(playerExtraArgv, 4);
    const char* rawExtraArgv[] = { "dddcli", "player", "raw-command", "?T", "extra" };
    assertParseThrows(rawExtraArgv, 5);
    const char* reorderedPositionalsArgv[] = { "dddcli", "stop", "player" };
    assertParseThrows(reorderedPositionalsArgv, 3);

    assert(playerRawCommandFits(std::string(19, 'A')));
    assert(playerRawCommandFits(std::string(19, 'A') + "\r"));
    assert(!playerRawCommandFits(std::string(20, 'A')));
    assert(!playerRawCommandFits(std::string(21, 'A') + "\r"));
    assert(playerTimeCodeSeekCommand(PlayerProfileCli::PioneerLdV2200, 60) == "TM00100SE\r");
    assert(playerTimeCodeSeekCommand(PlayerProfileCli::PioneerLdV4300D, 60) == "FR0010000SE\r");
    assert(playerTimeCodeSeekCommand(PlayerProfileCli::GenericLevel3, 60) == "FR0010000SE\r");

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
        file << "on_screen_display = false\n";
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
    assert(!options.onScreenDisplay);

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

    auto zeroDurationPath = std::filesystem::temp_directory_path() / "dddcli-zero-duration-test.toml";
    assertConfigApplyThrows(zeroDurationPath, "[capture]\nduration_seconds = 0\n");

    auto negativeDurationPath = std::filesystem::temp_directory_path() / "dddcli-negative-duration-test.toml";
    assertConfigApplyThrows(negativeDurationPath, "[capture]\nduration_seconds = -1\n");

    auto invalidSizePath = std::filesystem::temp_directory_path() / "dddcli-invalid-size-test.toml";
    assertConfigApplyThrows(invalidSizePath, "[usb]\ndisk_buffer_queue_size = \"128MiBtypo\"\n");
    auto zeroDiskBufferSizePath = std::filesystem::temp_directory_path() / "dddcli-zero-disk-buffer-size-test.toml";
    assertConfigApplyThrows(zeroDiskBufferSizePath, "[usb]\ndisk_buffer_queue_size = 0\n");
    auto smallDiskBufferSizePath = std::filesystem::temp_directory_path() / "dddcli-small-disk-buffer-size-test.toml";
    assertConfigApplyThrows(smallDiskBufferSizePath, "[usb]\ndisk_buffer_queue_size = \"1MiB\"\n");
    auto decimalSmallDiskBufferSizePath = std::filesystem::temp_directory_path() / "dddcli-decimal-small-disk-buffer-size-test.toml";
    assertConfigApplyThrows(decimalSmallDiskBufferSizePath, "[usb]\ndisk_buffer_queue_size = \"6MB\"\n");

    auto invalidClvAddressPath = std::filesystem::temp_directory_path() / "dddcli-invalid-clv-address-test.toml";
    assertConfigApplyThrows(invalidClvAddressPath, "[auto_capture]\ndisc_type = \"clv\"\nstart_address = \"123456\"\n");

    auto negativeCavStartPath = std::filesystem::temp_directory_path() / "dddcli-negative-cav-start-test.toml";
    assertConfigApplyThrows(negativeCavStartPath, "[auto_capture]\ndisc_type = \"cav\"\nstart_address = -1\n");

    auto negativeCavEndPath = std::filesystem::temp_directory_path() / "dddcli-negative-cav-end-test.toml";
    assertConfigApplyThrows(negativeCavEndPath, "[auto_capture]\ndisc_type = \"cav\"\nend_address = -1\n");

    CliOptions negativeConfiguredStart;
    negativeConfiguredStart.discType = DiscTypeCli::Cav;
    negativeConfiguredStart.startAddress = -1;
    assertParseThrows(configuredPartialArgv, 2, negativeConfiguredStart);

    auto unknownConfigKeyPath = std::filesystem::temp_directory_path() / "dddcli-unknown-key-test.toml";
    assertConfigApplyThrows(unknownConfigKeyPath, "[capture]\nduration_second = 10\n");

    assert(parseClvAddressSeconds("754") == 754);
    assert(parseClvAddressSeconds("01234") == 754);
    assert(parseClvAddressSeconds("0123400") == 754);
    assertClvAddressThrows("123456");
    assertClvAddressThrows("01234000");
    assertClvAddressThrows("999999999999999999999999");

    auto wholeDiscEnd = resolveAutoCaptureEndAddress(AutoCaptureModeCli::WholeDisc, 90, 60, 75);
    assert(wholeDiscEnd.endAddress == 75);
    assert(!wholeDiscEnd.cappedToDiscEnd);
    assert(wholeDiscEnd.usesDetectedDiscEnd);
    assert(wholeDiscEnd.validRange);
    auto leadInDefaultEnd = resolveAutoCaptureEndAddress(AutoCaptureModeCli::LeadIn, 0, 0, 75);
    assert(leadInDefaultEnd.endAddress == 75);
    assert(!leadInDefaultEnd.cappedToDiscEnd);
    assert(leadInDefaultEnd.usesDetectedDiscEnd);
    assert(leadInDefaultEnd.validRange);
    auto leadInCappedEnd = resolveAutoCaptureEndAddress(AutoCaptureModeCli::LeadIn, 90, 0, 75);
    assert(leadInCappedEnd.endAddress == 75);
    assert(leadInCappedEnd.cappedToDiscEnd);
    assert(leadInCappedEnd.usesDetectedDiscEnd);
    assert(leadInCappedEnd.validRange);
    auto partialCappedEnd = resolveAutoCaptureEndAddress(AutoCaptureModeCli::Partial, 90, 60, 75);
    assert(partialCappedEnd.endAddress == 75);
    assert(partialCappedEnd.cappedToDiscEnd);
    assert(partialCappedEnd.usesDetectedDiscEnd);
    assert(partialCappedEnd.validRange);
    auto partialRequestedEnd = resolveAutoCaptureEndAddress(AutoCaptureModeCli::Partial, 70, 60, 75);
    assert(partialRequestedEnd.endAddress == 70);
    assert(!partialRequestedEnd.cappedToDiscEnd);
    assert(!partialRequestedEnd.usesDetectedDiscEnd);
    assert(partialRequestedEnd.validRange);
    auto partialInvalidEnd = resolveAutoCaptureEndAddress(AutoCaptureModeCli::Partial, 90, 75, 75);
    assert(partialInvalidEnd.endAddress == 75);
    assert(partialInvalidEnd.cappedToDiscEnd);
    assert(partialInvalidEnd.usesDetectedDiscEnd);
    assert(!partialInvalidEnd.validRange);

    assert(clvEndAddressPostRoll(wholeDiscEnd, 75) == ClvSecondAddressPostRoll);
    auto wholeDiscMinuteEnd = resolveAutoCaptureEndAddress(AutoCaptureModeCli::WholeDisc, 0, 0, 3600);
    assert(clvEndAddressPostRoll(wholeDiscMinuteEnd, 3600) == ClvMinuteAddressPostRoll);
    assert(clvEndAddressPostRoll(partialRequestedEnd, 3600) == ClvSecondAddressPostRoll);

    assert(playerProfileForModelCode("15", PlayerProfileCli::Auto) == PlayerProfileCli::PioneerLdV4300D);
    assert(playerProfileForModelCode("07", PlayerProfileCli::Auto) == PlayerProfileCli::PioneerLdV2200);
    assert(playerProfileForModelCode("42", PlayerProfileCli::Auto) == PlayerProfileCli::GenericLevel3);
    assert(playerProfileForModelCode("07", PlayerProfileCli::PioneerLdV4300D) == PlayerProfileCli::PioneerLdV4300D);
    assert(parsePlayerPhysicalPositionResponse("0001\r") == 2.56f);
    assert(parsePlayerPhysicalPositionResponse("0010\r") == 40.96f);
    assert(parsePlayerPhysicalPositionResponse("123") == 0.0f);
    assert(parsePlayerPhysicalPositionResponse("zzzz\r") == 0.0f);
    assert(parsePlayerDiscTypeResponse("0XXXX\r") == DiscTypeCli::Unknown);
    assert(parsePlayerDiscTypeResponse("1XXXX\r") == DiscTypeCli::Unknown);
    assert(parsePlayerDiscTypeResponse("10001\r") == DiscTypeCli::Cav);
    assert(parsePlayerDiscTypeResponse("11001\r") == DiscTypeCli::Clv);
    assert(parsePlayerDiscTypeResponse("E04\r") == DiscTypeCli::Unknown);
    assert(parsePlayerDiscTypeResponse("") == DiscTypeCli::Unknown);
    assert(parsePlayerDiscTypeResponse("1\r") == DiscTypeCli::Unknown);
    assert(parsePlayerDiscTypeResponse("abcde\r") == DiscTypeCli::Unknown);

    auto timeCode5 = parsePlayerTimeCodeResponse("01234\r");
    assert(timeCode5.address == 754);
    assert(!timeCode5.inLeadIn);
    assert(!timeCode5.inLeadOut);
    auto timeCode7 = parsePlayerTimeCodeResponse(">0123400\r");
    assert(timeCode7.address == 754);
    assert(timeCode7.inLeadOut);
    auto minuteOnlyTimeCode = parsePlayerTimeCodeResponse("123\r");
    assert(minuteOnlyTimeCode.address == 4980);
    assert(parsePlayerTimeCodeResponse("199\r").address == -1);
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

    AutoCaptureStopState clvMinuteStop;
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 3600, 3600, now, clvMinuteStop, ClvMinuteAddressPostRoll));
    assert(!shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 3600, 3600, now + std::chrono::seconds(60), clvMinuteStop, ClvMinuteAddressPostRoll));
    assert(shouldStopAutoCaptureAtAddress(DiscTypeCli::Clv, 3600, 3600, now + std::chrono::seconds(61), clvMinuteStop, ClvMinuteAddressPostRoll));

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
    assert(shouldStopAutoCaptureOnClvWrap(2687, 0, 2688));
    assert(shouldStopAutoCaptureOnClvWrap(2600, 0, 2688));
    assert(!shouldStopAutoCaptureOnClvWrap(2500, 0, 2688));
    assert(!shouldStopAutoCaptureOnClvWrap(2687, 2688, 2688));
    assert(!shouldStopAutoCaptureOnClvWrap(-1, 0, 2688));

    CaptureMetadata cavMetadata;
    recordAutoCaptureAddress(cavMetadata, DiscTypeCli::Cav, -1);
    assert(!cavMetadata.minFrameNumber.has_value());
    recordAutoCaptureAddress(cavMetadata, DiscTypeCli::Cav, 1300);
    recordAutoCaptureAddress(cavMetadata, DiscTypeCli::Cav, 1000);
    assert(cavMetadata.minFrameNumber == 1000);
    assert(cavMetadata.maxFrameNumber == 1300);
    assert(!cavMetadata.minTimeCode.has_value());
    assert(!cavMetadata.maxTimeCode.has_value());

    CaptureMetadata clvMetadata;
    recordAutoCaptureAddress(clvMetadata, DiscTypeCli::Clv, 90);
    recordAutoCaptureAddress(clvMetadata, DiscTypeCli::Clv, 60);
    assert(clvMetadata.minTimeCode == 60);
    assert(clvMetadata.maxTimeCode == 90);
    assert(!clvMetadata.minFrameNumber.has_value());
    assert(!clvMetadata.maxFrameNumber.has_value());

    assert(shouldFailCavStillFrameResume(DiscTypeCli::Cav, PlayerStateCli::StillFrame, false));
    assert(!shouldFailCavStillFrameResume(DiscTypeCli::Cav, PlayerStateCli::StillFrame, true));
    assert(!shouldFailCavStillFrameResume(DiscTypeCli::Cav, PlayerStateCli::Play, false));
    assert(!shouldFailCavStillFrameResume(DiscTypeCli::Clv, PlayerStateCli::StillFrame, false));
    assert(finalPlayerActionForAutoCapture(DiscTypeCli::Cav, false) == AutoCaptureFinalPlayerAction::Stop);
    assert(finalPlayerActionForAutoCapture(DiscTypeCli::Clv, false) == AutoCaptureFinalPlayerAction::Stop);
    assert(finalPlayerActionForAutoCapture(DiscTypeCli::Cav, true) == AutoCaptureFinalPlayerAction::StillFrame);
    assert(finalPlayerActionForAutoCapture(DiscTypeCli::Clv, true) == AutoCaptureFinalPlayerAction::StillFrame);

    std::filesystem::remove(path);
    std::filesystem::remove(quotedHashPath);
    std::filesystem::remove(quotedHashCommentPath);
    std::filesystem::remove(invalidUsbIdPath);
    return 0;
}
