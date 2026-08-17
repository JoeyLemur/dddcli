// SPDX-FileCopyrightText: Copyright (C) 2026 Ed Powell
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CaptureMetadata.h"
#include "CliConfig.h"
#include <chrono>
#include <optional>

enum class AutoCaptureFinalPlayerAction
{
    StillFrame,
    Stop,
    Pause,
};

enum class ClvEndProbeCompletion
{
    None,
    Wrapped,
    TerminalState,
};

struct AutoCapturePositionWatchdogState
{
    int highestAddress = -1;
    std::chrono::steady_clock::time_point lastProgressTime{};
};

void recordAutoCaptureAddress(CaptureMetadata& metadata, DiscTypeCli discType, int address);
std::optional<std::chrono::seconds> autoCaptureSafetyLimit(DiscTypeCli discType);
std::optional<std::chrono::seconds> autoCapturePositionStallTimeout(DiscTypeCli discType);
bool hasAutoCapturePositionStalled(
    DiscTypeCli discType,
    int address,
    const std::chrono::steady_clock::time_point& now,
    AutoCapturePositionWatchdogState& state);
std::chrono::seconds cavEndProbeRolloverTimeout(int nearEndFloor);
bool hasCavFrameWrapped(int previousFrame, int currentFrame);
bool shouldStopCavOnWrap(int previousFrame, int currentFrame, int nearEndFloor);
bool hasConfirmedClvEndProbeFloor(PlayerStateCli playerState, int previousTimeCode, int currentTimeCode);
ClvEndProbeCompletion confirmClvEndProbeCompletion(
    PlayerStateCli playerState,
    int previousTimeCode,
    int currentTimeCode,
    int nearEndFloor,
    bool advancedPastNearEndFloor);
std::string describeLastObservedAutoCaptureAddress(DiscTypeCli discType, int address);
bool shouldFailCavStillFrameResume(DiscTypeCli discType, PlayerStateCli playerState, bool resumeSucceeded);
AutoCaptureFinalPlayerAction finalPlayerActionForAutoCapture(DiscTypeCli discType, bool autoCaptureError);
