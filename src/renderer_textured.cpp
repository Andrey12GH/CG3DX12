#include "dx12renderer.h"
#include "shader.h"

#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace DirectX;

namespace
{
    struct TexturePixels
    {
        UINT width = 0;
        UINT height = 0;
        UINT rowPitch = 0;
        std::vector<uint8_t> rgba;
    };

    void ThrowIfFailed(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            std::ostringstream message;
            message << operation << " failed (HRESULT 0x" << std::hex
                << static_cast<unsigned long>(result) << ')';
            throw std::runtime_error(message.str());
        }
    }

    D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
    {
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = type;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        return properties;
    }

    D3D12_RESOURCE_DESC BufferDescription(UINT64 size)
    {
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = size;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return description;
    }

    D3D12_RESOURCE_BARRIER TransitionBarrier(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return barrier;
    }

    ComPtr<ID3DBlob> CompileShader(const std::string& source, const char* target)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        ComPtr<ID3DBlob> shader;
        ComPtr<ID3DBlob> errors;
        const HRESULT result = D3DCompile(
            source.data(), source.size(), nullptr, nullptr, nullptr,
            "main", target, flags, 0, &shader, &errors);

        if (errors)
            OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ThrowIfFailed(result, "D3DCompile");
        return shader;
    }

    std::filesystem::path FindAssetPath(const std::filesystem::path& requested)
    {
        if (requested.is_absolute() && std::filesystem::exists(requested))
            return requested;
        if (std::filesystem::exists(requested))
            return std::filesystem::absolute(requested);

        wchar_t modulePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        std::filesystem::path directory = std::filesystem::path(modulePath).parent_path();
        for (int level = 0; level < 5 && !directory.empty(); ++level)
        {
            const std::filesystem::path candidate = directory / requested;
            if (std::filesystem::exists(candidate))
                return std::filesystem::absolute(candidate);
            directory = directory.parent_path();
        }

        return requested;
    }

    std::string ReadPpmToken(std::istream& input)
    {
        std::string token;
        while (input >> token)
        {
            if (!token.empty() && token.front() == '#')
            {
                input.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
                continue;
            }
            return token;
        }
        return {};
    }

    bool LoadPPM(const std::filesystem::path& path, TexturePixels& texture)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return false;

        const std::string magic = ReadPpmToken(input);
        if (magic != "P3" && magic != "P6")
            return false;

        try
        {
            texture.width = static_cast<UINT>(std::stoul(ReadPpmToken(input)));
            texture.height = static_cast<UINT>(std::stoul(ReadPpmToken(input)));
            const unsigned int maximum = std::stoul(ReadPpmToken(input));
            if (texture.width == 0 || texture.height == 0 || maximum == 0 || maximum > 255)
                return false;

            texture.rowPitch = texture.width * 4;
            texture.rgba.resize(static_cast<size_t>(texture.rowPitch) * texture.height);

            if (magic == "P6")
            {
                input.get();
                std::vector<uint8_t> rgb(static_cast<size_t>(texture.width) * texture.height * 3);
                input.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
                if (input.gcount() != static_cast<std::streamsize>(rgb.size()))
                    return false;

                for (size_t pixel = 0; pixel < static_cast<size_t>(texture.width) * texture.height; ++pixel)
                {
                    texture.rgba[pixel * 4 + 0] = rgb[pixel * 3 + 0];
                    texture.rgba[pixel * 4 + 1] = rgb[pixel * 3 + 1];
                    texture.rgba[pixel * 4 + 2] = rgb[pixel * 3 + 2];
                    texture.rgba[pixel * 4 + 3] = 255;
                }
            }
            else
            {
                for (size_t pixel = 0; pixel < static_cast<size_t>(texture.width) * texture.height; ++pixel)
                {
                    texture.rgba[pixel * 4 + 0] = static_cast<uint8_t>(std::stoul(ReadPpmToken(input)) * 255 / maximum);
                    texture.rgba[pixel * 4 + 1] = static_cast<uint8_t>(std::stoul(ReadPpmToken(input)) * 255 / maximum);
                    texture.rgba[pixel * 4 + 2] = static_cast<uint8_t>(std::stoul(ReadPpmToken(input)) * 255 / maximum);
                    texture.rgba[pixel * 4 + 3] = 255;
                }
            }
        }
        catch (...)
        {
            return false;
        }

        return true;
    }

    bool LoadWIC(const std::filesystem::path& path, TexturePixels& texture)
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory));
        if (FAILED(result))
        {
            result = CoCreateInstance(
                CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory));
        }
        if (FAILED(result))
            return false;

        ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromFilename(
            path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(result))
            return false;

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)))
            return false;

        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)))
            return false;
        if (FAILED(converter->Initialize(
            frame.Get(), GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0,
            WICBitmapPaletteTypeCustom)))
        {
            return false;
        }

        if (FAILED(converter->GetSize(&texture.width, &texture.height)) ||
            texture.width == 0 || texture.height == 0)
        {
            return false;
        }

        texture.rowPitch = texture.width * 4;
        texture.rgba.resize(static_cast<size_t>(texture.rowPitch) * texture.height);
        return SUCCEEDED(converter->CopyPixels(
            nullptr, texture.rowPitch,
            static_cast<UINT>(texture.rgba.size()), texture.rgba.data()));
    }

    bool LoadTexturePixels(const std::filesystem::path& path, TexturePixels& texture)
    {
        std::wstring extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        return extension == L".ppm" ? LoadPPM(path, texture) : LoadWIC(path, texture);
    }

    TexturePixels MakeFallbackChecker()
    {
        TexturePixels texture;
        texture.width = 64;
        texture.height = 64;
        texture.rowPitch = texture.width * 4;
        texture.rgba.resize(static_cast<size_t>(texture.rowPitch) * texture.height);

        for (UINT y = 0; y < texture.height; ++y)
        {
            for (UINT x = 0; x < texture.width; ++x)
            {
                const bool light = ((x / 8) + (y / 8)) % 2 == 0;
                const size_t offset = static_cast<size_t>(y) * texture.rowPitch + x * 4;
                texture.rgba[offset + 0] = light ? 235 : 45;
                texture.rgba[offset + 1] = light ? 235 : 70;
                texture.rgba[offset + 2] = light ? 235 : 150;
                texture.rgba[offset + 3] = 255;
            }
        }
        return texture;
    }
}

