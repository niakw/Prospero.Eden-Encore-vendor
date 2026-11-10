// SPDX-License-Identifier: MIT
// Compare authentic scalar ViT global-attention shader with the isolated
// 32-key workgroup-shared-memory prototype using REAL Vulkan compute.
// Synthetic PUBLIC packed E4M3FN tensor contents only, no model weights.
// This is host Lavapipe image-readback equivalence, NOT a PS5 FPS proof.
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
using Word=std::uint32_t;
struct Allocated {
    VkBuffer buffer{};
    VkDeviceMemory memory{};
};
VKAPI_ATTR VkBool32 VKAPI_CALL OnValidation(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* details,void* userdata){
    if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){
        static_cast<std::atomic<unsigned>*>(userdata)->fetch_add(1u);
        std::cerr<<"R09_MIN_ATTENTION_KHRONOS_ERROR "
                 <<(details&&details->pMessage?details->pMessage:"unknown")<<"\n";
    }
    return VK_FALSE;
}
void Verify(VkResult status,const char* msg){
    if(status!=VK_SUCCESS)
        throw std::runtime_error(std::string(msg)+" Vulkan="+std::to_string(status));
}
class Compare {
    static constexpr std::uint32_t kBuffers=5u;
    static constexpr std::uint32_t kBindings=4u;
    VkDebugUtilsMessengerEXT messenger{};
    std::atomic<unsigned> validation_errors{0u};
    bool validation{};
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t family{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::array<Allocated,kBuffers> bufs{};
    VkDescriptorSetLayout set_layout{};
    VkDescriptorPool descriptor_pool{};
    std::array<VkDescriptorSet,2u> sets{};
    std::array<VkShaderModule,2u> shaders{};
    VkPipelineLayout layout{};
    std::array<VkPipeline,2u> pipelines{};
    VkCommandPool command_pool{};
    VkCommandBuffer command{};
    VkFence completion{};
    std::uint32_t tokens{};
    VkDeviceSize bytes{};
    std::uint32_t seed{};
public:
    Compare(std::uint32_t token_count,std::uint32_t fixture)
        :tokens{token_count},bytes{VkDeviceSize(token_count)*1024u},seed{fixture}{}
    Compare(const Compare&)=delete;
    Compare& operator=(const Compare&)=delete;
    ~Compare(){
        if(device){
            vkDeviceWaitIdle(device);
            if(completion)vkDestroyFence(device,completion,nullptr);
            for(auto p:pipelines)if(p)vkDestroyPipeline(device,p,nullptr);
            if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
            for(auto s:shaders)if(s)vkDestroyShaderModule(device,s,nullptr);
            if(descriptor_pool)vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            if(set_layout)vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
            if(command_pool)vkDestroyCommandPool(device,command_pool,nullptr);
            for(const auto& b:bufs){
                if(b.buffer)vkDestroyBuffer(device,b.buffer,nullptr);
                if(b.memory)vkFreeMemory(device,b.memory,nullptr);
            }
            vkDestroyDevice(device,nullptr);
        }
        if(instance){
            if(messenger){
                const auto destroy=
                    reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                        vkGetInstanceProcAddr(instance,
                                             "vkDestroyDebugUtilsMessengerEXT"));
                if(destroy)destroy(instance,messenger,nullptr);
            }
            vkDestroyInstance(instance,nullptr);
        }
    }
    std::uint32_t FindMemory(std::uint32_t permitted)const{
        for(std::uint32_t i=0u;i<memory.memoryTypeCount;++i) {
            const auto flags=memory.memoryTypes[i].propertyFlags;
            if((permitted&(1u<<i))&&
               (flags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
                      (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                return i;
        }
        throw std::runtime_error("No host-visible coherent Vulkan memory");
    }
    void Setup(){
        VkApplicationInfo info{};
        info.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        info.apiVersion=VK_API_VERSION_1_1;
        info.pApplicationName="R09 Min tiled attention baseline comparison";
        VkInstanceCreateInfo create{};
        create.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create.pApplicationInfo=&info;
        validation=std::getenv("R09_MIN_VALIDATE_SYNC")!=nullptr;
        const char* layer="VK_LAYER_KHRONOS_validation";
        const std::array<const char*,2u> extensions{
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
            VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        VkValidationFeatureEnableEXT feature=
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT features{};
        features.sType=VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount=1u;
        features.pEnabledValidationFeatures=&feature;
        VkDebugUtilsMessengerCreateInfoEXT debug{};
        debug.sType=VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
                          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        debug.pfnUserCallback=OnValidation;
        debug.pUserData=&validation_errors;
        if(validation){
            features.pNext=&debug;
            create.pNext=&features;
            create.enabledLayerCount=1u;
            create.ppEnabledLayerNames=&layer;
            create.enabledExtensionCount=std::uint32_t(extensions.size());
            create.ppEnabledExtensionNames=extensions.data();
        }
        Verify(vkCreateInstance(&create,nullptr,&instance),"instance");
        if(validation){
            auto make=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
            if(!make)throw std::runtime_error("Khronos debug utils absent");
            Verify(make(instance,&debug,nullptr,&messenger),"debug messenger");
        }
        std::uint32_t n{};
        Verify(vkEnumeratePhysicalDevices(instance,&n,nullptr),"device count");
        if(n==0u)throw std::runtime_error("No Vulkan ICD");
        std::vector<VkPhysicalDevice> devices(n);
        Verify(vkEnumeratePhysicalDevices(instance,&n,devices.data()),
               "enumerate devices");
        for(auto gpu:devices){
            VkPhysicalDeviceProperties caps{};
            vkGetPhysicalDeviceProperties(gpu,&caps);
            if(caps.limits.maxPerStageDescriptorStorageBuffers<kBindings ||
               caps.limits.maxStorageBufferRange<bytes)continue;
            std::uint32_t count{};
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu,&count,families.data());
            for(std::uint32_t i=0;i<count;++i){
                if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT){
                    physical=gpu;family=i;
                    std::cout<<"R09_MIN_ATTENTION_GPU "<<caps.deviceName<<"\n";
                    break;
                }
            }
            if(physical)break;
        }
        if(!physical)throw std::runtime_error("No compute GPU supporting 4 SSBO");
        vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        float priority=1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex=family;
        qci.queueCount=1u;qci.pQueuePriorities=&priority;
        VkDeviceCreateInfo dci{};
        dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount=1u;dci.pQueueCreateInfos=&qci;
        Verify(vkCreateDevice(physical,&dci,nullptr,&device),"logical device");
        vkGetDeviceQueue(device,family,0u,&queue);
        for(auto& b:bufs){
            VkBufferCreateInfo ci{};
            ci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            ci.size=bytes;
            ci.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Verify(vkCreateBuffer(device,&ci,nullptr,&b.buffer),"buffer");
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device,b.buffer,&req);
            VkMemoryAllocateInfo alloc{};
            alloc.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize=req.size;
            alloc.memoryTypeIndex=FindMemory(req.memoryTypeBits);
            Verify(vkAllocateMemory(device,&alloc,nullptr,&b.memory),"allocate");
            Verify(vkBindBufferMemory(device,b.buffer,b.memory,0u),"bind");
        }
        std::array<VkDescriptorSetLayoutBinding,kBindings> bindings{};
        for(Word i=0;i<kBindings;++i)
            bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1u,
                         VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo sci{};
        sci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sci.bindingCount=kBindings;sci.pBindings=bindings.data();
        Verify(vkCreateDescriptorSetLayout(device,&sci,nullptr,&set_layout),
               "descriptor layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8u};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.poolSizeCount=1u;pi.pPoolSizes=&size;pi.maxSets=2u;
        Verify(vkCreateDescriptorPool(device,&pi,nullptr,&descriptor_pool),
               "descriptor pool");
        std::array<VkDescriptorSetLayout,2u> layouts{set_layout,set_layout};
        VkDescriptorSetAllocateInfo ai{};
        ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool=descriptor_pool;ai.descriptorSetCount=2u;
        ai.pSetLayouts=layouts.data();
        Verify(vkAllocateDescriptorSets(device,&ai,sets.data()),"descriptor sets");
        for(Word variant=0u;variant<2u;++variant){
            std::array<VkDescriptorBufferInfo,kBindings> infos{};
            std::array<VkWriteDescriptorSet,kBindings> writes{};
            for(Word binding=0u;binding<kBindings;++binding){
                const Word slot=binding==3u?3u+variant:binding;
                infos[binding]={bufs[slot].buffer,0u,bytes};
                auto& w=writes[binding];
                w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet=sets[variant];
                w.dstBinding=binding;
                w.descriptorCount=1u;
                w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w.pBufferInfo=&infos[binding];
            }
            vkUpdateDescriptorSets(device,kBindings,writes.data(),0u,nullptr);
        }
        VkCommandPoolCreateInfo pc{};
        pc.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pc.queueFamilyIndex=family;
        Verify(vkCreateCommandPool(device,&pc,nullptr,&command_pool),"command pool");
        VkCommandBufferAllocateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ci.commandPool=command_pool;
        ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ci.commandBufferCount=1u;
        Verify(vkAllocateCommandBuffers(device,&ci,&command),"command");
    }
    void CreateShader(const char* path,Word index){
        std::ifstream f(path,std::ios::binary|std::ios::ate);
        if(!f)throw std::runtime_error("Missing compiled original/tiled SPIR-V");
        const auto length=f.tellg();
        if(length<=0||length%4!=0)throw std::runtime_error("Invalid SPIR-V size");
        std::vector<Word> words(std::size_t(length)/4u);
        f.seekg(0);
        f.read(reinterpret_cast<char*>(words.data()),length);
        if(!f)throw std::runtime_error("Invalid SPIR-V read");
        VkShaderModuleCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize=std::size_t(length);
        ci.pCode=words.data();
        Verify(vkCreateShaderModule(device,&ci,nullptr,&shaders[index]),"module");
    }
    void BuildPipelines(const char* original,const char* tiled){
        CreateShader(original,0u);CreateShader(tiled,1u);
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0u,20u};
        VkPipelineLayoutCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ci.setLayoutCount=1u;ci.pSetLayouts=&set_layout;
        ci.pushConstantRangeCount=1u;ci.pPushConstantRanges=&range;
        Verify(vkCreatePipelineLayout(device,&ci,nullptr,&layout),"pipeline layout");
        for(Word i=0;i<2u;++i){
            VkPipelineShaderStageCreateInfo stage{};
            stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
            stage.module=shaders[i];stage.pName="main";
            VkComputePipelineCreateInfo cp{};
            cp.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cp.stage=stage;cp.layout=layout;
            Verify(vkCreateComputePipelines(device,VK_NULL_HANDLE,1u,
                                            &cp,nullptr,&pipelines[i]),
                   "compute pipeline");
        }
    }
    void Populate(){
        constexpr std::array<Word,8u> e4m3{
            0x00u,0x30u,0x38u,0x40u,0xb0u,0xb8u,0x28u,0x34u};
        for(Word slot=0u;slot<kBuffers;++slot){
            void* mem{};
            Verify(vkMapMemory(device,bufs[slot].memory,0u,bytes,0u,&mem),
                   "map source");
            auto* words=static_cast<Word*>(mem);
            for(Word i=0u;i<tokens*256u;++i){
                if(slot>=3u) {words[i]=0xbaadf00du;continue;}
                Word packed{};
                for(Word lane=0u;lane<4u;++lane){
                    const auto pick=(i*7u+lane*11u+slot*3u+seed*17u+
                                     (i/8u)*5u)%e4m3.size();
                    const auto byte=seed==0u&&slot<2u?0u:e4m3[pick];
                    packed|=byte<<(8u*lane);
                }
                words[i]=packed;
            }
            vkUnmapMemory(device,bufs[slot].memory);
        }
    }
    void Compute(){
        VkCommandBufferBeginInfo bi{};
        bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Verify(vkBeginCommandBuffer(command,&bi),"begin");
        VkMemoryBarrier host{};
        host.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
        host.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,1u,&host,
            0u,nullptr,0u,nullptr);
        const std::array<Word,5u> push{0u,0u,0u,0u,tokens};
        for(Word variant=0u;variant<2u;++variant){
            vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,
                              pipelines[variant]);
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,
                                    layout,0u,1u,&sets[variant],0u,nullptr);
            vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,
                               0u,20u,push.data());
            vkCmdDispatch(command,(tokens+31u)/32u,32u,1u);
        }
        VkMemoryBarrier completion_barrier{};
        completion_barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        completion_barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        completion_barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,0u,1u,&completion_barrier,
            0u,nullptr,0u,nullptr);
        Verify(vkEndCommandBuffer(command),"end");
        VkFenceCreateInfo fc{};
        fc.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Verify(vkCreateFence(device,&fc,nullptr,&completion),"fence");
        VkSubmitInfo submit{};
        submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount=1u;submit.pCommandBuffers=&command;
        Verify(vkQueueSubmit(queue,1u,&submit,completion),"submit");
        Verify(vkWaitForFences(device,1u,&completion,
                               VK_TRUE,20000000000ull),"fence wait");
    }
    void CheckOutputs()const{
        std::vector<Word> expected(std::size_t(tokens)*256u);
        for(Word v=0u;v<2u;++v){
            void* mapped{};
            Verify(vkMapMemory(device,bufs[3u+v].memory,0u,bytes,0u,&mapped),
                   "readback");
            const auto* data=static_cast<const Word*>(mapped);
            if(v==0u){
                for(std::size_t i=0u;i<expected.size();++i)
                    expected[i]=data[i];
            }else{
                for(std::size_t i=0u;i<expected.size();++i){
                    if(expected[i]!=data[i]){
                        const auto before=expected[i],after=data[i];
                        vkUnmapMemory(device,bufs[3u+v].memory);
                        throw std::runtime_error(
                            "Tiled attention differs from native shader at word "+
                            std::to_string(i)+" baseline="+std::to_string(before)+
                            " tiled="+std::to_string(after));
                    }
                }
            }
            vkUnmapMemory(device,bufs[3u+v].memory);
        }
        bool changed=false;
        for(const auto value:expected)if(value!=0xbaadf00du)changed=true;
        if(!changed)throw std::runtime_error("No observed GPU output writes");
        std::cout<<"R09_MIN_VIT_ORIGINAL_VS_TILED_VULKAN_GPU_EXACT_PASS"
                 <<" tokens="<<tokens<<" fixture="<<seed
                 <<" words="<<expected.size()<<"\n";
    }
    void Run(const char* original,const char* tiled){
        Setup();BuildPipelines(original,tiled);
        Populate();Compute();CheckOutputs();
        if(validation){
            if(validation_errors.load()!=0u)
                throw std::runtime_error(
                    "Khronos reported attention Vulkan validation errors");
            std::cout<<"R09_MIN_VIT_ATTENTION_KHRONOS_ZERO_ERRORS"
                     <<" tokens="<<tokens<<" fixture="<<seed<<"\n";
        }
    }
};
} // namespace
int main(int argc,char** argv){
    if(argc!=3){
        std::cerr<<"usage: r09_min_vk_tiled_attention_compare original.spv tiled.spv\n";
        return 2;
    }
    try{
        for(const Word tokens : {1u,31u,32u,37u,64u})
            for(const Word fixture : {0u,1u}){
                Compare test(tokens,fixture);
                test.Run(argv[1],argv[2]);
            }
        std::cout<<"R09_MIN_VIT_TILED_ATTENTION_ALL_10_GPU_CASES_PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"R09_MIN_VIT_TILED_ATTENTION_COMPARE_FAIL "<<e.what()<<"\n";
        return 1;
    }
}
