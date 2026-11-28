// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/hle/service/audio/final_output_recorder_manager.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::Audio {

class IFinalOutputRecorder final : public ServiceFramework<IFinalOutputRecorder> {
public:
    explicit IFinalOutputRecorder(Core::System& system_)
        : ServiceFramework{system_, "IFinalOutputRecorder"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &IFinalOutputRecorder::RejectUnsupportedDevice, "GetFinalOutputRecorderState"},
            {1, &IFinalOutputRecorder::RejectUnsupportedDevice, "Start"},
            {2, &IFinalOutputRecorder::RejectUnsupportedDevice, "Stop"},
            {3, &IFinalOutputRecorder::RejectUnsupportedDevice, "AppendFinalOutputRecorderBuffer"},
            {4, &IFinalOutputRecorder::RejectUnsupportedDevice, "RegisterBufferEvent"},
            {5, &IFinalOutputRecorder::RejectUnsupportedDevice, "GetReleasedFinalOutputRecorderBuffers"},
            {6, &IFinalOutputRecorder::RejectUnsupportedDevice, "ContainsFinalOutputRecorderBuffer"},
            {7, &IFinalOutputRecorder::RejectUnsupportedDevice, "GetFinalOutputRecorderBufferEndTime"},
            {8, &IFinalOutputRecorder::RejectUnsupportedDevice, "AppendFinalOutputRecorderBufferAuto"},
            {9, &IFinalOutputRecorder::RejectUnsupportedDevice, "GetReleasedFinalOutputRecorderBufferAuto"},
            {10, &IFinalOutputRecorder::RejectUnsupportedDevice, "FlushFinalOutputRecorderBuffers"},
            {11, &IFinalOutputRecorder::RejectUnsupportedDevice, "AttachWorkBuffer"},
        };
        // clang-format on

        RegisterHandlers(functions);
    }
private:
    void RejectUnsupportedDevice(HLERequestContext& ctx) {
        // Missing physical Switch power/clock/capture peripheral on the PS5.
        // Do not manufacture successful completion for guest requests.
        IPC::ResponseBuilder rb{ctx, 2};
        rb.Push(ResultUnknown);
    }

};

IFinalOutputRecorderManager::IFinalOutputRecorderManager(Core::System& system_)
    : ServiceFramework{system_, "audrec:u"} {
    static const FunctionInfo functions[] = {
        {0, &IFinalOutputRecorderManager::RejectUnsupportedDevice, "OpenFinalOutputRecorder"},
    };
    RegisterHandlers(functions);
}

void IFinalOutputRecorderManager::RejectUnsupportedDevice(HLERequestContext& ctx) {
    // No guest final-output recording backend. Playback continues through
    // the existing audio rendering path without this optional capture device.
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(ResultUnknown);
}

IFinalOutputRecorderManager::~IFinalOutputRecorderManager() = default;

} // namespace Service::Audio
