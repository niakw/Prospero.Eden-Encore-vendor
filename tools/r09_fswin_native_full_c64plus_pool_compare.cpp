// SPDX-License-Identifier: MIT
// Exact native RDNA2 FSwin full Dense0, Dense2, QKV, attention, Dense3 SPIR-V, PUBLIC
// deterministic FP8 inputs/weights. Four sequential C32 layers with a
// GPU-to-GPU handoff, each layer's QKV and Context copied to private
// snapshots. Compare independent versus shared expanded/context VkBuffers.
// Vulkan software device only; no Original model or PS5 claim.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
#ifndef R09_FSWIN_TEST_CHANNELS
#define R09_FSWIN_TEST_CHANNELS 64
#endif
constexpr unsigned C=R09_FSWIN_TEST_CHANNELS;
static_assert(C==64u||C==128u||C==256u);
constexpr unsigned HEADS=C/32u;
constexpr unsigned L=C>=128u?2u:4u;
constexpr VkDeviceSize IO=64u*C;
constexpr VkDeviceSize QKV=3u*IO;
constexpr VkDeviceSize EXP=4u*IO;
constexpr VkDeviceSize EXP_WEIGHTS=0u;
constexpr VkDeviceSize MIDDLE_WEIGHTS=4u*C*C;
constexpr VkDeviceSize CONTRACT_WEIGHTS=MIDDLE_WEIGHTS+C*128u;
constexpr VkDeviceSize QKV_WEIGHTS=CONTRACT_WEIGHTS+C*C;
constexpr VkDeviceSize FINAL_WEIGHTS=QKV_WEIGHTS+3u*C*C;
constexpr VkDeviceSize WEIGHTS=FINAL_WEIGHTS+C*C;
constexpr VkDeviceSize SCALES=C*4u;
constexpr VkDeviceSize BIAS=HEADS*64u*64u*4u;
constexpr VkDeviceSize ALL=QKV+IO;
struct QkvPush {
    std::uint32_t a_base{},w_base{},scale_base{},q_base{},k_base{},
                  v_base{},windows{1u},channels{C},heads{HEADS};
};
struct AttentionPush {
    std::uint32_t q_base{},k_base{},v_base{},out_base{},bias_base{},
                  windows{1u},heads{HEADS};
};
struct DensePush{
    std::uint32_t x_base{},w_base{},residual_base{},scale_base{},
                  out_base{},windows{1u},channels{C},stage{3u};
};
static_assert(sizeof(QkvPush)==36u&&sizeof(AttentionPush)==28u&&
              sizeof(DensePush)==32u);
