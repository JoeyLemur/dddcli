#include "PlayerSerial.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace
{
constexpr int NormalTimeoutMs = 5 * 1000;
constexpr int LongTimeoutMs = 30 * 1000;

speed_t toNativeSpeed(SerialSpeedCli speed)
{
    switch (speed)
    {
    case SerialSpeedCli::Bps9600: return B9600;
    case SerialSpeedCli::Bps4800: return B4800;
    case SerialSpeedCli::Bps2400: return B2400;
    case SerialSpeedCli::Bps1200: return B1200;
    case SerialSpeedCli::Auto: return B9600;
    }
    return B9600;
}

std::vector<SerialSpeedCli> speedsToTry(SerialSpeedCli requested)
{
    if (requested != SerialSpeedCli::Auto)
    {
        return { requested };
    }
    return { SerialSpeedCli::Bps9600, SerialSpeedCli::Bps4800, SerialSpeedCli::Bps2400, SerialSpeedCli::Bps1200 };
}

std::string stripTrailingCr(std::string value)
{
    if (!value.empty() && value.back() == '\r')
    {
        value.pop_back();
    }
    return value;
}

bool containsError(const std::string& response)
{
    return response.find('E') != std::string::npos;
}
}

PlayerSerial::PlayerSerial() = default;

PlayerSerial::~PlayerSerial()
{
    disconnect();
}

bool PlayerSerial::connect(const std::string& serialDevice, SerialSpeedCli speed, std::string& error)
{
    disconnect();
    if (serialDevice.empty())
    {
        error = "serial device is required";
        return false;
    }

    for (auto candidateSpeed : speedsToTry(speed))
    {
        if (!tryOpen(serialDevice, candidateSpeed, error))
        {
            continue;
        }

        int attempts = speed == SerialSpeedCli::Auto ? 3 : 1;
        int timeout = speed == SerialSpeedCli::Auto ? 250 : 2500;
        for (int i = 0; i < attempts; ++i)
        {
            auto response = commandResponse("?X\r", timeout);
            if (response.starts_with("P15") && response.size() >= 5)
            {
                currentModelCode = response.substr(0, std::min<size_t>(7, response.size()));
                std::string playerCode = response.substr(3, 2);
                currentVersionNumber = response.substr(5, 2);
                currentModelName = playerCodeToName(playerCode);
                currentSpeed = candidateSpeed;
                physicalPositionSupported = playerCode == "06" && currentVersionNumber == "A9";
                return true;
            }
        }
        closePort();
    }

    if (error.empty())
    {
        error = "could not connect to player";
    }
    return false;
}

void PlayerSerial::disconnect()
{
    closePort();
    currentModelCode.clear();
    currentModelName.clear();
    currentVersionNumber.clear();
    physicalPositionSupported = false;
    currentSpeed = SerialSpeedCli::Auto;
}

bool PlayerSerial::connected() const
{
    return fd >= 0;
}

std::string PlayerSerial::modelCode() const { return currentModelCode; }
std::string PlayerSerial::modelName() const { return currentModelName; }
std::string PlayerSerial::versionNumber() const { return currentVersionNumber; }
SerialSpeedCli PlayerSerial::detectedSpeed() const { return currentSpeed; }
bool PlayerSerial::supportsPhysicalPosition() const { return physicalPositionSupported; }

PlayerStateCli PlayerSerial::getPlayerState()
{
    auto response = commandResponse("?P\r", NormalTimeoutMs);
    if (response.find("P00") != std::string::npos) return PlayerStateCli::Stop;
    if (response.find("P01") != std::string::npos) return PlayerStateCli::Stop;
    if (response.find("P02") != std::string::npos) return PlayerStateCli::Play;
    if (response.find("P03") != std::string::npos) return PlayerStateCli::Stop;
    if (response.find("P04") != std::string::npos || response.find("P42") != std::string::npos) return PlayerStateCli::Play;
    if (response.find("P05") != std::string::npos) return PlayerStateCli::StillFrame;
    if (response.find("P06") != std::string::npos) return PlayerStateCli::Pause;
    if (response.find("P07") != std::string::npos) return PlayerStateCli::Play;
    if (response.find("P08") != std::string::npos) return PlayerStateCli::Play;
    if (response.find("P09") != std::string::npos) return PlayerStateCli::Play;
    if (response.find("PA5") != std::string::npos) return PlayerStateCli::Play;
    return PlayerStateCli::Unknown;
}

