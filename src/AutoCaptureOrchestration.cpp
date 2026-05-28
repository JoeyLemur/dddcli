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
