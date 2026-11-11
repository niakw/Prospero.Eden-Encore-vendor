// SPDX-License-Identifier: MIT
// REAL maintained RDNA2 FSwin Dense shader (stages 0/2), but PUBLIC
// deterministic fake E4M3FN weights/input, NOT the learned Original model.
// Compare private per-layer expanded tensors against ONE shared expanded
// scratch VkBuffer over 4 sequential C32 layers, exact per-layer readback.
// No PS5 build, no full six-pass FSwin graph or complete DLSS-NR inference.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::uint32_t kLayers=4u;
constexpr std::uint32_t kTokens=64u;
constexpr std::uint32_t kChannels=32u;
constexpr VkDeviceSize kIo=kTokens*kChannels;
constexpr VkDeviceSize kExpanded=kIo*4u;
constexpr VkDeviceSize kWeights=8192u;
constexpr VkDeviceSize kScales=kChannels*sizeof(float);
static_assert(kExpanded==8192u&&kIo==2048u);
void Must(VkResult result,const char* what){
    if(result!=VK_SUCCESS)
        throw std::runtime_error(std::string(what)+": "+
                                 std::to_string(result));
}
struct Buffer{VkBuffer handle{};VkDeviceMemory memory{};VkDeviceSize bytes{};};
struct FSwinDenseParams{
    std::uint32_t x_base_bytes{},w_base_bytes{},residual_base_bytes{},
                  scale_base_floats{},out_base_bytes{},windows{},
                  channels{},stage{};
};
static_assert(sizeof(FSwinDenseParams)==8u*sizeof(std::uint32_t));
struct Host{
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkPhysicalDeviceMemoryProperties memory_types{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queue_family{};
    VkShaderModule shader{};
    VkDescriptorSetLayout set_layout{};
    VkDescriptorPool descriptor_pool{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    VkCommandPool command_pool{};
    VkCommandBuffer cmd{};
    VkFence fence{};
    std::vector<Buffer> buffers{};
    std::vector<VkDescriptorSet> sets{};
    bool pooled{};
    explicit Host(bool requested):pooled{requested}{}
    ~Host(){
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
            if(command_pool)vkDestroyCommandPool(device,command_pool,nullptr);
            for(const auto& b:buffers){
                if(b.handle)vkDestroyBuffer(device,b.handle,nullptr);
                if(b.memory)vkFreeMemory(device,b.memory,nullptr);
            }
            vkDestroyDevice(device,nullptr);
        }
        if(instance)vkDestroyInstance(instance,nullptr);
    }
    std::size_t In(std::uint32_t layer)const{return 2u*layer;}
    std::size_t Out(std::uint32_t layer)const{return 2u*layer+1u;}
    std::size_t Expanded(std::uint32_t layer)const{
        return pooled?8u:8u+layer;
    }
    std::size_t Weights()const{return pooled?9u:12u;}
    std::size_t Scales()const{return pooled?10u:13u;}
    [[nodiscard]] std::uint32_t MemoryType(
        std::uint32_t bits,VkMemoryPropertyFlags flags)const{
        for(std::uint32_t i=0u;i<memory_types.memoryTypeCount;++i)
            if((bits&(1u<<i))&&
                (memory_types.memoryTypes[i].propertyFlags&flags)==flags)
                return i;
        throw std::runtime_error("Vulkan HOST coherent memory unavailable");
    }
    void Initialize(){
        VkApplicationInfo application{};
        application.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.apiVersion=VK_API_VERSION_1_1;
        application.pApplicationName="R09 FSwin authentic Dense scratch A/B";
        VkInstanceCreateInfo ici{};
        ici.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ici.pApplicationInfo=&application;
        Must(vkCreateInstance(&ici,nullptr,&instance),"instance");
        std::uint32_t count{};
        Must(vkEnumeratePhysicalDevices(instance,&count,nullptr),
             "physical count");
        if(!count)throw std::runtime_error("No Vulkan physical device");
        std::vector<VkPhysicalDevice> available(count);
        Must(vkEnumeratePhysicalDevices(instance,&count,available.data()),
             "physical list");
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
        if(!physical)throw std::runtime_error("No Vulkan compute queue");
        vkGetPhysicalDeviceMemoryProperties(physical,&memory_types);
        const float priority=1.0f;
        VkDeviceQueueCreateInfo q{};
        q.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex=queue_family;
        q.queueCount=1u;
        q.pQueuePriorities=&priority;
        VkDeviceCreateInfo dci{};
        dci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount=1u;
        dci.pQueueCreateInfos=&q;
        Must(vkCreateDevice(physical,&dci,nullptr,&device),"device");
        vkGetDeviceQueue(device,queue_family,0u,&queue);
        // 4 independent Upload inputs, 4 independent GPU outputs,
        // 1 shared or 4 private expanded tensors, and weights/scales.
        buffers.resize(pooled?11u:14u);
        for(std::size_t i=0u;i<buffers.size();++i){
            auto& b=buffers[i];
            b.bytes=i<8u?kIo:
                i<Weights()?kExpanded:
                i==Weights()?kWeights:kScales;
            VkBufferCreateInfo bi{};
            bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size=b.bytes;
            bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT|
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Must(vkCreateBuffer(device,&bi,nullptr,&b.handle),"buffer");
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device,b.handle,&req);
            VkMemoryAllocateInfo ma{};
            ma.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ma.allocationSize=req.size;
            ma.memoryTypeIndex=MemoryType(
                req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            Must(vkAllocateMemory(device,&ma,nullptr,&b.memory),"memory");
            Must(vkBindBufferMemory(device,b.handle,b.memory,0u),"bind memory");
        }
        if(pooled&&buffers[Expanded(0)].handle!=
           buffers[Expanded(3)].handle)
            throw std::runtime_error("Pooled expanded identity mismatch");
        // Public deterministic FP8 input: [0.125,0.25,0.5,1.0] repeated.
        {
            void* mapped{};
            Must(vkMapMemory(device,buffers[In(0)].memory,0u,kIo,0u,&mapped),
                 "input map");
            auto* bytes=static_cast<std::uint8_t*>(mapped);
            constexpr std::array<std::uint8_t,4u> values{
                0x20u,0x28u,0x30u,0x38u};
            for(std::size_t i=0u;i<kIo;++i)
                bytes[i]=values[(i/32u+i%32u)%values.size()];
            vkUnmapMemory(device,buffers[In(0)].memory);
        }
        // Two learned-shape public identity matrices, each genuine
        // row-major FP8 1.0 weights; zeros elsewhere.
        {
            void* mapped{};
            Must(vkMapMemory(device,buffers[Weights()].memory,0u,kWeights,
                             0u,&mapped),"weights map");
            auto* bytes=static_cast<std::uint8_t*>(mapped);
            std::memset(bytes,0,kWeights);
            for(std::uint32_t row=0u;row<128u;++row)
                bytes[row*32u+row%32u]=0x38u;
            for(std::uint32_t row=0u;row<32u;++row)
                bytes[4096u+row*128u+row]=0x38u;
            vkUnmapMemory(device,buffers[Weights()].memory);
        }
        {
            void* mapped{};
            Must(vkMapMemory(device,buffers[Scales()].memory,0u,kScales,0u,
                             &mapped),"scales map");
            std::memset(mapped,0,kScales);
            vkUnmapMemory(device,buffers[Scales()].memory);
        }
    }
    void InitializePipeline(const std::string& source){
        std::ifstream in(source,std::ios::binary|std::ios::ate);
        if(!in)throw std::runtime_error("Native FSwin Dense SPIRV missing");
        const std::streamoff bytes=in.tellg();
        if(bytes<=0||bytes%4u)
            throw std::runtime_error("Invalid native SPIRV size");
        std::vector<std::uint32_t> code(static_cast<std::size_t>(bytes)/4u);
        in.seekg(0);
        in.read(reinterpret_cast<char*>(code.data()),bytes);
        if(!in)throw std::runtime_error("Native SPIRV read failed");
        VkShaderModuleCreateInfo sm{};
        sm.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        sm.codeSize=static_cast<std::size_t>(bytes);
        sm.pCode=code.data();
        Must(vkCreateShaderModule(device,&sm,nullptr,&shader),"shader module");
        std::array<VkDescriptorSetLayoutBinding,5u> bindings{};
        for(std::size_t i=0u;i<bindings.size();++i){
            bindings[i].binding=static_cast<std::uint32_t>(i);
            bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount=1u;
            bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo sl{};
        sl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sl.bindingCount=static_cast<std::uint32_t>(bindings.size());
        sl.pBindings=bindings.data();
        Must(vkCreateDescriptorSetLayout(device,&sl,nullptr,&set_layout),
             "set layout");
        VkDescriptorPoolSize psize{};
        psize.type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        psize.descriptorCount=kLayers*2u*5u;
        VkDescriptorPoolCreateInfo pi{};
        pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets=kLayers*2u;
        pi.poolSizeCount=1u;
        pi.pPoolSizes=&psize;
        Must(vkCreateDescriptorPool(device,&pi,nullptr,&descriptor_pool),
             "descriptor pool");
        sets.resize(kLayers*2u);
        std::vector<VkDescriptorSetLayout> wanted(sets.size(),set_layout);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool=descriptor_pool;
        ai.descriptorSetCount=static_cast<std::uint32_t>(sets.size());
        ai.pSetLayouts=wanted.data();
        Must(vkAllocateDescriptorSets(device,&ai,sets.data()),
             "descriptor sets");
        std::vector<VkDescriptorBufferInfo> infos(kLayers*2u*5u);
        std::vector<VkWriteDescriptorSet> writes(infos.size());
        for(std::uint32_t layer=0u;layer<kLayers;++layer){
            for(std::uint32_t step=0u;step<2u;++step){
                const auto input=step==0u?In(layer):Expanded(layer);
                const auto output=step==0u?Expanded(layer):Out(layer);
                const std::array<std::size_t,5u> owner{
                    input,Weights(),In(layer),Scales(),output};
                const auto set_index=layer*2u+step;
                for(std::uint32_t binding=0u;binding<5u;++binding){
                    const auto index=set_index*5u+binding;
                    const auto& b=buffers[owner[binding]];
                    infos[index]={b.handle,0u,b.bytes};
                    auto& w=writes[index];
                    w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w.dstSet=sets[set_index];
                    w.dstBinding=binding;
                    w.descriptorCount=1u;
                    w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    w.pBufferInfo=&infos[index];
                }
            }
        }
        vkUpdateDescriptorSets(device,static_cast<std::uint32_t>(writes.size()),
                               writes.data(),0u,nullptr);
        VkPushConstantRange push{};
        push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        push.size=sizeof(FSwinDenseParams);
        VkPipelineLayoutCreateInfo pl{};
        pl.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount=1u;
        pl.pSetLayouts=&set_layout;
        pl.pushConstantRangeCount=1u;
        pl.pPushConstantRanges=&push;
        Must(vkCreatePipelineLayout(device,&pl,nullptr,&layout),
             "pipeline layout");
        VkComputePipelineCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module=shader;
        ci.stage.pName="main";
        ci.layout=layout;
        Must(vkCreateComputePipelines(device,VK_NULL_HANDLE,1u,
                                      &ci,nullptr,&pipeline),"pipeline");
        VkCommandPoolCreateInfo cp{};
        cp.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cp.queueFamilyIndex=queue_family;
        Must(vkCreateCommandPool(device,&cp,nullptr,&command_pool),
             "command pool");
        VkCommandBufferAllocateInfo cb{};
        cb.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cb.commandPool=command_pool;
        cb.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cb.commandBufferCount=1u;
        Must(vkAllocateCommandBuffers(device,&cb,&cmd),"command buffer");
    }
    void Run(){
        VkCommandBufferBeginInfo start{};
        start.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        start.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Must(vkBeginCommandBuffer(cmd,&start),"begin");
        VkMemoryBarrier upload{};
        upload.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        upload.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
        upload.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,1u,&upload,0u,nullptr,0u,nullptr);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        for(std::uint32_t layer=0u;layer<kLayers;++layer){
            for(std::uint32_t step=0u;step<2u;++step){
                const auto set=sets[layer*2u+step];
                vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,
                    layout,0u,1u,&set,0u,nullptr);
                FSwinDenseParams params{};
                params.w_base_bytes=step==0u?0u:4096u;
                params.windows=1u;
                params.channels=kChannels;
                params.stage=step==0u?0u:2u;
                vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,
                    0u,sizeof(params),&params);
                // REAL maintained FSwinDense::Record dispatch for C32.
                vkCmdDispatch(cmd,1u,64u,1u);
                VkMemoryBarrier visible{};
                visible.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                visible.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                visible.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|
                                     VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0u,1u,&visible,0u,nullptr,0u,nullptr);
            }
            if(layer+1u<kLayers){
                VkBufferMemoryBarrier release{};
                release.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                release.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                release.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
                release.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                release.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                release.buffer=buffers[Out(layer)].handle;
                release.size=kIo;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0u,0u,nullptr,1u,&release,0u,nullptr);
                VkBufferCopy copy{};copy.size=kIo;
                vkCmdCopyBuffer(cmd,buffers[Out(layer)].handle,
                                buffers[In(layer+1u)].handle,1u,&copy);
                VkBufferMemoryBarrier acquire{};
                acquire.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                acquire.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
                acquire.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
                acquire.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                acquire.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                acquire.buffer=buffers[In(layer+1u)].handle;
                acquire.size=kIo;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0u,0u,nullptr,1u,&acquire,0u,nullptr);
            }
        }
        VkMemoryBarrier readback{};
        readback.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        readback.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        readback.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0u,1u,&readback,0u,nullptr,0u,nullptr);
        Must(vkEndCommandBuffer(cmd),"end");
        VkFenceCreateInfo f{};
        f.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Must(vkCreateFence(device,&f,nullptr,&fence),"fence");
        VkSubmitInfo submission{};
        submission.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submission.commandBufferCount=1u;
        submission.pCommandBuffers=&cmd;
        Must(vkQueueSubmit(queue,1u,&submission,fence),"submit");
        Must(vkWaitForFences(device,1u,&fence,VK_TRUE,10000000000ull),
             "wait");
    }
    [[nodiscard]] std::vector<std::uint8_t> ReadOutputs()const{
        std::vector<std::uint8_t> result(kLayers*kIo);
        for(std::uint32_t layer=0u;layer<kLayers;++layer){
            void* source{};
            Must(vkMapMemory(device,buffers[Out(layer)].memory,0u,
                             kIo,0u,&source),"readback");
            std::memcpy(result.data()+layer*kIo,source,kIo);
            vkUnmapMemory(device,buffers[Out(layer)].memory);
        }
        return result;
    }
};
[[nodiscard]] std::vector<std::uint8_t>
Execute(const std::string& shader,bool pooled){
    Host host{pooled};
    host.Initialize();
    host.InitializePipeline(shader);
    host.Run();
    return host.ReadOutputs();
}
}
int main(int argc,char** argv){
    if(argc!=2){
        std::cerr<<"usage: r09_fswin_native_dense_pool_compare "
                    "<authentic-fswin-dense.spv>\n";
        return 2;
    }
    try{
        const auto reference=Execute(argv[1],false);
        const auto pooled=Execute(argv[1],true);
        if(reference!=pooled)
            throw std::runtime_error(
                "Authentic FSwinDense GPU output changed with shared scratch");
        unsigned nonzero{},nonconstant{};
        for(std::size_t i=0u;i<reference.size();++i){
            nonzero+=reference[i]!=0u;
            nonconstant+=reference[i]!=reference[0];
        }
        if(nonzero<kIo||nonconstant<kIo/4u)
            throw std::runtime_error(
                "GPU result degenerate; A/B comparison not meaningful");
        std::cout<<"R09_FSWIN_REAL_NATIVE_DENSE_GPU_POOL_AB_PASS "
                 <<"layers="<<kLayers<<" channels="<<kChannels
                 <<" windows=1 stages=0,2 shared_expanded=1"
                 <<" baseline_expanded=4 outputs_checked="<<reference.size()
                 <<" nonzero_bytes="<<nonzero<<" nonconstant="<<nonconstant
                 <<" weights=PUBLIC_IDENTITY_FIXTURE"
                 <<" not_full_FSwin_model=YES no_PS5=YES\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"R09_FSWIN_REAL_NATIVE_DENSE_GPU_AB_FAIL: "
                 <<e.what()<<"\n";
        return 1;
    }
}
