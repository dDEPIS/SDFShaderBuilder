#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <webgpu/webgpu.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_wgpu.h"
#include <imgui_node_editor.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace ed = ax::NodeEditor;

static std::string g_LastWebGPUError = "";

struct alignas(16) ShaderUniforms {
    float resolution[2];
    float time;
    float color_t;
    float yaw;
    float pitch;
    float quality;
    float _pad;
};

struct WebGPUContext {
    WGPUInstance instance = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    WGPUSurfaceConfiguration surfaceConfig = {};
    WGPUTextureFormat swapchainFormat = WGPUTextureFormat_BGRA8Unorm;
};

struct ViewportRenderer {
    WGPUTexture texture = nullptr;
    WGPUTextureView textureView = nullptr;
    WGPUBuffer uniformBuffer = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPURenderPipeline pipeline = nullptr;

    uint32_t width = 800;
    uint32_t height = 600;

    void InitResources(WGPUDevice device) {
        WGPUBufferDescriptor bufDesc = {};
        bufDesc.size = sizeof(ShaderUniforms);
        bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uniformBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);

        WGPUBindGroupLayoutEntry bglEntry = {};
        bglEntry.binding = 0;
        bglEntry.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
        bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
        bglEntry.buffer.minBindingSize = sizeof(ShaderUniforms);

        WGPUBindGroupLayoutDescriptor bglDesc = {};
        bglDesc.entryCount = 1;
        bglDesc.entries = &bglEntry;
        bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

        WGPUBindGroupEntry bgEntry = {};
        bgEntry.binding = 0;
        bgEntry.buffer = uniformBuffer;
        bgEntry.size = sizeof(ShaderUniforms);

        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = bindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

        WGPUPipelineLayoutDescriptor plDesc = {};
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts = &bindGroupLayout;
        pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

        ResizeTarget(device, width, height);
    }

    void ResizeTarget(WGPUDevice device, uint32_t newWidth, uint32_t newHeight) {
        if (newWidth == 0 || newHeight == 0) return;
        if (newWidth == width && newHeight == height && texture != nullptr) return;

        width = newWidth;
        height = newHeight;

        if (textureView) { wgpuTextureViewRelease(textureView); textureView = nullptr; }
        if (texture) { wgpuTextureRelease(texture); texture = nullptr; }

        WGPUTextureDescriptor texDesc = {};
        texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.size = { width, height, 1 };
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texture = wgpuDeviceCreateTexture(device, &texDesc);
        textureView = wgpuTextureCreateView(texture, nullptr);
    }
};

bool InitWebGPU(WebGPUContext& ctx, GLFWwindow* window, int width, int height) {
    WGPUInstanceDescriptor instDesc = {};
    ctx.instance = wgpuCreateInstance(&instDesc);

    WGPUSurfaceDescriptorFromWindowsHWND hwndDesc = {};
    hwndDesc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    hwndDesc.hinstance = GetModuleHandle(NULL);
    hwndDesc.hwnd = glfwGetWin32Window(window);

    WGPUSurfaceDescriptor surfDesc = {};
    surfDesc.nextInChain = (const WGPUChainedStruct*)&hwndDesc;
    ctx.surface = wgpuInstanceCreateSurface(ctx.instance, &surfDesc);

    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = ctx.surface;
    WGPUAdapter adapter = nullptr;
    wgpuInstanceRequestAdapter(ctx.instance, &adapterOpts, [](WGPURequestAdapterStatus status, WGPUAdapter res, const char* msg, void* userdata) {
        *(WGPUAdapter*)userdata = res;
        }, &adapter);

    WGPUDeviceDescriptor deviceDesc = {};
    wgpuAdapterRequestDevice(adapter, &deviceDesc, [](WGPURequestDeviceStatus status, WGPUDevice res, const char* msg, void* userdata) {
        *(WGPUDevice*)userdata = res;
        }, &ctx.device);

    wgpuDeviceSetUncapturedErrorCallback(ctx.device, [](WGPUErrorType type, const char* message, void* userdata) {
        g_LastWebGPUError = message ? message : "Unknown WebGPU Error";
        }, nullptr);

    ctx.queue = wgpuDeviceGetQueue(ctx.device);

    ctx.surfaceConfig = {};
    ctx.surfaceConfig.device = ctx.device;
    ctx.surfaceConfig.format = ctx.swapchainFormat;
    ctx.surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
    ctx.surfaceConfig.width = (uint32_t)width;
    ctx.surfaceConfig.height = (uint32_t)height;
    ctx.surfaceConfig.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(ctx.surface, &ctx.surfaceConfig);

    return true;
}

