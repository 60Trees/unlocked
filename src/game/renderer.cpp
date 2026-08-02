#include <base/renderer.hpp>
#include <format>
#include <string_view>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <sdl3webgpu.h>

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>
#include <cassert>
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// <AI> (manually tweaked some)
namespace wgpu_util {
    constexpr WGPUStringView toStringView(std::string_view sv) noexcept { return WGPUStringView{sv.data(), sv.size()}; }
    constexpr WGPUStringView toStringView(const char* cstr) noexcept { return WGPUStringView{cstr, WGPU_STRLEN}; }
    constexpr WGPUStringView nullStringView() noexcept { return WGPUStringView{nullptr, WGPU_STRLEN}; }
    constexpr std::string_view fromStringView(WGPUStringView wsv) noexcept {
        return wsv.data ? std::string_view{wsv.data, wsv.length} : std::string_view{};
    }
}  // namespace wgpu_util

constexpr WGPUStringView operator""_wgpu(const char* str, size_t len) noexcept { return WGPUStringView{str, len}; }

class GameRenderer : public Base::Renderer {
    public:
    void init() override;
    void loop() override;
    void quit() override;

    void addAtlasFromData(std::string_view atlas_name, std::span<const unsigned char> bytes) override;
    uint8_t getAtlasId(std::string_view atlas_name) override;

    private:
    // --- GPU-facing vertex layouts (distinct from the CPU-facing Base::Renderer ones) ---
    struct GPUColouredVertex {
        float pos[2];
        float colour[4];
    };
    struct GPUTexturedVertex {
        float pos[2];
        float uv[2];
    };
    struct TransformUBO {
        float offset[2];
        float scale[2];
    };

    struct DrawRun {
        bool textured = false;
        uint8_t atlasId = 0;
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
    };

    struct SpaceGPU {
        WGPUBuffer colouredVB = nullptr;
        size_t colouredCapacity = 0;  // in vertices
        WGPUBuffer texturedVB = nullptr;
        size_t texturedCapacity = 0;  // in vertices

        WGPUBuffer transformUBO = nullptr;
        WGPUBindGroup transformBG = nullptr;
    };

    struct Atlas {
        WGPUTexture texture = nullptr;
        WGPUTextureView view = nullptr;
        WGPUBindGroup bindGroup = nullptr;
        int width = 0;
        int height = 0;
    };

    SDL_Window* window = nullptr;
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    uint32_t surfaceWidth = 0;
    uint32_t surfaceHeight = 0;

    WGPUBindGroupLayout transformBGL = nullptr;
    WGPUBindGroupLayout atlasBGL = nullptr;

    WGPUShaderModule colouredShader = nullptr;
    WGPUShaderModule texturedShader = nullptr;
    WGPURenderPipeline colouredPipeline = nullptr;
    WGPURenderPipeline texturedPipeline = nullptr;

    SpaceGPU worldGPU;
    SpaceGPU uiGPU;

    WGPUSampler atlasSampler = nullptr;
    std::vector<Atlas> atlases;
    std::unordered_map<std::string, uint8_t> atlasIds;

    WGPUAdapter requestAdapterSync(WGPURequestAdapterOptions const& options);
    WGPUDevice requestDeviceSync(WGPUDeviceDescriptor const& descriptor);
    void configureSurface(int width, int height);
    void createBindGroupLayouts();
    void createPipelines();
    void createTransformResources();
    void createSampler();
    void updateTransform(SpaceGPU& space, float offsetX, float offsetY, double zoomUnits);
    void ensureVertexBufferCapacity(WGPUBuffer& buffer, size_t& capacityVerts, size_t neededVerts, size_t vertexStride, const char* label);
    std::vector<DrawRun> buildAndUploadDraws(SpaceGPU& space, const std::vector<Base::Renderer::ColouredVertex>& coloured,
        const std::vector<Base::Renderer::TexturedVertex>& textured);
    void drawSpace(WGPURenderPassEncoder pass, const SpaceGPU& space, const std::vector<DrawRun>& runs);
};

extern "C" Base::BaseClass* GetRenderer() { return new GameRenderer(); }

#define _err(_format) std::runtime_error(std::format _format)

