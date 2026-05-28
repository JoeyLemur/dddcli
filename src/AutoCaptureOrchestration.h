// SPDX-FileCopyrightText: Copyright (C) 2026 Ed Powell
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "CaptureMetadata.h"

enum class AutoCaptureFinalPlayerAction
{
    StillFrame,
    Stop,
    Pause,
};

void recordAutoCaptureAddress(CaptureMetadata& metadata, DiscTypeCli discType, int address);
bool shouldFailCavStillFrameResume(DiscTypeCli discType, PlayerStateCli playerState, bool resumeSucceeded);
AutoCaptureFinalPlayerAction finalPlayerActionForAutoCapture(DiscTypeCli discType, bool autoCaptureError);
