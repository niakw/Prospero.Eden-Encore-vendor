// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// Successful NS2 supervisor commands have real kernel IPC backing, but
// NO native NVDRV address space, event, worker, queue or PS5 rendering path.
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include "common/logging.h"
#include <memory>
#include <limits>
#include "core/hle/service/nvdrv/nvdrv_interface.h"
#include "core/hle/service/nvdrv/core/container.h"
#include "core/hle/service/nvdrv/core/syncpoint_manager.h"
#include "core/hle/service/nvdrv/core/nvmap.h"
#include "core/hle/service/nvdrv/nvdrv.h"
#include "core/hle/service/nvdrv/devices/nvhost_as_gpu.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/service/sm/sm.h"
#include <span>
#include "core/core.h"
#include "core/hle/kernel/k_client_session.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_memory_block.h"
#include "core/hle/kernel/k_scoped_resource_reservation.h"
#include "core/hle/kernel/k_session.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/am/service/gpu_session.h"
#include "core/hle/service/am/service/graphics_supervisor.h"
#include "core/hle/service/am/service/native_image_layout.h"
#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/server_manager.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/service/nvnflinger/buffer_queue_producer.h"
#include "core/hle/service/nvnflinger/ui/graphic_buffer.h"
#include "core/hle/service/vi/application_display_service.h"
#include "core/hle/service/vi/container.h"
#include "core/hle/service/vi/manager_root_service.h"
#include "core/hle/service/vi/vi_types.h"

namespace Service::AM {
namespace GB = GraphicsSupervisorBootstrap;
static Result TranslateBootstrap(GB::Status status) {
    switch (status) {
    case GB::Status::Success:         return ResultSuccess;
    case GB::Status::InvalidPacket:   return Kernel::ResultInvalidCombination;
    case GB::Status::InvalidHandle:   return Kernel::ResultInvalidHandle;
    case GB::Status::InvalidState:    return Kernel::ResultInvalidState;
    case GB::Status::InvalidArgument: return Kernel::ResultInvalidArgument;
    case GB::Status::Unsupported:     return Kernel::ResultNotImplemented;
    }
    return Kernel::ResultNotImplemented;
}
static Result CreateMetadataSession(Core::System& system, HLERequestContext& ctx,
                                    std::shared_ptr<ServiceFrameworkBase> handler,
                                    Kernel::KClientSession** out_client) {
    auto& kernel = system.Kernel();
    Kernel::KScopedResourceReservation reservation{
        kernel, Kernel::GetCurrentProcessPointer(kernel), Kernel::LimitableResource::SessionCountMax};
    R_UNLESS(reservation.Succeeded(), Kernel::ResultLimitReached);
    auto* session = Kernel::KSession::Create(kernel);
    R_UNLESS(session != nullptr, Kernel::ResultOutOfResource);
    session->Initialize(kernel, nullptr, 0);
    reservation.Commit();
    Kernel::KSession::Register(kernel, session);
    auto& server = ctx.GetManager()->GetServerManager();
    auto manager = std::make_shared<SessionRequestManager>(kernel, server);
    manager->SetSessionHandler(std::move(handler));
    R_TRY(server.RegisterSession(&session->GetServerSession(), std::move(manager)));
    *out_client = &session->GetClientSession();
    R_SUCCEED();
}
// VI2 uses the actual VI compositor; a frame is acknowledged only when
// Nvnflinger QueueBuffer accepts a real, process-owned NVMap GraphicBuffer.
class IVi2Session final : public ServiceFramework<IVi2Session> {
    static constexpr u64 Default = 0x00746c7561666564ULL;
    struct Client {
        u64 token{};
        u32 signals{};
        u32 release_bit{};
        std::optional<u64> display;
        std::optional<u64> layer;
        u64 available{0x7fff};
        std::array<std::optional<s32>,15> queued{};
        std::array<u32,15> images{};
        std::array<std::shared_ptr<Nvidia::Module>,15> queued_modules{};
        // Retain buffer pins through the real producer's Free->DetachNext
        // transition, and until its returned release fence is satisfied.
        std::array<std::shared_ptr<android::GraphicBuffer>,15> retained{};
        std::array<std::optional<android::Fence>,15> release_fences{};
        std::shared_ptr<android::BufferQueueProducer> producer;
    };
public:
    IVi2Session(Core::System& sys, std::shared_ptr<IGraphicsSupervisor> owner, u64 aruid)
        : ServiceFramework{sys,"IVi2Session"}, m_owner{std::move(owner)},
          m_aruid{aruid}, m_context{sys,"NativeVi2"} {
        static const FunctionInfo f[]{
            {1,&IVi2Session::Limit,"GetNotificationLimit"},
            {3,&IVi2Session::Limit,"GetSessionLimit"},
            {5,&IVi2Session::Limit,"GetRequestGroupLimit"},
            {11,&IVi2Session::Event,"GetNotificationEvent"},
            {21,&IVi2Session::Notifications,"GetNotifications"},
            {101,&IVi2Session::Open,"OpenDisplay"},
            {102,&IVi2Session::Close,"CloseDisplay"},
            {201,&IVi2Session::Register,"RegisterClientState"},
            {202,&IVi2Session::Unregister,"UnregisterClientState"},
            {212,&IVi2Session::Availability,"GetClientAvailability"},
            {229,&IVi2Session::Present,"PresentClientBuffer"},
            {228,&IVi2Session::Cancel,"CancelClientBuffer"},
            {251,&IVi2Session::Bind,"BindClientDisplay"},
            {252,&IVi2Session::Unbind,"UnbindClientDisplay"},
            {351,&IVi2Session::Encoding,"SetClientOutputEncoding"},
            {352,&IVi2Session::OutputLevels,"SetClientOutputLevels"},
            {111,&IVi2Session::OpenEndpoint,"OpenEndpoint"},
            {112,&IVi2Session::CloseEndpoint,"CloseEndpoint"},
            {106,&IVi2Session::MaxResolution,"GetBufferMaxResolution"},
        };
        RegisterHandlersTipc(f);
    }
    ~IVi2Session() override {
        if (m_poll) system.CoreTiming().UnscheduleEvent(m_poll);
        if (m_vi) {
            for (auto& c:m_clients)
                if (c.layer) m_vi->GetContainer()->DestroyManagedLayer(*c.layer);
            if (m_display) m_vi->CloseDisplay(*m_display);
        }
        m_context.CloseEvent(m_event);
    }
    Result HandleSyncRequest(Kernel::KServerSession& session,HLERequestContext& ctx) override {
        if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand()))
            return ServiceFrameworkBase::HandleSyncRequest(session,ctx);
        const auto guard=LockService();
        InvokeRequestTipc(ctx);
        IPC::ResponseBuilder rb{ctx,2}; rb.Push(Kernel::ResultNotImplemented.raw);
        return system.IsPoweredOn()?ctx.WriteToOutgoingCommandBuffer():ResultSuccess;
    }
