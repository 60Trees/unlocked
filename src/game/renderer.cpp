#include <base/renderer.hpp>
#include <format>
#include <string_view>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <string>
#include <sdl3webgpu.h>
#include <iostream>

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>
#include <cassert>
#include <cstdio>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// <AI>
namespace wgpu_util {
    constexpr WGPUStringView toStringView(std::string_view sv) noexcept { return WGPUStringView{sv.data(), sv.size()}; }
    constexpr WGPUStringView toStringView(const char* c) noexcept { return WGPUStringView{c, WGPU_STRLEN}; }
}  // namespace wgpu_util
constexpr WGPUStringView operator""_wgpu(const char* str, size_t len) noexcept { return WGPUStringView{str, len}; }
#define _err(_format) std::runtime_error(std::format _format)

// Shared WGSL fragments every pipeline is assembled from. VSOut is deliberately identical for the
// world and screen vertex shaders so any pixel shader can pair with either.
static constexpr const char* kVSOut =
    "struct VSOut { @builtin(position) pos: vec4f, @location(0) colour: vec4f, @location(1) uv: vec2f };\n";
static constexpr const char* kTransformDecl =
    "struct Transform { offset: vec2f, scale: vec2f };\n@group(0) @binding(0) var<uniform> transform: Transform;\n";

static constexpr const char* kWorldVS = R"(
@vertex fn vs_main(@location(0) posRaw: vec3<u32>, @location(1) sdRaw: vec2<u32>) -> VSOut {
    var out: VSOut;
    let x = bitcast<f32>(posRaw.y);
    let y = bitcast<f32>(posRaw.z);
    out.pos = vec4f((vec2f(x, y) - transform.offset) * transform.scale, 0.0, 1.0);
    out.colour = unpack4x8unorm(sdRaw.x);
    let u = f32(extractBits(sdRaw.x, 16u, 16u));
    let v = f32(extractBits(sdRaw.y, 0u, 16u));
    out.uv = vec2f(u, v);
    return out;
}
)";
// Screen space ignores the camera entirely: `transform.offset` is unused, `transform.scale` holds
// (2/width, 2/height) so a pixel offset converts straight to an NDC delta. anchor byte: high nibble
// = column (1 left, 2 mid, 3 right), low nibble = row (0xa top, 0xb mid, 0xc bottom) — adjust the
// select()s below if your intended anchor semantics differ.
static constexpr const char* kScreenVS = R"(
@vertex fn vs_main(@location(0) posRaw: vec3<u32>, @location(1) sdRaw: vec2<u32>) -> VSOut {
    var out: VSOut;
    let sx = f32(extractBits(bitcast<i32>(posRaw.x), 0u, 16u));
    let sy = f32(extractBits(bitcast<i32>(posRaw.x), 16u, 16u));
    let anchorByte = extractBits(posRaw.y, 0u, 8u);
    let col = (anchorByte >> 4u) & 0xFu;
    let row = anchorByte & 0xFu;
    let ax = select(select(-1.0, 0.0, col == 2u), 1.0, col == 3u);
    let ay = select(select(-1.0, 0.0, row == 0xbu), 1.0, row == 0xcu);
    out.pos = vec4f(vec2f(ax, ay) + vec2f(sx, sy) * transform.scale, 0.0, 1.0);
    out.colour = unpack4x8unorm(sdRaw.x);
    let u = f32(extractBits(sdRaw.x, 16u, 16u));
    let v = f32(extractBits(sdRaw.y, 0u, 16u));
    out.uv = vec2f(u, v);
    return out;
}
)";
static constexpr const char* kColouredPS = R"(
@fragment fn fs_main(in: VSOut) -> @location(0) vec4f { return in.colour; }
)";
// texture_id (low 16 bits of sdRaw.x) is resolved CPU-side to a bound atlas per draw; only u,v
// (high 16 of sdRaw.x, low 16 of sdRaw.y) are needed here.
static constexpr const char* kTexturedPS = R"(
// USES_TEXTURES
// The engine checks if that string exists in the file.
// If so, then it adds the texture info to the shaderdata
@group(1) @binding(0) var atlasTex: texture_2d<f32>;
@group(1) @binding(1) var atlasSamp: sampler;
@fragment fn fs_main(in: VSOut) -> @location(0) vec4f {
    return textureSample(atlasTex, atlasSamp, in.uv / vec2f(textureDimensions(atlasTex)));
}
)";
static constexpr const char* kPostVS = R"(
struct VSOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VSOut {
    var out: VSOut;
    let x = f32((i << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(i & 2u) * 2.0 - 1.0;
    out.pos = vec4f(x, y, 0.0, 1.0);
    out.uv = vec2f((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
    return out;
}
)";
static constexpr const char* kBlitPS = R"(
@group(0) @binding(0) var prevTex: texture_2d<f32>;
@group(0) @binding(1) var prevSamp: sampler;
struct VSOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f };
@fragment fn fs_main(in: VSOut) -> @location(0) vec4f { return textureSample(prevTex, prevSamp, in.uv); }
)";

static constexpr std::string_view kBuiltinVertexShaders[] = {kWorldVS, kScreenVS};
static constexpr std::string_view kBuiltinPixelShaders[] = {kColouredPS, kTexturedPS};
static constexpr Base::Renderer::BlendMode kAllBlendModes[] = {Base::Renderer::Opaque, Base::Renderer::Alpha,
    Base::Renderer::Additive, Base::Renderer::Multiply, Base::Renderer::Screen};

static constexpr uint32_t kParamAlign = 256;  // WebGPU's minUniformBufferOffsetAlignment floor
static constexpr uint32_t kParamMax = 256;    // per-draw params budget; raise if you need bigger structs

// TODO: Fix Emscripten with WGPU (its very broken)
#ifdef __EMSCRITEN__
#define spamlog(message) std::cout << message << std::endl
#else
#define spamlog(message)
#endif

class GameRenderer : public Base::Renderer {
    public:
    void init() override;
    void loop() override;
    void quit() override;

