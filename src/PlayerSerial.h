#pragma once

#include "CliConfig.h"
#include <optional>
#include <string>

enum class PlayerStateCli
{
    Unknown,
    Stop,
    Play,
    PlayWithStopCodesDisabled,
    Pause,
    StillFrame,
};

struct AddressResult
{
    int address = -1;
    bool inLeadIn = false;
    bool inLeadOut = false;
};

class PlayerSerial
{
public:
    PlayerSerial();
    ~PlayerSerial();

    bool connect(const std::string& serialDevice, SerialSpeedCli speed, std::string& error);
    void disconnect();
    bool connected() const;

    std::string modelCode() const;
    std::string modelName() const;
    std::string versionNumber() const;
    SerialSpeedCli detectedSpeed() const;

    PlayerStateCli getPlayerState();
    DiscTypeCli getDiscType();
    std::string getDiscStatus();
    std::string getStandardUserCode();
    std::string getPioneerUserCode();
    AddressResult getCurrentFrame();
    AddressResult getCurrentTimeCode();
    bool supportsPhysicalPosition() const;
    float getPhysicalPosition();

    bool setPlayerState(PlayerStateCli state);
    bool setKeyLock(bool locked);
    bool setPositionFrame(int address);
    bool setPositionTimeCode(int address);

private:
    bool tryOpen(const std::string& serialDevice, SerialSpeedCli speed, std::string& error);
    void closePort();
    bool configurePort(SerialSpeedCli speed, std::string& error);
    bool sendCommand(const std::string& command);
    std::string readResponse(int timeoutMilliseconds, int expectedResponseCount = 1);
    bool responseOk(const std::string& response) const;
    std::string commandResponse(const std::string& command, int timeoutMilliseconds, int expectedResponseCount = 1);
    std::string playerCodeToName(const std::string& playerCode) const;

    int fd = -1;
    SerialSpeedCli currentSpeed = SerialSpeedCli::Auto;
    std::string currentModelCode;
    std::string currentModelName;
    std::string currentVersionNumber;
    bool physicalPositionSupported = false;
};

std::string playerStateToString(PlayerStateCli state);
std::string discTypeToString(DiscTypeCli discType);
std::string serialSpeedToString(SerialSpeedCli speed);

