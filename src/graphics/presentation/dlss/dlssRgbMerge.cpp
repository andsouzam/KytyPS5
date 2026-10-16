#include "graphics/presentation/dlss/dlssRgbMerge.h"

#include <array>
#include <atomic>

namespace Libs::Graphics {
namespace {
// glslang16.5.0 -V --target-env vulkan1.1. SPIR-V SHA256:
// 6341ea7a9743f9f9fb767cb51364438cb9e017b1101162d827de9878f21e6949.
// #version450; local_size8x8. Bindings0/1/2 are rgba16f storage image2D.
// layout(push_constant) uniform Extent { uvec2 size; } extent;
// if(any(greaterThanEqual(gl_GlobalInvocationID.xy,extent.size))) return;
// ivec2 p=ivec2(gl_GlobalInvocationID.xy); vec3 rgb=imageLoad(source,p).rgb;
// float a=imageLoad(destinationA,p).a, b=imageLoad(destinationB,p).a;
// imageStore(destinationA,p,vec4(rgb,a)); imageStore(destinationB,p,vec4(rgb,b));
constexpr uint32_t MergeShader[] = {
    0x07230203u, 0x00010300u, 0x0008000bu, 0x00000051u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0006000fu, 0x00000005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000cu, 0x00060010u, 0x00000004u,
    0x00000011u, 0x00000008u, 0x00000008u, 0x00000001u, 0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00040005u, 0x00000009u, 0x65786970u, 0x0000006cu, 0x00080005u,
    0x0000000cu, 0x475f6c67u, 0x61626f6cu, 0x766e496cu, 0x7461636fu, 0x496e6f69u, 0x00000044u, 0x00040005u,
    0x00000010u, 0x65747845u, 0x0000746eu, 0x00050006u, 0x00000010u, 0x00000000u, 0x657a6973u, 0x00000000u,
    0x00040005u, 0x00000012u, 0x65747865u, 0x0000746eu, 0x00030005u, 0x00000021u, 0x00000070u, 0x00030005u,
    0x00000027u, 0x00626772u, 0x00050005u, 0x0000002au, 0x72756f73u, 0x725f6563u, 0x00006267u, 0x00040005u,
    0x00000031u, 0x68706c61u, 0x00615f61u, 0x00060005u, 0x00000032u, 0x74736564u, 0x74616e69u, 0x5f6e6f69u,
    0x00000061u, 0x00040005u, 0x00000038u, 0x68706c61u, 0x00625f61u, 0x00060005u, 0x00000039u, 0x74736564u,
    0x74616e69u, 0x5f6e6f69u, 0x00000062u, 0x00040047u, 0x0000000cu, 0x0000000bu, 0x0000001cu, 0x00030047u,
    0x00000010u, 0x00000002u, 0x00050048u, 0x00000010u, 0x00000000u, 0x00000023u, 0x00000000u, 0x00030047u,
    0x0000002au, 0x00000018u, 0x00040047u, 0x0000002au, 0x00000021u, 0x00000000u, 0x00040047u, 0x0000002au,
    0x00000022u, 0x00000000u, 0x00040047u, 0x00000032u, 0x00000021u, 0x00000001u, 0x00040047u, 0x00000032u,
    0x00000022u, 0x00000000u, 0x00040047u, 0x00000039u, 0x00000021u, 0x00000002u, 0x00040047u, 0x00000039u,
    0x00000022u, 0x00000000u, 0x00040047u, 0x00000050u, 0x0000000bu, 0x00000019u, 0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u, 0x00040015u, 0x00000006u, 0x00000020u, 0x00000000u, 0x00040017u,
    0x00000007u, 0x00000006u, 0x00000002u, 0x00040020u, 0x00000008u, 0x00000007u, 0x00000007u, 0x00040017u,
    0x0000000au, 0x00000006u, 0x00000003u, 0x00040020u, 0x0000000bu, 0x00000001u, 0x0000000au, 0x0004003bu,
    0x0000000bu, 0x0000000cu, 0x00000001u, 0x0003001eu, 0x00000010u, 0x00000007u, 0x00040020u, 0x00000011u,
    0x00000009u, 0x00000010u, 0x0004003bu, 0x00000011u, 0x00000012u, 0x00000009u, 0x00040015u, 0x00000013u,
    0x00000020u, 0x00000001u, 0x0004002bu, 0x00000013u, 0x00000014u, 0x00000000u, 0x00040020u, 0x00000015u,
    0x00000009u, 0x00000007u, 0x00020014u, 0x00000018u, 0x00040017u, 0x00000019u, 0x00000018u, 0x00000002u,
    0x00040017u, 0x0000001fu, 0x00000013u, 0x00000002u, 0x00040020u, 0x00000020u, 0x00000007u, 0x0000001fu,
    0x00030016u, 0x00000024u, 0x00000020u, 0x00040017u, 0x00000025u, 0x00000024u, 0x00000003u, 0x00040020u,
    0x00000026u, 0x00000007u, 0x00000025u, 0x00090019u, 0x00000028u, 0x00000024u, 0x00000001u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000002u, 0x00000002u, 0x00040020u, 0x00000029u, 0x00000000u, 0x00000028u,
    0x0004003bu, 0x00000029u, 0x0000002au, 0x00000000u, 0x00040017u, 0x0000002du, 0x00000024u, 0x00000004u,
    0x00040020u, 0x00000030u, 0x00000007u, 0x00000024u, 0x0004003bu, 0x00000029u, 0x00000032u, 0x00000000u,
    0x0004002bu, 0x00000006u, 0x00000036u, 0x00000003u, 0x0004003bu, 0x00000029u, 0x00000039u, 0x00000000u,
    0x0004002bu, 0x00000006u, 0x0000004eu, 0x00000008u, 0x0004002bu, 0x00000006u, 0x0000004fu, 0x00000001u,
    0x0006002cu, 0x0000000au, 0x00000050u, 0x0000004eu, 0x0000004eu, 0x0000004fu, 0x00050036u, 0x00000002u,
    0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0004003bu, 0x00000008u, 0x00000009u,
    0x00000007u, 0x0004003bu, 0x00000020u, 0x00000021u, 0x00000007u, 0x0004003bu, 0x00000026u, 0x00000027u,
    0x00000007u, 0x0004003bu, 0x00000030u, 0x00000031u, 0x00000007u, 0x0004003bu, 0x00000030u, 0x00000038u,
    0x00000007u, 0x0004003du, 0x0000000au, 0x0000000du, 0x0000000cu, 0x0007004fu, 0x00000007u, 0x0000000eu,
    0x0000000du, 0x0000000du, 0x00000000u, 0x00000001u, 0x0003003eu, 0x00000009u, 0x0000000eu, 0x0004003du,
    0x00000007u, 0x0000000fu, 0x00000009u, 0x00050041u, 0x00000015u, 0x00000016u, 0x00000012u, 0x00000014u,
    0x0004003du, 0x00000007u, 0x00000017u, 0x00000016u, 0x000500aeu, 0x00000019u, 0x0000001au, 0x0000000fu,
    0x00000017u, 0x0004009au, 0x00000018u, 0x0000001bu, 0x0000001au, 0x000300f7u, 0x0000001du, 0x00000000u,
    0x000400fau, 0x0000001bu, 0x0000001cu, 0x0000001du, 0x000200f8u, 0x0000001cu, 0x000100fdu, 0x000200f8u,
    0x0000001du, 0x0004003du, 0x00000007u, 0x00000022u, 0x00000009u, 0x0004007cu, 0x0000001fu, 0x00000023u,
    0x00000022u, 0x0003003eu, 0x00000021u, 0x00000023u, 0x0004003du, 0x00000028u, 0x0000002bu, 0x0000002au,
    0x0004003du, 0x0000001fu, 0x0000002cu, 0x00000021u, 0x00050062u, 0x0000002du, 0x0000002eu, 0x0000002bu,
    0x0000002cu, 0x0008004fu, 0x00000025u, 0x0000002fu, 0x0000002eu, 0x0000002eu, 0x00000000u, 0x00000001u,
    0x00000002u, 0x0003003eu, 0x00000027u, 0x0000002fu, 0x0004003du, 0x00000028u, 0x00000033u, 0x00000032u,
    0x0004003du, 0x0000001fu, 0x00000034u, 0x00000021u, 0x00050062u, 0x0000002du, 0x00000035u, 0x00000033u,
    0x00000034u, 0x00050051u, 0x00000024u, 0x00000037u, 0x00000035u, 0x00000003u, 0x0003003eu, 0x00000031u,
    0x00000037u, 0x0004003du, 0x00000028u, 0x0000003au, 0x00000039u, 0x0004003du, 0x0000001fu, 0x0000003bu,
    0x00000021u, 0x00050062u, 0x0000002du, 0x0000003cu, 0x0000003au, 0x0000003bu, 0x00050051u, 0x00000024u,
    0x0000003du, 0x0000003cu, 0x00000003u, 0x0003003eu, 0x00000038u, 0x0000003du, 0x0004003du, 0x00000028u,
    0x0000003eu, 0x00000032u, 0x0004003du, 0x0000001fu, 0x0000003fu, 0x00000021u, 0x0004003du, 0x00000025u,
    0x00000040u, 0x00000027u, 0x0004003du, 0x00000024u, 0x00000041u, 0x00000031u, 0x00050051u, 0x00000024u,
    0x00000042u, 0x00000040u, 0x00000000u, 0x00050051u, 0x00000024u, 0x00000043u, 0x00000040u, 0x00000001u,
    0x00050051u, 0x00000024u, 0x00000044u, 0x00000040u, 0x00000002u, 0x00070050u, 0x0000002du, 0x00000045u,
    0x00000042u, 0x00000043u, 0x00000044u, 0x00000041u, 0x00040063u, 0x0000003eu, 0x0000003fu, 0x00000045u,
    0x0004003du, 0x00000028u, 0x00000046u, 0x00000039u, 0x0004003du, 0x0000001fu, 0x00000047u, 0x00000021u,
    0x0004003du, 0x00000025u, 0x00000048u, 0x00000027u, 0x0004003du, 0x00000024u, 0x00000049u, 0x00000038u,
    0x00050051u, 0x00000024u, 0x0000004au, 0x00000048u, 0x00000000u, 0x00050051u, 0x00000024u, 0x0000004bu,
    0x00000048u, 0x00000001u, 0x00050051u, 0x00000024u, 0x0000004cu, 0x00000048u, 0x00000002u, 0x00070050u,
    0x0000002du, 0x0000004du, 0x0000004au, 0x0000004bu, 0x0000004cu, 0x00000049u, 0x00040063u, 0x00000046u,
    0x00000047u, 0x0000004du, 0x000100fdu, 0x00010038u,
};
constexpr size_t MaximumDescriptorLeases = 16;
DlssStatus InvalidMerge(const char* message) {
    return {DlssStatusCode::InvalidArgument, 0, message};
}
}

struct DlssRgbMerge::State {
    VkDevice device{};
    VkDescriptorSetLayout descriptors{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    std::shared_ptr<std::atomic<size_t>> outstanding = std::make_shared<std::atomic<size_t>>(0);
    struct PoolLease {
        VkDevice device{};
        VkDescriptorPool pool{};
        PFN_vkDestroyDescriptorPool destroy{};
        std::shared_ptr<std::atomic<size_t>> outstanding;
        bool counted = false;
        ~PoolLease() {
            if (pool) destroy(device, pool, nullptr);
            if (counted) outstanding->fetch_sub(1, std::memory_order_relaxed);
        }
    };
    std::array<std::shared_ptr<void>, MaximumDescriptorLeases> retained{};
    size_t retained_count = 0;
#define MERGE_FUNCTIONS(X) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) X(vkCreateComputePipelines) X(vkDestroyPipeline) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) \
    X(vkUpdateDescriptorSets) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
    X(vkCmdPushConstants) X(vkCmdDispatch)
#define DECLARE_MERGE(name) PFN_##name name{};
    MERGE_FUNCTIONS(DECLARE_MERGE)
#undef DECLARE_MERGE
    void Destroy() {
        retained = {}; retained_count = 0;
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (descriptors) vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
        pipeline = VK_NULL_HANDLE;
        layout = VK_NULL_HANDLE; descriptors = VK_NULL_HANDLE; device = VK_NULL_HANDLE;
    }
};

DlssRgbMerge::DlssRgbMerge(): m_state(std::make_unique<State>()) {}
DlssRgbMerge::~DlssRgbMerge() { (void)DestroyAfterGpuIdle(); }

DlssStatus DlssRgbMerge::Init(VkDevice device, PFN_vkGetDeviceProcAddr get_device_proc_addr) {
    if (device == VK_NULL_HANDLE || get_device_proc_addr == nullptr)
        return InvalidMerge("RGB merge requires a real device/proc loader");
    auto& state = *m_state;
    if (state.device != VK_NULL_HANDLE)
        return {DlssStatusCode::InvalidState, 0, "RGB merge already initialized"};
    state.device = device;
#define LOAD_MERGE(name) \
    state.name = reinterpret_cast<PFN_##name>(get_device_proc_addr(device, #name)); \
    if (!state.name) { state.Destroy(); return {DlssStatusCode::Unsupported, 0, #name " unavailable"}; }
    MERGE_FUNCTIONS(LOAD_MERGE)
#undef LOAD_MERGE
#undef MERGE_FUNCTIONS
    const auto failed = [&](VkResult result, const char* operation) {
        state.Destroy(); return DlssStatus{DlssStatusCode::NativeError, uint32_t(result), operation};
    };
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i; bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    descriptors.bindingCount = uint32_t(bindings.size()); descriptors.pBindings = bindings.data();
    auto result = state.vkCreateDescriptorSetLayout(device, &descriptors, nullptr, &state.descriptors);
    if (result != VK_SUCCESS) return failed(result, "RGB merge descriptor layout creation failed");
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &state.descriptors;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    result = state.vkCreatePipelineLayout(device, &layout, nullptr, &state.layout);
    if (result != VK_SUCCESS) return failed(result, "RGB merge pipeline layout creation failed");
    VkShaderModuleCreateInfo shader_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_info.codeSize = sizeof(MergeShader); shader_info.pCode = MergeShader;
    VkShaderModule shader{};
    result = state.vkCreateShaderModule(device, &shader_info, nullptr, &shader);
    if (result != VK_SUCCESS) return failed(result, "RGB merge shader creation failed");
    VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    compute.layout = state.layout;
    compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; compute.stage.module = shader;
    compute.stage.pName = "main";
    result = state.vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute, nullptr, &state.pipeline);
    state.vkDestroyShaderModule(device, shader, nullptr);
    if (result != VK_SUCCESS) return failed(result, "RGB merge compute pipeline creation failed");
    return {};
}

DlssStatus DlssRgbMerge::Record(VkCommandBuffer command, VkImageView source,
    VkImageView destination_a, VkImageView destination_b, DlssExtent extent,
    std::shared_ptr<void>* retirement_lease) {
    if (command == VK_NULL_HANDLE || source == VK_NULL_HANDLE || destination_a == VK_NULL_HANDLE ||
        destination_b == VK_NULL_HANDLE || source == destination_a || source == destination_b ||
        destination_a == destination_b || extent.width == 0 || extent.height == 0 ||
        extent.width > 16384 || extent.height > 16384 ||
        (retirement_lease && *retirement_lease))
        return InvalidMerge("RGB merge needs distinct views, a command buffer and bounded nonempty extent");
    auto& state = *m_state;
    if (state.device == VK_NULL_HANDLE || state.pipeline == VK_NULL_HANDLE)
        return {DlssStatusCode::InvalidState, 0, "RGB merge has not been initialized"};
    if (state.outstanding->load(std::memory_order_relaxed) >= MaximumDescriptorLeases)
        return {DlssStatusCode::Unsupported, 0, "RGB merge outstanding descriptor lease capacity exhausted"};
    auto lease = std::make_shared<State::PoolLease>();
    if (!lease) {
        return {DlssStatusCode::NativeError, uint32_t(VK_ERROR_OUT_OF_HOST_MEMORY), "RGB merge lease allocation failed"};
    }

    lease->device = state.device; lease->destroy = state.vkDestroyDescriptorPool;
    lease->outstanding = state.outstanding;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
    auto result = state.vkCreateDescriptorPool(state.device, &pool, nullptr, &lease->pool);
    if (result != VK_SUCCESS)
        return {DlssStatusCode::NativeError, uint32_t(result), "RGB merge descriptor pool creation failed"};
    state.outstanding->fetch_add(1, std::memory_order_relaxed); lease->counted = true;
    VkDescriptorSet descriptor{};
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = lease->pool; allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &state.descriptors;
    result = state.vkAllocateDescriptorSets(state.device, &allocate, &descriptor);
    if (result != VK_SUCCESS)
        return {DlssStatusCode::NativeError, uint32_t(result), "RGB merge descriptor allocation failed"};
    const std::array<VkImageView, 3> views{source, destination_a, destination_b};
    std::array<VkDescriptorImageInfo, 3> images{};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t i = 0; i < images.size(); ++i) {
        images[i].imageView = views[i]; images[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = descriptor; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[i].pImageInfo = &images[i];
    }
    state.vkUpdateDescriptorSets(state.device, uint32_t(writes.size()), writes.data(), 0, nullptr);
    // Publish ownership before the first native recording call. The caller must
    // retain it even if subsequent recording in its own pipeline reports failure.
    if (retirement_lease) *retirement_lease = lease;
    else state.retained[state.retained_count++] = lease;
    const std::array<uint32_t, 2> push{extent.width, extent.height};
    state.vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, state.pipeline);
    state.vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, state.layout,
        0, 1, &descriptor, 0, nullptr);
    state.vkCmdPushConstants(command, state.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
        uint32_t(sizeof(push)), push.data());
    state.vkCmdDispatch(command, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
    return {};
}

DlssStatus DlssRgbMerge::DestroyAfterGpuIdle() {
    auto& state = *m_state;
    if (state.outstanding->load(std::memory_order_relaxed) > state.retained_count)
        return {DlssStatusCode::InvalidState, 0, "RGB merge still has unretired external descriptor leases"};
    state.Destroy(); return {};
}
} // namespace Libs::Graphics
