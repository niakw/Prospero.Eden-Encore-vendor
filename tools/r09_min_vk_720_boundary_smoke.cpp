// SPDX-License-Identifier: MIT
// Real Vulkan 1.1 720p ViT boundary test using the production GLSL shader.
// 22x40 live pixels -> 960 padded tile tokens, then reverse. NO model weights
// and NO PS5 hardware claim. The 80 tail tokens MUST contain zeros.
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
constexpr std::uint32_t W=22u,H=40u,TOKENS=960u,WORDS=256u;
constexpr VkDeviceSize BYTES=VkDeviceSize(TOKENS)*WORDS*4u;
void Need(VkResult code,const char* what) {
    if(code!=VK_SUCCESS) throw std::runtime_error(
        std::string(what)+" VkResult="+std::to_string(code));
}
struct Buffer { VkBuffer object{}; VkDeviceMemory allocation{}; };
struct Host {
    VkInstance instance{};VkPhysicalDevice gpu{};VkDevice device{};
    VkQueue queue{};std::uint32_t family{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::array<Buffer,3u> buffers{};
    VkDescriptorSetLayout set_layout{};VkDescriptorPool pool{};
    std::array<VkDescriptorSet,2u> sets{};
    VkShaderModule module{};VkPipelineLayout layout{};VkPipeline pipeline{};
    VkCommandPool command_pool{};VkCommandBuffer cmd{};VkFence fence{};
    ~Host(){
        if(device) {
            vkDeviceWaitIdle(device);
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
            if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
            if(module)vkDestroyShaderModule(device,module,nullptr);
            if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
            if(set_layout)vkDestroyDescriptorSetLayout(device,set_layout,nullptr);
            if(command_pool)vkDestroyCommandPool(device,command_pool,nullptr);
            for(auto b:buffers){
                if(b.object)vkDestroyBuffer(device,b.object,nullptr);
                if(b.allocation)vkFreeMemory(device,b.allocation,nullptr);
            }
            vkDestroyDevice(device,nullptr);
        }
        if(instance)vkDestroyInstance(instance,nullptr);
    }
    std::uint32_t MemoryType(std::uint32_t bits) const {
        const auto mask=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(std::uint32_t i=0u;i<memory.memoryTypeCount;++i)
            if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&mask)==mask)
                return i;
        throw std::runtime_error("No host-coherent Vulkan storage memory");
    }
    void Setup(){
        VkApplicationInfo app{};
        app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion=VK_API_VERSION_1_1;
        app.pApplicationName="R09 720p ViT boundary actual GPU test";
        VkInstanceCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo=&app;
        Need(vkCreateInstance(&ci,nullptr,&instance),"vkCreateInstance");
        std::uint32_t count{};
        Need(vkEnumeratePhysicalDevices(instance,&count,nullptr),"get GPUs");
        if(!count)throw std::runtime_error("Vulkan device absent");
        std::vector<VkPhysicalDevice> devices(count);
        Need(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"enumerate");
        for(const auto candidate:devices){
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate,&props);
            if(props.limits.maxPerStageDescriptorStorageBuffers<2u ||
               props.limits.maxStorageBufferRange<BYTES)continue;
            std::uint32_t n{};
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);
            std::vector<VkQueueFamilyProperties> families(n);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,families.data());
            for(std::uint32_t i=0u;i<n;++i) {
                if(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT) {
                    gpu=candidate;family=i;
                    std::cout<<"R09_MIN_720_REPACK_DEVICE "<<props.deviceName<<"\n";
                    break;
                }
            }
            if(gpu)break;
        }
        if(!gpu)throw std::runtime_error("Missing Vulkan compute + storage support");
        vkGetPhysicalDeviceMemoryProperties(gpu,&memory);
        float priority=1.f;
        VkDeviceQueueCreateInfo queue_ci{};
        queue_ci.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_ci.queueFamilyIndex=family;
        queue_ci.queueCount=1u;queue_ci.pQueuePriorities=&priority;
        VkDeviceCreateInfo device_ci{};
        device_ci.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_ci.queueCreateInfoCount=1u;device_ci.pQueueCreateInfos=&queue_ci;
        Need(vkCreateDevice(gpu,&device_ci,nullptr,&device),"device");
        vkGetDeviceQueue(device,family,0u,&queue);
        for(auto& b:buffers) {
            VkBufferCreateInfo info{};
            info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size=BYTES;info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            Need(vkCreateBuffer(device,&info,nullptr,&b.object),"VkBuffer");
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device,b.object,&req);
            VkMemoryAllocateInfo ai{};
            ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize=req.size;
            ai.memoryTypeIndex=MemoryType(req.memoryTypeBits);
            Need(vkAllocateMemory(device,&ai,nullptr,&b.allocation),"VkMemory");
            Need(vkBindBufferMemory(device,b.object,b.allocation,0u),"BindBuffer");
        }
        std::array<VkDescriptorSetLayoutBinding,2u> bindings{};
        for(std::uint32_t i=0u;i<2u;++i)
            bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1u,
                         VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount=2u;li.pBindings=bindings.data();
        Need(vkCreateDescriptorSetLayout(device,&li,nullptr,&set_layout),"layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4u};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets=2u;pi.poolSizeCount=1u;pi.pPoolSizes=&size;
        Need(vkCreateDescriptorPool(device,&pi,nullptr,&pool),"pool");
        std::array<VkDescriptorSetLayout,2u> layouts{set_layout,set_layout};
        VkDescriptorSetAllocateInfo ai{};
        ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool=pool;ai.descriptorSetCount=2u;
        ai.pSetLayouts=layouts.data();
        Need(vkAllocateDescriptorSets(device,&ai,sets.data()),"allocate sets");
        for(std::uint32_t pass=0u;pass<2u;++pass){
            std::array<VkDescriptorBufferInfo,2u> desc{};
            std::array<VkWriteDescriptorSet,2u> writes{};
            for(std::uint32_t i=0u;i<2u;++i){
                desc[i]={buffers[pass+i].object,0u,BYTES};
                auto& w=writes[i];
                w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet=sets[pass];w.dstBinding=i;
                w.descriptorCount=1u;
                w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                w.pBufferInfo=&desc[i];
            }
            vkUpdateDescriptorSets(device,2u,writes.data(),0u,nullptr);
        }
        VkCommandPoolCreateInfo poolinfo{};
        poolinfo.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolinfo.queueFamilyIndex=family;
        Need(vkCreateCommandPool(device,&poolinfo,nullptr,&command_pool),"command pool");
        VkCommandBufferAllocateInfo cai{};
        cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool=command_pool;
        cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount=1u;
        Need(vkAllocateCommandBuffers(device,&cai,&cmd),"command buffer");
    }
    void Pipeline(const char* filename){
        std::ifstream f(filename,std::ios::binary|std::ios::ate);
        if(!f)throw std::runtime_error("Missing genuine repack SPIR-V");
        const auto size=f.tellg();
        if(size<=0||size%4!=0)throw std::runtime_error("Invalid shader size");
        std::vector<std::uint32_t> code(std::size_t(size)/4u);
        f.seekg(0);
        f.read(reinterpret_cast<char*>(code.data()),size);
        if(!f)throw std::runtime_error("Truncated shader");
        VkShaderModuleCreateInfo mi{};
        mi.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        mi.codeSize=std::size_t(size);mi.pCode=code.data();
        Need(vkCreateShaderModule(device,&mi,nullptr,&module),"shader module");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0u,28u};
        VkPipelineLayoutCreateInfo li{};
        li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount=1u;li.pSetLayouts=&set_layout;
        li.pushConstantRangeCount=1u;li.pPushConstantRanges=&range;
        Need(vkCreatePipelineLayout(device,&li,nullptr,&layout),"pipeline layout");
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module=module;stage.pName="main";
        VkComputePipelineCreateInfo ci{};
        ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage=stage;ci.layout=layout;
        Need(vkCreateComputePipelines(device,VK_NULL_HANDLE,1u,&ci,
                                      nullptr,&pipeline),"compute pipeline");
    }
    static std::uint32_t TileOf(std::uint32_t x,std::uint32_t y){
        return ((y/4u)*((W+3u)/4u)+(x/4u))*16u+
                (y%4u)*4u+(x%4u);
    }
    static std::uint32_t Data(std::uint32_t tile,std::uint32_t word){
        return ((tile*271u)^(word*131u)^0x5a7ec5d3u)|1u;
    }
    void Fill(){
        for(std::uint32_t slot=0u;slot<3u;++slot){
            void* m{};
            Need(vkMapMemory(device,buffers[slot].allocation,0u,BYTES,0u,&m),"map");
            auto* value=static_cast<std::uint32_t*>(m);
            for(std::uint32_t t=0u;t<TOKENS;++t)
                for(std::uint32_t w=0u;w<WORDS;++w)
                    value[t*WORDS+w]=(slot==0u)?Data(t,w):0xdeadbeefu;
            vkUnmapMemory(device,buffers[slot].allocation);
        }
    }
    void Run(){
        VkCommandBufferBeginInfo bi{};
        bi.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Need(vkBeginCommandBuffer(cmd,&bi),"begin");
        VkMemoryBarrier host{};
        host.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;
        host.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0u,1u,&host,0u,nullptr,0u,nullptr);
        for(std::uint32_t pass=0u;pass<2u;++pass){
            const std::array<std::uint32_t,7u> push{
                W,H,1024u,TOKENS,pass,0u,0u};
            vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,
                layout,0u,1u,&sets[pass],0u,nullptr);
            vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,
                0u,28u,push.data());
            vkCmdDispatch(cmd,TOKENS,1u,1u);
            VkMemoryBarrier sync{};
            sync.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            sync.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
            sync.dstAccessMask=pass?VK_ACCESS_HOST_READ_BIT:
                                     VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                pass?VK_PIPELINE_STAGE_HOST_BIT:
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u,1u,&sync,0u,nullptr,0u,nullptr);
        }
        Need(vkEndCommandBuffer(cmd),"end");
        VkFenceCreateInfo fi{};fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        Need(vkCreateFence(device,&fi,nullptr,&fence),"fence");
        VkSubmitInfo si{};si.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount=1u;si.pCommandBuffers=&cmd;
        Need(vkQueueSubmit(queue,1u,&si,fence),"submit");
        Need(vkWaitForFences(device,1u,&fence,VK_TRUE,15000000000ull),"fence");
    }
    void Inspect() const {
        std::array<std::vector<std::uint32_t>,2u> actual{};
        for(std::uint32_t slot=1u;slot<=2u;++slot){
            void* m{};
            Need(vkMapMemory(device,buffers[slot].allocation,0u,BYTES,0u,&m),
                 "readback");
            const auto* ptr=static_cast<const std::uint32_t*>(m);
            actual[slot-1u].assign(ptr,ptr+TOKENS*WORDS);
            vkUnmapMemory(device,buffers[slot].allocation);
        }
        std::uint32_t live{},padded{};
        for(std::uint32_t t=0u;t<TOKENS;++t){
            const bool real=t<W*H;
            const auto linear_expected_tile=real?TileOf(t%W,t/W):0u;
            const auto tile_col=t/16u%( (W+3u)/4u);
            const auto tile_row=t/16u/( (W+3u)/4u);
            const auto local=t%16u;
            const auto x=tile_col*4u+local%4u;
            const auto y=tile_row*4u+local/4u;
            const bool real_tile=x<W&&y<H;
            for(std::uint32_t w=0u;w<WORDS;++w){
                const auto expected_linear=real?Data(linear_expected_tile,w):0u;
                const auto expected_tile=real_tile?Data(t,w):0u;
                if(actual[0u][t*WORDS+w]!=expected_linear||
                   actual[1u][t*WORDS+w]!=expected_tile)
                    throw std::runtime_error(
                        "R09_MIN_720_REPACK_GPU_MISMATCH token="+
                        std::to_string(t)+" channel_word="+std::to_string(w)+
                        " direction="+std::to_string(
                            actual[0u][t*WORDS+w]!=expected_linear?0u:1u));
            }
            if(real_tile)++live;else ++padded;
        }
        if(live!=W*H||padded!=TOKENS-W*H)
            throw std::runtime_error("Wrong tiled zero-padding coverage");
        std::cout<<"R09_MIN_720_VIT_BOUNDARY_REAL_VULKAN_ROUNDTRIP_PASS"
                 <<" live="<<live<<" padded="<<padded
                 <<" tokens="<<TOKENS<<"\n";
        std::cout<<"R09_MIN_720_VIT_BOUNDARY_GPU_HOST_ONLY_NO_MODEL_NO_PS5\n";
    }
};
} // namespace
int main(int argc,char** argv){
    if(argc!=2){
        std::cerr<<"usage: r09_min_vk_720_boundary_smoke <real-boundary.spv>\n";
        return 2;
    }
    try{
        Host h{};h.Setup();h.Pipeline(argv[1]);h.Fill();h.Run();h.Inspect();
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"R09_MIN_720_BOUNDARY_GPU_FAIL "<<e.what()<<"\n";
        return 1;
    }
}