void Must(VkResult code,const char* why){
    if(code!=VK_SUCCESS)throw std::runtime_error(
        std::string(why)+" Vulkan error="+std::to_string(code));
}
VKAPI_ATTR VkBool32 VKAPI_CALL OnError(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* detail,
    void* userdata) {
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){
        static_cast<std::atomic<unsigned>*>(userdata)->fetch_add(1u);
        std::cerr<<"R09_FSWIN_REAL_ATTENTION_KHRONOS_ERROR "
                 <<(detail&&detail->pMessage?detail->pMessage:"unknown")
                 <<"\n";
    }
    return VK_FALSE;
}
struct Buffer{VkBuffer buffer{};VkDeviceMemory memory{};VkDeviceSize size{};};
struct Pipeline{
    VkShaderModule shader{};
    VkDescriptorSetLayout descriptor_layout{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    std::vector<VkDescriptorSet> sets{};
};
struct Host {
    bool pooled{},validate{};
    VkInstance instance{};
    VkDebugUtilsMessengerEXT debug_messenger{};
    std::atomic<unsigned> errors{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties properties{};
    VkDevice device{};
    VkQueue queue{};
    unsigned family{};
    std::vector<Buffer> buffers{};
    VkDescriptorPool descriptor_pool{};
    Pipeline expand{},middle{},contract{},qkv{},attention{},dense{};
    VkCommandPool pool{};
    VkCommandBuffer commands{};
    VkFence fence{};
    explicit Host(bool mode):pooled{mode}{}
    ~Host(){
        if(device){
            vkDeviceWaitIdle(device);
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(pool)vkDestroyCommandPool(device,pool,nullptr);
            if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            for(auto* p:{&expand,&middle,&contract,&qkv,&attention,&dense}){
                if(p->pipeline)vkDestroyPipeline(device,p->pipeline,nullptr);
                if(p->layout)vkDestroyPipelineLayout(device,p->layout,nullptr);
                if(p->shader)vkDestroyShaderModule(device,p->shader,nullptr);
                if(p->descriptor_layout)vkDestroyDescriptorSetLayout(
                    device,p->descriptor_layout,nullptr);
            }
            for(const auto& b:buffers){
                if(b.buffer)vkDestroyBuffer(device,b.buffer,nullptr);
                if(b.memory)vkFreeMemory(device,b.memory,nullptr);
            }
            vkDestroyDevice(device,nullptr);
        }
        if(instance){
            if(debug_messenger){
                auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(
                        instance,"vkDestroyDebugUtilsMessengerEXT"));
                if(destroy)destroy(instance,debug_messenger,nullptr);
            }
            vkDestroyInstance(instance,nullptr);
        }
    }
    std::size_t In(unsigned layer)const{return 3u*layer;}
    std::size_t Out(unsigned layer)const{return 3u*layer+1u;}
    std::size_t Snapshot(unsigned layer)const{return 3u*layer+2u;}
    std::size_t Expanded(unsigned layer)const{
        return 3u*L+(pooled?0u:layer);
    }
    std::size_t Middle(unsigned layer)const{
        return 3u*L+(pooled?1u:L)+(pooled?0u:layer);
    }
    std::size_t Contracted(unsigned layer)const{
        return 3u*L+(pooled?2u:2u*L)+(pooled?0u:layer);
    }
    std::size_t Context(unsigned layer)const{
        return 3u*L+(pooled?3u:3u*L)+(pooled?0u:layer);
    }
    std::size_t Weights()const{return 3u*L+(pooled?4u:4u*L);}
    std::size_t Scales()const{return Weights()+1u;}
    std::size_t Bias()const{return Weights()+2u;}
    unsigned MemoryType(unsigned mask,VkMemoryPropertyFlags flags)const{
        for(unsigned i=0u;i<properties.memoryTypeCount;++i)
            if((mask&(1u<<i))&&
               (properties.memoryTypes[i].propertyFlags&flags)==flags)
                return i;
        throw std::runtime_error("No host coherent Vulkan memory");
    }
    void Init(){
        validate=std::getenv("R09_FSWIN_VALIDATE_SYNC")!=nullptr;
        VkApplicationInfo app{};
        app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion=VK_API_VERSION_1_1;
        app.pApplicationName="R09 authentic FSwin QKV/attention A/B";
        VkInstanceCreateInfo ici{};
        ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo=&app;
        const char* layer="VK_LAYER_KHRONOS_validation";
        const std::array<const char*,2u> ext{
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        VkValidationFeatureEnableEXT sync=
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT vf{};
        vf.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        vf.enabledValidationFeatureCount=1u;
        vf.pEnabledValidationFeatures=&sync;
        VkDebugUtilsMessengerCreateInfoEXT debug{};
        debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
                          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback=OnError;
        debug.pUserData=&errors;
        if(validate){
            vf.pNext=&debug;
            ici.pNext=&vf;
            ici.enabledLayerCount=1u;
            ici.ppEnabledLayerNames=&layer;
            ici.enabledExtensionCount=static_cast<unsigned>(ext.size());
            ici.ppEnabledExtensionNames=ext.data();
        }
        Must(vkCreateInstance(&ici,nullptr,&instance),"instance");
        if(validate){
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance,
                    "vkCreateDebugUtilsMessengerEXT"));
            if(!create)throw std::runtime_error("Missing Khronos messenger");
            Must(create(instance,&debug,nullptr,&debug_messenger),"debug");
        }
        unsigned count{};
        Must(vkEnumeratePhysicalDevices(instance,&count,nullptr),"device count");
        if(!count)throw std::runtime_error("No Vulkan physical GPU");
        std::vector<VkPhysicalDevice> available(count);
        Must(vkEnumeratePhysicalDevices(instance,&count,available.data()),
             "device enumerate");
        for(const auto gpu:available){
            unsigned groups{};
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&groups,nullptr);
            std::vector<VkQueueFamilyProperties> queues(groups);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&groups,queues.data());
            for(unsigned i=0u;i<groups;++i)
                if(queues[i].queueCount&&
                   (queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){
                    physical=gpu;family=i;break;
                }
            if(physical)break;
        }
        if(!physical)throw std::runtime_error("Compute queue unavailable");
        vkGetPhysicalDeviceMemoryProperties(physical,&properties);
        const float priority=1.0f;
        VkDeviceQueueCreateInfo qi{};
        qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex=family;
        qi.queueCount=1u;
        qi.pQueuePriorities=&priority;
        VkDeviceCreateInfo di{};
        di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount=1u;
        di.pQueueCreateInfos=&qi;
        Must(vkCreateDevice(physical,&di,nullptr,&device),"logical device");
        vkGetDeviceQueue(device,family,0u,&queue);
        buffers.resize(Bias()+1u);
        for(std::size_t id=0u;id<buffers.size();++id){
            auto& b=buffers[id];
            b.size=id<3u*L?
                (id%3u==2u?QKV:IO):
                id<Middle(0u)?EXP:
                id<Weights()?IO:
                id==Weights()?WEIGHTS:
                id==Scales()?SCALES:BIAS;
            VkBufferCreateInfo bi{};
            bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size=b.size;
            bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Must(vkCreateBuffer(device,&bi,nullptr,&b.buffer),"buffer");
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device,b.buffer,&req);
            VkMemoryAllocateInfo ai{};
            ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize=req.size;
            ai.memoryTypeIndex=MemoryType(
                req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Must(vkAllocateMemory(device,&ai,nullptr,&b.memory),"memory");
            Must(vkBindBufferMemory(device,b.buffer,b.memory,0u),
                 "bind memory");
        }
        for(std::size_t i=0u;i<buffers.size();++i)
            for(std::size_t j=0u;j<i;++j)
                if(buffers[i].buffer==buffers[j].buffer)
                    throw std::runtime_error("Physical VkBuffer alias unrelated");
        auto upload=[&](std::size_t id,const void* src,VkDeviceSize bytes){
            void* dst{};
            Must(vkMapMemory(device,buffers[id].memory,0u,bytes,0u,&dst),"map");
            std::memcpy(dst,src,static_cast<std::size_t>(bytes));
            vkUnmapMemory(device,buffers[id].memory);
        };
        std::array<std::uint8_t,IO> input{};
        constexpr std::array<std::uint8_t,8u> values{
            0x18u,0x20u,0x28u,0x30u,0x38u,0x40u,0x48u,0x50u};
        for(unsigned token=0u;token<64u;++token)
            for(unsigned feature=0u;feature<C;++feature)
                input[token*C+feature]=
                    values[(token*7u+feature*3u+token*feature)%values.size()];
        upload(In(0u),input.data(),IO);
        std::array<std::uint8_t,WEIGHTS> weights{};
        for(unsigned row=0u;row<4u*C;++row)
            weights[EXP_WEIGHTS+row*C+row%C]=0x38u;
        for(unsigned row=0u;row<C;++row)
            weights[MIDDLE_WEIGHTS+row*128u+row%128u]=0x38u;
        for(unsigned row=0u;row<C;++row)
            weights[CONTRACT_WEIGHTS+row*C+row]=0x38u;
        for(unsigned head=0u;head<HEADS;++head)
            for(unsigned part=0u;part<3u;++part)
                for(unsigned i=0u;i<32u;++i)
                    weights[QKV_WEIGHTS+
                            (head*96u+part*32u+i)*C+head*32u+i]=0x38u;
        for(unsigned i=0u;i<C;++i)
            weights[FINAL_WEIGHTS+i*C+i]=0x38u;
        upload(Weights(),weights.data(),WEIGHTS);
        std::array<float,C> scales{};
        scales.fill(0.25f);
        for(unsigned i=0u;i<HEADS;++i)scales[i]=1.0f;
        upload(Scales(),scales.data(),SCALES);
        std::array<float,HEADS*64u*64u> bias{};
        for(unsigned head=0u;head<HEADS;++head)
            for(unsigned query=0u;query<64u;++query)
                for(unsigned key=0u;key<64u;++key)
                    bias[(head*64u+query)*64u+key]=
                        0.06f*static_cast<float>(
                            int((head*5u+query*7u+key*11u)%19u)-9);
        upload(Bias(),bias.data(),BIAS);
    }
    void MakePipeline(Pipeline& target,
                      const std::string& file,unsigned binding_count,
                      unsigned push_bytes){
        std::ifstream in(file,std::ios::binary|std::ios::ate);
        if(!in)throw std::runtime_error("Missing native SPIR-V "+file);
        const std::streamoff bytes=in.tellg();
        if(bytes<=0||bytes%4u)
            throw std::runtime_error("Invalid native SPIR-V size");
        std::vector<std::uint32_t> code(static_cast<std::size_t>(bytes)/4u);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(code.data()),bytes);
        if(!in)throw std::runtime_error("Native SPIR-V read failed");
        VkShaderModuleCreateInfo mi{};
        mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        mi.codeSize=static_cast<std::size_t>(bytes);
        mi.pCode=code.data();
        Must(vkCreateShaderModule(device,&mi,nullptr,&target.shader),
             "shader module");
        std::vector<VkDescriptorSetLayoutBinding> binds(binding_count);
        for(unsigned i=0u;i<binding_count;++i){
            binds[i].binding=i;
            binds[i].descriptorCount=1u;
            binds[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binds[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount=binding_count;
        li.pBindings=binds.data();
        Must(vkCreateDescriptorSetLayout(
            device,&li,nullptr,&target.descriptor_layout),"set layout");
        std::vector<VkDescriptorSetLayout> set_layouts(
            L,target.descriptor_layout);
        target.sets.resize(L);
        VkDescriptorSetAllocateInfo si{};
        si.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        si.descriptorPool=descriptor_pool;
        si.descriptorSetCount=L;
        si.pSetLayouts=set_layouts.data();
        Must(vkAllocateDescriptorSets(device,&si,target.sets.data()),
             "set allocation");
        VkPushConstantRange push{};
        push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        push.size=push_bytes;
        VkPipelineLayoutCreateInfo pi{};
        pi.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pi.setLayoutCount=1u;
        pi.pSetLayouts=&target.descriptor_layout;
        pi.pushConstantRangeCount=1u;
        pi.pPushConstantRanges=&push;
        Must(vkCreatePipelineLayout(device,&pi,nullptr,&target.layout),
             "pipeline layout");
        VkComputePipelineCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module=target.shader;
        ci.stage.pName="main";
        ci.layout=target.layout;
        Must(vkCreateComputePipelines(device,VK_NULL_HANDLE,1u,
                                      &ci,nullptr,&target.pipeline),
             "compute pipeline");
    }
    void InitDescriptors(const std::string& qkv_spv,
                         const std::string& attention_spv,
                         const std::string& dense_spv){
        VkDescriptorPoolSize ps{};
        ps.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ps.descriptorCount=L*(5u+5u+5u+6u+5u+5u);
        VkDescriptorPoolCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.maxSets=6u*L;
        ci.poolSizeCount=1u;
        ci.pPoolSizes=&ps;
        Must(vkCreateDescriptorPool(device,&ci,nullptr,&descriptor_pool),
             "descriptor pool");
        MakePipeline(expand,dense_spv,5u,sizeof(DensePush));
        MakePipeline(middle,dense_spv,5u,sizeof(DensePush));
        MakePipeline(contract,dense_spv,5u,sizeof(DensePush));
        MakePipeline(qkv,qkv_spv,6u,sizeof(QkvPush));
        MakePipeline(attention,attention_spv,5u,sizeof(AttentionPush));
        MakePipeline(dense,dense_spv,5u,sizeof(DensePush));
        std::vector<VkDescriptorBufferInfo> e0info(L*5u),e1info(L*5u),
                                            e2info(L*5u),qinfo(L*6u),
                                            ainfo(L*5u),dinfo(L*5u);
        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(L*(5u+5u+5u+6u+5u+5u));
        const auto append=[&](VkDescriptorSet set,unsigned binding,
                              VkDescriptorBufferInfo* ptr){
            VkWriteDescriptorSet w{};
            w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=set;
            w.dstBinding=binding;
            w.descriptorCount=1u;
            w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w.pBufferInfo=ptr;
            writes.push_back(w);
        };
        for(unsigned i=0u;i<L;++i){
            const auto exp=Expanded(i),mid=Middle(i),
                       con=Contracted(i),ctx=Context(i);
            const std::array<VkDescriptorBufferInfo,5u> first{{
                {buffers[In(i)].buffer,0u,IO},
                {buffers[Weights()].buffer,0u,WEIGHTS},
                {buffers[In(i)].buffer,0u,IO},
                {buffers[Scales()].buffer,0u,SCALES},
                {buffers[exp].buffer,0u,EXP},
            }};
            const std::array<VkDescriptorBufferInfo,5u> grouped{{
                {buffers[exp].buffer,0u,EXP},
                {buffers[Weights()].buffer,0u,WEIGHTS},
                {buffers[In(i)].buffer,0u,IO},
                {buffers[Scales()].buffer,0u,SCALES},
                {buffers[mid].buffer,0u,IO},
            }};
            const std::array<VkDescriptorBufferInfo,5u> second{{
                {buffers[mid].buffer,0u,IO},
                {buffers[Weights()].buffer,0u,WEIGHTS},
                {buffers[In(i)].buffer,0u,IO},
                {buffers[Scales()].buffer,0u,SCALES},
                {buffers[con].buffer,0u,IO},
            }};
            for(unsigned b=0u;b<5u;++b){
                e0info[i*5u+b]=first[b];
                append(expand.sets[i],b,&e0info[i*5u+b]);
                e1info[i*5u+b]=grouped[b];
                append(middle.sets[i],b,&e1info[i*5u+b]);
                e2info[i*5u+b]=second[b];
                append(contract.sets[i],b,&e2info[i*5u+b]);
            }
            // 3 byte-aligned disjoint Q/K/V descriptor subranges into one
            // 4*IO expanded scratch, matching native Info indices 4/5/6.
            const std::array<VkDescriptorBufferInfo,6u> qb{{
                {buffers[con].buffer,0u,IO},
                {buffers[Weights()].buffer,0u,WEIGHTS},
                {buffers[Scales()].buffer,0u,SCALES},
                {buffers[exp].buffer,0u,IO},
                {buffers[exp].buffer,IO,IO},
                {buffers[exp].buffer,2u*IO,IO},
            }};
            const std::array<VkDescriptorBufferInfo,5u> ab{{
                {buffers[exp].buffer,0u,IO},
                {buffers[exp].buffer,IO,IO},
                {buffers[exp].buffer,2u*IO,IO},
                {buffers[Bias()].buffer,0u,BIAS},
                {buffers[ctx].buffer,0u,IO},
            }};
            for(unsigned b=0u;b<qb.size();++b){
                qinfo[i*6u+b]=qb[b];
                append(qkv.sets[i],b,&qinfo[i*6u+b]);
            }
            for(unsigned b=0u;b<ab.size();++b){
                ainfo[i*5u+b]=ab[b];
                append(attention.sets[i],b,&ainfo[i*5u+b]);
            }
            const std::array<VkDescriptorBufferInfo,5u> db{{
                {buffers[ctx].buffer,0u,IO},
                {buffers[Weights()].buffer,0u,WEIGHTS},
                {buffers[con].buffer,0u,IO},
                {buffers[Scales()].buffer,0u,SCALES},
                {buffers[Out(i)].buffer,0u,IO},
            }};
            for(unsigned b=0u;b<db.size();++b){
                dinfo[i*5u+b]=db[b];
                append(dense.sets[i],b,&dinfo[i*5u+b]);
            }
        }
        vkUpdateDescriptorSets(
            device,static_cast<unsigned>(writes.size()),writes.data(),
            0u,nullptr);
        VkCommandPoolCreateInfo pi{};
        pi.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pi.queueFamilyIndex=family;
        Must(vkCreateCommandPool(device,&pi,nullptr,&pool),"command pool");
        VkCommandBufferAllocateInfo ca{};
        ca.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ca.commandPool=pool;
        ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount=1u;
        Must(vkAllocateCommandBuffers(device,&ca,&commands),"command buffer");
    }
    void Record(){
        VkCommandBufferBeginInfo bi{};
        bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Must(vkBeginCommandBuffer(commands,&bi),"begin");
        VkMemoryBarrier host{};
        host.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
        host.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,1u,&host,0u,nullptr,0u,nullptr);
        for(unsigned layer=0u;layer<L;++layer){
            auto stage_dense=[&](const Pipeline& pass,
                                 unsigned stage,unsigned w_base){
                const auto& set=pass.sets[layer];
                vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                                  pass.pipeline);
                vkCmdBindDescriptorSets(commands,
                    VK_PIPELINE_BIND_POINT_COMPUTE,
                    pass.layout,0u,1u,&set,0u,nullptr);
                DensePush request{};
                request.stage=stage;
                request.channels=C;
                request.w_base=w_base;
                vkCmdPushConstants(commands,pass.layout,
                    VK_SHADER_STAGE_COMPUTE_BIT,
                    0u,sizeof(request),&request);
                vkCmdDispatch(commands,stage==0u?(C+63u)/64u:1u,
                              64u,1u);
                VkMemoryBarrier visible{};
                visible.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                visible.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                visible.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                     VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(commands,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0u,1u,&visible,0u,nullptr,0u,nullptr);
            };
            stage_dense(expand,0u,EXP_WEIGHTS);
            stage_dense(middle,1u,MIDDLE_WEIGHTS);
            stage_dense(contract,2u,CONTRACT_WEIGHTS);
            const auto& qset=qkv.sets[layer];
            vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                              qkv.pipeline);
            vkCmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                qkv.layout,0u,1u,&qset,0u,nullptr);
            QkvPush qp{};
            qp.w_base=QKV_WEIGHTS;
            qp.channels=C;
            qp.heads=HEADS;
            vkCmdPushConstants(commands,qkv.layout,VK_SHADER_STAGE_COMPUTE_BIT,
                0u,sizeof(qp),&qp);
            // Exactly as NrRdna2FSwinQkv::Record: windows,heads,64
            vkCmdDispatch(commands,1u,HEADS,64u);
            VkMemoryBarrier qvisible{};
            qvisible.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            qvisible.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            qvisible.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|
                                   VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commands,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT|
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,1u,&qvisible,0u,nullptr,0u,nullptr);
            VkBufferCopy qc{};qc.size=QKV;
            vkCmdCopyBuffer(commands,buffers[Expanded(layer)].buffer,
                buffers[Snapshot(layer)].buffer,1u,&qc);
            VkMemoryBarrier after_copy{};
            after_copy.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            after_copy.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            after_copy.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,1u,&after_copy,0u,nullptr,0u,nullptr);
            const auto& aset=attention.sets[layer];
            vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                              attention.pipeline);
            vkCmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                attention.layout,0u,1u,&aset,0u,nullptr);
            AttentionPush ap{};
            ap.heads=HEADS;
            vkCmdPushConstants(commands,attention.layout,
                VK_SHADER_STAGE_COMPUTE_BIT,0u,sizeof(ap),&ap);
            // Exactly as NrRdna2WindowAttention::Record: 1,heads,windows
            vkCmdDispatch(commands,1u,HEADS,1u);
            VkMemoryBarrier context_visible{};
            context_visible.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            context_visible.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            context_visible.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commands,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,1u,&context_visible,0u,nullptr,0u,nullptr);
            const auto& dset=dense.sets[layer];
            vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                              dense.pipeline);
            vkCmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                dense.layout,0u,1u,&dset,0u,nullptr);
            DensePush dp{};
            dp.w_base=FINAL_WEIGHTS;
            dp.channels=C;
            vkCmdPushConstants(commands,dense.layout,
                VK_SHADER_STAGE_COMPUTE_BIT,0u,sizeof(dp),&dp);
            // Actual production C32 stage-3 final projection dispatch.
            vkCmdDispatch(commands,1u,64u,1u);
            VkMemoryBarrier projected_visible{};
            projected_visible.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            projected_visible.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            projected_visible.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(commands,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0u,1u,&projected_visible,0u,nullptr,0u,nullptr);
            if(layer+1u<L){
                VkBufferCopy projected_copy{};
                projected_copy.size=IO;
                vkCmdCopyBuffer(commands,buffers[Out(layer)].buffer,
                    buffers[In(layer+1u)].buffer,1u,&projected_copy);
            }
            // Full per-layer retirement before any shared scratch is
            // overwritten by the next QKV/attention pass.
            VkMemoryBarrier retire{};
            retire.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            retire.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                 VK_ACCESS_SHADER_WRITE_BIT|
                                 VK_ACCESS_TRANSFER_WRITE_BIT|
                                 VK_ACCESS_TRANSFER_READ_BIT;
            retire.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                 VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(commands,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,1u,&retire,0u,nullptr,0u,nullptr);
        }
        VkMemoryBarrier readback{};
        readback.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        readback.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        readback.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,0u,1u,&readback,
            0u,nullptr,0u,nullptr);
        Must(vkEndCommandBuffer(commands),"end");
        VkFenceCreateInfo fi{};
        fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Must(vkCreateFence(device,&fi,nullptr,&fence),"fence");
        VkSubmitInfo submit{};
        submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount=1u;
        submit.pCommandBuffers=&commands;
        Must(vkQueueSubmit(queue,1u,&submit,fence),"submit");
        Must(vkWaitForFences(device,1u,&fence,VK_TRUE,10000000000ull),
             "wait fence");
    }
    std::vector<std::uint8_t> Read(){
        std::vector<std::uint8_t> result(L*static_cast<std::size_t>(ALL));
        for(unsigned layer=0u;layer<L;++layer){
            auto capture=[&](std::size_t index,std::size_t offset,
                             VkDeviceSize size){
                void* data{};
                Must(vkMapMemory(device,buffers[index].memory,
                                 0u,size,0u,&data),"readback map");
                std::memcpy(result.data()+layer*ALL+offset,
                            data,static_cast<std::size_t>(size));
                vkUnmapMemory(device,buffers[index].memory);
            };
            capture(Snapshot(layer),0u,QKV);
            capture(Out(layer),QKV,IO);
        }
        if(validate&&errors.load()!=0u)
            throw std::runtime_error("Khronos sync reported errors");
        return result;
    }
};
std::vector<std::uint8_t> Run(const std::string& qkv,
                              const std::string& att,
                              const std::string& dense_spv,bool pooled){
    Host h{pooled};h.Init();h.InitDescriptors(qkv,att,dense_spv);h.Record();
    return h.Read();
}
}
int main(int argc,char** argv){
    if(argc!=4){
        std::cerr<<"usage: r09_fswin_native_attention_pool_compare "
                    "<fswin_qkv.spv> <window_attention.spv> <fswin_dense.spv>\n";
        return 2;
    }
    try{
        const auto separate=Run(argv[1],argv[2],argv[3],false);
        const auto pooled=Run(argv[1],argv[2],argv[3],true);
        if(separate!=pooled)
            throw std::runtime_error(
                "Authentic FSwin QKV/attention GPU outputs differ under pool");
        unsigned nonzero{},varying{};
        for(const auto value:separate){
            nonzero+=value!=0u;
            varying+=value!=separate[0];
        }
        if(nonzero<separate.size()/2u||varying<separate.size()/8u)
            throw std::runtime_error(
                "Authentic QKV/attention test degenerate");
        std::cout<<"R09_FSWIN_FULL_C64PLUS_SIX_NATIVE_SHADERS_POOL_AB_PASS "
                 <<"layers="<<L<<" channels="<<C<<" windows=1"
                 <<" bytes_compared="<<separate.size()
                 <<" nonzero="<<nonzero<<" varying="<<varying
                 <<" private_scratch_roles_per_layer=4 pooled_scratch_roles_per_chain=4"
                 <<" trained_weights=NO PS5=NO\n";
        if(std::getenv("R09_FSWIN_VALIDATE_SYNC"))
            std::cout<<"R09_FSWIN_FULL_C64PLUS_SIX_NATIVE_SHADERS_KHRONOS_ZERO_ERRORS "
                     <<"private_and_pooled=YES\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"R09_FSWIN_REAL_QKV_ATTENTION_POOL_AB_FAIL "
                 <<error.what()<<"\n";
        return 1;
    }
}