DiscTypeCli PlayerSerial::getDiscType()
{
    auto response = commandResponse("?D\r", NormalTimeoutMs);
    if (response.size() >= 2)
    {
        if (response[1] == '0') return DiscTypeCli::Cav;
        if (response[1] == '1') return DiscTypeCli::Clv;
    }
    return DiscTypeCli::Unknown;
}

std::string PlayerSerial::getDiscStatus()
{
    return stripTrailingCr(commandResponse("?D\r", NormalTimeoutMs));
}

std::string PlayerSerial::getStandardUserCode()
{
    return stripTrailingCr(commandResponse("$Y\r", NormalTimeoutMs));
}

std::string PlayerSerial::getPioneerUserCode()
{
    return stripTrailingCr(commandResponse("?U\r", NormalTimeoutMs));
}

AddressResult PlayerSerial::getCurrentFrame()
{
    auto response = commandResponse("?F\r", NormalTimeoutMs);
    AddressResult result;
    if (response.starts_with("<"))
    {
        result.inLeadIn = true;
        response.erase(response.begin());
    }
    else if (response.starts_with(">"))
    {
        result.inLeadOut = true;
        response.erase(response.begin());
    }
    if (!response.empty())
    {
        result.address = std::stoi(response.substr(0, std::min<size_t>(5, response.size())));
    }
    return result;
}

AddressResult PlayerSerial::getCurrentTimeCode()
{
    auto response = commandResponse("?F\r", NormalTimeoutMs);
    AddressResult result;
    if (response.starts_with("<"))
    {
        result.inLeadIn = true;
        response.erase(response.begin());
    }
    else if (response.starts_with(">"))
    {
        result.inLeadOut = true;
        response.erase(response.begin());
    }
    if (!response.empty())
    {
        result.address = std::stoi(response.substr(0, std::min<size_t>(7, response.size())));
    }
    return result;
}

float PlayerSerial::getPhysicalPosition()
{
    auto response = commandResponse("2962MQ\r", NormalTimeoutMs);
    if (response.size() < 4)
    {
        return 0;
    }
    unsigned long value = std::stoul(response, nullptr, 16);
    value = ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
    return (float)value / 100.0f;
}

bool PlayerSerial::setPlayerState(PlayerStateCli state)
{
    std::string response;
    switch (state)
    {
    case PlayerStateCli::Pause: response = commandResponse("PA\r", NormalTimeoutMs); break;
    case PlayerStateCli::Play: response = commandResponse("PL\r", LongTimeoutMs); break;
    case PlayerStateCli::PlayWithStopCodesDisabled: response = commandResponse("PL64RBMF\r", LongTimeoutMs); break;
    case PlayerStateCli::StillFrame: response = commandResponse("ST\r", NormalTimeoutMs); break;
    case PlayerStateCli::Stop: response = commandResponse("RJ\r", LongTimeoutMs); break;
    case PlayerStateCli::Unknown: return false;
    }
    return responseOk(response);
}

bool PlayerSerial::setKeyLock(bool locked)
{
    return responseOk(commandResponse(locked ? "1KL\r" : "0KL\r", NormalTimeoutMs));
}

bool PlayerSerial::setPositionFrame(int address)
{
    return responseOk(commandResponse("FR" + std::to_string(address) + "SE\r", LongTimeoutMs));
}

bool PlayerSerial::setPositionTimeCode(int address)
{
    return responseOk(commandResponse("FR" + std::to_string(address) + "SE\r", LongTimeoutMs));
}

