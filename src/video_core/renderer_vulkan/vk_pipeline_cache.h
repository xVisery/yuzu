// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/functional/hash.hpp>

#include "common/common_types.h"
#include "video_core/engines/const_buffer_engine_interface.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/rasterizer_cache.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_renderpass_cache.h"
#include "video_core/renderer_vulkan/vk_resource_manager.h"
#include "video_core/renderer_vulkan/vk_shader_decompiler.h"
#include "video_core/renderer_vulkan/wrapper.h"
#include "video_core/shader/registry.h"
#include "video_core/shader/shader_ir.h"
#include "video_core/surface.h"

namespace Core {
class System;
}

namespace Vulkan {

class RasterizerVulkan;
class VKComputePipeline;
class VKDescriptorPool;
class VKDevice;
class VKFence;
class VKScheduler;
class VKUpdateDescriptorQueue;

class CachedShader;
using Shader = std::shared_ptr<CachedShader>;
using Maxwell = Tegra::Engines::Maxwell3D::Regs;

using ProgramCode = std::vector<u64>;

struct GraphicsPipelineCacheKey {
    FixedPipelineState fixed_state;
    std::array<GPUVAddr, Maxwell::MaxShaderProgram> shaders;
    RenderPassParams renderpass_params;

    std::size_t Hash() const noexcept {
        std::size_t hash = fixed_state.Hash();
        for (const auto& shader : shaders) {
            boost::hash_combine(hash, shader);
        }
        boost::hash_combine(hash, renderpass_params.Hash());
        return hash;
    }

    bool operator==(const GraphicsPipelineCacheKey& rhs) const noexcept {
        return std::tie(fixed_state, shaders, renderpass_params) ==
               std::tie(rhs.fixed_state, rhs.shaders, rhs.renderpass_params);
    }
};

struct ComputePipelineCacheKey {
    GPUVAddr shader{};
    u32 shared_memory_size{};
    std::array<u32, 3> workgroup_size{};

    std::size_t Hash() const noexcept {
        return static_cast<std::size_t>(shader) ^
               ((static_cast<std::size_t>(shared_memory_size) >> 7) << 40) ^
               static_cast<std::size_t>(workgroup_size[0]) ^
               (static_cast<std::size_t>(workgroup_size[1]) << 16) ^
               (static_cast<std::size_t>(workgroup_size[2]) << 24);
    }

    bool operator==(const ComputePipelineCacheKey& rhs) const noexcept {
        return std::tie(shader, shared_memory_size, workgroup_size) ==
               std::tie(rhs.shader, rhs.shared_memory_size, rhs.workgroup_size);
    }
};

} // namespace Vulkan

namespace std {

template <>
struct hash<Vulkan::GraphicsPipelineCacheKey> {
    std::size_t operator()(const Vulkan::GraphicsPipelineCacheKey& k) const noexcept {
        return k.Hash();
    }
};

template <>
struct hash<Vulkan::ComputePipelineCacheKey> {
    std::size_t operator()(const Vulkan::ComputePipelineCacheKey& k) const noexcept {
        return k.Hash();
    }
};

} // namespace std

namespace Vulkan {

class CachedShader final : public RasterizerCacheObject {
public:
    explicit CachedShader(Core::System& system, Tegra::Engines::ShaderType stage, GPUVAddr gpu_addr,
                          VAddr cpu_addr, ProgramCode program_code, u32 main_offset);
    ~CachedShader();

    GPUVAddr GetGpuAddr() const {
        return gpu_addr;
    }

    std::size_t GetSizeInBytes() const override {
        return program_code.size() * sizeof(u64);
    }

    VideoCommon::Shader::ShaderIR& GetIR() {
        return shader_ir;
    }

    const VideoCommon::Shader::Registry& GetRegistry() const {
        return registry;
    }

    const VideoCommon::Shader::ShaderIR& GetIR() const {
        return shader_ir;
    }

    const ShaderEntries& GetEntries() const {
        return entries;
    }

private:
    static Tegra::Engines::ConstBufferEngineInterface& GetEngine(Core::System& system,
                                                                 Tegra::Engines::ShaderType stage);

    GPUVAddr gpu_addr{};
    ProgramCode program_code;
    VideoCommon::Shader::Registry registry;
    VideoCommon::Shader::ShaderIR shader_ir;
    ShaderEntries entries;
};

class VKPipelineCache final : public RasterizerCache<Shader> {
public:
    explicit VKPipelineCache(Core::System& system, RasterizerVulkan& rasterizer,
                             const VKDevice& device, VKScheduler& scheduler,
                             VKDescriptorPool& descriptor_pool,
                             VKUpdateDescriptorQueue& update_descriptor_queue,
                             VKRenderPassCache& renderpass_cache);
    ~VKPipelineCache();

    std::array<Shader, Maxwell::MaxShaderProgram> GetShaders();

    VKGraphicsPipeline& GetGraphicsPipeline(const GraphicsPipelineCacheKey& key);

    VKComputePipeline& GetComputePipeline(const ComputePipelineCacheKey& key);

protected:
    void Unregister(const Shader& shader) override;

    void FlushObjectInner(const Shader& object) override {}

private:
    std::pair<SPIRVProgram, std::vector<VkDescriptorSetLayoutBinding>> DecompileShaders(
        const GraphicsPipelineCacheKey& key);

    Core::System& system;
    const VKDevice& device;
    VKScheduler& scheduler;
    VKDescriptorPool& descriptor_pool;
    VKUpdateDescriptorQueue& update_descriptor_queue;
    VKRenderPassCache& renderpass_cache;

    std::array<Shader, Maxwell::MaxShaderProgram> last_shaders;

    GraphicsPipelineCacheKey last_graphics_key;
    VKGraphicsPipeline* last_graphics_pipeline = nullptr;

    std::unordered_map<GraphicsPipelineCacheKey, std::unique_ptr<VKGraphicsPipeline>>
        graphics_cache;
    std::unordered_map<ComputePipelineCacheKey, std::unique_ptr<VKComputePipeline>> compute_cache;
};

void FillDescriptorUpdateTemplateEntries(
    const ShaderEntries& entries, u32& binding, u32& offset,
    std::vector<VkDescriptorUpdateTemplateEntryKHR>& template_entries);

} // namespace Vulkan