DX12Renderer::DX12Renderer()
    : mWorld(XMMatrixIdentity()),
      mView(XMMatrixIdentity()),
      mProjection(XMMatrixIdentity()),
      mStartTime(std::chrono::steady_clock::now())
{
}

DX12Renderer::~DX12Renderer()
{
    if (mCommandQueue && mFence)
        WaitForGPU();

    if (mObjectCB && mObjectCBMapped)
        mObjectCB->Unmap(0, nullptr);
    if (mLightCB && mLightCBMapped)
        mLightCB->Unmap(0, nullptr);
    if (mMaterialCB && mMaterialCBMapped)
        mMaterialCB->Unmap(0, nullptr);
    if (mFenceEvent)
        CloseHandle(mFenceEvent);
}

bool DX12Renderer::Initialize(HWND hwnd, int width, int height)
{
    try
    {
        mWidth = width;
        mHeight = height;

        CreateDevice();
        CreateCommandObjects();
        CreateSwapChain(hwnd);
        CreateDescriptorHeaps();
        CreateRenderTargets();
        CreateDepthStencil();
        CreateFence();

        BuildObj(FindAssetPath("sponza.obj").string());
        BuildTextureResources();
        BuildRootSignature();
        BuildShadersAndPSO();
        BuildConstantBuffers();

        mViewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        mScissorRect = { 0, 0, width, height };

        mWorld = XMMatrixIdentity();
        mView = XMMatrixLookAtLH(
            XMVectorSet(0.0f, 1.7f, -7.0f, 1.0f),
            XMVectorZero(),
            XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        mProjection = XMMatrixPerspectiveFovLH(
            XM_PIDIV4, static_cast<float>(width) / height, 0.1f, 100.0f);
        mStartTime = std::chrono::steady_clock::now();

        Update();
        return true;
    }
    catch (const std::exception& exception)
    {
        OutputDebugStringA(exception.what());
        OutputDebugStringA("\n");
        return false;
    }
}

void DX12Renderer::Update()
{
    const float seconds = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - mStartTime).count();

    mWorld = XMMatrixRotationY(seconds * 0.12f);
    mTextureOffset.x = std::fmod(seconds * mTextureScrollSpeed.x, 1.0f);
    mTextureOffset.y = std::fmod(seconds * mTextureScrollSpeed.y, 1.0f);

    ObjectConstants object{};
    object.world = XMMatrixTranspose(mWorld);
    object.view = XMMatrixTranspose(mView);
    object.projection = XMMatrixTranspose(mProjection);
    *mObjectCBMapped = object;

    LightConstants light{};
    XMStoreFloat3(&light.lightDir, XMVector3Normalize(XMVectorSet(0.5f, -1.0f, 0.5f, 0.0f)));
    light.ambientColor = XMFLOAT4(0.28f, 0.28f, 0.32f, 1.0f);
    light.diffuseColor = XMFLOAT4(0.85f, 0.82f, 0.78f, 1.0f);
    *mLightCBMapped = light;

    for (size_t index = 0; index < mModel.materials.size(); ++index)
    {
        MaterialConstants material{};
        material.diffuseColor = mModel.materials[index].diffuseColor;
        material.uvScale = mTextureTiling;
        material.uvOffset = mTextureOffset;
        std::memcpy(
            mMaterialCBMapped + index * ConstantBufferAlignment,
            &material, sizeof(material));
    }
}

