#include "CliConfig.h"
#include "PlayerSerial.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

int main()
{
    assert(captureFormatExtension(CaptureFormatCli::Lds) == ".lds");
    assert(captureFormatExtension(CaptureFormatCli::Raw) == ".raw");
    assert(captureFormatExtension(CaptureFormatCli::Cds) == ".cds");

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

    assert(parseClvAddressSeconds("754") == 754);
    assert(parseClvAddressSeconds("01234") == 754);
    assert(parseClvAddressSeconds("0123400") == 754);
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
    return 0;
}
