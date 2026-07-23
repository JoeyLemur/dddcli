// SPDX-FileCopyrightText: Copyright (C) 2026 Ed Powell
// SPDX-License-Identifier: GPL-3.0-only

#include "UsbDeviceBase.h"
#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <algorithm>
#include <iostream>
#include <thread>
#include <functional>
#include <cassert>

//----------------------------------------------------------------------------------------------------------------------
// Constructors
//----------------------------------------------------------------------------------------------------------------------
UsbDeviceBase::UsbDeviceBase(const ILogger& log)
:log(log)
{ }

//----------------------------------------------------------------------------------------------------------------------
UsbDeviceBase::~UsbDeviceBase()
{ }

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::Initialize(uint16_t vendorId, uint16_t productId)
{
    (void)vendorId;
    (void)productId;
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
// Log methods
//----------------------------------------------------------------------------------------------------------------------
const ILogger& UsbDeviceBase::Log() const
{
    return log;
}

//----------------------------------------------------------------------------------------------------------------------
// Device methods
//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::SendConfigurationCommand(const std::string& preferredDevicePath, bool testMode)
{
    // Bit 0: Set test mode
    // Bit 1: Unused
    // Bit 2: Unused
    // Bit 3: Unused
    // Bit 4: Unused
    uint16_t configurationFlags = 0b00000;
    configurationFlags |= (testMode ? 0b00001 : 0b00000);
    SendVendorSpecificCommand(preferredDevicePath, 0xB6, configurationFlags);
}

//----------------------------------------------------------------------------------------------------------------------
// Capture methods
//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::StartCapture(const std::filesystem::path& filePath, CaptureFormat format, const std::string& preferredDevicePath, bool isTestMode, bool useSmallUsbTransfers, size_t usbTransferQueueSizeInBytes, size_t diskBufferQueueSizeInBytes)
{
    // If we're already performing a capture, abort any further processing.
    if (transferInProgress)
    {
        Log().Error("StartCapture(): Capture was currently in progress");
        return false;
    }

    // Attempt to connect to the target device
    if (!ConnectToDevice(preferredDevicePath))
    {
        Log().Error("StartCapture(): Failed to connect to the target device");
        captureResult = TransferResult::ConnectionFailure;
        return false;
    }

    // Calculate the device-aligned buffer layout before allocating buffers or creating output. The USB transfer
    // pipeline requires one buffer for processing plus at least two in-flight buffers.
    CalculateDesiredBufferCountAndSize(useSmallUsbTransfers, usbTransferQueueSizeInBytes, diskBufferQueueSizeInBytes, totalDiskBufferEntryCount, diskBufferSizeInBytes);
    constexpr size_t minimumDiskBufferCount = 3;
    if (diskBufferSizeInBytes == 0 || totalDiskBufferEntryCount < minimumDiskBufferCount)
    {
        Log().Error("StartCapture(): Disk buffer queue is too small for the USB transfer pipeline");
        captureResult = TransferResult::ProgramError;
        DisconnectFromDevice();
        return false;
    }

    // Attempt to create/open the output file
    captureOutputFile.clear();
    captureOutputFile.rdbuf()->pubsetbuf(0, 0);
    captureOutputFile.open(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!captureOutputFile.is_open())
    {
        Log().Error("StartCapture(): Failed to create the output file at path {0}", filePath);
        captureResult = TransferResult::FileCreationError;
        return false;
    }

    // Initialize the device-aligned disk buffers. We use an unusual case of wrapping an array new into a unique_ptr
    // rather than std::vector here, as we have an atomic_flag member in the structure which can't be moved.
    diskBufferEntries.reset(new DiskBufferEntry[totalDiskBufferEntryCount]);
    for (size_t i = 0; i < totalDiskBufferEntryCount; ++i)
    {
        DiskBufferEntry& entry = diskBufferEntries[i];
        entry.readBuffer.resize(diskBufferSizeInBytes);
        entry.isDiskBufferFull.clear();
    }

    // Record the capture settings
    captureFilePath = filePath;
    captureFormat = format;
    captureIsTestMode = isTestMode;
    currentUsbTransferQueueSizeInBytes = usbTransferQueueSizeInBytes;
    currentUseSmallUsbTransfers = useSmallUsbTransfers;
    memoryLockingEnabled = true;

    // Initialize capture status
    transferInProgress = true;
    captureResult = TransferResult::Running;
    transferCount = 0;
    transferBufferWrittenCount = 0;
    transferFileSizeWrittenInBytes = 0;
    processedSampleCount = 0;
    minSampleValue = std::numeric_limits<decltype(minSampleValue.load())>::max();
    maxSampleValue = 0;
    clippedMinSampleCount = 0;
    clippedMaxSampleCount = 0;
    recentMinSampleValue = std::numeric_limits<decltype(recentMinSampleValue.load())>::max();
    recentMaxSampleValue = 0;
    recentClippedMinSampleCount = 0;
    recentClippedMaxSampleCount = 0;
    captureThreadStopRequested.clear();
    captureThreadRunning.test_and_set();
    captureThreadRunning.notify_all();

    // Initialize our sequence/test data check state
    sequenceState = SequenceState::Sync;
    savedSequenceCounter = 0;
    expectedNextTestDataValue.reset();
    testDataMax.reset();

    // Spin up a thread to handle the execution of the capture process from here on
    std::thread captureThread(std::bind(std::mem_fn(&UsbDeviceBase::CaptureThread), this));
    captureThread.detach();
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::StopCapture()
{
    // If a transfer isn't currently in progress, abort any further processing.
    if (!transferInProgress)
    {
        Log().Error("StopCapture(): No capture was currently in progress");
        return;
    }

    // Instruct the capture thread to terminate, and wait for confirmation that it has stopped.
    captureThreadStopRequested.test_and_set();
    captureThreadStopRequested.notify_all();
    captureThreadRunning.wait(true);

    // Release our memory holding the disk buffers
    diskBufferEntries.reset();

    // Close the output file
    captureOutputFile.close();

    // Disconnect from the target device
    DisconnectFromDevice();

    // Record that the capture process has completed
    Log().Info("StopCapture(): Ended capture process");
    transferInProgress = false;
}

//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::CaptureThread()
{
    // Determine how large our conversion buffers need to be based on the disk buffer size and the capture format
    size_t requiredConversionBufferSize = 0;
    switch (captureFormat)
    {
    case CaptureFormat::Signed16Bit:
        requiredConversionBufferSize = diskBufferSizeInBytes;
        break;
    case CaptureFormat::Unsigned10Bit:
        requiredConversionBufferSize = (diskBufferSizeInBytes / 8) * 5;
        break;
    case CaptureFormat::Unsigned10Bit4to1Decimation:
        requiredConversionBufferSize = (diskBufferSizeInBytes / (8 * 4)) * 5;
        break;
    }

    // Allocate our conversion buffers
    for (size_t i = 0; i < conversionBufferCount; ++i)
    {
        conversionBuffers[i].resize(requiredConversionBufferSize);
    }

    size_t requiredLockSizeInBytes =
        (requiredConversionBufferSize * conversionBufferCount) +
        (diskBufferSizeInBytes * totalDiskBufferEntryCount) +
        (sizeof(diskBufferEntries[0]) * totalDiskBufferEntryCount) +
        sizeof(*this);
    memoryLockingEnabled = CheckMemoryLockLimit(requiredLockSizeInBytes);

    // Lock all the critical structures into physical memory. This stops these buffers getting paged out, which could
    // cause page faults and lead to missed data packets.
    for (size_t i = 0; i < conversionBufferCount; ++i)
    {
        LockMemoryBufferIntoPhysicalMemory(conversionBuffers[i].data(), conversionBuffers[i].size());
    }
    for (size_t i = 0; i < totalDiskBufferEntryCount; ++i)
    {
        DiskBufferEntry& entry = diskBufferEntries[i];
        LockMemoryBufferIntoPhysicalMemory(entry.readBuffer.data(), entry.readBuffer.size());
    }
    LockMemoryBufferIntoPhysicalMemory(diskBufferEntries.get(), sizeof(diskBufferEntries[0]) * totalDiskBufferEntryCount);
    LockMemoryBufferIntoPhysicalMemory(this, sizeof(*this));

    // Record that a capture process is starting
    Log().Info("CaptureThread(): Starting capture process");

    usbTransferRunning.test_and_set();
    processingRunning.test_and_set();
    usbTransferStopRequested.clear();
    processingStopRequested.clear();
    dumpAllCaptureDataInProgress.clear();
    usbTransferResult = TransferResult::Running;
    processingResult = TransferResult::Running;

    // Start a worker thread to process data after it's read
    std::thread processingThread(std::bind(std::mem_fn(&UsbDeviceBase::ProcessingThread), this));

    // Start a worker thread to transfer data from the USB device
    std::thread usbTransferThread(std::bind(std::mem_fn(&UsbDeviceBase::UsbTransferThread), this));

    // Run transfer continously until we're signalled to stop for some reason
    captureThreadStopRequested.wait(false);

    // Wind up the capture process, latching the appropriate result if an error has occurred.
    TransferResult result = TransferResult::ProgramError;
    bool errorCodeLatched = false;
    bool usbTransferFailed = false;
    bool usbTransferResultChecked = false;
    bool processingFailed = false;
    bool processingResultChecked = false;
    while (usbTransferRunning.test() || processingRunning.test())
    {
        // Check for a USB transfer failure
        if (!usbTransferResultChecked && !usbTransferRunning.test())
        {
            auto transferResultTemp = usbTransferResult.load();
            if (transferResultTemp == TransferResult::Running)
            {
                if (!errorCodeLatched)
                {
                    result = TransferResult::ProgramError;
                    errorCodeLatched = true;
                }
                usbTransferFailed = true;
            }
            else if (transferResultTemp != TransferResult::Success)
            {
                if (!errorCodeLatched)
                {
                    result = transferResultTemp;
                    errorCodeLatched = true;
                }
                usbTransferFailed = true;
            }
            usbTransferResultChecked = true;
        }

        // Check for a data processing failure
        if (!processingResultChecked && !processingRunning.test())
        {
            auto transferResultTemp = processingResult.load();
            if (transferResultTemp == TransferResult::Running)
            {
                if (!errorCodeLatched)
                {
                    result = TransferResult::ProgramError;
                    errorCodeLatched = true;
                }
                processingFailed = true;
            }
            else if (transferResultTemp != TransferResult::Success)
            {
                if (!errorCodeLatched)
                {
                    result = transferResultTemp;
                    errorCodeLatched = true;
                }
                processingFailed = true;
            }
            processingResultChecked = true;
        }

        // Request the USB transfer child worker thread to stop. If no errors have occurred or occur during the final
        // stages, this should cause it to emit all remaining queued transfers and stop gracefully.
        usbTransferStopRequested.test_and_set();
        usbTransferStopRequested.notify_all();

        // Request the processing child worker thread to stop if the USB transfer worker thread has completed, or if an
        // error has occurred. This should cause it to process all queued disk buffers and stop gracefully. We pad out
        // any empty disk buffers as being dumped, and signal their completion, to unblock the worker thread in case
        // it's currently waiting on the next buffer.
        if (!usbTransferRunning.test() || usbTransferFailed || processingFailed)
        {
            processingStopRequested.test_and_set();
            processingStopRequested.notify_all();
            for (size_t i = 0; i < totalDiskBufferEntryCount; ++i)
            {
                DiskBufferEntry& entry = diskBufferEntries[i];
                if (!entry.isDiskBufferFull.test())
                {
                    entry.dumpingBuffer.test_and_set();
                    entry.isDiskBufferFull.test_and_set();
                    entry.isDiskBufferFull.notify_all();
                }
            }
        }

        // If an error occurred, force our worker threads to unblock and dump any data currently in the buffers.
        if (usbTransferFailed || processingFailed)
        {
            dumpAllCaptureDataInProgress.test_and_set();
            for (size_t i = 0; i < totalDiskBufferEntryCount; ++i)
            {
                DiskBufferEntry& entry = diskBufferEntries[i];
                entry.isDiskBufferFull.clear();
                entry.isDiskBufferFull.notify_all();
            }
            for (size_t i = 0; i < totalDiskBufferEntryCount; ++i)
            {
                DiskBufferEntry& entry = diskBufferEntries[i];
                entry.isDiskBufferFull.test_and_set();
                entry.isDiskBufferFull.notify_all();
            }
        }

        // Yield the remainder of the timeslice, to help keep CPU resources free while we try and spin down.
        sched_yield();
    }

    // Wait for our spawned threads to terminate
    usbTransferThread.join();
    processingThread.join();

    // Set the result of this transfer process
    if (!errorCodeLatched)
    {
        result = TransferResult::Success;
    }
    captureResult = result;

    // Release all our memory buffer locks
    for (size_t i = 0; i < conversionBufferCount; ++i)
    {
        UnlockMemoryBuffer(conversionBuffers[i].data(), conversionBuffers[i].size());
    }
    for (size_t i = 0; i < totalDiskBufferEntryCount; ++i)
    {
        DiskBufferEntry& entry = diskBufferEntries[i];
        UnlockMemoryBuffer(entry.readBuffer.data(), entry.readBuffer.size());
    }
    UnlockMemoryBuffer(diskBufferEntries.get(), sizeof(diskBufferEntries[0]) * totalDiskBufferEntryCount);
    UnlockMemoryBuffer(this, sizeof(*this));

    // Flag that this thread is no longer running
    captureThreadRunning.clear();
    captureThreadRunning.notify_all();
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::GetTransferInProgress() const
{
    return transferInProgress && (captureResult == TransferResult::Running);
}

//----------------------------------------------------------------------------------------------------------------------
UsbDeviceBase::TransferResult UsbDeviceBase::GetTransferResult() const
{
    return captureResult;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetNumberOfTransfers() const
{
    return transferCount;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetNumberOfDiskBuffersWritten() const
{
    return transferBufferWrittenCount;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetFileSizeWrittenInBytes() const
{
    return transferFileSizeWrittenInBytes;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetProcessedSampleCount() const
{
    return processedSampleCount;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetMinSampleValue() const
{
    return minSampleValue;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetMaxSampleValue() const
{
    return maxSampleValue;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetClippedMinSampleCount() const
{
    return clippedMinSampleCount;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetClippedMaxSampleCount() const
{
    return clippedMaxSampleCount;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetRecentMinSampleValue() const
{
    return recentMinSampleValue;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetRecentMaxSampleValue() const
{
    return recentMaxSampleValue;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetRecentClippedMinSampleCount() const
{
    return recentClippedMinSampleCount;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetRecentClippedMaxSampleCount() const
{
    return recentClippedMaxSampleCount;
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::GetTransferHadSequenceNumbers() const
{
    return (sequenceState != SequenceState::Disabled);
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::UsbTransferDumpBuffers() const
{
    return dumpAllCaptureDataInProgress.test();
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::UsbTransferStopRequested() const
{
    return usbTransferStopRequested.test();
}

//----------------------------------------------------------------------------------------------------------------------
UsbDeviceBase::DiskBufferEntry& UsbDeviceBase::GetDiskBuffer(size_t bufferNo)
{
    assert(bufferNo < totalDiskBufferEntryCount);
    return diskBufferEntries[bufferNo];
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetDiskBufferCount() const
{
    return totalDiskBufferEntryCount;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetSingleDiskBufferSizeInBytes() const
{
    return diskBufferSizeInBytes;
}

//----------------------------------------------------------------------------------------------------------------------
size_t UsbDeviceBase::GetUsbTransferQueueSizeInBytes() const
{
    return currentUsbTransferQueueSizeInBytes;
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::GetUseSmallUsbTransfers() const
{
    return currentUseSmallUsbTransfers;
}

//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::SetUsbTransferFinished(TransferResult result)
{
    usbTransferResult = result;
    usbTransferRunning.clear();
    usbTransferRunning.notify_all();
    captureThreadStopRequested.test_and_set();
    captureThreadStopRequested.notify_all();
}

//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::SetProcessingFinished(TransferResult result)
{
    processingResult = result;
    processingRunning.clear();
    processingRunning.notify_all();
    captureThreadStopRequested.test_and_set();
    captureThreadStopRequested.notify_all();
}

//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::AddCompletedTransferCount(size_t incrementCount)
{
    transferCount += incrementCount;
}

//----------------------------------------------------------------------------------------------------------------------
// Processing methods
//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::ProcessingThread()
{
    ThreadPriorityRestoreInfo priorityRestoreInfo = {};
    bool boostedThreadPriority = SetCurrentThreadRealtimePriority(priorityRestoreInfo);
    std::shared_ptr<void> currentThreadPriorityReducer;
    if (!boostedThreadPriority)
    {
        Log().Warning("ProcessingThread(): Failed to boost thread priority");
    }
    else
    {
        currentThreadPriorityReducer.reset((void*)nullptr, [&](void*) { RestoreCurrentThreadPriority(priorityRestoreInfo); });
    }

    bool transferComplete = false;
    bool processingFailure = false;
    size_t currentDiskBuffer = 0;
    while (!processingFailure && !transferComplete)
    {
        // If processing has been requested to stop, and we've reached the end of the buffered data, or we're being
        // stopped forcefully, break out of the processing loop. If the stop isn't forceful, let the current loop
        // iteration complete so the current disk write can finish cleanly.
        DiskBufferEntry& bufferEntry = diskBufferEntries[currentDiskBuffer];
        bool flushOnly = false;
        if (processingStopRequested.test())
        {
            if (dumpAllCaptureDataInProgress.test())
            {
                transferComplete = true;
                continue;
            }
            if (!bufferEntry.isDiskBufferFull.test() || bufferEntry.dumpingBuffer.test())
            {
                flushOnly = true;
                transferComplete = true;
            }
        }

        // If this isn't a flush-only pass, retrieve, validate, process, and write the next block of data to the output
        // file.
        if (!flushOnly)
        {
            // Wait for the next disk buffer to be filled
            bufferEntry.isDiskBufferFull.wait(false);

            // If the buffer isn't really filled, we've just been woken because the buffer is being dumped while
            // stopping the capture process, loop around again. We expect processing is either being gracefully or
            // forcefully halted. The next loop iteration will allow us to determine what the case is and whether we
            // need to flush pending writes or abort the transfers in flight.
            if (bufferEntry.dumpingBuffer.test())
            {
                continue;
            }

            // Verify and strip the sequence markers from the sample data, and update our sample metrics.
            uint16_t minValue = std::numeric_limits<uint16_t>::max();
            uint16_t maxValue = std::numeric_limits<uint16_t>::min();
            size_t samplesProcessedForBuffer = 0;
            size_t minClippedCount = 0;
            size_t maxClippedCount = 0;
            if (!ProcessSequenceMarkersAndUpdateSampleMetrics(currentDiskBuffer, samplesProcessedForBuffer, minValue, maxValue, minClippedCount, maxClippedCount))
            {
                SetProcessingFinished(TransferResult::SequenceMismatch);
                processingFailure = true;
                continue;
            }
            minSampleValue = std::min(minSampleValue.load(), minValue);
            maxSampleValue = std::max(maxSampleValue.load(), maxValue);
            clippedMinSampleCount += minClippedCount;
            clippedMaxSampleCount += maxClippedCount;
            recentMinSampleValue = minValue;
            recentMaxSampleValue = maxValue;
            recentClippedMinSampleCount = minClippedCount;
            recentClippedMaxSampleCount = maxClippedCount;
            processedSampleCount += samplesProcessedForBuffer;

            // If a buffer sample has been requested, capture it now.
            if (bufferSampleRequestPending.test() && (bufferSamplingRequestedLengthInBytes <= diskBufferSizeInBytes))
            {
                capturedBufferSample.assign(bufferEntry.readBuffer.data(), bufferEntry.readBuffer.data() + bufferSamplingRequestedLengthInBytes);
                bufferSampleRequestPending.clear();
                bufferSampleAvailable.test_and_set();
                bufferSampleAvailable.notify_all();
            }

            // Verify the test data sequence if required
            if (captureIsTestMode && !VerifyTestSequence(currentDiskBuffer))
            {
                SetProcessingFinished(TransferResult::VerificationError);
                processingFailure = true;
                continue;
            }

            // Convert the sample data into the requested data format
            auto& currentConversionBuffer = conversionBuffers[0];
            if (!ConvertRawSampleData(currentDiskBuffer, captureFormat, currentConversionBuffer))
            {
                SetProcessingFinished(TransferResult::ProgramError);
                processingFailure = true;
                continue;
            }

            // Write the data to the output file
            captureOutputFile.write((const char*)currentConversionBuffer.data(), currentConversionBuffer.size());
            if (!captureOutputFile.good())
            {
                Log().Error("ProcessingThread(): An error occurred when writing to the output file");
                SetProcessingFinished(TransferResult::FileWriteError);
                processingFailure = true;
                continue;
            }

            // Mark the disk buffer as empty, notifying the USB transfer thread in case it's blocking waiting for this
            // buffer to be free.
            bufferEntry.isDiskBufferFull.clear();
            bufferEntry.isDiskBufferFull.notify_all();

            // Add the totals from this buffer to the transfer statistics
            ++transferBufferWrittenCount;
            transferFileSizeWrittenInBytes += currentConversionBuffer.size();
        }

        // Advance to the next disk buffer in the queue
        currentDiskBuffer = (currentDiskBuffer + 1) % totalDiskBufferEntryCount;
    }

    // If we've been requested to stop the capture process, and an error hasn't been flagged, mark the process as
    // successful.
    if (!processingFailure)
    {
        SetProcessingFinished(TransferResult::Success);
    }
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::ProcessSequenceMarkersAndUpdateSampleMetrics(size_t diskBufferIndex, size_t& processedSampleCount, uint16_t& minValue, uint16_t& maxValue, size_t& minClippedCount, size_t& maxClippedCount)
{
    // If sequence checking has already failed, return false immediately. This condition should not occur, as a
    // sequence failure is treated as an unrecoverable error and aborts the capture process. If this is to be changed,
    // note that we would still need to perform sequence number stripping and update our sample metrics below, so this
    // condition here should be removed and the rest of the function allowed to run.
    if (sequenceState == SequenceState::Failed)
    {
        return false;
    }

    // Retrieve the previous sequence counter number. If we don't have a saved sequence number because we're just
    // starting a capture and we need to synchronize with the device, extract the sequence number from the buffer
    // and calculate the previous sequence number from it as a starting point.
    const uint16_t minPossibleSampleValue = 0;
    const uint16_t maxPossibleSampleValue = 0b1111111111;
    const int COUNTER_SHIFT = 16;
    const uint32_t COUNTER_MAX = 0b111111;
    uint32_t sequenceCounter = savedSequenceCounter;
    uint8_t* diskBuffer = diskBufferEntries[diskBufferIndex].readBuffer.data();
    if (sequenceState == SequenceState::Sync)
    {
        // Initialize the sequence counter to an impossible value
        sequenceCounter = 0xFFFFFFFF;

        // Get the first sequence number from the buffer
        uint32_t firstSequenceNumber = (uint32_t)(diskBuffer[1] >> 2);

        // Find the first time the sequence number changes. Since each sequence number appears on (1 << COUNTER_SHIFT)
        // samples, at worst we will see a change within (1 << COUNTER_SHIFT) + 1 samples.
        for (size_t pointer = 2; pointer < ((1 << COUNTER_SHIFT) + 1) * 2; pointer += 2)
        {
            uint32_t sequenceNumber = diskBuffer[pointer + 1] >> 2;
            if (sequenceNumber != firstSequenceNumber)
            {
                // Found it -- compute sequenceCounter's value at the start of the buffer
                if (sequenceNumber == 0)
                {
                    sequenceNumber = COUNTER_MAX;
                }
                sequenceCounter = (sequenceNumber << COUNTER_SHIFT) - ((uint32_t)pointer / 2);
                break;
            }
        }

        // If no sequence numbers were detected, disable sequence checking, otherwise activate it.
        if (sequenceCounter == 0xFFFFFFFF)
        {
            Log().Warning("ProcessSequenceMarkersAndUpdateSampleMetrics(): Data does not include sequence numbers");
            sequenceState = SequenceState::Disabled;
        }
        else
        {
            Log().Trace("ProcessSequenceMarkersAndUpdateSampleMetrics(): Synchronised with data sequence numbers");
            sequenceState = SequenceState::Running;
        }
    }

    // Validate sequence number progression, and update our sample metrics.
    for (size_t i = 0; i < diskBufferSizeInBytes; i += 2)
    {
        // If sequence checking is active, validate sequence numbers for this sample.
        if (sequenceState == SequenceState::Running)
        {
            // Ensure the sequence number in this sample matches the expected value. Note that this is treated as an
            // unrecoverable and immediate fail, so we abort any further processing and return false here.
            uint32_t expected = sequenceCounter >> COUNTER_SHIFT;
            uint32_t sequenceNumber = (uint32_t)(diskBuffer[i + 1] >> 2);
            if (sequenceNumber != expected)
            {
                Log().Error("ProcessSequenceMarkersAndUpdateSampleMetrics(): Sequence number mismatch! Expecting {0} but got {1}", expected, sequenceNumber);
                sequenceState = SequenceState::Failed;
                savedSequenceCounter = sequenceCounter;
                return false;
            }

            // Advance the sequence counter
            ++sequenceCounter;
            if (sequenceCounter == (COUNTER_MAX << COUNTER_SHIFT))
            {
                sequenceCounter = 0;
            }
        }

        // Remove the sequence number from the sample in the disk buffer (modify the source data)
        diskBuffer[i + 1] &= 0x03;

        // Get the original 10-bit unsigned value from the disk data buffer
        uint16_t originalValue = (uint16_t)diskBuffer[i + 0] | ((uint16_t)diskBuffer[i + 1] << 8);

        // Update our min/max values
        minValue = std::min(minValue, originalValue);
        maxValue = std::max(maxValue, originalValue);

        // If the sample value is either the minimum or maximum value, increment our clipped sample counts.
        if (originalValue == minPossibleSampleValue)
        {
            ++minClippedCount;
        }
        else if (originalValue == maxPossibleSampleValue)
        {
            ++maxClippedCount;
        }
    }

    // Set the processed sample count for this buffer
    processedSampleCount += diskBufferSizeInBytes / 2;

    // Save the resulting sequence counter so we can continue checking from the same position in the next buffer
    savedSequenceCounter = sequenceCounter;
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::VerifyTestSequence(size_t diskBufferIndex)
{
    // Retrieve the stored expected next sample value. If we haven't processed a buffer in this capture yet,
    // latch the first sample value as the expected value so we can start from here.
    DiskBufferEntry& bufferEntry = diskBufferEntries[diskBufferIndex];
    uint16_t expectedValue = expectedNextTestDataValue.value_or((uint16_t)bufferEntry.readBuffer[0] | (uint16_t)((uint16_t)bufferEntry.readBuffer[1] << 8));

    // Verify each sample in the buffer matches our expected sequence progression
    const uint8_t* readBufferPointer = bufferEntry.readBuffer.data();
    for (size_t i = 0; i < bufferEntry.readBuffer.size(); i += 2)
    {
        // Get the original 10-bit unsigned value from the disk data buffer
        uint16_t actualValue = (uint16_t)readBufferPointer[0] | ((uint16_t)readBufferPointer[1] << 8);
        readBufferPointer += 2;

        // If the actual value doesn't match our expected value, but this is the first time the test sequence
        // has wrapped around to 0, check if this appears to be the wrap point for the sequence, and latch it.
        // Valid wrapp points are either 1021 (newer FPGA firmware) or 1024 (older FPGA firmware).
        if (!testDataMax.has_value() && (expectedValue != actualValue) && (actualValue == 0) && ((expectedValue == 1021) || (expectedValue == 1024)))
        {
            testDataMax = expectedValue;
            expectedValue = 1;
            continue;
        }

        // If the expected value differs from the actual value, log an error.
        if (expectedValue != actualValue)
        {
            // Data error
            Log().Error("VerifyTestSequence(): Data error in test data verification! Expecting {0} but got {1}", expectedValue, actualValue);
            return false;
        }

        // Calculate the value we expect to find for the next sample
        ++expectedValue;
        if (testDataMax.has_value() && (expectedValue == testDataMax))
        {
            expectedValue = 0;
        }
    }

    // Store the next expected sample value so we can check against the next buffer
    expectedNextTestDataValue = expectedValue;
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::ConvertRawSampleData(size_t diskBufferIndex, CaptureFormat captureFormat, std::vector<uint8_t>& outputBuffer) const
{
    const DiskBufferEntry& bufferEntry = diskBufferEntries[diskBufferIndex];
    const uint8_t* readBufferPointer = bufferEntry.readBuffer.data();
    size_t readBufferSizeInBytes = bufferEntry.readBuffer.size();

    // Convert the data to the required format
    uint8_t* writeBufferPointer = outputBuffer.data();
    if (captureFormat == CaptureFormat::Signed16Bit)
    {
        // Translate the data in the disk buffer to scaled 16-bit signed data
        for (size_t i = 0; i < readBufferSizeInBytes; i += 2)
        {
            // Get the original 10-bit unsigned value from the disk data buffer
            uint16_t originalValue = (uint16_t)readBufferPointer[0] | ((uint16_t)readBufferPointer[1] << 8);
            readBufferPointer += 2;

            // Sign and scale the data to 16-bits. Technically a line like this would use the entire 16-bit range:
            //uint16_t signedValue = ((uint16_t)((int16_t)originalValue - 0x0200) << 6) | ((originalValue >> 4) & 0x003F);
            // In our case here however, that would not be preferred, since we can't restore the lost 6 bits of
            // precision, and where we guess wrong we'd create very slight frequency distortions. It's better to leave
            // the data as 10-bit and just shift it up, which doesn't technically preserve the relative mplitude of the
            // signal, but we don't care about the overall amplitude in this case, it's the frequency we care about.
            uint16_t signedValue = (uint16_t)((int16_t)originalValue - 0x0200) << 6;
            writeBufferPointer[0] = (uint8_t)((uint16_t)signedValue & 0x00FF);
            writeBufferPointer[1] = (uint8_t)(((uint16_t)signedValue & 0xFF00) >> 8);
            writeBufferPointer += 2;
        }
    }
    else if (captureFormat == CaptureFormat::Unsigned10Bit)
    {
        // Translate the data in the disk buffer to unsigned 10-bit packed data
        for (size_t i = 0; i < readBufferSizeInBytes; i += 8)
        {
            // Get the original 4 10-bit words
            uint16_t originalWords[4];
            originalWords[0] = (uint16_t)readBufferPointer[0] | ((uint16_t)readBufferPointer[1] << 8);
            originalWords[1] = (uint16_t)readBufferPointer[2] | ((uint16_t)readBufferPointer[3] << 8);
            originalWords[2] = (uint16_t)readBufferPointer[4] | ((uint16_t)readBufferPointer[5] << 8);
            originalWords[3] = (uint16_t)readBufferPointer[6] | ((uint16_t)readBufferPointer[7] << 8);
            readBufferPointer += 8;

            // Convert into 5 bytes of packed 10-bit data
            writeBufferPointer[0] = (uint8_t)((originalWords[0] & 0x03FC) >> 2);
            writeBufferPointer[1] = (uint8_t)((originalWords[0] & 0x0003) << 6) | (uint8_t)((originalWords[1] & 0x03F0) >> 4);
            writeBufferPointer[2] = (uint8_t)((originalWords[1] & 0x000F) << 4) | (uint8_t)((originalWords[2] & 0x03C0) >> 6);
            writeBufferPointer[3] = (uint8_t)((originalWords[2] & 0x003F) << 2) | (uint8_t)((originalWords[3] & 0x0300) >> 8);
            writeBufferPointer[4] = (uint8_t)((originalWords[3] & 0x00FF));
            writeBufferPointer += 5;
        }
    }
    else if (captureFormat == CaptureFormat::Unsigned10Bit4to1Decimation)
    {
        // Translate the data in the disk buffer to unsigned 10-bit packed data with 4:1 decimation
        for (size_t i = 0; i < readBufferSizeInBytes; i += (8 * 4))
        {
            // Get the original 4 10-bit words
            uint16_t originalWords[4];
            originalWords[0] = (uint16_t)readBufferPointer[0 + 0] | ((uint16_t)readBufferPointer[1 + 0] << 8);
            originalWords[1] = (uint16_t)readBufferPointer[2 + 4] | ((uint16_t)readBufferPointer[3 + 4] << 8);
            originalWords[2] = (uint16_t)readBufferPointer[4 + 8] | ((uint16_t)readBufferPointer[5 + 8] << 8);
            originalWords[3] = (uint16_t)readBufferPointer[6 + 12] | ((uint16_t)readBufferPointer[7 + 12] << 8);
            readBufferPointer += 8 * 4;

            // Convert into 5 bytes of packed 10-bit data
            writeBufferPointer[0] = (uint8_t)((originalWords[0] & 0x03FC) >> 2);
            writeBufferPointer[1] = (uint8_t)((originalWords[0] & 0x0003) << 6) | (uint8_t)((originalWords[1] & 0x03F0) >> 4);
            writeBufferPointer[2] = (uint8_t)((originalWords[1] & 0x000F) << 4) | (uint8_t)((originalWords[2] & 0x03C0) >> 6);
            writeBufferPointer[3] = (uint8_t)((originalWords[2] & 0x003F) << 2) | (uint8_t)((originalWords[3] & 0x0300) >> 8);
            writeBufferPointer[4] = (uint8_t)((originalWords[3] & 0x00FF));
            writeBufferPointer += 5;
        }
    }
    else
    {
        Log().Error("ConvertRawSampleData(): Unknown capture format {0} specified", captureFormat);
        return false;
    }
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
// Buffer sampling methods
//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::QueueBufferSampleRequest(size_t requestedSampleLengthInBytes)
{
    bufferSamplingRequestedLengthInBytes = requestedSampleLengthInBytes;
    bufferSampleRequestPending.test_and_set();
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::GetNextBufferSample(std::vector<uint8_t>& bufferSample)
{
    // If no new buffer sample is available, abort any further processing.
    if (!bufferSampleAvailable.test())
    {
        return false;
    }

    // Copy the requested data into the buffer
    bufferSampleAvailable.clear();
    bufferSample.assign(capturedBufferSample.data(), capturedBufferSample.data() + capturedBufferSample.size());
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
// Utility methods
//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::LockMemoryBufferIntoPhysicalMemory(void* baseAddress, size_t sizeInBytes)
{
    if (!memoryLockingEnabled || baseAddress == nullptr || sizeInBytes == 0)
    {
        return true;
    }

    // Lock the target memory buffer into physical memory
    if (mlock(baseAddress, sizeInBytes) == -1)
    {
        Log().Error("mlock failed for {0} bytes: {1}", sizeInBytes, std::strerror(errno));
        return false;
    }

    lockedMemoryRegions.push_back({ baseAddress, sizeInBytes });
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::UnlockMemoryBuffer(void* baseAddress, size_t sizeInBytes)
{
    auto region = std::find_if(lockedMemoryRegions.begin(), lockedMemoryRegions.end(),
        [baseAddress, sizeInBytes](const LockedMemoryRegion& candidate)
        {
            return candidate.baseAddress == baseAddress && candidate.sizeInBytes == sizeInBytes;
        });
    if (region == lockedMemoryRegions.end())
    {
        return;
    }

    // Release the lock on the target memory buffer
    munlock(baseAddress, sizeInBytes);

    lockedMemoryRegions.erase(region);
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::CheckMemoryLockLimit(size_t requiredLockSizeInBytes) const
{
    rlimit memoryLockLimit{};
    if (getrlimit(RLIMIT_MEMLOCK, &memoryLockLimit) != 0)
    {
        Log().Warning("getrlimit(RLIMIT_MEMLOCK) failed: {0}", std::strerror(errno));
        return true;
    }
    if (memoryLockLimit.rlim_cur == RLIM_INFINITY || memoryLockLimit.rlim_cur >= requiredLockSizeInBytes)
    {
        return true;
    }

    std::string hardLimit = memoryLockLimit.rlim_max == RLIM_INFINITY
        ? std::string("unlimited")
        : std::to_string((unsigned long long)memoryLockLimit.rlim_max);
    Log().Warning(
        "RLIMIT_MEMLOCK is {0} bytes, but capture buffers need about {1} bytes locked; "
        "raise the user's memlock limit or mlock will fail. Hard limit is {2} bytes.",
        (unsigned long long)memoryLockLimit.rlim_cur,
        (unsigned long long)requiredLockSizeInBytes,
        hardLimit);
    return false;
}

//----------------------------------------------------------------------------------------------------------------------
bool UsbDeviceBase::SetCurrentThreadRealtimePriority(ThreadPriorityRestoreInfo& priorityRestoreInfo)
{
    // Retrieve the current scheduling policy for the calling thread
    int oldSchedPolicy;
    sched_param oldSchedParam;
    pthread_getschedparam(pthread_self(), &oldSchedPolicy, &oldSchedParam);
    priorityRestoreInfo.oldSchedPolicy = oldSchedPolicy;
    priorityRestoreInfo.oldSchedParam = oldSchedParam;

    // Attempt to increase the thread scheduling policy to realtime
    int targetPolicy = SCHED_RR;
    int minSchedPriority = sched_get_priority_min(targetPolicy);
    int maxSchedPriority = sched_get_priority_max(targetPolicy);
    sched_param schedParams;
    if (minSchedPriority == -1 || maxSchedPriority == -1)
    {
        schedParams.sched_priority = 0;
    }
    else
    {
        // Put the priority about 3/4 of the way through its range
        schedParams.sched_priority = (minSchedPriority + (3 * maxSchedPriority)) / 4;
    }
    if (pthread_setschedparam(pthread_self(), targetPolicy, &schedParams) == 0)
    {
        Log().Info("SetCurrentThreadRealtimePriority: Thread priority set with policy SCHED_RR");
    }
    else
    {
        Log().Warning("SetCurrentThreadRealtimePriority: Unable to set thread priority");
    }
    return true;
}

//----------------------------------------------------------------------------------------------------------------------
void UsbDeviceBase::RestoreCurrentThreadPriority(const ThreadPriorityRestoreInfo& priorityRestoreInfo)
{
    if (pthread_setschedparam(pthread_self(), priorityRestoreInfo.oldSchedPolicy, &priorityRestoreInfo.oldSchedParam) != 0)
    {
        Log().Warning("RestoreCurrentThreadPriority: Unable to restore original scheduling policy");
    }
}