    void compile_default_shaders() override;
    void precompile_shaders(std::span<const std::string_view> pixel_shaders, std::span<const BlendMode> blend_modes,
        std::span<const std::string_view> vertex_shaders) override;

    uint16_t addTextureFromBytes(std::string_view name, std::span<const char> bytes) override;
    uint16_t getTextureID(std::string_view name) override;

    std::string_view builtin_coloured_pshader() const override { return kColouredPS; }
    std::string_view builtin_textured_pshader() const override { return kTexturedPS; }
    std::string_view builtin_worldspace_vshader() const override { return kWorldVS; }
    std::string_view builtin_uispace_vshader() const override { return kScreenVS; }
    void compile_used_shaders() override;

    private:
    struct GPUVertex {
        uint32_t pos[3];
        uint32_t sd[2];
    };
    struct TransformUBO {
        float offset[2];
        float scale[2];
    };

    struct PipelineKey {
        const void* vs;
        const void* ps;
        bool screenspace;
        BlendMode blend;
        bool operator<(const PipelineKey& o) const {
            return std::tie(vs, ps, screenspace, blend) < std::tie(o.vs, o.ps, o.screenspace, o.blend);
        }
    };
    struct PostKey {
        const void* ps;
        bool operator<(const PostKey& o) const { return ps < o.ps; }
    };

    struct Texture {
        WGPUTexture texture = nullptr;
        WGPUTextureView view = nullptr;
        WGPUBindGroup bindGroup = nullptr;
        int width = 0, height = 0;
    };
    struct PostPipeline {
        WGPURenderPipeline pipeline = nullptr;
        WGPUBindGroup bg[2] = {nullptr, nullptr};
    };

    SDL_Window* window = nullptr;
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    uint32_t surfaceWidth = 0, surfaceHeight = 0;

    WGPUBindGroupLayout transformBGL = nullptr, atlasBGL = nullptr, paramsBGL = nullptr, blitBGL = nullptr;
    WGPUSampler texSampler = nullptr, postSampler = nullptr;

    WGPUBuffer worldUBO = nullptr, uiUBO = nullptr;
    WGPUBindGroup worldBG = nullptr, uiBG = nullptr;

    WGPUBuffer paramsScratch = nullptr;
    size_t paramsScratchCap = 0;
    WGPUBindGroup paramsScratchBG = nullptr;

    WGPUBuffer vertexScratch = nullptr;
    size_t vertexScratchCap = 0;

    WGPUTexture sceneTex[2] = {nullptr, nullptr};
    WGPUTextureView sceneView[2] = {nullptr, nullptr};
    WGPUBindGroup blitBG[2] = {nullptr, nullptr};
    WGPURenderPipeline blitPipeline = nullptr;
    WGPUShaderModule postVSModule = nullptr;

    std::vector<Texture> textures;
    std::unordered_map<std::string, uint16_t> textureIds;
    uint16_t nullTextureId = 0;

    std::map<PipelineKey, WGPURenderPipeline> pipelines;
    std::map<PostKey, PostPipeline> postPipelines;

    uint64_t lastTick = 0;

    WGPUAdapter requestAdapterSync(WGPURequestAdapterOptions const&);
    WGPUDevice requestDeviceSync(WGPUDeviceDescriptor const&);
    void configureSurface(int w, int h);
    void resizeSceneTargets();
    void createLayoutsAndStatics();
    WGPUShaderModule compileWGSL(std::string_view code, const char* label);
    WGPURenderPipeline buildPipeline(const Material& mat);
    PostPipeline buildPostPipeline(const PostEffect& fx);
    void ensureVertexScratch(size_t vertexCount);
    void ensureParamsScratch(size_t bytesNeeded);
    static GPUVertex packVertex(const Vertex& v);
};

extern "C" Base::BaseClass* GetRenderer() { return new GameRenderer(); }

// ============================== lifecycle ==============================

void GameRenderer::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw _err(("SDL_Init failed: {}", SDL_GetError()));
    spamlog("Initialized");

    window = SDL_CreateWindow("Run", 1280, 720, SDL_WINDOW_RESIZABLE);
    spamlog("Created window");

    instance = wgpuCreateInstance(nullptr);
    spamlog("Created WGPUInstane");

    surface = SDL_GetWGPUSurface(instance, window);
    if (!surface) throw _err(("Failed to get WGPU surface"));
    spamlog("Created WGPUSurface");

    WGPURequestAdapterOptions ao{};
    ao.compatibleSurface = surface;
    adapter = requestAdapterSync(ao);
    spamlog("Requested adapter");

    WGPUAdapterInfo info{};
    wgpuAdapterGetInfo(adapter, &info);

