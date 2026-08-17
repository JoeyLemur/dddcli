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

std::optional<std::chrono::seconds> autoCaptureSafetyLimit(DiscTypeCli discType)
{
    if (discType == DiscTypeCli::Cav)
    {
        return std::chrono::minutes(32);
    }
    if (discType == DiscTypeCli::Clv)
    {
        return std::chrono::minutes(62);
    }
    return std::nullopt;
}

std::optional<std::chrono::seconds> autoCapturePositionStallTimeout(DiscTypeCli discType)
{
    if (discType == DiscTypeCli::Cav)
    {
        return std::chrono::seconds(10);
    }
    if (discType == DiscTypeCli::Clv)
    {
        return std::chrono::seconds(75);
    }
    return std::nullopt;
}

bool hasAutoCapturePositionStalled(
    DiscTypeCli discType,
    int address,
    const std::chrono::steady_clock::time_point& now,
    AutoCapturePositionWatchdogState& state)
{
    if (address < 0)
    {
        return false;
    }

    auto stallTimeout = autoCapturePositionStallTimeout(discType);
    if (!stallTimeout.has_value())
    {
        return false;
    }

    if (address > state.highestAddress)
    {
        state.highestAddress = address;
        state.lastProgressTime = now;
        return false;
    }

    return state.highestAddress >= 0 && now - state.lastProgressTime >= stallTimeout.value();
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

bool hasCavFrameWrapped(int previousFrame, int currentFrame)
{
    constexpr int minimumCavWrapFrameDrop = 1000;
    return previousFrame >= 0 &&
        currentFrame >= 0 &&
        previousFrame - currentFrame > minimumCavWrapFrameDrop;
}

bool shouldStopCavOnWrap(int previousFrame, int currentFrame, int nearEndFloor)
{
    return previousFrame >= nearEndFloor &&
        nearEndFloor >= 0 &&
        hasCavFrameWrapped(previousFrame, currentFrame);
}

bool hasConfirmedClvEndProbeFloor(PlayerStateCli playerState, int previousTimeCode, int currentTimeCode)
{
    bool terminalState = playerState == PlayerStateCli::Stop ||
        playerState == PlayerStateCli::Pause ||
        playerState == PlayerStateCli::StillFrame;
    return terminalState && previousTimeCode >= 0 && previousTimeCode == currentTimeCode;
}

ClvEndProbeCompletion confirmClvEndProbeCompletion(
    PlayerStateCli playerState,
    int previousTimeCode,
    int currentTimeCode,
    int nearEndFloor,
    bool advancedPastNearEndFloor)
{
    if (previousTimeCode >= nearEndFloor &&
        currentTimeCode >= 0 &&
        nearEndFloor >= 0 &&
        currentTimeCode < previousTimeCode)
    {
        return ClvEndProbeCompletion::Wrapped;
    }

    bool terminalState = playerState == PlayerStateCli::Stop ||
        playerState == PlayerStateCli::Pause ||
        playerState == PlayerStateCli::StillFrame;
    return terminalState && advancedPastNearEndFloor
        ? ClvEndProbeCompletion::TerminalState
        : ClvEndProbeCompletion::None;
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