void GameRenderer::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw _err(("SDL_Init failed: {}", SDL_GetError()));

    window = SDL_CreateWindow("Run", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) throw _err(("SDL_CreateWindow failed: {}", SDL_GetError()));

    instance = wgpuCreateInstance(nullptr);
    if (!instance) throw _err(("wgpuCreateInstance failed"));

    surface = SDL_GetWGPUSurface(instance, window);
    if (!surface) throw _err(("Failed to get WGPU Surface from SDL window"));

    {
        WGPURequestAdapterOptions adapterOpts = {};
        adapterOpts.compatibleSurface = surface;
        adapter = requestAdapterSync(adapterOpts);
        if (!adapter) throw _err(("Failed to get WGPUAdapter"));
    }

    {
        WGPUDeviceDescriptor deviceDesc = {
            .label = "Run Device"_wgpu,
            .defaultQueue = {.label = "Run Default Queue"_wgpu},
        };

        device = requestDeviceSync(deviceDesc);
        if (!device) throw _err(("Failed to get WGPUDevice"));
    }

    queue = wgpuDeviceGetQueue(device);
    if (!queue) throw _err(("Failed to get WGPUQueue from WGPUDevice"));

    // --- Surface config ---
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    configureSurface(w, h);

    // Order matters: sampler + bind group layouts must exist before any
    // addAtlasFromData() call, and before pipeline creation.
    createBindGroupLayouts();
    createSampler();
    createPipelines();
    createTransformResources();
}

void GameRenderer::configureSurface(int width, int height) {
    {
        WGPUSurfaceCapabilities caps{};
        wgpuSurfaceGetCapabilities(surface, adapter, &caps);
        surfaceFormat = caps.formatCount > 0 ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }

    surfaceWidth = (uint32_t)width;
    surfaceHeight = (uint32_t)height;

    WGPUSurfaceConfiguration config{
        .device = device,
        .format = surfaceFormat,
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = (uint32_t)width,
        .height = (uint32_t)height,
        .alphaMode = WGPUCompositeAlphaMode_Auto,
        .presentMode = WGPUPresentMode_Fifo,
    };

    wgpuSurfaceConfigure(surface, &config);
}

void GameRenderer::createBindGroupLayouts() {
    WGPUBindGroupLayoutEntry transformEntry{};
    transformEntry.binding = 0;
    transformEntry.visibility = WGPUShaderStage_Vertex;
    transformEntry.buffer.type = WGPUBufferBindingType_Uniform;
    transformEntry.buffer.minBindingSize = sizeof(TransformUBO);

    WGPUBindGroupLayoutDescriptor transformDesc{};
    transformDesc.label = "Transform BGL"_wgpu;
    transformDesc.entryCount = 1;
    transformDesc.entries = &transformEntry;
    transformBGL = wgpuDeviceCreateBindGroupLayout(device, &transformDesc);

    WGPUBindGroupLayoutEntry atlasEntries[2]{};
    atlasEntries[0].binding = 0;
    atlasEntries[0].visibility = WGPUShaderStage_Fragment;
    atlasEntries[0].texture.sampleType = WGPUTextureSampleType_Float;
    atlasEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    atlasEntries[1].binding = 1;
    atlasEntries[1].visibility = WGPUShaderStage_Fragment;
    atlasEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor atlasDesc{};
    atlasDesc.label = "Atlas BGL"_wgpu;
    atlasDesc.entryCount = 2;
    atlasDesc.entries = atlasEntries;
    atlasBGL = wgpuDeviceCreateBindGroupLayout(device, &atlasDesc);

    if (!transformBGL || !atlasBGL) throw _err(("Failed to create bind group layouts"));
}

