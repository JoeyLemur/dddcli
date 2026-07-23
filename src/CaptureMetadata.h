// SPDX-FileCopyrightText: Copyright (C) 2026 Ed Powell
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CliConfig.h"
#include "PlayerSerial.h"
#include "UsbDeviceBase.h"
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

struct CaptureMetadata
{
    std::filesystem::path captureFilePath;
    CaptureFormatCli captureFormat = CaptureFormatCli::Lds;
    bool testMode = false;
    std::chrono::system_clock::time_point creationTimeUtc;
    std::chrono::milliseconds duration{0};

    std::string playerModelCode;
    std::string playerModelName;
    std::string playerVersionNumber;
    std::string serialSpeed;
    std::string discType;
    std::string discStatus;
    std::string discStandardUserCode;
    std::string discPioneerUserCode;
    std::optional<int> minFrameNumber;
    std::optional<int> maxFrameNumber;
    std::optional<int> minTimeCode;
    std::optional<int> maxTimeCode;
};

bool writeCaptureMetadata(
    const std::filesystem::path& jsonPath,
    const CaptureMetadata& metadata,
    const UsbDeviceBase& usb,
    std::string& error);