void DX12Renderer::CreateDevice()
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        debug->EnableDebugLayer();
#endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mFactory)), "CreateDXGIFactory1");

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; mFactory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index)
    {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            adapter.Reset();
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(
            adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice))))
        {
            break;
        }
        adapter.Reset();
    }

    if (!mDevice)
    {
        ComPtr<IDXGIAdapter> warp;
        ThrowIfFailed(mFactory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter");
        ThrowIfFailed(D3D12CreateDevice(
            warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)),
            "D3D12CreateDevice");
    }
}

void DX12Renderer::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(mDevice->CreateCommandQueue(
        &queueDescription, IID_PPV_ARGS(&mCommandQueue)), "CreateCommandQueue");
    ThrowIfFailed(mDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mCommandAllocator)),
        "CreateCommandAllocator");
    ThrowIfFailed(mDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocator.Get(), nullptr,
        IID_PPV_ARGS(&mCommandList)), "CreateCommandList");
    ThrowIfFailed(mCommandList->Close(), "Close initial command list");
}

void DX12Renderer::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.BufferCount = FrameCount;
    description.Width = mWidth;
    description.Height = mHeight;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(mFactory->CreateSwapChainForHwnd(
        mCommandQueue.Get(), hwnd, &description, nullptr, nullptr, &swapChain),
        "CreateSwapChainForHwnd");
    ThrowIfFailed(swapChain.As(&mSwapChain), "Query IDXGISwapChain3");
    mCurrentBackBuffer = mSwapChain->GetCurrentBackBufferIndex();
}

void DX12Renderer::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDescription{};
    rtvDescription.NumDescriptors = FrameCount;
    rtvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(
        &rtvDescription, IID_PPV_ARGS(&mRtvHeap)), "Create RTV heap");
    mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvDescription{};
    dsvDescription.NumDescriptors = 1;
    dsvDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(
        &dsvDescription, IID_PPV_ARGS(&mDsvHeap)), "Create DSV heap");
}