    std::print("vendor: {}\n", std::string_view(info.vendor.data, info.vendor.length));
    std::print("architecture: {}\n", std::string_view(info.architecture.data, info.architecture.length));
    std::print("device: {}\n", std::string_view(info.device.data, info.device.length));
    std::print("description: {}\n", std::string_view(info.description.data, info.description.length));

#ifdef WGPU_VERSION_MAJOR
    SDL_Log("WebGPU headers: %d.%d.%d", WGPU_VERSION_MAJOR, WGPU_VERSION_MINOR, WGPU_VERSION_PATCH);
#endif
    WGPUDeviceDescriptor dd{.label = "Run Device"_wgpu, .defaultQueue = {.label = "Run Queue"_wgpu}};
    device = requestDeviceSync(dd);
    queue = wgpuDeviceGetQueue(device);

    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    configureSurface(w, h);
    createLayoutsAndStatics();
    ensureParamsScratch(kParamAlign);
    resizeSceneTargets();

    // texture_id=0 is a 1x1 white dummy so coloured/untextured materials always have something
    // valid bound at group(1), keeping every pipeline layout identical.
    unsigned char white[4] = {255, 255, 255, 255};
    WGPUTextureDescriptor td{.label = "Dummy"_wgpu,
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = {1, 1, 1},
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1};
    WGPUTexture tex = wgpuDeviceCreateTexture(device, &td);
    WGPUTexelCopyTextureInfo dst{.texture = tex, .aspect = WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout{.bytesPerRow = 4, .rowsPerImage = 1};
    WGPUExtent3D size{1, 1, 1};
    wgpuQueueWriteTexture(queue, &dst, white, 4, &layout, &size);
    WGPUTextureViewDescriptor vd{.format = td.format,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All};
    WGPUTextureView view = wgpuTextureCreateView(tex, &vd);
    WGPUBindGroupEntry be[2] = {{.binding = 0, .textureView = view}, {.binding = 1, .sampler = texSampler}};
    WGPUBindGroupDescriptor bgd{.label = "Dummy BG"_wgpu, .layout = atlasBGL, .entryCount = 2, .entries = be};
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device, &bgd);
    textures.push_back({tex, view, bg, 1, 1});
    nullTextureId = 0;

    lastTick = SDL_GetTicks();
}

