#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""FC27 PS5 exact-source shutdown ownership: SM factories must retire before KProcess."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
core=(ROOT/"encore-active/eden/src/core/core.cpp").read_text()
sm_h=(ROOT/"encore-active/eden/src/core/hle/service/sm/sm.h").read_text()
sm=(ROOT/"encore-active/eden/src/core/hle/service/sm/sm.cpp").read_text()
as_gpu=(ROOT/"encore-active/eden/src/core/hle/service/nvdrv/devices/nvhost_as_gpu.cpp").read_text()
server=(ROOT/"encore-active/eden/src/core/hle/service/server_manager.cpp").read_text()
shutdown=core.split("    void ShutdownMainProcess() {",1)[1].split("\n    bool IsShuttingDown()",1)[0]
sequence=(
    "gpu_core.reset();",
    "service_manager->Close();",
    "kernel.CloseServices();",
    "service_manager->ReleaseRegisteredHandlersForShutdown();",
    "services.reset();",
    "kernel.ShutdownCores();",
    "service_manager.reset();",
    "kernel.Shutdown();",
)
indices=[shutdown.index(x) for x in sequence]
assert indices==sorted(indices),indices
assert "void ReleaseRegisteredHandlersForShutdown();" in sm_h
retire=sm.split("void ServiceManager::ReleaseRegisteredHandlersForShutdown() {",1)[1].split(
    "\n}\n",1)[0]
for token in ("decltype(registered_services) retired;", "std::unique_lock l{lock};",
              "!closing || service_waiters != 0",
              "retired.swap(registered_services);","retired.clear();"):
    assert token in retire,token
assert retire.index("retired.swap(registered_services);") < retire.index(
    "retired.clear();")
assert retire.index("retired.clear();") > retire.index("\n    }\n")
# NVDRV retains ownership of address spaces through its service factory.
assert "const auto HandlerFactory = [handler]() { return handler; };" in server
as_dtor=as_gpu.split("nvhost_as_gpu::~nvhost_as_gpu() {",1)[1].split("\n}\n",1)[0]
assert "nvmap.UnpinHandle(mapping.handle);" in as_dtor
assert "container.CloseSession(*m_nv_session);" in as_dtor
assert "m_native_process->Close(system.Kernel());" in as_dtor
print("FC27_PS5_SM_NVDRV_SHUTDOWN_SOURCE_PASS phase=after_services_join before_kprocess=1 sm_alive=1 out_of_lock=1")