void DX12Renderer::CreateRenderTargets()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < FrameCount; ++index)
    {
        ThrowIfFailed(mSwapChain->GetBuffer(index, IID_PPV_ARGS(&mRenderTargets[index])),
            "Get swap-chain buffer");
        mDevice->CreateRenderTargetView(mRenderTargets[index].Get(), nullptr, handle);
        handle.ptr += mRtvDescriptorSize;
    }
}

void DX12Renderer::CreateDepthStencil()
{
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = mWidth;
    description.Height = mHeight;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &description,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
        IID_PPV_ARGS(&mDepthStencil)), "Create depth stencil");
    mDevice->CreateDepthStencilView(
        mDepthStencil.Get(), nullptr, mDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void DX12Renderer::CreateFence()
{
    ThrowIfFailed(mDevice->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)), "Create fence");
    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!mFenceEvent)
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "Create fence event");
}

void DX12Renderer::BuildRootSignature()
{
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameters[2].Descriptor.ShaderRegister = 2;
    parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    parameters[3].DescriptorTable.pDescriptorRanges = &srvRange;
    parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MipLODBias = 0.0f;
    sampler.MaxAnisotropy = 8;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = _countof(parameters);
    description.pParameters = parameters;
    description.NumStaticSamplers = 1;
    description.pStaticSamplers = &sampler;
    description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3D12SerializeRootSignature(
        &description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (errors)
        OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    ThrowIfFailed(result, "D3D12SerializeRootSignature");
    ThrowIfFailed(mDevice->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)), "CreateRootSignature");
}

void DX12Renderer::BuildShadersAndPSO()
{
    const ComPtr<ID3DBlob> vertexShader = CompileShader(Shaders::VertexShader, "vs_5_0");
    const ComPtr<ID3DBlob> pixelShader = CompileShader(Shaders::PixelShader, "ps_5_0");

    const D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, position)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, color)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, normal)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, static_cast<UINT>(offsetof(Vertex, texcoord)), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer.FrontCounterClockwise = FALSE;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_RENDER_TARGET_BLEND_DESC targetBlend{};
    targetBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0] = targetBlend;

    D3D12_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
    description.InputLayout = { layout, _countof(layout) };
    description.pRootSignature = mRootSignature.Get();
    description.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    description.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    description.RasterizerState = rasterizer;
    description.BlendState = blend;
    description.DepthStencilState = depth;
    description.SampleMask = UINT_MAX;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;

    ThrowIfFailed(mDevice->CreateGraphicsPipelineState(
        &description, IID_PPV_ARGS(&mPipelineState)), "CreateGraphicsPipelineState");
}

void DX12Renderer::BuildObj(const std::string& path)
{
    if (!LoadOBJ(path, mModel))
        throw std::runtime_error("Failed to load OBJ model: " + path);

    const UINT64 vertexBufferSize = sizeof(Vertex) * static_cast<UINT64>(mModel.vertices.size());
    const UINT64 indexBufferSize = sizeof(uint32_t) * static_cast<UINT64>(mModel.indices.size());
    if (vertexBufferSize > UINT_MAX || indexBufferSize > UINT_MAX)
        throw std::runtime_error("OBJ model is too large for a single vertex/index view");

    const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC buffer = BufferDescription(vertexBufferSize);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mVertexBuffer)), "Create vertex buffer");

    void* mapped = nullptr;
    ThrowIfFailed(mVertexBuffer->Map(0, nullptr, &mapped), "Map vertex buffer");
    std::memcpy(mapped, mModel.vertices.data(), static_cast<size_t>(vertexBufferSize));
    mVertexBuffer->Unmap(0, nullptr);

    mVertexBufferView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
    mVertexBufferView.StrideInBytes = sizeof(Vertex);

    buffer.Width = indexBufferSize;
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mIndexBuffer)), "Create index buffer");
    ThrowIfFailed(mIndexBuffer->Map(0, nullptr, &mapped), "Map index buffer");
    std::memcpy(mapped, mModel.indices.data(), static_cast<size_t>(indexBufferSize));
    mIndexBuffer->Unmap(0, nullptr);

    mIndexBufferView.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    mIndexBufferView.SizeInBytes = static_cast<UINT>(indexBufferSize);
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
}