void GameRenderer::createPipelines() {
    static const char* colouredWGSL = R"(
        struct Transform {
            offset : vec2f,
            scale : vec2f,
        };
        @group(0) @binding(0) var<uniform> transform : Transform;

        struct VSOut {
            @builtin(position) pos : vec4f,
            @location(0) color : vec4f,
        };

        @vertex
        fn vs_main(@location(0) in_pos : vec2f,
                   @location(1) in_color : vec4f) -> VSOut {
            var out : VSOut;
            let ndc = (in_pos - transform.offset) * transform.scale;
            out.pos = vec4f(ndc, 0.0, 1.0);
            out.color = in_color;
            return out;
        }

        @fragment
        fn fs_main(in : VSOut) -> @location(0) vec4f {
            return in.color;
        }
    )";

    static const char* texturedWGSL = R"(
        struct Transform {
            offset : vec2f,
            scale : vec2f,
        };
        @group(0) @binding(0) var<uniform> transform : Transform;
        @group(1) @binding(0) var atlasTex : texture_2d<f32>;
        @group(1) @binding(1) var atlasSamp : sampler;

        struct VSOut {
            @builtin(position) pos : vec4f,
            @location(0) uv : vec2f,
        };

        @vertex
        fn vs_main(@location(0) in_pos : vec2f,
                   @location(1) in_uv : vec2f) -> VSOut {
            var out : VSOut;
            let ndc = (in_pos - transform.offset) * transform.scale;
            out.pos = vec4f(ndc, 0.0, 1.0);
            out.uv = in_uv;
            return out;
        }

        @fragment
        fn fs_main(in : VSOut) -> @location(0) vec4f {
            return textureSample(atlasTex, atlasSamp, in.uv);
        }
    )";

    WGPUBlendState blend{};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};

    // --- Coloured pipeline ---
    {
        WGPUShaderSourceWGSL wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = wgpu_util::toStringView(colouredWGSL);

        WGPUShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &wgslDesc.chain;
        shaderDesc.label = "Coloured Shader"_wgpu;
        colouredShader = wgpuDeviceCreateShaderModule(device, &shaderDesc);
        if (!colouredShader) throw _err(("Failed to create coloured shader module"));

        WGPUVertexAttribute attrs[2]{};
        attrs[0].format = WGPUVertexFormat_Float32x2;
        attrs[0].offset = offsetof(GPUColouredVertex, pos);
        attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x4;
        attrs[1].offset = offsetof(GPUColouredVertex, colour);
        attrs[1].shaderLocation = 1;

        WGPUVertexBufferLayout vbLayout{};
        vbLayout.stepMode = WGPUVertexStepMode_Vertex;
        vbLayout.arrayStride = sizeof(GPUColouredVertex);
        vbLayout.attributeCount = 2;
        vbLayout.attributes = attrs;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = surfaceFormat;
        colorTarget.blend = &blend;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragState{};
        fragState.module = colouredShader;
        fragState.entryPoint = "fs_main"_wgpu;
        fragState.targetCount = 1;
        fragState.targets = &colorTarget;

        WGPUPipelineLayoutDescriptor layoutDesc{};
        layoutDesc.label = "Coloured Pipeline Layout"_wgpu;
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &transformBGL;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label = "Coloured Pipeline"_wgpu;
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = colouredShader;
        pipelineDesc.vertex.entryPoint = "vs_main"_wgpu;
        pipelineDesc.vertex.bufferCount = 1;
        pipelineDesc.vertex.buffers = &vbLayout;
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = 0xFFFFFFFF;
        pipelineDesc.fragment = &fragState;

        colouredPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
        wgpuPipelineLayoutRelease(pipelineLayout);
        if (!colouredPipeline) throw _err(("Failed to create coloured pipeline"));
    }

    // --- Textured pipeline ---
    {
        WGPUShaderSourceWGSL wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = wgpu_util::toStringView(texturedWGSL);

        WGPUShaderModuleDescriptor shaderDesc{};
        shaderDesc.nextInChain = &wgslDesc.chain;
        shaderDesc.label = "Textured Shader"_wgpu;
        texturedShader = wgpuDeviceCreateShaderModule(device, &shaderDesc);
        if (!texturedShader) throw _err(("Failed to create textured shader module"));

        WGPUVertexAttribute attrs[2]{};
        attrs[0].format = WGPUVertexFormat_Float32x2;
        attrs[0].offset = offsetof(GPUTexturedVertex, pos);
        attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x2;
        attrs[1].offset = offsetof(GPUTexturedVertex, uv);
        attrs[1].shaderLocation = 1;

        WGPUVertexBufferLayout vbLayout{};
        vbLayout.stepMode = WGPUVertexStepMode_Vertex;
        vbLayout.arrayStride = sizeof(GPUTexturedVertex);
        vbLayout.attributeCount = 2;
        vbLayout.attributes = attrs;

        WGPUColorTargetState colorTarget{};
        colorTarget.format = surfaceFormat;
        colorTarget.blend = &blend;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragState{};
        fragState.module = texturedShader;
        fragState.entryPoint = "fs_main"_wgpu;
        fragState.targetCount = 1;
        fragState.targets = &colorTarget;

        WGPUBindGroupLayout bgls[2] = {transformBGL, atlasBGL};
        WGPUPipelineLayoutDescriptor layoutDesc{};
        layoutDesc.label = "Textured Pipeline Layout"_wgpu;
        layoutDesc.bindGroupLayoutCount = 2;
        layoutDesc.bindGroupLayouts = bgls;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.label = "Textured Pipeline"_wgpu;
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = texturedShader;
        pipelineDesc.vertex.entryPoint = "vs_main"_wgpu;
        pipelineDesc.vertex.bufferCount = 1;
        pipelineDesc.vertex.buffers = &vbLayout;
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = 0xFFFFFFFF;
        pipelineDesc.fragment = &fragState;

        texturedPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
        wgpuPipelineLayoutRelease(pipelineLayout);
        if (!texturedPipeline) throw _err(("Failed to create textured pipeline"));
    }
}