bool PlayerSerial::tryOpen(const std::string& serialDevice, SerialSpeedCli speed, std::string& error)
{
    fd = open(serialDevice.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0)
    {
        error = "failed to open serial device " + serialDevice + ": " + std::strerror(errno);
        return false;
    }
    if (!configurePort(speed, error))
    {
        closePort();
        return false;
    }
    return true;
}

void PlayerSerial::closePort()
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

bool PlayerSerial::configurePort(SerialSpeedCli speed, std::string& error)
{
    termios tty{};
    if (tcgetattr(fd, &tty) != 0)
    {
        error = "tcgetattr failed: " + std::string(std::strerror(errno));
        return false;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, toNativeSpeed(speed));
    cfsetospeed(&tty, toNativeSpeed(speed));
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        error = "tcsetattr failed: " + std::string(std::strerror(errno));
        return false;
    }
    tcflush(fd, TCIOFLUSH);
    return true;
}

bool PlayerSerial::sendCommand(const std::string& command)
{
    auto limited = command.substr(0, 20);
    return write(fd, limited.data(), limited.size()) == (ssize_t)limited.size();
}

std::string PlayerSerial::readResponse(int timeoutMilliseconds, int expectedResponseCount)
{
    std::string response;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);

    while (std::chrono::steady_clock::now() < deadline)
    {
        int remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        int pollResult = poll(&pfd, 1, std::min(remaining, 100));
        if (pollResult > 0 && (pfd.revents & POLLIN))
        {
            char buffer[256];
            ssize_t readCount = read(fd, buffer, sizeof(buffer));
            if (readCount > 0)
            {
                response.append(buffer, (size_t)readCount);
                if ((int)std::count(response.begin(), response.end(), '\r') >= expectedResponseCount)
                {
                    return response;
                }
            }
        }
    }
    return "";
}

bool PlayerSerial::responseOk(const std::string& response) const
{
    return !response.empty() && !containsError(response);
}

std::string PlayerSerial::commandResponse(const std::string& command, int timeoutMilliseconds, int expectedResponseCount)
{
    if (!connected() || !sendCommand(command))
    {
        return "";
    }
    return readResponse(timeoutMilliseconds, expectedResponseCount);
}

std::string PlayerSerial::playerCodeToName(const std::string& playerCode) const
{
    if (playerCode == "42") return "Pioneer CLD-V5000";
    if (playerCode == "37") return "Pioneer CLD-V2800";
    if (playerCode == "27") return "Pioneer CLD-V2600";
    if (playerCode == "18") return "Pioneer CLD-V2400";
    if (playerCode == "16") return "Pioneer LD-V4400";
    if (playerCode == "15") return "Pioneer LD-V4300D";
    if (playerCode == "07") return "Pioneer LD-V2200";
    if (playerCode == "06") return "Pioneer LD-V8000";
    if (playerCode == "05") return "Pioneer VC-V330";
    if (playerCode == "02") return "Pioneer LD-V4200";
    return "Unknown Player [" + playerCode + "]";
}

std::string playerStateToString(PlayerStateCli state)
{
    switch (state)
    {
    case PlayerStateCli::Unknown: return "unknown";
    case PlayerStateCli::Stop: return "stop";
    case PlayerStateCli::Play: return "play";
    case PlayerStateCli::PlayWithStopCodesDisabled: return "play-with-stop-codes-disabled";
    case PlayerStateCli::Pause: return "pause";
    case PlayerStateCli::StillFrame: return "still-frame";
    }
    return "unknown";
}

std::string discTypeToString(DiscTypeCli discType)
{
    switch (discType)
    {
    case DiscTypeCli::Unknown: return "unknown";
    case DiscTypeCli::Cav: return "CAV";
    case DiscTypeCli::Clv: return "CLV";
    }
    return "unknown";
}

std::string serialSpeedToString(SerialSpeedCli speed)
{
    switch (speed)
    {
    case SerialSpeedCli::Auto: return "auto";
    case SerialSpeedCli::Bps9600: return "9600";
    case SerialSpeedCli::Bps4800: return "4800";
    case SerialSpeedCli::Bps2400: return "2400";
    case SerialSpeedCli::Bps1200: return "1200";
    }
    return "auto";
}