void DX12Renderer::BuildTextureResources()
{
    struct PendingTexture
    {
        std::string key;
        TexturePixels pixels;
    };

    std::vector<PendingTexture> pending;
    pending.push_back({ "__fallback_checker__", MakeFallbackChecker() });
    std::unordered_map<std::string, uint32_t> textureLookup;
    textureLookup[pending.front().key] = 0;

    for (ObjMaterial& material : mModel.materials)
    {
        material.textureSrvIndex = 0;
        if (material.diffuseTexturePath.empty())
            continue;

        const std::filesystem::path path = FindAssetPath(material.diffuseTexturePath);
        std::string key = path.lexically_normal().string();
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });

        const auto existing = textureLookup.find(key);
        if (existing != textureLookup.end())
        {
            material.textureSrvIndex = existing->second;
            continue;
        }

        TexturePixels pixels;
        if (!std::filesystem::exists(path) || !LoadTexturePixels(path, pixels))
        {
            const std::string message = "Texture could not be loaded; using checker: " + path.string() + "\n";
            OutputDebugStringA(message.c_str());
            continue;
        }

        const uint32_t index = static_cast<uint32_t>(pending.size());
        textureLookup[key] = index;
        pending.push_back({ key, std::move(pixels) });
        material.textureSrvIndex = index;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
    heapDescription.NumDescriptors = static_cast<UINT>(pending.size());
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(
        &heapDescription, IID_PPV_ARGS(&mSrvHeap)), "Create SRV heap");
    mSrvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ThrowIfFailed(mCommandAllocator->Reset(), "Reset upload command allocator");
    ThrowIfFailed(mCommandList->Reset(mCommandAllocator.Get(), nullptr),
        "Reset upload command list");

    std::vector<ComPtr<ID3D12Resource>> uploadBuffers;
    mTextures.reserve(pending.size());
    uploadBuffers.reserve(pending.size());
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = mSrvHeap->GetCPUDescriptorHandleForHeapStart();

    for (const PendingTexture& source : pending)
    {
        D3D12_RESOURCE_DESC textureDescription{};
        textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDescription.Width = source.pixels.width;
        textureDescription.Height = source.pixels.height;
        textureDescription.DepthOrArraySize = 1;
        textureDescription.MipLevels = 1;
        textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        ComPtr<ID3D12Resource> texture;
        const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(mDevice->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDescription,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&texture)), "Create texture resource");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rowCount = 0;
        UINT64 rowSize = 0;
        UINT64 uploadSize = 0;
        mDevice->GetCopyableFootprints(
            &textureDescription, 0, 1, 0,
            &footprint, &rowCount, &rowSize, &uploadSize);

        ComPtr<ID3D12Resource> upload;
        const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC uploadDescription = BufferDescription(uploadSize);
        ThrowIfFailed(mDevice->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDescription,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload)), "Create texture upload buffer");

        uint8_t* mapped = nullptr;
        const D3D12_RANGE noRead{ 0, 0 };
        ThrowIfFailed(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)),
            "Map texture upload buffer");
        for (UINT row = 0; row < rowCount; ++row)
        {
            std::memcpy(
                mapped + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                source.pixels.rgba.data() + static_cast<size_t>(row) * source.pixels.rowPitch,
                source.pixels.rowPitch);
        }
        upload->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
        sourceLocation.pResource = upload.Get();
        sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        sourceLocation.PlacedFootprint = footprint;
        mCommandList->CopyTextureRegion(
            &destination, 0, 0, 0, &sourceLocation, nullptr);

        D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(
            texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mCommandList->ResourceBarrier(1, &barrier);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = textureDescription.Format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = 1;
        srv.Texture2D.ResourceMinLODClamp = 0.0f;
        mDevice->CreateShaderResourceView(texture.Get(), &srv, srvHandle);
        srvHandle.ptr += mSrvDescriptorSize;

        mTextures.push_back(std::move(texture));
        uploadBuffers.push_back(std::move(upload));
    }

    ThrowIfFailed(mCommandList->Close(), "Close texture upload command list");
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, commandLists);
    WaitForGPU();
}