void GameRenderer::createTransformResources() {
    auto makeSpace = [&](SpaceGPU& space, const char* label) {
        WGPUBufferDescriptor bufDesc{};
        bufDesc.label = wgpu_util::toStringView(label);
        bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        bufDesc.size = sizeof(TransformUBO);
        space.transformUBO = wgpuDeviceCreateBuffer(device, &bufDesc);
        if (!space.transformUBO) throw _err(("Failed to create transform uniform buffer"));

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = space.transformUBO;
        entry.offset = 0;
        entry.size = sizeof(TransformUBO);

        WGPUBindGroupDescriptor bgDesc{};
        bgDesc.label = wgpu_util::toStringView(label);
        bgDesc.layout = transformBGL;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        space.transformBG = wgpuDeviceCreateBindGroup(device, &bgDesc);
        if (!space.transformBG) throw _err(("Failed to create transform bind group"));
    };

    makeSpace(worldGPU, "World Transform");
    makeSpace(uiGPU, "UI Transform");
}

void GameRenderer::updateTransform(SpaceGPU& space, float offsetX, float offsetY, double zoomUnits) {
    float minDim = (float)mth::min(surfaceWidth, surfaceHeight);
    float pixelsPerUnit = zoomUnits > 0.0 ? minDim / (float)zoomUnits : 0.0f;

    TransformUBO ubo{};
    ubo.offset[0] = offsetX;
    ubo.offset[1] = offsetY;
    ubo.scale[0] = surfaceWidth > 0 ? pixelsPerUnit * (2.0f / (float)surfaceWidth) : 0.0f;
    ubo.scale[1] = surfaceHeight > 0 ? pixelsPerUnit * (2.0f / (float)surfaceHeight) : 0.0f;

    wgpuQueueWriteBuffer(queue, space.transformUBO, 0, &ubo, sizeof(ubo));
}

void GameRenderer::createSampler() {
    WGPUSamplerDescriptor samplerDesc{};
    samplerDesc.label = "Atlas Sampler"_wgpu;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.maxAnisotropy = 1;

    atlasSampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!atlasSampler) throw _err(("Failed to create atlas sampler"));
}

void GameRenderer::ensureVertexBufferCapacity(
    WGPUBuffer& buffer, size_t& capacityVerts, size_t neededVerts, size_t vertexStride, const char* label) {
    if (buffer && neededVerts <= capacityVerts) return;

    if (buffer) wgpuBufferRelease(buffer);
    capacityVerts = std::max(neededVerts, capacityVerts * 2 + 64);

    WGPUBufferDescriptor desc{};
    desc.label = wgpu_util::toStringView(label);
    desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    desc.size = capacityVerts * vertexStride;
    buffer = wgpuDeviceCreateBuffer(device, &desc);
    if (!buffer) throw _err(("Failed to (re)create vertex buffer '{}'", label));
}