void GameRenderer::configureSurface(int width, int height) {
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(surface, adapter, &caps);
    surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    for (uint32_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
            surfaceFormat = caps.formats[i];
            break;
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    surfaceWidth = (uint32_t)width;
    surfaceHeight = (uint32_t)height;
    WGPUSurfaceConfiguration cfg{.device = device,
        .format = surfaceFormat,
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = surfaceWidth,
        .height = surfaceHeight,
        .alphaMode = WGPUCompositeAlphaMode_Auto,
        .presentMode = WGPUPresentMode_Fifo};
    wgpuSurfaceConfigure(surface, &cfg);
}

void GameRenderer::resizeSceneTargets() {
    for (int i = 0; i < 2; ++i) {
        if (sceneView[i]) wgpuTextureViewRelease(sceneView[i]);
        if (sceneTex[i]) wgpuTextureRelease(sceneTex[i]);
        WGPUTextureDescriptor td{.label = i == 0 ? "SceneA"_wgpu : "SceneB"_wgpu,
            .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = {surfaceWidth, surfaceHeight, 1},
            .format = surfaceFormat,
            .mipLevelCount = 1,
            .sampleCount = 1};
        sceneTex[i] = wgpuDeviceCreateTexture(device, &td);
        sceneView[i] = wgpuTextureCreateView(sceneTex[i], nullptr);
    }
    for (int i = 0; i < 2; ++i) {
        if (blitBG[i]) wgpuBindGroupRelease(blitBG[i]);
        WGPUBindGroupEntry e[2] = {{.binding = 0, .textureView = sceneView[i]}, {.binding = 1, .sampler = postSampler}};
        WGPUBindGroupDescriptor d{.label = "Blit BG"_wgpu, .layout = blitBGL, .entryCount = 2, .entries = e};
        blitBG[i] = wgpuDeviceCreateBindGroup(device, &d);
    }
    // Any post pipelines already built reference the old scene views; drop their bind groups so
    // compile_all_shaders() rebuilds them fresh against the new views on the next call.
    for (auto& [k, pp] : postPipelines) {
        for (int i = 0; i < 2; ++i)
            if (pp.bg[i]) {
                wgpuBindGroupRelease(pp.bg[i]);
                pp.bg[i] = nullptr;
            }
    }
}

void GameRenderer::quit() {
    for (auto& t : textures) {
        if (t.bindGroup) wgpuBindGroupRelease(t.bindGroup);
        if (t.view) wgpuTextureViewRelease(t.view);
        if (t.texture) wgpuTextureRelease(t.texture);
    }
    for (auto& [k, p] : pipelines)
        if (p) wgpuRenderPipelineRelease(p);
    for (auto& [k, pp] : postPipelines) {
        if (pp.pipeline) wgpuRenderPipelineRelease(pp.pipeline);
        for (auto bg : pp.bg)
            if (bg) wgpuBindGroupRelease(bg);
    }
    for (int i = 0; i < 2; ++i) {
        if (blitBG[i]) wgpuBindGroupRelease(blitBG[i]);
        if (sceneView[i]) wgpuTextureViewRelease(sceneView[i]);
        if (sceneTex[i]) wgpuTextureRelease(sceneTex[i]);
    }
    if (worldBG) wgpuBindGroupRelease(worldBG);
    if (uiBG) wgpuBindGroupRelease(uiBG);
    if (worldUBO) wgpuBufferRelease(worldUBO);
    if (uiUBO) wgpuBufferRelease(uiUBO);
    if (paramsScratchBG) wgpuBindGroupRelease(paramsScratchBG);
    if (paramsScratch) wgpuBufferRelease(paramsScratch);
    if (vertexScratch) wgpuBufferRelease(vertexScratch);
    if (texSampler) wgpuSamplerRelease(texSampler);
    if (postSampler) wgpuSamplerRelease(postSampler);
    if (blitPipeline) wgpuRenderPipelineRelease(blitPipeline);
    if (postVSModule) wgpuShaderModuleRelease(postVSModule);
    if (transformBGL) wgpuBindGroupLayoutRelease(transformBGL);
    if (atlasBGL) wgpuBindGroupLayoutRelease(atlasBGL);
    if (paramsBGL) wgpuBindGroupLayoutRelease(paramsBGL);
    if (blitBGL) wgpuBindGroupLayoutRelease(blitBGL);
    if (queue) wgpuQueueRelease(queue);
    if (device) wgpuDeviceRelease(device);
    if (adapter) wgpuAdapterRelease(adapter);
    if (surface) wgpuSurfaceRelease(surface);
    if (instance) wgpuInstanceRelease(instance);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

WGPUAdapter GameRenderer::requestAdapterSync(WGPURequestAdapterOptions const& options) {
    WGPUAdapter retval = nullptr;
    WGPURequestAdapterCallbackInfo cb{.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback =
            [](WGPURequestAdapterStatus s, WGPUAdapterImpl* a, WGPUStringView, void* ud, void*) {
                if (s == WGPURequestAdapterStatus_Success) *(WGPUAdapter*)ud = WGPUAdapter(a);
            },
        .userdata1 = &retval};
    wgpuInstanceRequestAdapter(instance, &options, cb);
    return retval;
}
WGPUDevice GameRenderer::requestDeviceSync(WGPUDeviceDescriptor const& d) {
    WGPUDevice retval = nullptr;
    WGPURequestDeviceCallbackInfo cb{.mode = WGPUCallbackMode_AllowSpontaneous,
        .callback =
            [](WGPURequestDeviceStatus s, WGPUDeviceImpl* a, WGPUStringView, void* ud, void*) {
                if (s == WGPURequestDeviceStatus_Success) *(WGPUDevice*)ud = WGPUDevice(a);
            },
        .userdata1 = &retval};
    wgpuAdapterRequestDevice(adapter, &d, cb);
    return retval;
}

// ============================== setup ==============================

WGPUShaderModule GameRenderer::compileWGSL(std::string_view code, const char* label) {
    WGPUShaderSourceWGSL wgsl{};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = wgpu_util::toStringView(code);
    WGPUShaderModuleDescriptor sd{.nextInChain = &wgsl.chain, .label = wgpu_util::toStringView(label)};
    return wgpuDeviceCreateShaderModule(device, &sd);
}

void GameRenderer::createLayoutsAndStatics() {
    WGPUBindGroupLayoutEntry te{.binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = {.type = WGPUBufferBindingType_Uniform, .minBindingSize = sizeof(TransformUBO)}};
    WGPUBindGroupLayoutDescriptor td{.label = "Transform BGL"_wgpu, .entryCount = 1, .entries = &te};
    transformBGL = wgpuDeviceCreateBindGroupLayout(device, &td);

    WGPUBindGroupLayoutEntry ae[2]{
        {.binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D}},
        {.binding = 1, .visibility = WGPUShaderStage_Fragment, .sampler = {.type = WGPUSamplerBindingType_Filtering}}};
    WGPUBindGroupLayoutDescriptor ad{.label = "Atlas BGL"_wgpu, .entryCount = 2, .entries = ae};
    atlasBGL = wgpuDeviceCreateBindGroupLayout(device, &ad);

    WGPUBindGroupLayoutEntry pe{.binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {.type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = true, .minBindingSize = kParamMax}};
    WGPUBindGroupLayoutDescriptor pd{.label = "Params BGL"_wgpu, .entryCount = 1, .entries = &pe};
    paramsBGL = wgpuDeviceCreateBindGroupLayout(device, &pd);

    WGPUBindGroupLayoutEntry be[2]{
        {.binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D}},
        {.binding = 1, .visibility = WGPUShaderStage_Fragment, .sampler = {.type = WGPUSamplerBindingType_Filtering}}};
    WGPUBindGroupLayoutDescriptor bd{.label = "Blit BGL"_wgpu, .entryCount = 2, .entries = be};
    blitBGL = wgpuDeviceCreateBindGroupLayout(device, &bd);
    if (!transformBGL || !atlasBGL || !paramsBGL || !blitBGL) throw _err(("Failed to create bind group layouts"));

    WGPUSamplerDescriptor sd{.addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Nearest,
        .minFilter = WGPUFilterMode_Nearest,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .lodMaxClamp = 1.0f,
        .maxAnisotropy = 1};
    texSampler = wgpuDeviceCreateSampler(device, &sd);
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    postSampler = wgpuDeviceCreateSampler(device, &sd);

    WGPUBufferDescriptor ud{.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .size = sizeof(TransformUBO)};
    ud.label = "World UBO"_wgpu;
    worldUBO = wgpuDeviceCreateBuffer(device, &ud);
    ud.label = "UI UBO"_wgpu;
    uiUBO = wgpuDeviceCreateBuffer(device, &ud);
    auto makeBG = [&](WGPUBuffer buf, const char* label) {
        WGPUBindGroupEntry e{.binding = 0, .buffer = buf, .size = sizeof(TransformUBO)};
        WGPUBindGroupDescriptor d{.label = wgpu_util::toStringView(label), .layout = transformBGL, .entryCount = 1, .entries = &e};
        return wgpuDeviceCreateBindGroup(device, &d);
    };
    worldBG = makeBG(worldUBO, "World BG");
    uiBG = makeBG(uiUBO, "UI BG");

    postVSModule = compileWGSL(kPostVS, "Post VS");
    WGPUShaderModule blitFS = compileWGSL(kBlitPS, "Blit FS");
    WGPUColorTargetState target{.format = surfaceFormat, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState frag{.module = blitFS, .entryPoint = "fs_main"_wgpu, .targetCount = 1, .targets = &target};
    WGPUPipelineLayoutDescriptor pld{.label = "Blit Layout"_wgpu, .bindGroupLayoutCount = 1, .bindGroupLayouts = &blitBGL};
    WGPUPipelineLayout playout = wgpuDeviceCreatePipelineLayout(device, &pld);
    WGPURenderPipelineDescriptor bpd{.label = "Blit Pipeline"_wgpu, .layout = playout};
    bpd.vertex = {.module = postVSModule, .entryPoint = "vs_main"_wgpu};
    bpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    bpd.multisample = {.count = 1, .mask = 0xFFFFFFFF};
    bpd.fragment = &frag;
    blitPipeline = wgpuDeviceCreateRenderPipeline(device, &bpd);
    wgpuPipelineLayoutRelease(playout);
    wgpuShaderModuleRelease(blitFS);
}

// ============================== textures ==============================

uint16_t GameRenderer::addTextureFromBytes(std::string_view name, std::span<const char> bytes) {
    std::string key(name);
    if (auto it = textureIds.find(key); it != textureIds.end()) return it->second;
    if (textures.size() >= 65535) {
        std::fprintf(stderr, "Texture limit reached, cannot add '%s'\n", key.c_str());
        return nullTextureId;
    }

    int w, h, ch;
    stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)bytes.data(), (int)bytes.size(), &w, &h, &ch, STBI_rgb_alpha);
    if (!pixels) {
        std::fprintf(stderr, "Failed to decode texture '%s': %s\n", key.c_str(), stbi_failure_reason());
        return nullTextureId;
    }

    WGPUTextureDescriptor td{.label = wgpu_util::toStringView(key),
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = {(uint32_t)w, (uint32_t)h, 1},
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1};
    WGPUTexture tex = wgpuDeviceCreateTexture(device, &td);
    WGPUTexelCopyTextureInfo dst{.texture = tex, .aspect = WGPUTextureAspect_All};
    WGPUTexelCopyBufferLayout layout{.bytesPerRow = (uint32_t)w * 4, .rowsPerImage = (uint32_t)h};
    WGPUExtent3D size{(uint32_t)w, (uint32_t)h, 1};
    wgpuQueueWriteTexture(queue, &dst, pixels, (size_t)w * h * 4, &layout, &size);
    stbi_image_free(pixels);

    WGPUTextureViewDescriptor vd{.format = td.format,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All};
    WGPUTextureView view = wgpuTextureCreateView(tex, &vd);
    WGPUBindGroupEntry be[2] = {{.binding = 0, .textureView = view}, {.binding = 1, .sampler = texSampler}};
    WGPUBindGroupDescriptor bgd{.label = wgpu_util::toStringView(key), .layout = atlasBGL, .entryCount = 2, .entries = be};
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(device, &bgd);

    uint16_t id = (uint16_t)textures.size();
    textures.push_back({tex, view, bg, w, h});
    textureIds.emplace(key, id);
    return id;
}