private:
    static u64 W(const u32* p,size_t i) {return u64{p[i]}|(u64{p[i+1]}<<32);}
    Client* Find(u64 id) {
        if (!id) return nullptr;
        const auto it=std::ranges::find(m_clients,id,&Client::token);
        return it==m_clients.end()?nullptr:std::addressof(*it);
    }
    void Signal(u32 bit) {
        if (bit>=1024)return;
        m_notes[bit/32]|=1U<<(bit%32);
        if(m_event)m_event->Signal(kernel);
    }
    void Limit(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        const Result result=p[0]==16+ctx.GetCommand()&&p[1]==0?
            ResultSuccess:Kernel::ResultInvalidCombination;
        IPC::ResponseBuilder rb{ctx,3}; rb.Push(result.raw);
        rb.Push<u32>(ctx.GetCommand()==5?7:0);
    }
    void Event(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        Kernel::KReadableEvent* readable{};
        if(p[0]==0x1b&&p[1]==1&&p[2]==0){
            if(!m_event)m_event=m_context.CreateEvent("Vi2Notify");
            result=m_event?ResultSuccess:Kernel::ResultOutOfResource;
            if(m_event){readable=&m_event->GetReadableEvent();readable->Open(kernel);}
        }
        IPC::ResponseBuilder rb{ctx,2,0,1};rb.Push(result.raw);
        ctx.AddMoveObject(readable);
    }
    void Notifications(HLERequestContext& ctx) {
        Result result=Kernel::ResultInvalidSize;
        if(ctx.GetWriteBufferSize()>=sizeof(m_notes)){
            ctx.WriteBuffer(m_notes);m_notes.fill(0);
            if(m_event)m_event->Clear(kernel);result=ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Open(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x75&&p[1]==3&&W(p,2)==Default&&p[4]==1){
            result=Kernel::ResultInvalidState;
            if(!m_display){
                auto root=system.ServiceManager().GetService<VI::IManagerRootService>("vi:m",true);
                if(root)result=root->GetDisplayService(&m_vi,VI::Policy::Compositor);
                if(result.IsSuccess()&&m_vi) {
                    u64 id{};result=m_vi->OpenDefaultDisplay(&id);
                    if(result.IsSuccess())m_display=id;
                }
            }else if(m_refs<1024)result=ResultSuccess;
            if(result.IsSuccess())++m_refs;
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Close(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x76&&p[1]==2&&W(p,2)==Default){
            result=Kernel::ResultNotFound;
            if(m_display&&m_refs){
                result=ResultSuccess;
                if(m_refs==1){
                    result=m_vi->CloseDisplay(*m_display);
                    if(result.IsSuccess())m_display.reset();
                }
                if(result.IsSuccess())--m_refs;
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Register(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        const u64 id=W(p,2);
        if(p[0]==0xd9&&p[1]==4&&id&&p[4]<32&&p[5]<32&&p[4]!=p[5]){
            const u32 slots=(1U<<p[4])|(1U<<p[5]);
            result=Kernel::ResultInvalidState;
            if(!Find(id)&&!(m_slots&slots)){
                result=Kernel::ResultOutOfResource;
                auto it=std::ranges::find(m_clients,u64{0},&Client::token);
                if(it!=m_clients.end()){
                    it->token=id;it->signals=slots;it->release_bit=p[4];
                    m_slots|=slots;result=ResultSuccess;
                }
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Unregister(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0xda&&p[1]==2){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){
                Release();
                result=Kernel::ResultBusy;
                if(std::ranges::none_of(c->queued,[](auto x){return x.has_value();})){
                    if(c->layer&&m_vi)m_vi->GetContainer()->DestroyManagedLayer(*c->layer);
                    Signal(c->release_bit);
                    if(m_endpoint==c->token)m_endpoint.reset();
                    m_slots&=~c->signals;*c={};result=ResultSuccess;
                }
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Bind(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x10b&&p[1]==4&&W(p,4)==Default){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){result=Kernel::ResultInvalidState;
                if(m_display&&m_refs&&!c->display){c->display=m_display;result=ResultSuccess;}}
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Unbind(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x10c&&p[1]==2){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c&&c->display){c->display.reset();result=ResultSuccess;}
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Availability(HLERequestContext& ctx) {
        Release();
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        u64 mask{};
        if(p[0]==0xe4&&p[1]==2){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c&&c->display&&m_vi){
                result=ResultSuccess;
                if(!c->layer){
                    u64 layer{};
                    result=m_vi->GetContainer()->CreateManagedLayer(&layer,*c->display,m_aruid);
                    if(result.IsSuccess())c->layer=layer;
                }
                if(result.IsSuccess())mask=c->available;
            }
        }
        IPC::ResponseBuilder rb{ctx,4};rb.Push(result.raw);rb.Push(mask);
    }
    void OutputLevels(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x170&&p[1]==5){
            const auto client=Find(W(p,2));
            result=Kernel::ResultNotFound;
            if(client){
                std::array<float,3> values{};
                std::memcpy(values.data(),p+4,sizeof(values));
                result=Kernel::ResultInvalidCombination;
                if(std::ranges::all_of(values,[](float v){return std::isfinite(v);})&&
                    values[0]>=0.0f&&values[0]<=1.0f&&
                    values[1]>=0.0f&&values[1]<=1.0f&&values[2]>=0.0f)
                    result=ResultSuccess;
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void OpenEndpoint(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x7f&&p[1]==6&&W(p,2)==Default&&W(p,4)==Default){
            const auto client=Find(W(p,6));
            result=Kernel::ResultNotFound;
            if(client&&client->display==m_display&&m_display){
                result=Kernel::ResultInvalidState;
                if(!m_endpoint){m_endpoint=client->token;result=ResultSuccess;}
            }
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void CloseEndpoint(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x80&&p[1]==4&&W(p,2)==Default&&W(p,4)==Default){
            result=Kernel::ResultNotFound;
            if(m_endpoint){m_endpoint.reset();result=ResultSuccess;}
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void MaxResolution(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();
        Result result=Kernel::ResultInvalidCombination;
        u64 mode{};
        if(p[0]==0x7a&&p[1]==2&&W(p,2)==Default){
            result=Kernel::ResultInvalidState;
            if(m_display&&m_vi){
                s64 width{},height{};
                result=m_vi->GetDisplayResolution(&width,&height,*m_display);
                if(result.IsSuccess()){
                    result=Kernel::ResultNotImplemented;
                    if(width==1280&&height==720){mode=1;result=ResultSuccess;}
                }
            }
        }
        IPC::ResponseBuilder rb{ctx,4};rb.Push(result.raw);rb.Push(mode);
    }
    void Encoding(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        if(p[0]==0x16f&&p[1]==4){
            result=Find(W(p,2))?Kernel::ResultNotImplemented:Kernel::ResultNotFound;
            if(result==Kernel::ResultNotImplemented&&W(p,4)==0x14)result=ResultSuccess;
        }
        IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
    }
    void Release() {
        for (auto& c : m_clients) {
            if (!c.producer) continue;
            // This Eden Nvnflinger version exposes DetachNextBuffer, not
            // upstream NS2's GetReleasedBufferFence/DetachReleasedBuffer.
            // Only Free buffers can be detached by that API.
            for (u32 tries=0;tries<c.queued.size();++tries) {
                std::shared_ptr<android::GraphicBuffer> buffer;
                android::Fence release_fence{};
                if (c.producer->DetachNextBuffer(&buffer,&release_fence)!=
                    android::Status::NoError) break;
                for (u32 slot=0;slot<c.queued.size();++slot) {
                    if (c.retained[slot] && c.retained[slot]==buffer) {
                        c.release_fences[slot]=release_fence;
                        break;
                    }
                }
            }
            for (u32 slot=0;slot<c.queued.size();++slot) {
                if (!c.queued[slot] || !c.release_fences[slot]) continue;
                auto module=c.queued_modules[slot];
                if (!module) continue;
                const auto& fence=*c.release_fences[slot];
                bool done=true;
                for (u32 i=0;i<fence.num_fences;++i) {
                    const auto& point=fence.fences[i];
                    if (point.id==~u32{0}) continue;
                    auto& syncpoints=module->GetContainer().GetSyncpointManager();
                    if (!syncpoints.IsSyncpointAllocated(point.id) ||
                        !syncpoints.HasSyncpointExpired(point.id,point.value)) {
                        done=false;break;
                    }
                }
                if (!done) continue;
                c.queued[slot].reset();
                c.images[slot]=0;
                c.queued_modules[slot].reset();
                c.release_fences[slot].reset();
                c.retained[slot].reset();
                c.available|=u64{1}<<slot;
                Signal(c.release_bit);
            }
        }
    }
    void Present(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        u64 mask{};
        if(p[0]==0xf5&&p[1]==24){
            Release();
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){
                const u32 slot=GraphicsSupervisorProtocol::DecodeBufferSlot(p[4]);
                result=Kernel::ResultInvalidState;
                if(slot<15&&c->display&&c->layer&&!c->queued[slot]){
                    const auto img=m_owner->GetPresentImage(p[5]);
                    result=Kernel::ResultNotFound;
                    if(img){
                        const auto& pref=img->layout;
                        const u32 width=(pref[2]&0xffffU)+1;
                        const u32 height=(pref[2]>>16)+1;
                        const u32 outw=(p[7]&0xffffU)+1;
                        const u32 outh=(p[7]>>16)+1;
                        result=Kernel::ResultNotImplemented;
                        if(p[6]==0&&p[8]==3&&pref[4]==7&&
                            pref[6]==1&&outw<=width&&outh<=height){
                            const u32 fence=~p[22];
                            auto& nv=img->module->GetContainer();
                            result=Kernel::ResultInvalidCombination;
                            if(p[23]==0&&p[25]==0&&
                                (fence==~u32{0}||
                                 nv.GetSyncpointManager().IsSyncpointAllocated(fence))){
                                if(!c->producer){
                                    result=m_vi->GetContainer()->GetLayerProducerHandle(
                                        &c->producer,*c->layer);
                                    if(result.IsSuccess()){
                                        android::QueueBufferOutput output{};
                                        if(c->producer->Connect({},
                                            android::NativeWindowApi::Egl,false,&output)!=
                                            android::Status::NoError||
                                           c->producer->SetBufferCount(16)!=
                                            android::Status::NoError)
                                            result=Kernel::ResultInvalidState;
                                    }
                                    if(result.IsSuccess()){
                                        s32 binder{};
                                        result=m_vi->GetContainer()->OpenLayer(
                                            &binder,*c->layer,m_aruid);
                                        if(result.IsSuccess())result=m_vi->GetContainer()
                                            ->SetLayerVisibility(*c->layer,true);
                                    }
                                }else result=ResultSuccess;
                                if(result.IsSuccess()){
                                    auto native=std::make_shared<android::NvGraphicBuffer>();
                                    native->width=static_cast<s32>(width);
                                    native->height=static_cast<s32>(height);
                                    native->stride=static_cast<s32>(pref[5]*16);
                                    native->format=android::PixelFormat::Rgba8888;
                                    native->external_format=native->format;
                                    native->buffer_id=img->buffer_id;native->offset=img->offset;
                                    auto buffer=std::make_shared<android::GraphicBuffer>(
                                        nv.GetNvMapFile(),native);
                                    s32 idx{};result=Kernel::ResultOutOfResource;
                                    if(c->producer->AttachBuffer(&idx,buffer)==android::Status::NoError){
                                        android::QueueBufferInput input{};
                                        android::QueueBufferOutput output{};
                                        input.crop={0,0,static_cast<s32>(outw),
                                            static_cast<s32>(outh)};
                                        input.swap_interval=1;input.fence=android::Fence::NoFence();
                                        if(fence!=~u32{0}){
                                            input.fence.num_fences=1;
                                            input.fence.fences[0]={static_cast<s32>(fence),p[24]};
                                        }
                                        if(c->producer->QueueBuffer(idx,input,&output)==
                                            android::Status::NoError){
                                            c->queued[slot]=idx;c->images[slot]=p[5];
                                            c->queued_modules[slot]=img->module;
                                            c->retained[slot]=buffer;
                                            c->available&=~(u64{1}<<slot);result=ResultSuccess;
                                            if(!m_poll){
                                                auto weak=std::weak_ptr<IVi2Session>(
                                                    std::static_pointer_cast<IVi2Session>(
                                                        shared_from_this()));
                                                m_poll=Core::Timing::CreateEvent(
                                                    "NativeVi2Release",
                                                    [weak](s64,std::chrono::nanoseconds)
                                                        ->std::optional<std::chrono::nanoseconds>{
                                                        if(auto self=weak.lock()){
                                                            const auto guard=self->LockService();
                                                            self->Release();
                                                        }
                                                        return std::nullopt;
                                                    });
                                                system.CoreTiming().ScheduleLoopingEvent(
                                                    std::chrono::milliseconds{1},
                                                    std::chrono::milliseconds{1},m_poll);
                                            }
                                        }else{
                                            c->producer->CancelBuffer(idx,input.fence);
                                            result=Kernel::ResultInvalidState;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                mask=c->available;
            }
        }
        IPC::ResponseBuilder rb{ctx,8};rb.Push(result.raw);rb.Push(mask);
        for(u32 i=0;i<4;++i)rb.Push<u32>(0);
    }
    void Cancel(HLERequestContext& ctx) {
        const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
        u64 mask{};
        if(p[0]==0xf4&&p[1]==19){
            Client* c=Find(W(p,2));result=Kernel::ResultNotFound;
            if(c){
                const u32 slot=GraphicsSupervisorProtocol::DecodeBufferSlot(p[4]);
                result=Kernel::ResultInvalidState;
                if(slot<15&&!c->queued[slot]){c->available|=u64{1}<<slot;result=ResultSuccess;}
                mask=c->available;
            }
        }
        IPC::ResponseBuilder rb{ctx,8};rb.Push(result.raw);rb.Push(mask);
        for(u32 i=0;i<4;++i)rb.Push<u32>(0);
    }
    std::shared_ptr<IGraphicsSupervisor> m_owner;
    u64 m_aruid{};
    std::array<Client,16> m_clients{};
    u32 m_slots{};
    std::shared_ptr<VI::IApplicationDisplayService> m_vi;
    std::optional<u64> m_display;
    std::optional<u64> m_endpoint;
    u32 m_refs{};
    std::array<u32,32> m_notes{};
    KernelHelpers::ServiceContext m_context;
    Kernel::KEvent* m_event{};
    std::shared_ptr<Core::Timing::EventType> m_poll;
};

IGraphicsSupervisor::IGraphicsSupervisor(Core::System& system_, Kernel::KProcess* process,
                                         u64 aruid)
    : ServiceFramework{system_, "IGraphicsSupervisor"}, m_process{process},
      m_bootstrap{aruid}, m_aruid{aruid} {
    m_process->Open(kernel);
    static const FunctionInfo functions[] = {
        {4, &IGraphicsSupervisor::Initialize, "Initialize"},
        {11, &IGraphicsSupervisor::BindProcess, "BindProcess"},
        {101, &IGraphicsSupervisor::CompleteBootstrap, "CompleteBootstrap"},
        {401, &IGraphicsSupervisor::InitializeGpuContext, "InitializeGpuContext"},
        {402, &IGraphicsSupervisor::InitializeGpuContext, "ActivateGpuContext"},
        {434, &IGraphicsSupervisor::RegisterGpuAddressSpace, "RegisterGpuAddressSpace"},
        {497, &IGraphicsSupervisor::OpenGpuSession, "OpenGpuSession"},
        {301, &IGraphicsSupervisor::InitializeDisplayContext, "InitializeDisplayContext"},
        {303, &IGraphicsSupervisor::FinalizeDisplayContext, "FinalizeDisplayContext"},
        {305, &IGraphicsSupervisor::OpenDisplaySession, "OpenDisplaySession"},
        {111, &IGraphicsSupervisor::CreateNativeMemory, "CreateNativeMemory"},
        {112, &IGraphicsSupervisor::DestroyNativeMemory, "DestroyNativeMemory"},
        {114, &IGraphicsSupervisor::BindNativeMemory, "BindNativeMemory"},
        {423, &IGraphicsSupervisor::BindNativeGpuMemory, "BindNativeGpuMemory"},
        {429, &IGraphicsSupervisor::CreateNativeImage, "CreateNativeImage"},
        {430, &IGraphicsSupervisor::DestroyNativeImage, "DestroyNativeImage"},
        {314, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "DestroyNativeWindowAliasImagePool"},
        {315, &IGraphicsSupervisor::ConfigureNativeWindowImagePool, "CreateNativeWindowAliasImagePool"},
        {911, &IGraphicsSupervisor::CreateVideoImagePool, "CreateVideoImagePool"},
        {912, &IGraphicsSupervisor::DestroyVideoImagePool, "DestroyVideoImagePool"},
        {426, &IGraphicsSupervisor::MapVideoGpuImage, "MapNativeGpuImage"},
        {427, &IGraphicsSupervisor::UnmapVideoGpuImage, "UnmapNativeGpuImage"},
        {428, &IGraphicsSupervisor::QueryVideoGpuImage, "QueryNativeGpuImage"},
        {435, &IGraphicsSupervisor::CalculateNativeImageSize, "CalculateNativeImageSize"},
        {1510, &IGraphicsSupervisor::CalculateNativeImageRequirements, "CalculateRequirements"},
    };
    RegisterHandlersTipc(functions);
}
IGraphicsSupervisor::~IGraphicsSupervisor() {
    // Release the real NVMap backing and page-table locks before the native
    // NVDRV session and the application KProcess lose their final owner.
    const std::scoped_lock lock{m_native_memory_mutex};
    if (m_native_module) {
        auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
        // The GMMU VA owner outlives the mapping until it is retired.
        for (const auto& [key, mapping] : m_video_mappings) {
            if (mapping.address_space)
                mapping.address_space->UnmapNative4K(mapping.address, mapping.size);
        }
        m_video_mappings.clear();
        for (const auto& [first, pool] : m_video_pools.Records()) {
            nvmap.UnpinHandle(pool.backing);
            nvmap.FreeHandle(pool.backing, true);
        }
        // Retire internal image duplicates before their underlying NVMap
        // allocations; otherwise the backing remains busy at shutdown.
        for (const auto& [image_id, image] : m_native_images.Records())
            nvmap.FreeHandle(image.backing, true);
        // Release unborrowed children before dropping their parent's pin.
        // Retry parents after child release: a parent may itself be an owned
        // native-memory allocation that was busy on the first pass.
        std::vector<u32> freed;
        const auto release = [&](u32 id) {
            auto handle = nvmap.GetHandle(id);
            if (!handle) return;
            {
                const std::scoped_lock handle_lock{handle->mutex};
                if (handle->dupes != 1 || handle->internal_dupes != 0 ||
                    handle->pins != 0) return;
            }
            handle.reset();
            const auto info = nvmap.FreeHandle(id, false);
            if (!info || !info->can_unlock) return;
            freed.push_back(id);
            if (info->address != 0) {
                const auto status = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                    info->address, info->size);
                if (status.IsError())
                    LOG_ERROR(Service_AM, "NS2 native supervisor teardown unlock failed: {:#x}",
                              status.raw);
            }
        };
        const auto ids = m_native_memory.OwnedIds();
        for (const u32 id : ids) release(id);
        for (const auto& [child, parent] : m_native_parents) {
            if (std::find(freed.begin(), freed.end(), child) == freed.end()) {
                // A live external child retains its parent pin. Never revoke
                // another GPU owner just to make shutdown appear successful.
                continue;
            }
            nvmap.UnpinHandle(parent);
            nvmap.FreeHandle(parent, true);
        }
        for (const u32 id : ids) {
            if (std::find(freed.begin(), freed.end(), id) == freed.end())
                release(id);
        }
        if (m_native_memory_session)
            m_native_module->GetContainer().CloseSession(
                Nvidia::NvCore::SessionId{*m_native_memory_session});
    }
    m_process->Close(kernel);
}
Result IGraphicsSupervisor::HandleSyncRequest(Kernel::KServerSession& session,
                                               HLERequestContext& ctx) {
    if (!ctx.IsTipc() || handlers_tipc.contains(ctx.GetCommand()))
        return ServiceFrameworkBase::HandleSyncRequest(session, ctx);
    const auto guard = LockService();
    InvokeRequestTipc(ctx);
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(Kernel::ResultNotImplemented.raw);
    return system.IsPoweredOn() ? ctx.WriteToOutgoingCommandBuffer() : ResultSuccess;
}
void IGraphicsSupervisor::Initialize(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    const auto request = GraphicsSupervisorProtocol::DecodeBootstrap(packet);
    bool matches = false;
    if (request) {
        const auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(request->process_handle);
        matches = !process.IsNull() && process.GetPointerUnsafe() == m_process;
    }
    const Result result = TranslateBootstrap(m_bootstrap.Initialize(packet, matches));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
void IGraphicsSupervisor::BindProcess(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    bool matches = false;
    if (packet[0] == 0x1b && packet[1] == 0x80000000 && packet[2] == 2) {
        const auto process = ctx.GetObjectFromHandle<Kernel::KProcess>(ctx.GetCopyHandle(0));
        matches = !process.IsNull() && process.GetPointerUnsafe() == m_process;
    }
    const Result result = TranslateBootstrap(m_bootstrap.BindProcess(packet, matches));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
void IGraphicsSupervisor::CompleteBootstrap(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    const Result result = TranslateBootstrap(m_bootstrap.Complete(packet));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}
void IGraphicsSupervisor::InitializeGpuContext(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    const Result result = TranslateBootstrap(
        m_bootstrap.InitializeGpuContext(packet, ctx.GetCommand()));
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::RegisterGpuAddressSpace(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const std::array<u32, 3> command{packet[0], packet[1], packet[2]};
    const auto id = GpuSessionProtocol::DecodeRegisterAddressSpace(command);
    Result result = Kernel::ResultInvalidCombination;
    if (id) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            result = Kernel::ResultNotFound;
            std::erase_if(m_gpu_sessions, [](const auto& weak) { return weak.expired(); });
            for (const auto& weak : m_gpu_sessions) {
                if (auto handler = weak.lock()) {
                    result = handler->RegisterAddressSpace(*id);
                    if (result.IsSuccess()) break;
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

std::optional<IGraphicsSupervisor::PresentImage>
IGraphicsSupervisor::GetPresentImage(u32 id) {
    const std::scoped_lock lock{m_native_memory_mutex};
    const auto image=m_native_images.Get(id);
    if(!image||!image->window_pool||!m_native_module)return {};
    auto& container=m_native_module->GetContainer();
    const auto handle=container.GetNvMapFile().GetHandle(image->backing);
    if(!handle||!handle->allocated)return {};
    const auto* session=container.GetSession(handle->session_id);
    if(!session||session->process!=m_process)return {};
    u32 backing=image->backing,offset{};
    if(const auto it=m_native_parents.find(backing);it!=m_native_parents.end()){
        const auto parent=container.GetNvMapFile().GetHandle(it->second);
        if(!parent||!parent->allocated||handle->address<parent->address||
            handle->address-parent->address>std::numeric_limits<u32>::max())return {};
        offset=static_cast<u32>(handle->address-parent->address);
        backing=it->second;
    }
    return PresentImage{image->preference,backing,offset,m_native_module};
}
void IGraphicsSupervisor::InitializeDisplayContext(HLERequestContext& ctx) {
    const auto* p=ctx.CommandBuffer();
    Result result=Kernel::ResultInvalidCombination;
    if(p[0]==0x13d&&p[1]==0)
        result=m_bootstrap.CurrentPhase()==GB::Phase::Ready
            ? ResultSuccess:Kernel::ResultInvalidState;
    if(result.IsSuccess())m_display_ready=true;
    IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
}
void IGraphicsSupervisor::FinalizeDisplayContext(HLERequestContext& ctx) {
    const auto* p=ctx.CommandBuffer();
    const Result result=p[0]==0x13f&&p[1]==0
        ? ResultSuccess:Kernel::ResultInvalidCombination;
    if(result.IsSuccess())m_display_ready=false;
    IPC::ResponseBuilder rb{ctx,2};rb.Push(result.raw);
}
void IGraphicsSupervisor::OpenDisplaySession(HLERequestContext& ctx) {
    const auto* p=ctx.CommandBuffer();Result result=Kernel::ResultInvalidCombination;
    Kernel::KClientSession* client{};
    if(p[0]==0x141&&p[1]==2&&
        GraphicsSupervisorProtocol::IsSupportedDisplaySession(
            u64{p[2]}|(u64{p[3]}<<32))){
        result=Kernel::ResultInvalidState;
        if(m_display_ready){
            auto owner=std::static_pointer_cast<IGraphicsSupervisor>(shared_from_this());
            auto handler=std::make_shared<IVi2Session>(system,std::move(owner),m_aruid);
            result=CreateMetadataSession(system,ctx,std::move(handler),&client);
        }
    }
    IPC::ResponseBuilder rb{ctx,2,0,1};rb.Push(result.raw);
    ctx.AddMoveObject(client);
}
void IGraphicsSupervisor::OpenGpuSession(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), IPC::COMMAND_BUFFER_LENGTH};
    // Only actual NVDRV service modules can back address-space FD creation.
    const auto source = system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
    const auto module = source ? source->GetModule() : nullptr;
    Result result = TranslateBootstrap(m_bootstrap.CanOpenGpuSession(packet, module != nullptr));
    Kernel::KClientSession* client{};
    if (result.IsSuccess()) {
        auto handler = std::make_shared<IGpuSession>(system, module, m_process);
        result = CreateMetadataSession(system, ctx, handler, &client);
        if (result.IsSuccess()) {
            std::erase_if(m_gpu_sessions, [](const auto& weak) { return weak.expired(); });
            m_gpu_sessions.emplace_back(handler);
        }
    }
    IPC::ResponseBuilder rb{ctx, 2, 0, 1};
    rb.Push(result.raw);
    ctx.AddMoveObject(client);
}

static Result NativeMemoryError(GraphicsSupervisorProtocol::NativeMemoryStatus status) {
    using S = GraphicsSupervisorProtocol::NativeMemoryStatus;
    switch (status) {
    case S::Success:       return ResultSuccess;
    case S::InvalidSize:   return Kernel::ResultInvalidSize;
    case S::InvalidAddress:return Kernel::ResultInvalidAddress;
    case S::InvalidHandle: return Kernel::ResultInvalidHandle;
    case S::AlreadyBound:  return Kernel::ResultInvalidState;
    case S::OutOfResource: return Kernel::ResultOutOfResource;
    }
    return Kernel::ResultNotImplemented;
}
static u64 NativeU64(const u32* packet, size_t word) {
    return u64{packet[word]} | (u64{packet[word + 1]} << 32);
}
void IGraphicsSupervisor::CreateNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    if (packet[0] == 0x7f && packet[1] == 3 && packet[2] <= 3) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            const u64 size = NativeU64(packet, 3);
            result = NativeMemoryError(
                size == 0 || size > GraphicsSupervisorProtocol::NativeMemoryLedger::MaxGuestMemory ||
                (size & (Kernel::PageSize - 1))
                    ? GraphicsSupervisorProtocol::NativeMemoryStatus::InvalidSize
                    : GraphicsSupervisorProtocol::NativeMemoryStatus::Success);
            if (result.IsSuccess()) {
                const std::scoped_lock lock{m_native_memory_mutex};
                result = Kernel::ResultOutOfResource;
                if (m_native_memory.Count() <
                    GraphicsSupervisorProtocol::NativeMemoryLedger::MaxEntries) {
                    if (!m_native_module) {
                        const auto service =
                            system.ServiceManager().GetService<Nvidia::NVDRV>("nvdrv:s", true);
                        m_native_module = service ? service->GetModule() : nullptr;
                    }
                    if (m_native_module) {
                        auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                        std::shared_ptr<Nvidia::NvCore::NvMap::Handle> handle;
                        if (nvmap.CreateHandle(size, handle) == Nvidia::NvResult::Success) {
                            id = handle->id;
                            const auto status = m_native_memory.Adopt(id, size);
                            if (status == GraphicsSupervisorProtocol::NativeMemoryStatus::Success) {
                                result = ResultSuccess;
                            } else {
                                handle.reset();
                                nvmap.FreeHandle(id, false);
                                id = 0;
                                result = NativeMemoryError(status);
                            }
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGraphicsSupervisor::DestroyNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x80 && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        const auto record = m_native_memory.Get(packet[2]);
        result = Kernel::ResultNotFound;
        if (record && m_native_module) {
            auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
            auto handle = nvmap.GetHandle(packet[2]);
            result = Kernel::ResultInvalidState;
            if (handle) {
                bool busy{};
                {
                    const std::scoped_lock handle_lock{handle->mutex};
                    busy = handle->dupes != 1 || handle->internal_dupes != 0 ||
                           handle->pins != 0;
                }
                result = Kernel::ResultBusy;
                if (!busy) {
                    handle.reset(); // FreeHandle's weak lifetime determines can_unlock.
                    const auto freed = nvmap.FreeHandle(packet[2], false);
                    result = Kernel::ResultInvalidState;
                    if (freed && freed->can_unlock) {
                        const auto removed = m_native_memory.Destroy(packet[2]);
                        result = NativeMemoryError(removed);
                        if (result.IsSuccess() && record->bound) {
                            result = m_process->GetPageTable().UnlockForDeviceAddressSpace(
                                freed->address, freed->size);
                        }
                        // Parent pin/duplicate are retained only while the
                        // child's NVMap and its page-table lock remain alive.
                        if (const auto parent = m_native_parents.find(packet[2]);
                            parent != m_native_parents.end()) {
                            nvmap.UnpinHandle(parent->second);
                            nvmap.FreeHandle(parent->second, true);
                            m_native_parents.erase(parent);
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

Result IGraphicsSupervisor::BindNativeMemoryBacking(
    u32 id, u64 address, u64 size, u64 flags, u64 offset) {
    if (!m_bootstrap.GpuContextReady()) return Kernel::ResultInvalidState;
    if ((flags != 0 && flags != 2) || offset != 0)
        return Kernel::ResultNotImplemented;
    const auto record = m_native_memory.Get(id);
    const auto validated = m_native_memory.ValidateBind(id, address, size);
    if (validated != GraphicsSupervisorProtocol::NativeMemoryStatus::Success)
        return NativeMemoryError(validated);
    if (!record || record->size != size || address == 0)
        return Kernel::ResultInvalidSize;
    if (!m_native_module) return Kernel::ResultInvalidState;

    auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
    const auto handle = nvmap.GetHandle(id);
    if (!handle || handle->allocated) return Kernel::ResultInvalidState;
    auto& table = m_process->GetPageTable();
    if (!table.Contains(address, size)) return Kernel::ResultInvalidCurrentMemory;
    const u64 end = address + size; // Bounds checked by the owned ledger.
    for (u64 cursor = address; cursor < end;) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page{};
        const Result query = table.QueryInfo(&info, &page, cursor);
        if (query.IsError() || info.m_size == 0 ||
            info.m_address > cursor ||
            info.m_size > std::numeric_limits<u64>::max() - info.m_address ||
            (info.m_state & Kernel::KMemoryState::FlagMapped) !=
                Kernel::KMemoryState::FlagMapped ||
            (flags == 2 &&
             !True(info.m_attribute & Kernel::KMemoryAttribute::GpuAccessible)))
            return Kernel::ResultInvalidCurrentMemory;
        const u64 next = info.m_address + info.m_size;
        if (next <= cursor) return Kernel::ResultInvalidCurrentMemory;
        cursor = next;
    }

    bool is_io{};
    const Result locked = table.LockForMapDeviceAddressSpace(
        &is_io, address, size, Kernel::KMemoryPermission::None, true, true);
    if (locked.IsError()) return locked;
    if (is_io) {
        table.UnlockForDeviceAddressSpace(address, size);
        return Kernel::ResultNotImplemented;
    }
    if (!m_native_memory_session)
        m_native_memory_session = m_native_module->GetContainer().OpenSession(m_process).id;
    const auto alloc = handle->Alloc(
        {}, Kernel::PageSize, 0, address,
        Nvidia::NvCore::SessionId{*m_native_memory_session});
    if (alloc != Nvidia::NvResult::Success) {
        table.UnlockForDeviceAddressSpace(address, size);
        return Kernel::ResultOutOfResource;
    }
    return NativeMemoryError(m_native_memory.Bind(id, address, size));
}

void IGraphicsSupervisor::BindNativeMemory(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x82 && packet[1] == 9) {
        const u32 id = packet[2];
        const u64 address = NativeU64(packet, 3);
        const u64 size = NativeU64(packet, 5);
        const u64 flags = NativeU64(packet, 7);
        const u64 offset = NativeU64(packet, 9);
        const std::scoped_lock lock{m_native_memory_mutex};
        result = BindNativeMemoryBacking(id, address, size, flags, offset);
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::BindNativeGpuMemory(HLERequestContext& ctx) {
    const std::span<const u32> packet{ctx.CommandBuffer(), 14};
    Result result = Kernel::ResultInvalidCombination;
    if (const auto request =
            GraphicsSupervisorProtocol::DecodeNativeParentBind(packet)) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        if (m_native_module && m_native_memory.Get(request->child) &&
            !m_native_parents.contains(request->child)) {
            auto& container = m_native_module->GetContainer();
            auto& nvmap = container.GetNvMapFile();
            const auto parent = nvmap.GetHandle(request->parent);
            if (parent && parent->allocated) {
                const auto* session = container.GetSession(parent->session_id);
                const u64 offset = request->parent_offset;
                if (session && session->process == m_process &&
                    offset <= parent->size && request->size <= parent->size - offset &&
                    offset <= std::numeric_limits<u64>::max() - parent->address &&
                    parent->address + offset == request->address) {
                    result = Kernel::ResultOutOfResource;
                    if (parent->Duplicate(true) == Nvidia::NvResult::Success) {
                        // Pin the real parent through NVDRV before committing
                        // the child. This is not a fabricated GMMU mapping.
                        const auto pinned = nvmap.PinHandle(request->parent, false);
                        if (pinned != 0) {
                            result = BindNativeMemoryBacking(request->child,
                                                             request->address,
                                                             request->size, 2, 0);
                            if (result.IsSuccess()) {
                                m_native_parents.emplace(request->child, request->parent);
                            } else {
                                nvmap.UnpinHandle(request->parent);
                            }
                        }
                        if (result.IsError())
                            nvmap.FreeHandle(request->parent, true);
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

std::shared_ptr<Nvidia::Devices::nvhost_as_gpu>
IGraphicsSupervisor::FindRegisteredGpuAddressSpace(u32 fd) {
    // The process identity is enforced by IGpuSession's registered FD owner.
    // A numeric FD alone must never grant access to another KProcess GMMU.
    for (const auto& weak : m_gpu_sessions) {
        if (const auto session = weak.lock()) {
            if (auto as = session->GetRegisteredAddressSpace(fd))
                return as;
        }
    }
    return {};
}

void IGraphicsSupervisor::CreateVideoImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 first{};
    if (packet[0] == 0x39f && packet[1] == 18) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            const std::span<const u32> preference{packet + 2, 16};
            const u32 count = packet[18];
            const auto layout = CalculateNativeVideoImageLayout(preference, count);
            result = Kernel::ResultNotImplemented;
            if (layout && (layout->image_bytes & 0xfffU) == 0 &&
                (layout->image_stride & 0xfffU) == 0) {
                const std::scoped_lock lock{m_native_memory_mutex};
                result = Kernel::ResultNotFound;
                const u32 backing = packet[19];
                const auto owned = m_native_memory.Get(backing);
                if (owned && owned->bound && m_native_module) {
                    auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                    const auto handle = nvmap.GetHandle(backing);
                    result = Kernel::ResultInvalidSize;
                    if (handle && handle->allocated && handle->size >= layout->total_bytes) {
                        const auto* session =
                            m_native_module->GetContainer().GetSession(handle->session_id);
                        result = Kernel::ResultInvalidHandle;
                        if (session && session->process == m_process) {
                            result = Kernel::ResultOutOfResource;
                            if (m_next_image_id &&
                                m_next_image_id + count - 1 <=
                                    std::numeric_limits<u32>::max() &&
                                m_video_pools.ImageCount() <=
                                    NativeVideoPoolLedger::MaxImages - count &&
                                handle->Duplicate(true) == Nvidia::NvResult::Success) {
                                const DAddr pinned = nvmap.PinHandle(backing, false);
                                if (pinned) {
                                    if (m_video_pools.Add(static_cast<u32>(m_next_image_id),
                                                          count, backing, handle->size,
                                                          preference[2], *layout)) {
                                        first = static_cast<u32>(m_next_image_id);
                                        m_next_image_id += count;
                                        result = ResultSuccess;
                                    } else {
                                        nvmap.UnpinHandle(backing);
                                    }
                                }
                                if (result.IsError()) nvmap.FreeHandle(backing, true);
                            }
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(first);
}

void IGraphicsSupervisor::DestroyVideoImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x3a0 && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        const auto pool = m_video_pools.Get(packet[2]);
        if (pool && m_native_module) {
            result = Kernel::ResultBusy;
            bool mapped = false;
            for (const auto& [key, mapping] : m_video_mappings) {
                const u32 image = key.second;
                if (image >= pool->first_id &&
                    image - pool->first_id < pool->count) {
                    mapped = true;
                    break;
                }
            }
            if (!mapped) {
                auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                nvmap.UnpinHandle(pool->backing);
                const auto freed = nvmap.FreeHandle(pool->backing, true);
                result = freed && m_video_pools.Remove(pool->first_id)
                    ? ResultSuccess : Kernel::ResultInvalidState;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::QueryVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 16> descriptor{};
    if (packet[0] == 0x1bc && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        if (const auto image = m_video_pools.Lookup(packet[2])) {
            descriptor[0] = 1;
            descriptor[1] = image->packed_dimensions;
            descriptor[4] = 0x101000001ULL;
            descriptor[5] = image->offset | (image->size << 32);
            result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 39};
    rb.Push(result.raw);
    rb.Push<u64>(0);
    rb.Push<u64>(0);
    for (const u64 field : descriptor) rb.Push(field);
    rb.Push<u32>(0);
}

void IGraphicsSupervisor::MapVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    std::array<u64, 16> descriptor{};
    u64 address{}, size{};
    if (packet[0] == 0x1ba && packet[1] == 4 && packet[3] == 1 &&
        ((packet[5] & 0xff) == 1 || (packet[5] & 0xff) == 3)) {
        result = Kernel::ResultNotFound;
        if (const auto as = FindRegisteredGpuAddressSpace(packet[4])) {
            const std::scoped_lock lock{m_native_memory_mutex};
            if (const auto image = m_video_pools.Lookup(packet[2])) {
                descriptor[0] = 1;
                descriptor[1] = image->packed_dimensions;
                descriptor[4] = 0x101000001ULL;
                descriptor[5] = image->offset | (image->size << 32);
                size = image->size;
                const auto key = std::make_pair(packet[4], packet[2]);
                if (auto it = m_video_mappings.find(key); it != m_video_mappings.end()) {
                    // A recycled numeric FD is not proof of the same AS/GMMU.
                    result = Kernel::ResultInvalidState;
                    if (it->second.address_space == as) {
                        result = Kernel::ResultOutOfResource;
                        if (it->second.references != std::numeric_limits<u32>::max()) {
                            ++it->second.references;
                            address = it->second.address;
                            result = ResultSuccess;
                        }
                    }
                } else {
                    result = Kernel::ResultOutOfResource;
                    if (as->MapNativeImage(image->backing, image->offset,
                                           image->size, address) == Nvidia::NvResult::Success) {
                        m_video_mappings.emplace(key,
                            VideoImageMapping{as, address, image->size, 1});
                        result = ResultSuccess;
                    }
                }
                if (result.IsSuccess()) descriptor[5] &= 0xffffffff00000000ULL;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 39};
    rb.Push(result.raw);
    rb.Push(address);
    rb.Push(size ? address + size - 1 : 0);
    for (const u64 field : descriptor) rb.Push(field);
    rb.Push<u32>(0);
}

void IGraphicsSupervisor::UnmapVideoGpuImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1bb && packet[1] == 6) {
        const auto as = FindRegisteredGpuAddressSpace(packet[3]);
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        const auto key = std::make_pair(packet[3], packet[2]);
        const auto it = m_video_mappings.find(key);
        const u64 address = NativeU64(packet, 4);
        const u64 end = NativeU64(packet, 6);
        if (as && it != m_video_mappings.end() &&
            it->second.address_space == as && it->second.address == address &&
            it->second.size > 0 && end >= address &&
            end - address == it->second.size - 1) {
            result = ResultSuccess;
            if (it->second.references == 1) {
                result = it->second.address_space->UnmapNative4K(
                    address, it->second.size) == Nvidia::NvResult::Success
                    ? ResultSuccess : Kernel::ResultInvalidState;
                if (result.IsSuccess()) m_video_mappings.erase(it);
            } else {
                --it->second.references;
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

// Image IDs are real logical resources: each holds one NVMap internal
// duplicate on a previously bound guest allocation. Creating an ID does
// not claim that a host Vulkan image has been instantiated.
void IGraphicsSupervisor::CreateNativeImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    u32 id{};
    if (packet[0] == 0x1bd && packet[1] == 18 && packet[3] == 1) {
        result = Kernel::ResultInvalidState;
        if (m_bootstrap.GpuContextReady()) {
            const std::span<const u32> preferences{packet + 2, 16};
            const auto requirements = ::Service::AM::CalculateNativeImageRequirements(preferences, packet[18]);
            result = Kernel::ResultNotImplemented;
            if (requirements) {
                const std::scoped_lock lock{m_native_memory_mutex};
                result = Kernel::ResultNotFound;
                const u32 backing = packet[19];
                const auto owned = m_native_memory.Get(backing);
                if (owned && owned->bound && m_native_module) {
                    auto& nvmap = m_native_module->GetContainer().GetNvMapFile();
                    const auto handle = nvmap.GetHandle(backing);
                    result = Kernel::ResultInvalidSize;
                    if (handle && handle->allocated && handle->size >= requirements->size) {
                        result = Kernel::ResultOutOfResource;
                        if (m_native_images.Count() < NativeImageLedger::MaxEntries &&
                            m_next_image_id <= std::numeric_limits<u32>::max() &&
                            handle->Duplicate(true) == Nvidia::NvResult::Success) {
                            if (const auto created =
                                    m_native_images.Create(
                                        backing, handle->size, *requirements, preferences,
                                        static_cast<u32>(m_next_image_id))) {
                                id = *created;
                                ++m_next_image_id;
                                result = ResultSuccess;
                            } else {
                                nvmap.FreeHandle(backing, true);
                            }
                        }
                    }
                }
            }
        }
    }
    IPC::ResponseBuilder rb{ctx, 3};
    rb.Push(result.raw);
    rb.Push(id);
}

void IGraphicsSupervisor::ConfigureNativeWindowImagePool(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    const bool create = ctx.GetCommand() == 315;
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == (create ? 0x14bU : 0x14aU) &&
        packet[1] == (create ? 2U : 1U)) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        if (m_native_images.Get(packet[2])) {
            result = Kernel::ResultNotImplemented;
            if (!create || packet[3] == 1)
                result = m_native_images.ConfigureWindowPool(packet[2], create)
                    ? ResultSuccess : Kernel::ResultInvalidState;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

void IGraphicsSupervisor::DestroyNativeImage(HLERequestContext& ctx) {
    const auto* packet = ctx.CommandBuffer();
    Result result = Kernel::ResultInvalidCombination;
    if (packet[0] == 0x1be && packet[1] == 1) {
        const std::scoped_lock lock{m_native_memory_mutex};
        result = Kernel::ResultNotFound;
        const auto image = m_native_images.Get(packet[2]);
        if (image && m_native_module && !image->window_pool) {
            const auto freed = m_native_module->GetContainer().GetNvMapFile().FreeHandle(
                image->backing, true);
            result = Kernel::ResultInvalidState;
            if (freed && m_native_images.Destroy(packet[2]))
                result = ResultSuccess;
        }
    }
    IPC::ResponseBuilder rb{ctx, 2};
    rb.Push(result.raw);
}

static std::optional<NativeImageRequirements> ParseNativeImageRequest(HLERequestContext& ctx) {
    // Native image preferences are exactly sixteen u32 words followed by
    // a single image count. The maintained layout helper verifies known
    // image types, width, height, pitch, block alignment and multiplication.
    // Reject malformed TIPC packets rather than returning fabricated sizes.
    const u32* const data = ctx.CommandBuffer();
    if (data[1] != 17) return {};
    return CalculateNativeImageRequirements(std::span<const u32>{data + 2, 16}, data[18]);
}
void IGraphicsSupervisor::CalculateNativeImageSize(HLERequestContext& ctx) {
    const auto request = ParseNativeImageRequest(ctx);
    const Result result = request ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 4};
    rb.Push(result.raw);
    rb.Push(request ? static_cast<u32>(request->size) : 0u);
    rb.Push(request ? static_cast<u32>(request->size >> 32) : 0u);
}
void IGraphicsSupervisor::CalculateNativeImageRequirements(HLERequestContext& ctx) {
    const auto request = ParseNativeImageRequest(ctx);
    const Result result = request ? ResultSuccess : Kernel::ResultInvalidCombination;
    IPC::ResponseBuilder rb{ctx, 6};
    rb.Push(result.raw);
    rb.Push(request ? static_cast<u32>(request->size) : 0u);
    rb.Push(request ? static_cast<u32>(request->size >> 32) : 0u);
    rb.Push(request ? static_cast<u32>(request->alignment) : 0u);
    rb.Push(request ? static_cast<u32>(request->alignment >> 32) : 0u);
}
} // namespace Service::AM