std::vector<GameRenderer::DrawRun> GameRenderer::buildAndUploadDraws(SpaceGPU& space,
    const std::vector<Base::Renderer::ColouredVertex>& coloured, const std::vector<Base::Renderer::TexturedVertex>& textured) {
    struct TriRef {
        bool textured;
        size_t triIndex;
        int16_t z;
        uint8_t atlasId;
    };

    std::vector<TriRef> refs;
    refs.reserve(coloured.size() / 3 + textured.size() / 3);

    for (size_t t = 0; t + 3 <= coloured.size(); t += 3) refs.push_back({false, t / 3, coloured[t].get_z_index(), 0});
    for (size_t t = 0; t + 3 <= textured.size(); t += 3) refs.push_back({true, t / 3, textured[t].get_z_index(), textured[t].textureid});

    // Back-to-front: lower z-index drawn first (further back).
    std::stable_sort(refs.begin(), refs.end(), [](const TriRef& a, const TriRef& b) { return a.z < b.z; });

    static thread_local std::vector<GPUColouredVertex> gpuColoured;
    static thread_local std::vector<GPUTexturedVertex> gpuTextured;
    gpuColoured.clear();
    gpuTextured.clear();

    std::vector<DrawRun> runs;
    runs.reserve(refs.size());

    for (const TriRef& r : refs) {
        if (!r.textured) {
            size_t base = r.triIndex * 3;
            uint32_t startVertex = (uint32_t)gpuColoured.size();
            for (int i = 0; i < 3; ++i) {
                const Base::Renderer::ColouredVertex& v = coloured[base + i];
                GPUColouredVertex gv{};
                gv.pos[0] = v.x;
                gv.pos[1] = v.y;
                gv.colour[0] = ((v.rgba >> 24) & 0xFF) / 255.0f;
                gv.colour[1] = ((v.rgba >> 16) & 0xFF) / 255.0f;
                gv.colour[2] = ((v.rgba >> 8) & 0xFF) / 255.0f;
                gv.colour[3] = (v.rgba & 0xFF) / 255.0f;
                gpuColoured.push_back(gv);
            }
            if (!runs.empty() && !runs.back().textured && runs.back().firstVertex + runs.back().vertexCount == startVertex) {
                runs.back().vertexCount += 3;
            } else {
                runs.push_back({false, 0, startVertex, 3});
            }
        } else {
            size_t base = r.triIndex * 3;
            uint32_t startVertex = (uint32_t)gpuTextured.size();
            for (int i = 0; i < 3; ++i) {
                const Base::Renderer::TexturedVertex& v = textured[base + i];
                GPUTexturedVertex gv{};
                gv.pos[0] = v.x;
                gv.pos[1] = v.y;
                if (v.textureid < atlases.size()) {
                    const Atlas& atlas = atlases[v.textureid];
                    gv.uv[0] = atlas.width > 0 ? (float)v.uvx / (float)atlas.width : 0.0f;
                    gv.uv[1] = atlas.height > 0 ? (float)v.uvy / (float)atlas.height : 0.0f;
                } else {
                    gv.uv[0] = gv.uv[1] = 0.0f;
                }
                gpuTextured.push_back(gv);
            }
            if (!runs.empty() && runs.back().textured && runs.back().atlasId == r.atlasId &&
                runs.back().firstVertex + runs.back().vertexCount == startVertex) {
                runs.back().vertexCount += 3;
            } else {
                runs.push_back({true, r.atlasId, startVertex, 3});
            }
        }
    }

    if (!gpuColoured.empty()) {
        ensureVertexBufferCapacity(space.colouredVB, space.colouredCapacity, gpuColoured.size(), sizeof(GPUColouredVertex), "Coloured VB");
        wgpuQueueWriteBuffer(queue, space.colouredVB, 0, gpuColoured.data(), gpuColoured.size() * sizeof(GPUColouredVertex));
    }
    if (!gpuTextured.empty()) {
        ensureVertexBufferCapacity(space.texturedVB, space.texturedCapacity, gpuTextured.size(), sizeof(GPUTexturedVertex), "Textured VB");
        wgpuQueueWriteBuffer(queue, space.texturedVB, 0, gpuTextured.data(), gpuTextured.size() * sizeof(GPUTexturedVertex));
    }

    return runs;
}

