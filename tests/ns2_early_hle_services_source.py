#!/usr/bin/env python3
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/"sources/eden/src/core/hle/service"
ACTIVE=ROOT/"encore-active/ns2-runtime/hle/core/hle/service"
def blob(path):
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+b"\0"+raw).hexdigest()
assert blob(BASE/"am/service/application_proxy.cpp")=="6e1328fee7d9d9cce46c4424c0eeb7f5ca56ac92"
assert blob(BASE/"am/service/application_proxy.h")=="8c62459c45ed040d28d6ffc80d7621f0a82fb123"
assert blob(BASE/"hid/hid_server.cpp")=="9689bce8dd69ba186bf9a24d02e53a9f2fb8954d"
assert blob(BASE/"hid/hid_server.h")=="57a1a04b9c7d135a8181f2dde0d418cade1f29b3"
# The shipping Eden appletOE service has only cmd 0; the Switch 2 SDK
# uses cmd 10 with an additional u32 in the reserved input. Keep the\n# original process/session ownership and do not enable global auto-stub.
proxy_base=(BASE/"am/service/application_proxy_service.cpp")
proxy_owner=(ACTIVE/"am/service/application_proxy_service.cpp")
assert blob(proxy_base)=="b7d7b3c2d957ee0d5414f73428afc0cd0891f179"
proxy_text=proxy_owner.read_text()
assert proxy_text.count('{0, D<&IApplicationProxyService::OpenApplicationProxy>')==1
assert proxy_text.count('{10, D<&IApplicationProxyService::OpenApplicationProxyWithFlags>')==1
assert 'Result IApplicationProxyService::OpenApplicationProxyWithFlags(' in proxy_text
assert 'R_UNLESS(flags == 0 || flags == 2, ResultUnknown)' in proxy_text
assert 'R_RETURN(OpenApplicationProxy(out_application_proxy, pid, process_handle))' in proxy_text
proxy_header=(ACTIVE/"am/service/application_proxy_service.h").read_text()
assert 'Result OpenApplicationProxyWithFlags(' in proxy_header
assert 'u32 flags, ClientProcessId pid' in proxy_header
assert blob(BASE/"am/service/application_proxy_service.h")=="e5f4ea345bdb7a50da8586d6347feae5851d47f6"
assert 'if (const auto applet = this->GetAppletFromProcessId(pid))' in proxy_text
# SwitchBrew AM appletOE: official users retry 0x19280 (AM 201) when
# their applet is not registered yet; never replace that with 0xFFFFFFFF.
assert 'R_THROW(Result{ErrorModule::AM, 201});' in proxy_text
assert 'UNIMPLEMENTED();\n        R_THROW(ResultUnknown);' not in proxy_text
assert 'std::make_shared<IApplicationProxy>' in proxy_text
assert 'use_auto_stub' not in proxy_text
ap=(ACTIVE/"am/service/application_proxy.cpp").read_text()
aph=(ACTIVE/"am/service/application_proxy.h").read_text()
gfx=(ACTIVE/"am/service/graphics_scaling_parameters_functions.h").read_text()
hid=(ACTIVE/"hid/hid_server.cpp").read_text()
hidh=(ACTIVE/"hid/hid_server.h").read_text()
# EglantinePS5 / Eglantine nx2: command 40 is a typed performance
# interface, not the command-60 graphics-scale interface or auto-success stub.
perf=(ACTIVE/"am/service/performance_functions.h").read_text()
assert ap.count('{40, D<&IApplicationProxy::GetPerformanceFunctions>')==1
assert 'std::make_shared<IPerformanceFunctions>(system, m_performance_state)' in ap
assert 'm_performance_state{std::make_shared<Ns2PerformanceRequest>()}' in ap
assert 'class IPerformanceFunctions;' in aph
assert 'const std::shared_ptr<Ns2PerformanceRequest> m_performance_state;' in aph
assert '{0, D<&IPerformanceFunctions::IsCpuOverclockEnabled>' in perf
assert '{1, D<&IPerformanceFunctions::SetCpuOverclockEnabled>' in perf
assert 'cpu_overclock_requested.load(std::memory_order_relaxed)' in perf
assert 'cpu_overclock_requested.store(enabled, std::memory_order_relaxed)' in perf
assert 'Settings::values.cpu_clock.GetValue() != Settings::CpuClock::Normal' in perf
assert 'use_auto_stub' not in perf and 'ReturnNx2StubInterface' not in perf
assert ap.count('{60, D<&IApplicationProxy::GetGraphicsScalingParametersFunctions>')==1
assert ap.count('std::make_shared<IGraphicsScalingParametersFunctions>(system)')==1
assert "class IGraphicsScalingParametersFunctions;" in aph
assert "*out_scale = 10;" in gfx and "*out_unknown1 = 0;" in gfx and "*out_unknown2 = 0;" in gfx
assert '{10, D<&IGraphicsScalingParametersFunctions::GetGraphicsScalingParameters>' in gfx
assert '{1050, D<&IGraphicsScalingParametersFunctions::SetGraphicsScaleChangedNotificationEnabled>' in gfx
for cmd,name in ((3110,"InitializeJcms"),(3111,"FinalizeJcms"),(3112,"StartJcms"),(3113,"StopJcms")):
    assert f'{{{cmd}, &IHidServer::StubJcms, "{name}"}}' in hid
assert hid.count("void IHidServer::StubJcms(HLERequestContext& ctx)") == 1
jcms=hid.split("void IHidServer::StubJcms(HLERequestContext& ctx)",1)[1].split("\n}",1)[0]
assert "IPC::ResponseBuilder rb{ctx, 2};" in jcms and "rb.Push(ResultSuccess);" in jcms
assert "use_auto_stub" not in jcms
assert "void StubJcms(HLERequestContext& ctx);" in hidh
# Switch 2 application functions command 321 is a one-byte output.
# It must not silently return success without the byte, nor modify 320.
appfunc=(ROOT/"encore-active/ns2-runtime/am/core/hle/service/am/service/application_functions.cpp").read_text()
appfunc_header=(ROOT/"encore-active/ns2-runtime/am/core/hle/service/am/service/application_functions.h").read_text()
assert '{321, D<&IApplicationFunctions::Unknown321>, "Unknown321"}' in appfunc
assert 'Result Unknown321(Out<u8> out);' in appfunc_header
assert 'Result IApplicationFunctions::Unknown321(Out<u8> out) {' in appfunc
assert '*out = 0;' in appfunc.split('Result IApplicationFunctions::Unknown321(Out<u8> out) {',1)[1].split('}',1)[0]
audio=(BASE/"audio/audio_device.cpp").read_text()
assert '{19, D<&IAudioDevice::SetAudioDeviceOutputVolumeAutoTuneEnabled>' in audio
assert '{20, D<&IAudioDevice::IsAudioDeviceOutputVolumeAutoTuneEnabled>' in audio
assert "*out_enabled = false;" in audio
print("NS2_EARLY_HLE_SERVICE_SOURCE_PASS graphics_scale=10 jcms=4 audio_autotune=preexisting syncpoint=later-excluded ps5=unverified")