void DX12Renderer::BuildConstantBuffers()
{
    const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC singleBuffer = BufferDescription(ConstantBufferAlignment);

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &singleBuffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mObjectCB)), "Create object constant buffer");
    ThrowIfFailed(mObjectCB->Map(0, nullptr, reinterpret_cast<void**>(&mObjectCBMapped)),
        "Map object constant buffer");

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &singleBuffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mLightCB)), "Create light constant buffer");
    ThrowIfFailed(mLightCB->Map(0, nullptr, reinterpret_cast<void**>(&mLightCBMapped)),
        "Map light constant buffer");

    D3D12_RESOURCE_DESC materialBuffer = BufferDescription(
        std::max<size_t>(1, mModel.materials.size()) * ConstantBufferAlignment);
    ThrowIfFailed(mDevice->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &materialBuffer,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&mMaterialCB)), "Create material constant buffer");
    ThrowIfFailed(mMaterialCB->Map(0, nullptr, reinterpret_cast<void**>(&mMaterialCBMapped)),
        "Map material constant buffer");
}

void DX12Renderer::Render()
{
    ThrowIfFailed(mCommandAllocator->Reset(), "Reset frame command allocator");
    ThrowIfFailed(mCommandList->Reset(mCommandAllocator.Get(), mPipelineState.Get()),
        "Reset frame command list");

    D3D12_RESOURCE_BARRIER toRenderTarget = TransitionBarrier(
        mRenderTargets[mCurrentBackBuffer].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &toRenderTarget);

    mCommandList->RSSetViewports(1, &mViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(mCurrentBackBuffer) * mRtvDescriptorSize;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

    const FLOAT clearColor[] = { 0.025f, 0.035f, 0.055f, 1.0f };
    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(1, heaps);
    mCommandList->SetGraphicsRootConstantBufferView(
        0, mObjectCB->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(
        1, mLightCB->GetGPUVirtualAddress());

    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetIndexBuffer(&mIndexBufferView);

    const D3D12_GPU_DESCRIPTOR_HANDLE heapStart =
        mSrvHeap->GetGPUDescriptorHandleForHeapStart();
    for (const ObjSubset& subset : mModel.subsets)
    {
        if (subset.materialIndex >= mModel.materials.size())
            continue;

        const ObjMaterial& material = mModel.materials[subset.materialIndex];
        mCommandList->SetGraphicsRootConstantBufferView(
            2, mMaterialCB->GetGPUVirtualAddress() +
            static_cast<UINT64>(subset.materialIndex) * ConstantBufferAlignment);

        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = heapStart;
        textureHandle.ptr += static_cast<UINT64>(material.textureSrvIndex) * mSrvDescriptorSize;
        mCommandList->SetGraphicsRootDescriptorTable(3, textureHandle);
        mCommandList->DrawIndexedInstanced(
            subset.indexCount, 1, subset.startIndex, 0, 0);
    }

    D3D12_RESOURCE_BARRIER toPresent = TransitionBarrier(
        mRenderTargets[mCurrentBackBuffer].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &toPresent);

    ThrowIfFailed(mCommandList->Close(), "Close frame command list");
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, commandLists);
    ThrowIfFailed(mSwapChain->Present(1, 0), "Present");

    WaitForGPU();
    mCurrentBackBuffer = mSwapChain->GetCurrentBackBufferIndex();
}

void DX12Renderer::WaitForGPU()
{
    const UINT64 value = ++mFenceValue;
    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), value), "Signal fence");
    if (mFence->GetCompletedValue() < value)
    {
        ThrowIfFailed(mFence->SetEventOnCompletion(value, mFenceEvent),
            "Set fence event");
        WaitForSingleObject(mFenceEvent, INFINITE);
    }
}