void GameRenderer::drawSpace(WGPURenderPassEncoder pass, const SpaceGPU& space, const std::vector<DrawRun>& runs) {
    if (runs.empty()) return;

    wgpuRenderPassEncoderSetBindGroup(pass, 0, space.transformBG, 0, nullptr);

    bool boundTextured = false;
    bool boundColoured = false;
    uint8_t boundAtlas = 255;

    for (const DrawRun& run : runs) {
        if (run.textured) {
            if (!boundTextured) {
                wgpuRenderPassEncoderSetPipeline(pass, texturedPipeline);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, space.texturedVB, 0, wgpuBufferGetSize(space.texturedVB));
                boundTextured = true;
                boundColoured = false;
                boundAtlas = 255;
            }
            if (boundAtlas != run.atlasId && run.atlasId < atlases.size()) {
                wgpuRenderPassEncoderSetBindGroup(pass, 1, atlases[run.atlasId].bindGroup, 0, nullptr);
                boundAtlas = run.atlasId;
            }
        } else {
            if (!boundColoured) {
                wgpuRenderPassEncoderSetPipeline(pass, colouredPipeline);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, space.colouredVB, 0, wgpuBufferGetSize(space.colouredVB));
                boundColoured = true;
                boundTextured = false;
            }
        }
        wgpuRenderPassEncoderDraw(pass, run.vertexCount, 1, run.firstVertex, 0);
    }
}

void GameRenderer::loop() {
    {
        static int prev_x = 0, prev_y = 0;
        int cur_x, cur_y;
        SDL_GetWindowSizeInPixels(window, &cur_x, &cur_y);
        if (prev_x != cur_x || prev_y != cur_y) {
            configureSurface(cur_x, cur_y);
            prev_x = cur_x;
            prev_y = cur_y;
        }
    }

    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) return;

    WGPUTextureView backbuffer = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    if (!backbuffer) return;

    // --- Sort + upload geometry for both spaces ---
    std::vector<DrawRun> worldRuns = buildAndUploadDraws(worldGPU, worldtris.coloured, worldtris.textured);
    std::vector<DrawRun> uiRuns = buildAndUploadDraws(uiGPU, ui_tris.coloured, ui_tris.textured);

    updateTransform(worldGPU, (float)camera.x, (float)camera.y, camera._real_zoom);
    updateTransform(uiGPU, 0.0f, 0.0f, camera.uizoom);

    // --- Record commands ---
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    WGPURenderPassColorAttachment colorAttachment{};
    colorAttachment.view = backbuffer;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};

    WGPURenderPassDescriptor passDesc{};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    drawSpace(pass, worldGPU, worldRuns);
    drawSpace(pass, uiGPU, uiRuns);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(queue, 1, &commands);
    wgpuCommandBufferRelease(commands);

    wgpuSurfacePresent(surface);

    wgpuTextureViewRelease(backbuffer);
    wgpuTextureRelease(surfaceTexture.texture);
}

