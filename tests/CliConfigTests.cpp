#include "CliConfig.h"
#include <cassert>
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

    std::filesystem::remove(path);
    return 0;
}
