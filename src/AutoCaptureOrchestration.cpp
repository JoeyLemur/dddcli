// SPDX-FileCopyrightText: Copyright (C) 2026 Ed Powell
// SPDX-License-Identifier: GPL-3.0-only

#include "AutoCaptureOrchestration.h"
#include <algorithm>

void recordAutoCaptureAddress(CaptureMetadata& metadata, DiscTypeCli discType, int address)
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

std::chrono::seconds cavEndProbeRolloverTimeout(int nearEndFloor)
{
    constexpr int cavEndProbeTargetFrame = 60000;
    constexpr int cavFramesPerSecond = 30;
    constexpr auto minimumTimeout = std::chrono::seconds(31);
    if (nearEndFloor < 0 || nearEndFloor >= cavEndProbeTargetFrame)
    {
        return minimumTimeout;
    }

    int remainingFrames = cavEndProbeTargetFrame - nearEndFloor;
    int timeoutSeconds = (remainingFrames + cavFramesPerSecond - 1) / cavFramesPerSecond;
    return std::max(minimumTimeout, std::chrono::seconds(timeoutSeconds + 1));
}

bool shouldStopCavOnWrap(int previousFrame, int currentFrame, int nearEndFloor)
{
    constexpr int minimumCavWrapFrameDrop = 1000;
    return previousFrame >= nearEndFloor &&
        currentFrame >= 0 &&
        nearEndFloor >= 0 &&
        previousFrame - currentFrame > minimumCavWrapFrameDrop;
}

std::string describeLastObservedAutoCaptureAddress(DiscTypeCli discType, int address)
{
    if (address < 0)
    {
        return {};
    }
    if (discType == DiscTypeCli::Cav)
    {
        return "Last observed CAV frame number: " + std::to_string(address);
    }
    if (discType == DiscTypeCli::Clv)
    {
        return "Last observed CLV time code: " + formatClvTimeCode(address);
    }
    return {};
}

bool shouldFailCavStillFrameResume(DiscTypeCli discType, PlayerStateCli playerState, bool resumeSucceeded)
{
    return discType == DiscTypeCli::Cav && playerState == PlayerStateCli::StillFrame && !resumeSucceeded;
}

AutoCaptureFinalPlayerAction finalPlayerActionForAutoCapture(DiscTypeCli discType, bool autoCaptureError)
{
    if (autoCaptureError)
    {
        return AutoCaptureFinalPlayerAction::StillFrame;
    }
    return AutoCaptureFinalPlayerAction::Stop;
}
