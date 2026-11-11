// SPDX-License-Identifier: MIT
// SYNTHETIC, host-only Vulkan 1.1 lifetime and descriptor stress:
// 44 Original-style or 16 Compact-style sequential FSwin layer positions.
// Private input/output VkBuffers for each layer, eight distinct segment
// scratch VkBuffers, real compute->compute and compute->transfer dependencies.
// No learned FSwin shader, real weights, PS5 GPU, or production integration.
#include <vulkan/vulkan.h>
#include <algorithm>
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
constexpr std::uint32_t kTokens=256u;
constexpr VkDeviceSize kBytes=kTokens*sizeof(std::uint32_t);
constexpr std::size_t kSegments=8u;
using Segments=std::array<std::uint32_t,kSegments>;
constexpr Segments Original{4u,4u,6u,8u,4u,4u,6u,8u};
constexpr Segments Compact{2u,2u,2u,2u,2u,2u,2u,2u};
void Require(VkResult code,const char* operation) {
    if(code!=VK_SUCCESS)throw std::runtime_error(
        std::string(operation)+": "+std::to_string(code));
}
struct Buffer{VkBuffer handle{};VkDeviceMemory memory{};};
VKAPI_ATTR VkBool32 VKAPI_CALL Report(
    VkDebugUtilsMessageSeverityFlagBitsEXT sev,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* message,void* user) {
    if(sev&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){
        static_cast<std::atomic<unsigned>*>(user)->fetch_add(1u);
        std::cerr<<"R09_FSWIN_CHAIN_KHRONOS_ERROR: "
                 <<(message&&message->pMessage?message->pMessage:"unknown")
                 <<"\n";
    }
    return VK_FALSE;
}
struct Test {
    VkInstance instance{};
    VkDebugUtilsMessengerEXT debug{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory_props{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t family{};
    VkDescriptorSetLayout set_layout{};
    VkDescriptorPool descriptor_pool{};
    VkShaderModule shader{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkCommandPool command_pool{};
    VkCommandBuffer commands{};
    VkFence fence{};
    std::vector<Buffer> buffers{};
    std::vector<VkDescriptorSet> sets{};
    std::vector<unsigned> segment_for_layer{};
    Segments counts{};
    std::atomic<unsigned> errors{0u};
    bool validation{};
    std::uint32_t layers{};
    explicit Test(Segments shape):counts{shape}{
        for(const auto n:counts)layers+=n;
        for(std::uint32_t segment=0u;segment<counts.size();++segment)
            for(std::uint32_t i=0u;i<counts[segment];++i)
                segment_for_layer.push_back(segment);
        if(layers!=16u&&layers!=44u)
            throw std::runtime_error("Invalid graph layer count");
    }
    ~Test(){
        if(device){
            vkDeviceWaitIdle(device);
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
            if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
            if(shader)vkDestroyShaderModule(device,shader,nullptr);
            if(descriptor_pool)
                vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            if(set_layout)
                vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
            if(command_pool)
                vkDestroyCommandPool(device,command_pool,nullptr);
            for(const auto& b:buffers){
                if(b.handle)vkDestroyBuffer(device,b.handle,nullptr);
                if(b.memory)vkFreeMemory(device,b.memory,nullptr);
            }
            vkDestroyDevice(device,nullptr);
        }
        if(instance){
            if(debug){
                auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance,
                        "vkDestroyDebugUtilsMessengerEXT"));
                if(destroy)destroy(instance,debug,nullptr);
            }
            vkDestroyInstance(instance,nullptr);
        }
    }
    [[nodiscard]] unsigned MemoryType(
        unsigned flags,VkMemoryPropertyFlags wanted)const {
        for(unsigned i=0u;i<memory_props.memoryTypeCount;++i)
            if((flags&(1u<<i))&&
               (memory_props.memoryTypes[i].propertyFlags&wanted)==wanted)
                return i;
        throw std::runtime_error("Vulkan coherent memory unsupported");
    }
    [[nodiscard]] std::size_t Input(unsigned layer)const {
        return std::size_t(layer)*2u;
    }
    [[nodiscard]] std::size_t Output(unsigned layer)const {
        return std::size_t(layer)*2u+1u;
    }
    [[nodiscard]] std::size_t Scratch(unsigned segment)const {
        return std::size_t(layers)*2u+segment;
    }
    void Initialize(){
        validation=std::getenv("R09_FSWIN_VALIDATE_SYNC")!=nullptr;
        VkApplicationInfo info{};
        info.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        info.apiVersion=VK_API_VERSION_1_1;
        info.pApplicationName="R09 FSwin 44-layer synthetic Vulkan";
        VkInstanceCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo=&info;
        const char* layer="VK_LAYER_KHRONOS_validation";
        const std::array<const char*,2u> ext{
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        VkValidationFeatureEnableEXT sync=
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT features{};
        features.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount=1u;
        features.pEnabledValidationFeatures=&sync;
        VkDebugUtilsMessengerCreateInfoEXT db{};
        db.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        db.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        db.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
                       VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        db.pfnUserCallback=Report;
        db.pUserData=&errors;
        if(validation){
            features.pNext=&db;
            ci.pNext=&features;
            ci.enabledLayerCount=1u;
            ci.ppEnabledLayerNames=&layer;
            ci.enabledExtensionCount=static_cast<unsigned>(ext.size());
            ci.ppEnabledExtensionNames=ext.data();
        }
        Require(vkCreateInstance(&ci,nullptr,&instance),"vkCreateInstance");
        if(validation){
            auto create=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance,
                                      "vkCreateDebugUtilsMessengerEXT"));
            if(!create)throw std::runtime_error("Missing Khronos debug util");
            Require(create(instance,&db,nullptr,&debug),"debug messenger");
        }
        unsigned phys_count{};
        Require(vkEnumeratePhysicalDevices(instance,&phys_count,nullptr),
                "enumerate physical");
        if(!phys_count)throw std::runtime_error("No Vulkan physical device");
        std::vector<VkPhysicalDevice> devices(phys_count);
        Require(vkEnumeratePhysicalDevices(instance,&phys_count,devices.data()),
                "enumerate physical list");
        for(const auto gpu:devices){
            unsigned size{};
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&size,nullptr);
            std::vector<VkQueueFamilyProperties> families(size);
            vkGetPhysicalDeviceQueueFamilyProperties(
                gpu,&size,families.data());
            for(unsigned i=0u;i<size;++i)
                if(families[i].queueCount&&
                   (families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){
                    physical=gpu;family=i;break;
                }
            if(physical)break;
        }
        if(!physical)throw std::runtime_error("Missing compute queue");
        vkGetPhysicalDeviceMemoryProperties(physical,&memory_props);
        const float priority=1.0f;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex=family;
        queue_info.queueCount=1u;
        queue_info.pQueuePriorities=&priority;
        VkDeviceCreateInfo dci{};
        dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount=1u;
        dci.pQueueCreateInfos=&queue_info;
        Require(vkCreateDevice(physical,&dci,nullptr,&device),
                "vkCreateDevice");
        vkGetDeviceQueue(device,family,0u,&queue);
        buffers.resize(std::size_t(layers)*2u+kSegments);
        for(auto& b:buffers){
            VkBufferCreateInfo bc{};
            bc.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bc.size=kBytes;
            bc.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bc.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Require(vkCreateBuffer(device,&bc,nullptr,&b.handle),
                    "vkCreateBuffer");
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device,b.handle,&req);
            VkMemoryAllocateInfo ma{};
            ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ma.allocationSize=req.size;
            ma.memoryTypeIndex=MemoryType(
                req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Require(vkAllocateMemory(device,&ma,nullptr,&b.memory),
                    "vkAllocateMemory");
            Require(vkBindBufferMemory(device,b.handle,b.memory,0u),
                    "vkBindBufferMemory");
        }
        for(std::size_t i=0u;i<buffers.size();++i)
            for(std::size_t j=0u;j<i;++j)
                if(buffers[i].handle==buffers[j].handle)
                    throw std::runtime_error("Shared scratch handle alias across chains");
        void* map{};
        Require(vkMapMemory(device,buffers[Input(0u)].memory,
                            0u,kBytes,0u,&map),"initial input map");
        auto* values=static_cast<std::uint32_t*>(map);
        for(std::uint32_t i=0u;i<kTokens;++i)
            values[i]=3u*i+5u;
        vkUnmapMemory(device,buffers[Input(0u)].memory);
    }
    void InitializePipeline(const std::string& path){
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in)throw std::runtime_error("Missing shader");
        const std::streamoff size=in.tellg();
        if(size<=0||size%4u!=0)
            throw std::runtime_error("Invalid SPIRV size");
        std::vector<std::uint32_t> words(static_cast<std::size_t>(size)/4u);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(words.data()),size);
        if(!in)throw std::runtime_error("SPIRV read failed");
        VkShaderModuleCreateInfo module{};
        module.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module.codeSize=static_cast<std::size_t>(size);
        module.pCode=words.data();
        Require(vkCreateShaderModule(device,&module,nullptr,&shader),
                "vkCreateShaderModule");
        std::array<VkDescriptorSetLayoutBinding,3u> bindings{};
        for(unsigned b=0u;b<bindings.size();++b){
            bindings[b].binding=b;
            bindings[b].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[b].descriptorCount=1u;
            bindings[b].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo lc{};
        lc.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        lc.bindingCount=static_cast<unsigned>(bindings.size());
        lc.pBindings=bindings.data();
        Require(vkCreateDescriptorSetLayout(device,&lc,nullptr,&set_layout),
                "set layout");
        VkDescriptorPoolSize ps{};
        ps.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ps.descriptorCount=layers*3u;
        VkDescriptorPoolCreateInfo pc{};
        pc.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pc.maxSets=layers;
        pc.poolSizeCount=1u;
        pc.pPoolSizes=&ps;
        Require(vkCreateDescriptorPool(device,&pc,nullptr,&descriptor_pool),
                "descriptor pool");
        std::vector<VkDescriptorSetLayout> layouts(layers,set_layout);
        sets.resize(layers);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool=descriptor_pool;
        ai.descriptorSetCount=layers;
        ai.pSetLayouts=layouts.data();
        Require(vkAllocateDescriptorSets(device,&ai,sets.data()),
                "descriptor allocations");
        std::vector<VkDescriptorBufferInfo> infos(std::size_t(layers)*3u);
        std::vector<VkWriteDescriptorSet> writes(infos.size());
        for(unsigned i=0u;i<layers;++i){
            const std::array<std::size_t,3u> owner{
                Input(i),Scratch(segment_for_layer[i]),Output(i)};
            for(unsigned binding=0u;binding<3u;++binding){
                const auto index=std::size_t(i)*3u+binding;
                infos[index]={buffers[owner[binding]].handle,0u,kBytes};
                auto& w=writes[index];
                w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet=sets[i];
                w.dstBinding=binding;
                w.descriptorCount=1u;
                w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w.pBufferInfo=&infos[index];
            }
        }
        // Adjacent layers in a segment share scratch; different
        // segments never do. Every input/output is genuinely unique.
        for(unsigned i=1u;i<layers;++i){
            const auto prev=Scratch(segment_for_layer[i-1u]);
            const auto curr=Scratch(segment_for_layer[i]);
            if((segment_for_layer[i]==segment_for_layer[i-1u])!=
               (buffers[prev].handle==buffers[curr].handle))
                throw std::runtime_error("Chain scratch identity mismatch");
            if(buffers[Input(i)].handle==buffers[Output(i-1u)].handle)
                throw std::runtime_error("Private input/output unexpectedly aliased");
        }
        vkUpdateDescriptorSets(device,static_cast<unsigned>(writes.size()),
                               writes.data(),0u,nullptr);
        VkPushConstantRange push{};
        push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        push.size=2u*sizeof(std::uint32_t);
        VkPipelineLayoutCreateInfo plc{};
        plc.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plc.setLayoutCount=1u;
        plc.pSetLayouts=&set_layout;
        plc.pushConstantRangeCount=1u;
        plc.pPushConstantRanges=&push;
        Require(vkCreatePipelineLayout(device,&plc,nullptr,&layout),
                "pipeline layout");
        VkComputePipelineCreateInfo cp{};
        cp.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        cp.stage.module=shader;
        cp.stage.pName="main";
        cp.layout=layout;
        Require(vkCreateComputePipelines(device,VK_NULL_HANDLE,1u,
                                         &cp,nullptr,&pipeline),
                "compute pipeline");
        VkCommandPoolCreateInfo pool{};
        pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex=family;
        Require(vkCreateCommandPool(device,&pool,nullptr,&command_pool),
                "command pool");
        VkCommandBufferAllocateInfo cbi{};
        cbi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbi.commandPool=command_pool;
        cbi.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbi.commandBufferCount=1u;
        Require(vkAllocateCommandBuffers(device,&cbi,&commands),
                "command buffer");
    }
    void Execute(bool negative){
        VkCommandBufferBeginInfo b{};
        b.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        b.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Require(vkBeginCommandBuffer(commands,&b),"begin");
        VkMemoryBarrier upload{};
        upload.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        upload.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
        upload.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,1u,&upload,
            0u,nullptr,0u,nullptr);
        vkCmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        struct Push{std::uint32_t stage,layer;};
        for(unsigned i=0u;i<layers;++i){
            vkCmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_COMPUTE,
                layout,0u,1u,&sets[i],0u,nullptr);
            const Push first{0u,i};
            vkCmdPushConstants(commands,layout,
                VK_SHADER_STAGE_COMPUTE_BIT,0u,sizeof(first),&first);
            vkCmdDispatch(commands,kTokens/64u,1u,1u);
            VkMemoryBarrier visible{};
            visible.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            visible.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            visible.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                  VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(commands,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,1u,&visible,0u,nullptr,0u,nullptr);
            const Push final{1u,i};
            vkCmdPushConstants(commands,layout,
                VK_SHADER_STAGE_COMPUTE_BIT,0u,sizeof(final),&final);
            vkCmdDispatch(commands,kTokens/64u,1u,1u);
            if(negative&&i==3u) {
                // Explicit unsynchronized scratch write, overlapping a
                // preceding compute READ; Khronos must see the hazard.
                vkCmdFillBuffer(commands,
                    buffers[Scratch(segment_for_layer[i])].handle,
                    0u,kBytes,0xdeadbeefu);
            }
            VkMemoryBarrier retirement{};
            retirement.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            retirement.srcAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                     VK_ACCESS_SHADER_WRITE_BIT;
            retirement.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                     VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(commands,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,1u,&retirement,0u,nullptr,0u,nullptr);
            if(i+1u<layers){
                VkBufferMemoryBarrier release{};
                release.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                release.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                release.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                release.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                release.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                release.buffer=buffers[Output(i)].handle;
                release.size=kBytes;
                vkCmdPipelineBarrier(commands,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,0u,
                    0u,nullptr,1u,&release,0u,nullptr);
                VkBufferCopy copy{};copy.size=kBytes;
                vkCmdCopyBuffer(commands,
                    buffers[Output(i)].handle,
                    buffers[Input(i+1u)].handle,1u,&copy);
                VkBufferMemoryBarrier acquire{};
                acquire.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                acquire.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                acquire.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                acquire.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                acquire.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                acquire.buffer=buffers[Input(i+1u)].handle;
                acquire.size=kBytes;
                vkCmdPipelineBarrier(commands,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0u,0u,nullptr,1u,&acquire,0u,nullptr);
            }
        }
        VkMemoryBarrier final{};
        final.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        final.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        final.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commands,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,0u,1u,&final,0u,nullptr,0u,nullptr);
        Require(vkEndCommandBuffer(commands),"end");
        VkFenceCreateInfo fc{};
        fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Require(vkCreateFence(device,&fc,nullptr,&fence),"fence");
        VkSubmitInfo submit{};
        submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount=1u;
        submit.pCommandBuffers=&commands;
        Require(vkQueueSubmit(queue,1u,&submit,fence),"submit");
        Require(vkWaitForFences(device,1u,&fence,VK_TRUE,10000000000ull),
                "fence wait");
    }
    void CheckReadback() const {
        for(unsigned layer=0u;layer<layers;++layer){
            void* mapped{};
            Require(vkMapMemory(device,buffers[Output(layer)].memory,
                                0u,kBytes,0u,&mapped),"readback map");
            const auto* result=static_cast<const std::uint32_t*>(mapped);
            for(unsigned token=0u;token<kTokens;++token){
                std::uint32_t expected=3u*token+5u;
                for(unsigned stage=0u;stage<=layer;++stage){
                    const std::uint32_t scratch=
                        expected+stage*7u+11u;
                    expected=scratch*3u+stage*5u;
                }
                if(result[token]!=expected){
                    const auto got=result[token];
                    vkUnmapMemory(device,buffers[Output(layer)].memory);
                    throw std::runtime_error(
                        "Synthetic FSwin graph readback mismatch at layer="+
                        std::to_string(layer)+" token="+
                        std::to_string(token)+" actual="+
                        std::to_string(got)+" expected="+
                        std::to_string(expected));
                }
            }
            vkUnmapMemory(device,buffers[Output(layer)].memory);
        }
        std::cout<<"R09_FSWIN_EIGHT_CHAINS_VULKAN_GRAPH_PASS"
                 <<" layers="<<layers<<" segments=8"
                 <<" own_io_buffers="<<2u*layers
                 <<" separate_segment_scratch_buffers=8"
                 <<" verified_outputs="<<layers
                 <<" gpu_compute_dispatches="<<2u*layers<<"\n";
    }
};
}
int main(int argc,char** argv){
    if(argc<3||argc>4){
        std::cerr<<"usage: r09_fswin_chain_pool_vulkan_graph_smoke "
                    "<shader.spv> Original|Compact "
                    "[--negative-missing-barrier]\n";
        return 2;
    }
    const std::string model=argv[2];
    const bool negative=argc==4&&
        std::string(argv[3])=="--negative-missing-barrier";
    if((model!="Original"&&model!="Compact")||(argc==4&&!negative))
        return 2;
    try{
        Test t{model=="Original"?Original:Compact};
        t.Initialize();
        t.InitializePipeline(argv[1]);
        if(negative&&!t.validation)
            throw std::runtime_error("Negative fixture requires sync validation");
        t.Execute(negative);
        if(negative){
            if(t.errors.load()==0u)
                throw std::runtime_error(
                    "Khronos failed to detect scratch transfer overwrite");
            std::cout<<"R09_FSWIN_EIGHT_CHAINS_KHRONOS_NEGATIVE_HAZARD_DETECTED"
                     <<" model="<<model<<"\n";
            return 0;
        }
        t.CheckReadback();
        if(t.validation){
            if(t.errors.load()!=0u)
                throw std::runtime_error("Unexpected validation error");
            std::cout<<"R09_FSWIN_EIGHT_CHAINS_KHRONOS_ZERO_ERRORS"
                     <<" model="<<model<<"\n";
        }
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"R09_FSWIN_EIGHT_CHAINS_VULKAN_FAIL: "
                 <<e.what()<<"\n";
        return 1;
    }
}