void GameRenderer::quit() {
    for (Atlas& a : atlases) {
        if (a.bindGroup) wgpuBindGroupRelease(a.bindGroup);
        if (a.view) wgpuTextureViewRelease(a.view);
        if (a.texture) wgpuTextureRelease(a.texture);
    }
    atlases.clear();
    atlasIds.clear();

    auto releaseSpace = [](SpaceGPU& space) {
        if (space.transformBG) wgpuBindGroupRelease(space.transformBG);
        if (space.transformUBO) wgpuBufferRelease(space.transformUBO);
        if (space.colouredVB) wgpuBufferRelease(space.colouredVB);
        if (space.texturedVB) wgpuBufferRelease(space.texturedVB);
    };
    releaseSpace(worldGPU);
    releaseSpace(uiGPU);

    if (atlasSampler) wgpuSamplerRelease(atlasSampler);
    if (colouredPipeline) wgpuRenderPipelineRelease(colouredPipeline);
    if (texturedPipeline) wgpuRenderPipelineRelease(texturedPipeline);
    if (colouredShader) wgpuShaderModuleRelease(colouredShader);
    if (texturedShader) wgpuShaderModuleRelease(texturedShader);
    if (transformBGL) wgpuBindGroupLayoutRelease(transformBGL);
    if (atlasBGL) wgpuBindGroupLayoutRelease(atlasBGL);
    if (queue) wgpuQueueRelease(queue);
    if (device) wgpuDeviceRelease(device);
    if (adapter) wgpuAdapterRelease(adapter);
    if (surface) wgpuSurfaceRelease(surface);
    if (instance) wgpuInstanceRelease(instance);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

// --- Synchronous adapter/device request helpers ---
WGPUAdapter GameRenderer::requestAdapterSync(WGPURequestAdapterOptions const& options) {
    WGPUAdapter retval = nullptr;
    WGPURequestAdapterCallbackInfo callback_info{
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback =
            [](WGPURequestAdapterStatus status, WGPUAdapterImpl* a, WGPUStringView, void* userdata, void*) {
                if (status == WGPURequestAdapterStatus_Success) *reinterpret_cast<WGPUAdapter*>(userdata) = WGPUAdapter(a);
            },
        .userdata1 = &retval,
    };
    assert(instance);
    wgpuInstanceRequestAdapter(instance, &options, callback_info);
    return retval;
}

WGPUDevice GameRenderer::requestDeviceSync(WGPUDeviceDescriptor const& descriptor) {
    WGPUDevice retval = nullptr;
    WGPURequestDeviceCallbackInfo callback_info{
        .mode = WGPUCallbackMode_AllowSpontaneous,
        .callback =
            [](WGPURequestDeviceStatus status, WGPUDeviceImpl* a, WGPUStringView, void* userdata, void*) {
                if (status == WGPURequestDeviceStatus_Success) *reinterpret_cast<WGPUDevice*>(userdata) = WGPUDevice(a);
            },
        .userdata1 = &retval,
    };
    assert(adapter);
    wgpuAdapterRequestDevice(adapter, &descriptor, callback_info);
    return retval;
}

void GameRenderer::addAtlasFromData(std::string_view atlas_name, std::span<const unsigned char> bytes) {
    if (atlasIds.contains(std::string(atlas_name))) {
        std::fprintf(stderr, "Atlas '%.*s' already exists, skipping\n", (int)atlas_name.size(), atlas_name.data());
        return;
    }
    if (atlases.size() >= 255) {
        std::fprintf(stderr, "Atlas limit (255) reached, cannot add '%.*s'\n", (int)atlas_name.size(), atlas_name.data());
        return;
    }
    if (!device || !atlasSampler || !atlasBGL) {
        std::fprintf(
            stderr, "addAtlasFromData called before renderer init completed for '%.*s'\n", (int)atlas_name.size(), atlas_name.data());
        return;
    }

    int w, h, channels;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        std::fprintf(stderr, "Failed to decode atlas '%.*s': %s\n", (int)atlas_name.size(), atlas_name.data(), stbi_failure_reason());
        return;
    }

    WGPUTextureDescriptor texDesc{};
    texDesc.label = wgpu_util::toStringView(atlas_name);
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {(uint32_t)w, (uint32_t)h, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!texture) {
        std::fprintf(stderr, "Failed to create texture for atlas '%.*s'\n", (int)atlas_name.size(), atlas_name.data());
        stbi_image_free(pixels);
        return;
    }

    WGPUTexelCopyTextureInfo dst{};
    dst.texture = texture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = (uint32_t)w * 4;
    layout.rowsPerImage = (uint32_t)h;

    WGPUExtent3D writeSize{(uint32_t)w, (uint32_t)h, 1};

    wgpuQueueWriteTexture(queue, &dst, pixels, (size_t)w * (size_t)h * 4, &layout, &writeSize);
    stbi_image_free(pixels);

    WGPUTextureViewDescriptor viewDesc{};
    viewDesc.label = wgpu_util::toStringView(atlas_name);
    viewDesc.format = texDesc.format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    WGPUTextureView view = wgpuTextureCreateView(texture, &viewDesc);
    if (!view) {
        std::fprintf(stderr, "Failed to create texture view for atlas '%.*s'\n", (int)atlas_name.size(), atlas_name.data());
        wgpuTextureRelease(texture);
        return;
    }

    WGPUBindGroupEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].sampler = atlasSampler;

    WGPUBindGroupDescriptor bgDesc{};
    bgDesc.label = wgpu_util::toStringView(atlas_name);
    bgDesc.layout = atlasBGL;
    bgDesc.entryCount = 2;
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    if (!bindGroup) {
        std::fprintf(stderr, "Failed to create bind group for atlas '%.*s'\n", (int)atlas_name.size(), atlas_name.data());
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(texture);
        return;
    }

    uint8_t id = (uint8_t)atlases.size();
    atlases.push_back(Atlas{texture, view, bindGroup, w, h});
    atlasIds.emplace(std::string(atlas_name), id);
}

uint8_t GameRenderer::getAtlasId(std::string_view atlas_name) {
    auto it = atlasIds.find(std::string(atlas_name));
    return it != atlasIds.end() ? it->second : 255;
}
// </AI>
