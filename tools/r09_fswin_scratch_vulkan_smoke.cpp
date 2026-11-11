// SPDX-License-Identifier: MIT
// Real Vulkan 1.1 HOST witness: two sequential layers borrow ONE physical
// scratch VkBuffer while all input/output VkBuffers remain private.
// The shader is synthetic/public only: no learned weights, no PS5 claim.
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr std::uint32_t kN=256u;
constexpr VkDeviceSize kBytes=kN*sizeof(std::uint32_t);
enum Slot:std::size_t {
    Input0=0u,Output0=1u,Input1=2u,Output1=3u,Scratch=4u
};
void Check(VkResult r,const char* what) {
    if(r!=VK_SUCCESS)
        throw std::runtime_error(std::string(what)+": "+std::to_string(r));
}
struct Buf{VkBuffer buffer{};VkDeviceMemory memory{};};
VKAPI_ATTR VkBool32 VKAPI_CALL OnValidation(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* info,void* userdata) {
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        static_cast<std::atomic<unsigned>*>(userdata)->fetch_add(1u);
        std::cerr<<"R09_FSWIN_KHRONOS_VALIDATION_ERROR "
                 <<(info&&info->pMessage?info->pMessage:"unknown")<<"\n";
    }
    return VK_FALSE;
}
struct Host {
    VkInstance instance{};
    VkDebugUtilsMessengerEXT messenger{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory_types{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queue_family{};
    std::array<Buf,5u> buffers{};
    VkDescriptorSetLayout set_layout{};
    VkDescriptorPool descriptor_pool{};
    std::array<VkDescriptorSet,2u> sets{};
    VkShaderModule shader{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline pipeline{};
    VkCommandPool command_pool{};
    VkCommandBuffer cmd{};
    VkFence fence{};
    std::atomic<unsigned> validation_errors{};
    bool validate{};
    ~Host(){
        if(device){
            vkDeviceWaitIdle(device);
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
            if(pipeline_layout)vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
            if(shader)vkDestroyShaderModule(device,shader,nullptr);
            if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            if(set_layout)vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
            if(command_pool)vkDestroyCommandPool(device,command_pool,nullptr);
            for(const auto& b:buffers){
                if(b.buffer)vkDestroyBuffer(device,b.buffer,nullptr);
                if(b.memory)vkFreeMemory(device,b.memory,nullptr);
            }
            vkDestroyDevice(device,nullptr);
        }
        if(instance){
            if(messenger){
                auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance,
                                          "vkDestroyDebugUtilsMessengerEXT"));
                if(destroy)destroy(instance,messenger,nullptr);
            }
            vkDestroyInstance(instance,nullptr);
        }
    }
    std::uint32_t MemoryType(std::uint32_t bits,
                              VkMemoryPropertyFlags flags) const {
        for(std::uint32_t i=0u;i<memory_types.memoryTypeCount;++i)
            if((bits&(1u<<i))&&
                (memory_types.memoryTypes[i].propertyFlags&flags)==flags)
                return i;
        throw std::runtime_error("Host coherent buffer memory unsupported");
    }
    void Init(){
        validate=std::getenv("R09_FSWIN_VALIDATE_SYNC")!=nullptr;
        VkApplicationInfo app{};
        app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName="R09 FSwin scratch host only";
        app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo=&app;
        const char* layer="VK_LAYER_KHRONOS_validation";
        const std::array<const char*,2u> extensions{
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        VkValidationFeatureEnableEXT sync=
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT features{};
        features.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount=1u;
        features.pEnabledValidationFeatures=&sync;
        VkDebugUtilsMessengerCreateInfoEXT debug{};
        debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
                          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback=OnValidation;
        debug.pUserData=&validation_errors;
        if(validate){
            features.pNext=&debug;
            ci.pNext=&features;
            ci.enabledLayerCount=1u;
            ci.ppEnabledLayerNames=&layer;
            ci.enabledExtensionCount=std::uint32_t(extensions.size());
            ci.ppEnabledExtensionNames=extensions.data();
        }
        Check(vkCreateInstance(&ci,nullptr,&instance),"vkCreateInstance");
        if(validate){
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance,
                                      "vkCreateDebugUtilsMessengerEXT"));
            if(!create)throw std::runtime_error("No debug utils messenger");
            Check(create(instance,&debug,nullptr,&messenger),"debug messenger");
        }
        std::uint32_t count{};
        Check(vkEnumeratePhysicalDevices(instance,&count,nullptr),
              "physical device count");
        if(!count)throw std::runtime_error("No software Vulkan device");
        std::vector<VkPhysicalDevice> available(count);
        Check(vkEnumeratePhysicalDevices(instance,&count,available.data()),
              "physical devices");
        for(const auto gpu:available){
            std::uint32_t families{};
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&families,nullptr);
            std::vector<VkQueueFamilyProperties> props(families);
            vkGetPhysicalDeviceQueueFamilyProperties(
                gpu,&families,props.data());
            for(std::uint32_t i=0u;i<families;++i)
                if(props[i].queueCount&&
                   (props[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){
                    physical=gpu;queue_family=i;break;
                }
            if(physical)break;
        }
        if(!physical)throw std::runtime_error("No compute queue");
        vkGetPhysicalDeviceMemoryProperties(physical,&memory_types);
        const float priority=1.0f;
        VkDeviceQueueCreateInfo q{};
        q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex=queue_family;
        q.queueCount=1u;
        q.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc{};
        dc.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dc.queueCreateInfoCount=1u;
        dc.pQueueCreateInfos=&q;
        Check(vkCreateDevice(physical,&dc,nullptr,&device),
              "vkCreateDevice");
        vkGetDeviceQueue(device,queue_family,0u,&queue);
        for(auto& b:buffers){
            VkBufferCreateInfo bc{};
            bc.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bc.size=kBytes;
            bc.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bc.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Check(vkCreateBuffer(device,&bc,nullptr,&b.buffer),
                  "vkCreateBuffer");
            VkMemoryRequirements mem{};
            vkGetBufferMemoryRequirements(device,b.buffer,&mem);
            VkMemoryAllocateInfo alloc{};
            alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize=mem.size;
            alloc.memoryTypeIndex=MemoryType(
                mem.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Check(vkAllocateMemory(device,&alloc,nullptr,&b.memory),
                  "vkAllocateMemory");
            Check(vkBindBufferMemory(device,b.buffer,b.memory,0u),
                  "vkBindBufferMemory");
        }
        // Distinct private inputs/outputs; exact SAME scratch VkBuffer
        // will be used by descriptor sets for both layers.
        for(std::size_t i=0u;i<buffers.size();++i)
            for(std::size_t j=0u;j<i;++j)
                if(buffers[i].buffer==buffers[j].buffer)
                    throw std::runtime_error("Unexpected aliased private handle");
        void* mapped{};
        Check(vkMapMemory(device,buffers[Input0].memory,0u,kBytes,0u,&mapped),
              "vkMapMemory input");
        auto* data=static_cast<std::uint32_t*>(mapped);
        for(std::uint32_t i=0u;i<kN;++i)data[i]=i*3u+5u;
        vkUnmapMemory(device,buffers[Input0].memory);
    }
    void InitPipeline(const std::string& path){
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in)throw std::runtime_error("Missing shader "+path);
        const auto bytes=in.tellg();
        if(bytes<=0||bytes%4!=0)throw std::runtime_error("Invalid SPIRV");
        std::vector<std::uint32_t> code(
            static_cast<std::size_t>(bytes)/4u);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(code.data()),bytes);
        if(!in)throw std::runtime_error("Cannot read SPIRV");
        VkShaderModuleCreateInfo module{};
        module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module.codeSize=std::size_t(bytes);
        module.pCode=code.data();
        Check(vkCreateShaderModule(device,&module,nullptr,&shader),
              "vkCreateShaderModule");
        std::array<VkDescriptorSetLayoutBinding,3u> bindings{};
        for(std::uint32_t i=0u;i<bindings.size();++i){
            bindings[i].binding=i;
            bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount=1u;
            bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo lc{};
        lc.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lc.bindingCount=std::uint32_t(bindings.size());
        lc.pBindings=bindings.data();
        Check(vkCreateDescriptorSetLayout(device,&lc,nullptr,&set_layout),
              "descriptor layout");
        VkDescriptorPoolSize ps{};
        ps.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ps.descriptorCount=6u;
        VkDescriptorPoolCreateInfo pc{};
        pc.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pc.maxSets=2u;
        pc.poolSizeCount=1u;
        pc.pPoolSizes=&ps;
        Check(vkCreateDescriptorPool(device,&pc,nullptr,&descriptor_pool),
              "descriptor pool");
        const std::array<VkDescriptorSetLayout,2u> layouts{
            set_layout,set_layout};
        VkDescriptorSetAllocateInfo da{};
        da.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        da.descriptorPool=descriptor_pool;
        da.descriptorSetCount=std::uint32_t(layouts.size());
        da.pSetLayouts=layouts.data();
        Check(vkAllocateDescriptorSets(device,&da,sets.data()),
              "allocate descriptor sets");
        const std::array<std::array<std::size_t,3u>,2u> source{{
            {{Input0,Scratch,Output0}},
            {{Input1,Scratch,Output1}}
        }};
        std::array<VkDescriptorBufferInfo,6u> infos{};
        std::array<VkWriteDescriptorSet,6u> writes{};
        for(std::uint32_t layer=0u;layer<2u;++layer){
            for(std::uint32_t b=0u;b<3u;++b){
                const auto index=layer*3u+b;
                infos[index]={buffers[source[layer][b]].buffer,0u,kBytes};
                writes[index].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[index].dstSet=sets[layer];
                writes[index].dstBinding=b;
                writes[index].descriptorCount=1u;
                writes[index].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[index].pBufferInfo=&infos[index];
            }
        }
        if(infos[1].buffer!=infos[4].buffer)
            throw std::runtime_error("Scratch not shared across VkDescriptorSets");
        vkUpdateDescriptorSets(device,std::uint32_t(writes.size()),
                               writes.data(),0u,nullptr);
        VkPushConstantRange push{};
        push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        push.offset=0u;push.size=8u;
        VkPipelineLayoutCreateInfo plc{};
        plc.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plc.setLayoutCount=1u;
        plc.pSetLayouts=&set_layout;
        plc.pushConstantRangeCount=1u;
        plc.pPushConstantRanges=&push;
        Check(vkCreatePipelineLayout(device,&plc,nullptr,&pipeline_layout),
              "pipeline layout");
        VkComputePipelineCreateInfo cp{};
        cp.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module=shader;
        cp.stage.pName="main";
        cp.layout=pipeline_layout;
        Check(vkCreateComputePipelines(
            device,VK_NULL_HANDLE,1u,&cp,nullptr,&pipeline),
            "compute pipeline");
        VkCommandPoolCreateInfo pool{};
        pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex=queue_family;
        Check(vkCreateCommandPool(device,&pool,nullptr,&command_pool),
              "command pool");
        VkCommandBufferAllocateInfo cb{};
        cb.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cb.commandPool=command_pool;
        cb.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cb.commandBufferCount=1u;
        Check(vkAllocateCommandBuffers(device,&cb,&cmd),
              "command buffer");
    }
    void RecordAndSubmit(bool negative){
        VkCommandBufferBeginInfo begin{};
        begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(vkBeginCommandBuffer(cmd,&begin),"begin");
        VkMemoryBarrier upload{};
        upload.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        upload.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
        upload.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,1u,&upload,
            0u,nullptr,0u,nullptr);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        for(std::uint32_t layer=0u;layer<2u;++layer){
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,
                pipeline_layout,0u,1u,&sets[layer],0u,nullptr);
            struct Push{std::uint32_t stage{},layer{};};
            const Push write{0u,layer};
            vkCmdPushConstants(cmd,pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT,0u,sizeof(write),&write);
            vkCmdDispatch(cmd,kN/64u,1u,1u);
            // Negative fixture deliberately omits scratch write->read
            // visibility for second-layer borrowed storage.
            if(!(negative&&layer==1u)){
                VkMemoryBarrier visible{};
                visible.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                visible.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                visible.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                     VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,
                    1u,&visible,0u,nullptr,0u,nullptr);
            }
            const Push finish{1u,layer};
            vkCmdPushConstants(cmd,pipeline_layout,
                VK_SHADER_STAGE_COMPUTE_BIT,0u,sizeof(finish),&finish);
            vkCmdDispatch(cmd,kN/64u,1u,1u);
            VkMemoryBarrier retire{};
            retire.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            retire.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                 VK_ACCESS_SHADER_WRITE_BIT;
            retire.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                 VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,
                1u,&retire,0u,nullptr,0u,nullptr);
            if(layer==0u){
                VkBufferMemoryBarrier release{};
                release.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                release.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                release.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                release.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                release.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                release.buffer=buffers[Output0].buffer;
                release.offset=0u;release.size=kBytes;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,0u,
                    0u,nullptr,1u,&release,0u,nullptr);
                VkBufferCopy copy{};
                copy.size=kBytes;
                vkCmdCopyBuffer(cmd,buffers[Output0].buffer,
                                buffers[Input1].buffer,1u,&copy);
                VkBufferMemoryBarrier acquire{};
                acquire.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                acquire.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                acquire.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                acquire.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                acquire.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                acquire.buffer=buffers[Input1].buffer;
                acquire.offset=0u;acquire.size=kBytes;
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,
                    0u,nullptr,1u,&acquire,0u,nullptr);
            }
        }
        VkMemoryBarrier readback{};
        readback.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        readback.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        readback.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,0u,1u,&readback,
            0u,nullptr,0u,nullptr);
        Check(vkEndCommandBuffer(cmd),"end");
        VkFenceCreateInfo fc{};
        fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Check(vkCreateFence(device,&fc,nullptr,&fence),"fence");
        VkSubmitInfo submit{};
        submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount=1u;
        submit.pCommandBuffers=&cmd;
        Check(vkQueueSubmit(queue,1u,&submit,fence),"submit");
        Check(vkWaitForFences(device,1u,&fence,VK_TRUE,10000000000ull),
              "wait");
    }
    void Verify()const{
        for(const auto idx:{Output0,Output1}){
            void* ptr{};
            Check(vkMapMemory(device,buffers[idx].memory,0u,kBytes,0u,&ptr),
                  "readback");
            const auto* p=static_cast<const std::uint32_t*>(ptr);
            for(std::uint32_t i=0u;i<kN;++i){
                const auto source=i*3u+5u;
                const auto first=(source+11u)*3u;
                const auto second=(first+18u)*3u+5u;
                const auto expected=idx==Output0?first:second;
                if(p[i]!=expected){
                    const auto got=p[i];
                    vkUnmapMemory(device,buffers[idx].memory);
                    throw std::runtime_error(
                        "FSwin shared scratch readback mismatch layer="+
                        std::to_string(idx==Output0?0:1)+
                        " token="+std::to_string(i)+
                        " got="+std::to_string(got)+
                        " expected="+std::to_string(expected));
                }
            }
            vkUnmapMemory(device,buffers[idx].memory);
        }
        std::cout<<"R09_FSWIN_REAL_VULKAN_TWO_LAYERS_ONE_SCRATCH_READBACK_PASS"
                 <<" private_io=4 scratch=1 dispatches=4\n";
    }
};
} // namespace
int main(int argc,char** argv){
    if(argc!=2&&argc!=3){
        std::cerr<<"usage: r09_fswin_scratch_vulkan_smoke "
                    "<shader.spv> [--negative-missing-barrier]\n";
        return 2;
    }
    const bool negative=argc==3&&
        std::string(argv[2])=="--negative-missing-barrier";
    if(argc==3&&!negative)return 2;
    try{
        Host host{};
        host.Init();
        host.InitPipeline(argv[1]);
        if(negative&&!host.validate)
            throw std::runtime_error(
                "Negative fixture requires Khronos sync validation");
        host.RecordAndSubmit(negative);
        if(negative){
            if(host.validation_errors.load()==0u)
                throw std::runtime_error(
                    "Khronos did not detect missing scratch barrier");
            std::cout<<"R09_FSWIN_KHRONOS_NEGATIVE_MISSING_BARRIER_DETECTED\n";
            return 0;
        }
        host.Verify();
        if(host.validate){
            if(host.validation_errors.load()!=0u)
                throw std::runtime_error(
                    "Unexpected Khronos validation errors");
            std::cout<<"R09_FSWIN_KHRONOS_SYNC_ZERO_ERRORS\n";
        }
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"R09_FSWIN_VULKAN_HOST_SMOKE_FAIL: "<<e.what()<<"\n";
        return 1;
    }
}