uint16_t GameRenderer::getTextureID(std::string_view name) {
    auto it = textureIds.find(std::string(name));
    return it != textureIds.end() ? it->second : nullTextureId;
}

// ============================== shader compilation ==============================

WGPURenderPipeline GameRenderer::buildPipeline(const Material& mat) {
    std::string vsSrc = std::string(kTransformDecl) + kVSOut + std::string(mat.vertex_shader);
    std::string psSrc = kVSOut + std::string(mat.pixel_shader);
    WGPUShaderModule vs = compileWGSL(vsSrc, "Material VS");
    WGPUShaderModule fs = compileWGSL(psSrc, "Material PS");

    WGPUVertexAttribute attrs[2]{{.format = WGPUVertexFormat_Uint32x3, .offset = offsetof(GPUVertex, pos), .shaderLocation = 0},
        {.format = WGPUVertexFormat_Uint32x2, .offset = offsetof(GPUVertex, sd), .shaderLocation = 1}};
    WGPUVertexBufferLayout vb{
        .stepMode = WGPUVertexStepMode_Vertex, .arrayStride = sizeof(GPUVertex), .attributeCount = 2, .attributes = attrs};

    WGPUBlendState blend{};
    switch (mat.blend_mode) {
        case Opaque:
            blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero};
            blend.alpha = blend.color;
            break;
        case Alpha:
            blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
            blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
            break;
        case Additive:
            blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One};
            blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
            break;
        case Multiply:
            blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_Dst, WGPUBlendFactor_Zero};
            blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero};
            break;
        case Screen:
            blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_OneMinusDst, WGPUBlendFactor_One};
            blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero};
            break;
    }
    WGPUColorTargetState target{.format = surfaceFormat, .blend = &blend, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState frag{.module = fs, .entryPoint = "fs_main"_wgpu, .targetCount = 1, .targets = &target};

    WGPUBindGroupLayout bgls[3] = {transformBGL, atlasBGL, paramsBGL};
    WGPUPipelineLayoutDescriptor pld{.label = "Material Layout"_wgpu, .bindGroupLayoutCount = 3, .bindGroupLayouts = bgls};
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &pld);

    WGPURenderPipelineDescriptor pd{.label = "Material Pipeline"_wgpu, .layout = layout};
    pd.vertex = {.module = vs, .entryPoint = "vs_main"_wgpu, .bufferCount = 1, .buffers = &vb};
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.multisample = {.count = 1, .mask = 0xFFFFFFFF};
    pd.fragment = &frag;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device, &pd);

    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(vs);
    wgpuShaderModuleRelease(fs);
    return pipeline;
}