bool CompileWGSLPipeline(WGPUDevice device, ViewportRenderer& r, const char* code, std::string& outError) {
    g_LastWebGPUError.clear();

    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = code;

    WGPUShaderModuleDescriptor smDesc = {};
    smDesc.nextInChain = (const WGPUChainedStruct*)&wgslDesc;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &smDesc);

    if (!g_LastWebGPUError.empty()) {
        outError = g_LastWebGPUError;
        if (shaderModule) wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = {};
    pipeDesc.layout = r.pipelineLayout;
    pipeDesc.vertex.module = shaderModule;
    pipeDesc.vertex.entryPoint = "vs_main";
    pipeDesc.fragment = &fragmentState;
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count = 1;
    pipeDesc.multisample.mask = ~0u;

    WGPURenderPipeline newPipeline = wgpuDeviceCreateRenderPipeline(device, &pipeDesc);
    wgpuShaderModuleRelease(shaderModule);

    if (!g_LastWebGPUError.empty() || !newPipeline) {
        outError = g_LastWebGPUError.empty() ? "Pipeline Link Error" : g_LastWebGPUError;
        if (newPipeline) wgpuRenderPipelineRelease(newPipeline);
        return false;
    }

    if (r.pipeline) wgpuRenderPipelineRelease(r.pipeline);
    r.pipeline = newPipeline;
    outError = "Shader successfully compiled.";
    return true;
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    int currentWidth = 1600;
    int currentHeight = 900;
    GLFWwindow* window = glfwCreateWindow(currentWidth, currentHeight, "SDF Shader Builder", nullptr, nullptr);

    WebGPUContext wgpuCtx;
    InitWebGPU(wgpuCtx, window, currentWidth, currentHeight);

    ViewportRenderer renderer;
    renderer.InitResources(wgpuCtx.device);

    ed::Config config;
    config.SettingsFile = "NodeEditorSettings.json";
    ed::EditorContext* nodeEditorCtx = ed::CreateEditor(&config);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplWGPU_InitInfo init_info = {};
    init_info.Device = wgpuCtx.device;
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = wgpuCtx.swapchainFormat;

    ImGui_ImplGlfw_InitForOther(window, true);
    ImGui_ImplWGPU_Init(&init_info);

    ShaderUniforms uniforms = {};
    uniforms.quality = 2.0f;

    bool isPlaying = true;
    float accumulatedTime = 0.0f;
    float lastFrameTime = (float)glfwGetTime();

    static char shaderSource[131072] =
        "struct Uniforms {\n"
        "  resolution: vec2f,\n  time: f32,\n  color_t: f32,\n"
        "  yaw: f32,\n  pitch: f32,\n  quality: f32,\n  _pad: f32,\n"
        "}\n\n"
        "@group(0) @binding(0) var<uniform> uniforms: Uniforms;\n\n"
        "@vertex fn vs_main(@builtin(vertex_index) id: u32) -> @builtin(position) vec4f {\n"
        "  var pos = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));\n"
        "  return vec4f(pos[id], 0.0, 1.0);\n"
        "}\n\n"
        "@fragment fn fs_main(@builtin(position) pos: vec4f) -> @location(0) vec4f {\n"
        "  let uv = pos.xy / uniforms.resolution;\n"
        "  return vec4f(uv.x, uv.y, abs(sin(uniforms.time)), 1.0);\n"
        "}";

    std::string compileStatus = "Ready.";
    CompileWGSLPipeline(wgpuCtx.device, renderer, shaderSource, compileStatus);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        float currentGlfwTime = (float)glfwGetTime();
        float deltaTime = currentGlfwTime - lastFrameTime;
        lastFrameTime = currentGlfwTime;

        if (isPlaying) {
            accumulatedTime += deltaTime;
        }
        uniforms.time = accumulatedTime;

        int fbWidth = 0, fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

        if (fbWidth <= 0 || fbHeight <= 0) {
            continue;
        }

        if (fbWidth != currentWidth || fbHeight != currentHeight) {
            currentWidth = fbWidth;
            currentHeight = fbHeight;
            wgpuCtx.surfaceConfig.width = (uint32_t)currentWidth;
            wgpuCtx.surfaceConfig.height = (uint32_t)currentHeight;
            wgpuSurfaceConfigure(wgpuCtx.surface, &wgpuCtx.surfaceConfig);
        }

        WGPUSurfaceTexture surfaceTexture;
        wgpuSurfaceGetCurrentTexture(wgpuCtx.surface, &surfaceTexture);
        if (!surfaceTexture.texture) {
            continue;
        }

        WGPUTextureView nextTexture = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
        if (!nextTexture) {
            wgpuTextureRelease(surfaceTexture.texture);
            continue;
        }

        ImGui_ImplWGPU_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        ImGui::Begin("Shader Parameters");
        ImGui::Text("Performance: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::Separator();

        ImGui::SliderFloat("Color Blend (Red <-> White)", &uniforms.color_t, 0.0f, 1.0f);
        const char* qualityTiers[] = { "Low (0)", "Medium (1)", "High (2)" };
        int currentTier = (int)uniforms.quality;
        if (ImGui::Combo("Quality Tier", &currentTier, qualityTiers, 3)) {
            uniforms.quality = (float)currentTier;
        }
        ImGui::Text("Yaw: %.2f | Pitch: %.2f", uniforms.yaw, uniforms.pitch);

        ImGui::Separator();
        ImGui::Text("Time Controls");
        if (ImGui::Button(isPlaying ? "Pause" : "Play")) {
            isPlaying = !isPlaying;
        }
        ImGui::SameLine();
        if (ImGui::Button("<< -0.1s")) {
            accumulatedTime -= 0.1f;
        }
        ImGui::SameLine();
        if (ImGui::Button("+0.1s >>")) {
            accumulatedTime += 0.1f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            accumulatedTime = 0.0f;
        }
        ImGui::Text("Time: %.2f s", uniforms.time);

        ImGui::Separator();
        ImGui::Text("Project File");

        static char projectName[128] = "my_artwork";
        ImGui::InputText(".json", projectName, IM_ARRAYSIZE(projectName));

        if (ImGui::Button("Save Project", ImVec2(120, 25))) {
            json j;
            j["parameters"]["color_t"] = uniforms.color_t;
            j["parameters"]["quality"] = uniforms.quality;
            j["camera"]["yaw"] = uniforms.yaw;
            j["camera"]["pitch"] = uniforms.pitch;
            j["shader_code"] = std::string(shaderSource);

            std::string filename = std::string(projectName) + ".json";
            std::ofstream file(filename);
            if (file.is_open()) {
                file << std::setw(4) << j << std::endl;
                compileStatus = "Successfully saved " + filename;
            }
            else {
                compileStatus = "Error: Could not write to " + filename;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Load Project", ImVec2(120, 25))) {
            std::string filename = std::string(projectName) + ".json";
            std::ifstream file(filename);
            if (file.is_open()) {
                json j;
                file >> j;

                if (j.contains("parameters")) {
                    uniforms.color_t = j["parameters"].value("color_t", uniforms.color_t);
                    uniforms.quality = j["parameters"].value("quality", uniforms.quality);
                }
                if (j.contains("camera")) {
                    uniforms.yaw = j["camera"].value("yaw", uniforms.yaw);
                    uniforms.pitch = j["camera"].value("pitch", uniforms.pitch);
                }
                if (j.contains("shader_code")) {
                    std::string loadedCode = j["shader_code"];
                    strncpy(shaderSource, loadedCode.c_str(), sizeof(shaderSource) - 1);
                }

                CompileWGSLPipeline(wgpuCtx.device, renderer, shaderSource, compileStatus);
                compileStatus = "Successfully loaded " + filename;
            }
            else {
                compileStatus = "Error: " + filename + " not found.";
            }
        }
        ImGui::End();

        ImGui::Begin("WGSL Editor");
        if (ImGui::Button("Compile Shader", ImVec2(150, 28))) {
            CompileWGSLPipeline(wgpuCtx.device, renderer, shaderSource, compileStatus);
        }

        if (!compileStatus.empty()) {
            bool isError = (compileStatus.find("successfully") == std::string::npos && compileStatus != "Ready.");
            if (isError) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            }

            ImGui::Separator();
            ImGui::TextUnformatted(isError ? "Compiler Output (Errors Found):" : "Compiler Output:");

            ImGui::BeginChild("ErrorBox", ImVec2(0, 100), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextWrapped("%s", compileStatus.c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::Separator();
        ImGui::InputTextMultiline("##source", shaderSource, IM_ARRAYSIZE(shaderSource),
            ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_AllowTabInput);
        ImGui::End();

        ImGui::Begin("Raymarcher Viewport");
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();

        if (viewportSize.x > 1.0f && viewportSize.y > 1.0f) {
            renderer.ResizeTarget(wgpuCtx.device, (uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            uniforms.yaw -= dragDelta.x * 0.01f;
            uniforms.pitch += dragDelta.y * 0.01f;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        if (renderer.textureView && viewportSize.x > 1.0f && viewportSize.y > 1.0f) {
            ImGui::Image((ImTextureID)renderer.textureView, viewportSize);
        }
        ImGui::End();

        ImGui::Begin("Node Graph");
        ed::SetCurrentEditor(nodeEditorCtx);
        ed::Begin("SDF Editor");
        ed::BeginNode(1);
        ImGui::Text("Node workspace");
        ed::EndNode();
        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::End();

        ImGui::Render();

        uniforms.resolution[0] = (float)renderer.width;
        uniforms.resolution[1] = (float)renderer.height;
        wgpuQueueWriteBuffer(wgpuCtx.queue, renderer.uniformBuffer, 0, &uniforms, sizeof(ShaderUniforms));

        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(wgpuCtx.device, &encDesc);

        if (renderer.pipeline && renderer.textureView) {
            WGPURenderPassColorAttachment viewAttachment = {};
            viewAttachment.view = renderer.textureView;
            viewAttachment.loadOp = WGPULoadOp_Clear;
            viewAttachment.storeOp = WGPUStoreOp_Store;
            viewAttachment.clearValue = WGPUColor{ 0.0, 0.0, 0.0, 1.0 };

            WGPURenderPassDescriptor viewPassDesc = {};
            viewPassDesc.colorAttachmentCount = 1;
            viewPassDesc.colorAttachments = &viewAttachment;

            WGPURenderPassEncoder viewPass = wgpuCommandEncoderBeginRenderPass(encoder, &viewPassDesc);
            wgpuRenderPassEncoderSetPipeline(viewPass, renderer.pipeline);
            wgpuRenderPassEncoderSetBindGroup(viewPass, 0, renderer.bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(viewPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(viewPass);
            wgpuRenderPassEncoderRelease(viewPass);
        }

        WGPURenderPassColorAttachment uiAttachment = {};
        uiAttachment.view = nextTexture;
        uiAttachment.loadOp = WGPULoadOp_Clear;
        uiAttachment.storeOp = WGPUStoreOp_Store;
        uiAttachment.clearValue = WGPUColor{ 0.1, 0.1, 0.12, 1.0 };

        WGPURenderPassDescriptor uiPassDesc = {};
        uiPassDesc.colorAttachmentCount = 1;
        uiPassDesc.colorAttachments = &uiAttachment;

        WGPURenderPassEncoder uiPass = wgpuCommandEncoderBeginRenderPass(encoder, &uiPassDesc);
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), uiPass);
        wgpuRenderPassEncoderEnd(uiPass);
        wgpuRenderPassEncoderRelease(uiPass);

        WGPUCommandBufferDescriptor cmdBufDesc = {};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cmdBufDesc);
        wgpuCommandEncoderRelease(encoder);

        wgpuQueueSubmit(wgpuCtx.queue, 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);

        wgpuSurfacePresent(wgpuCtx.surface);
        wgpuTextureViewRelease(nextTexture);
        wgpuTextureRelease(surfaceTexture.texture);
    }

    ed::DestroyEditor(nodeEditorCtx);
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}