GameRenderer::PostPipeline GameRenderer::buildPostPipeline(const PostEffect& fx) {
    PostPipeline pp;
    std::string psSrc =
        "struct VSOut { @builtin(position) pos: vec4f, @location(0) uv: vec2f };\n"
        "@group(0) @binding(0) var prevTex: texture_2d<f32>;\n@group(0) @binding(1) var prevSamp: sampler;\n"
        "@group(0) @binding(2) var<uniform> params: array<vec4f, " +
        std::to_string(kParamMax / 16) + ">;\n" + std::string(fx.pixel_shader);
    WGPUShaderModule fs = compileWGSL(psSrc, "Post Shader FS");

    WGPUBindGroupLayoutEntry entries[3]{
        {.binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {.sampleType = WGPUTextureSampleType_Float, .viewDimension = WGPUTextureViewDimension_2D}},
        {.binding = 1, .visibility = WGPUShaderStage_Fragment, .sampler = {.type = WGPUSamplerBindingType_Filtering}},
        {.binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {.type = WGPUBufferBindingType_Uniform, .hasDynamicOffset = true, .minBindingSize = kParamMax}}};
    WGPUBindGroupLayoutDescriptor bgld{.label = "Post Effect BGL"_wgpu, .entryCount = 3, .entries = entries};
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bgld);

    for (int i = 0; i < 2; ++i) {
        WGPUBindGroupEntry e[3] = {{.binding = 0, .textureView = sceneView[i]}, {.binding = 1, .sampler = postSampler},
            {.binding = 2, .buffer = paramsScratch, .size = kParamMax}};
        WGPUBindGroupDescriptor d{.label = "Post Effect BG"_wgpu, .layout = bgl, .entryCount = 3, .entries = e};
        pp.bg[i] = wgpuDeviceCreateBindGroup(device, &d);
    }

    WGPUColorTargetState target{.format = surfaceFormat, .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState frag{.module = fs, .entryPoint = "fs_main"_wgpu, .targetCount = 1, .targets = &target};
    WGPUPipelineLayoutDescriptor pld{.label = "Post Layout"_wgpu, .bindGroupLayoutCount = 1, .bindGroupLayouts = &bgl};
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &pld);
    WGPURenderPipelineDescriptor pd{.label = "Post Pipeline"_wgpu, .layout = layout};
    pd.vertex = {.module = postVSModule, .entryPoint = "vs_main"_wgpu};
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.multisample = {.count = 1, .mask = 0xFFFFFFFF};
    pd.fragment = &frag;
    pp.pipeline = wgpuDeviceCreateRenderPipeline(device, &pd);

    wgpuPipelineLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(bgl);  // pipeline keeps its own internal reference
    wgpuShaderModuleRelease(fs);
    return pp;
}

void GameRenderer::compile_used_shaders() {
    for (size_t i = 0; i < renderqueue().size(); i++) {
        VertexLayer& vl = renderqueue()[i];

        PipelineKey key{vl.material.vertex_shader.data(), vl.material.pixel_shader.data(), vl.material.screenspace, vl.material.blend_mode};

        if (!pipelines.contains(key)) pipelines[key] = buildPipeline(vl.material);
    }
    for (const PostEffect& fx : post_queue) {
        PostKey key{fx.pixel_shader.data()};
        auto it = postPipelines.find(key);

        if (it == postPipelines.end()) {
            postPipelines[key] = buildPostPipeline(fx);
            continue;
        }

        if (it->second.bg[0]) continue;  // still valid

        // resize dropped the bind groups (they referenced the old scene views) — rebuild them
        WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(it->second.pipeline, 0);

        for (int i = 0; i < 2; ++i) {
            WGPUBindGroupEntry e[3] = {{.binding = 0, .textureView = sceneView[i]}, {.binding = 1, .sampler = postSampler},
                {.binding = 2, .buffer = paramsScratch, .size = kParamMax}};

            WGPUBindGroupDescriptor d{.label = "Post Effect BG"_wgpu, .layout = bgl, .entryCount = 3, .entries = e};

            it->second.bg[i] = wgpuDeviceCreateBindGroup(device, &d);
        }
    }
}

// Builds (and caches) a pipeline for every combination of the given vertex/pixel shaders and
// blend modes, whether or not anything in renderqueue() currently uses them. Safe to call
// repeatedly — pipelines already in the map are skipped, so this composes fine with
// compile_all_shaders() running every frame.
void GameRenderer::precompile_shaders(std::span<const std::string_view> pixel_shaders, std::span<const BlendMode> blend_modes,
    std::span<const std::string_view> vertex_shaders) {
    for (std::string_view vs : vertex_shaders) {
        // Convention: kScreenVS-paired materials are the ones that set screenspace = true.
        // If that's not how your Material is populated elsewhere, adjust this line to match.
        bool screenspace = (vs.data() == kScreenVS);
        for (std::string_view ps : pixel_shaders) {
            for (BlendMode blend : blend_modes) {
                PipelineKey key{vs.data(), ps.data(), screenspace, blend};
                if (pipelines.contains(key)) continue;

                Material mat{};
                mat.vertex_shader = vs;
                mat.pixel_shader = ps;
                mat.screenspace = screenspace;
                mat.blend_mode = blend;
                pipelines[key] = buildPipeline(mat);
            }
        }
    }
}

// Warms every builtin vertex/pixel shader combination across every blend mode, regardless of
// whether renderqueue() currently contains a material that uses them.
void GameRenderer::compile_default_shaders() { precompile_shaders(kBuiltinPixelShaders, kAllBlendModes, kBuiltinVertexShaders); }

// ============================== per-frame ==============================

GameRenderer::GPUVertex GameRenderer::packVertex(const Vertex& v) {
    GPUVertex g;
    std::memcpy(g.pos, v.pos.raw, 12);
    std::memset(g.sd, 0, sizeof(g.sd));
    std::memcpy(g.sd, v.shaderdata.raw, 6);
    return g;
}

void GameRenderer::ensureVertexScratch(size_t vertexCount) {
    size_t needed = vertexCount * sizeof(GPUVertex);
    if (vertexScratch && needed <= vertexScratchCap) return;
    if (vertexScratch) wgpuBufferRelease(vertexScratch);
    vertexScratchCap = std::max(needed, vertexScratchCap * 2 + 4096);
    WGPUBufferDescriptor bd{
        .label = "Vertex Scratch"_wgpu, .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, .size = vertexScratchCap};
    vertexScratch = wgpuDeviceCreateBuffer(device, &bd);
}

void GameRenderer::ensureParamsScratch(size_t bytesNeeded) {
    if (paramsScratch && bytesNeeded <= paramsScratchCap) return;
    if (paramsScratchBG) {
        wgpuBindGroupRelease(paramsScratchBG);
        paramsScratchBG = nullptr;
    }
    if (paramsScratch) wgpuBufferRelease(paramsScratch);
    paramsScratchCap = std::max(bytesNeeded, paramsScratchCap * 2 + kParamAlign * 64);
    WGPUBufferDescriptor bd{
        .label = "Params Scratch"_wgpu, .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, .size = paramsScratchCap};
    paramsScratch = wgpuDeviceCreateBuffer(device, &bd);
    WGPUBindGroupEntry e{.binding = 0, .buffer = paramsScratch, .size = kParamMax};
    WGPUBindGroupDescriptor d{.label = "Params Scratch BG"_wgpu, .layout = paramsBGL, .entryCount = 1, .entries = &e};
    paramsScratchBG = wgpuDeviceCreateBindGroup(device, &d);
}

void GameRenderer::loop() {
    {
        static int prevW = 0, prevH = 0;
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w != prevW || h != prevH) {
            configureSurface(w, h);
            resizeSceneTargets();
            prevW = w;
            prevH = h;
        }
    }
    uint64_t now = SDL_GetTicks();
    double dt = lastTick ? (double)(now - lastTick) / 1000.0 : 0.0;
    lastTick = now;
    camera.update_zoom(dt);

    compile_used_shaders();

    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) return;
    WGPUTextureView backbuffer = wgpuTextureCreateView(st.texture, nullptr);
    if (!backbuffer) return;

    // --- transforms ---
    float minDim = (float)std::min(surfaceWidth, surfaceHeight);
    float pxPerUnit = camera._real_zoom > 0.0 ? minDim / (float)camera._real_zoom : 0.0f;
    TransformUBO worldT{{camera.x, camera.y},
        {surfaceWidth ? pxPerUnit * 2.0f / surfaceWidth : 0.0f, surfaceHeight ? pxPerUnit * 2.0f / surfaceHeight : 0.0f}};
    TransformUBO uiT{{0, 0}, {surfaceWidth ? 2.0f / surfaceWidth : 0.0f, surfaceHeight ? 2.0f / surfaceHeight : 0.0f}};
    wgpuQueueWriteBuffer(queue, worldUBO, 0, &worldT, sizeof(worldT));
    wgpuQueueWriteBuffer(queue, uiUBO, 0, &uiT, sizeof(uiT));

    // --- upload all vertices for this frame into one scratch buffer ---
    static thread_local std::vector<GPUVertex> gpuVerts;
    static thread_local std::vector<uint32_t> firstVertex;  // per VertexList
    gpuVerts.clear();
    firstVertex.clear();
    for (size_t i = 0; i < renderqueue().size(); i++) {
        VertexLayer& vl = renderqueue()[i];
        firstVertex.push_back((uint32_t)gpuVerts.size());
        for (const Vertex& v : vl.vertices) gpuVerts.push_back(packVertex(v));
    }
    if (!gpuVerts.empty()) {
        ensureVertexScratch(gpuVerts.size());
        wgpuQueueWriteBuffer(queue, vertexScratch, 0, gpuVerts.data(), gpuVerts.size() * sizeof(GPUVertex));
    }

    // --- gather + upload params for every draw (materials + enabled post effects) at aligned offsets ---
    static thread_local std::vector<uint32_t> matParamOffset;
    static thread_local std::vector<uint32_t> postParamOffset;
    matParamOffset.assign(renderqueue().size(), 0);
    postParamOffset.assign(post_queue.size(), 0);
    {
        size_t cursor = 0;
        std::vector<std::pair<const std::byte*, size_t>> writes;  // (src, offset) pairs to upload
        auto reserve = [&](std::span<const std::byte> params) -> uint32_t {
            uint32_t off = (uint32_t)cursor;
            if (!params.empty()) writes.push_back({params.data(), off});
            cursor += kParamAlign;
            return off;
        };
        for (size_t i = 0; i < renderqueue().size(); ++i) matParamOffset[i] = reserve(renderqueue()[i].material.params);
        for (size_t i = 0; i < post_queue.size(); ++i) postParamOffset[i] = reserve(post_queue[i].params);
        ensureParamsScratch(std::max<size_t>(cursor, kParamAlign));
        for (auto& [src, off] : writes) wgpuQueueWriteBuffer(queue, paramsScratch, off, src, kParamMax);
    }

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // --- main scene pass: renderqueue(), in order, into sceneTex[0] ---
    {
        WGPURenderPassColorAttachment att{
            .view = sceneView[0], .loadOp = WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store, .clearValue = {0.1, 0.1, 0.1, 1.0}};
        WGPURenderPassDescriptor pd{.colorAttachmentCount = 1, .colorAttachments = &att};
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pd);
        if (vertexScratch) wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexScratch, 0, gpuVerts.size() * sizeof(GPUVertex));
        for (size_t i = 0; i < renderqueue().size(); ++i) {
            const VertexLayer& vl = renderqueue()[i];
            if (vl.vertices.empty()) continue;
            const Material& mat = vl.material;
            PipelineKey key{mat.vertex_shader.data(), mat.pixel_shader.data(), mat.screenspace, mat.blend_mode};
            wgpuRenderPassEncoderSetPipeline(pass, pipelines.at(key));
            wgpuRenderPassEncoderSetBindGroup(pass, 0, mat.screenspace ? uiBG : worldBG, 0, nullptr);
            uint16_t texId = nullTextureId;
            if (mat.pixel_shader.contains("USES_TEXTURES")) std::memcpy(&texId, vl.vertices[0].shaderdata.raw, sizeof(texId));
            wgpuRenderPassEncoderSetBindGroup(pass, 1, textures[texId < textures.size() ? texId : nullTextureId].bindGroup, 0, nullptr);
            uint32_t dynOff = matParamOffset[i];
            wgpuRenderPassEncoderSetBindGroup(pass, 2, paramsScratchBG, 1, &dynOff);
            wgpuRenderPassEncoderDraw(pass, (uint32_t)vl.vertices.size(), 1, firstVertex[i], 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    // --- post chain: sceneTex[0] -> ... -> backbuffer ---
    int src = 0;
    for (size_t i = 0; i < post_queue.size(); ++i) {
        const PostEffect& fx = post_queue[i];
        if (!fx.enabled) continue;
        PostPipeline& pp = postPipelines.at(PostKey{fx.pixel_shader.data()});
        int dst = 1 - src;
        WGPURenderPassColorAttachment att{
            .view = sceneView[dst], .loadOp = WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store, .clearValue = {0, 0, 0, 1}};
        WGPURenderPassDescriptor pd{.colorAttachmentCount = 1, .colorAttachments = &att};
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pd);
        wgpuRenderPassEncoderSetPipeline(pass, pp.pipeline);
        uint32_t dynOff = postParamOffset[i];
        wgpuRenderPassEncoderSetBindGroup(pass, 0, pp.bg[src], 1, &dynOff);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        src = dst;
    }
    {
        WGPURenderPassColorAttachment att{
            .view = backbuffer, .loadOp = WGPULoadOp_Clear, .storeOp = WGPUStoreOp_Store, .clearValue = {0, 0, 0, 1}};
        WGPURenderPassDescriptor pd{.colorAttachmentCount = 1, .colorAttachments = &att};
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pd);
        wgpuRenderPassEncoderSetPipeline(pass, blitPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, blitBG[src], 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuSurfacePresent(surface);

    wgpuTextureViewRelease(backbuffer);
    wgpuTextureRelease(st.texture);
}
// </AI>
