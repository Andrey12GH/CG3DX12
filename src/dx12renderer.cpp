#include "dx12renderer.h"
#include "shader.h"

#include <d3dcompiler.h>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

DX12Renderer::DX12Renderer()
    : mFenceValue(0),
    mCurrentBackBuffer(0),
    mObjectCBMapped(nullptr),
    mLightCBMapped(nullptr)
{
}

DX12Renderer::~DX12Renderer()
{
    WaitForGPU();
    CloseHandle(mFenceEvent);
}

bool DX12Renderer::Initialize(HWND hwnd, int width, int height)
{
    CreateDevice();
    CreateCommandObjects();
    CreateSwapChain(hwnd);
    CreateDescriptorHeaps();
    CreateRenderTargets();
    CreateDepthStencil();
    CreateFence();

    BuildRootSignature();
    BuildShadersAndPSO();
    //BuildCubeGeometry();
    BuildObj("sponza.obj");
    BuildConstantBuffers();

    mViewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
    mScissorRect = { 0, 0, width, height };

    mWorld = XMMatrixIdentity();
    mView = XMMatrixLookAtLH(
        XMVectorSet(0, 2, -5, 1),
        XMVectorZero(),
        XMVectorSet(0, 1, 0, 0)
    );
    mProjection = XMMatrixPerspectiveFovLH(
        XM_PIDIV4,
        (float)width / height,
        0.1f,
        100.0f
    );

    return true;
}

void DX12Renderer::Update()
{
    static float angle = 0.0f;
    angle += 0.01f;
    mWorld = XMMatrixRotationY(angle);

    ObjectConstants obj;
    obj.world = XMMatrixTranspose(mWorld);
    obj.view = XMMatrixTranspose(mView);
    obj.projection = XMMatrixTranspose(mProjection);
    *mObjectCBMapped = obj;

    LightConstants light;
    light.lightDir = XMFLOAT3(0.5f, -1.0f, 0.5f);
    light.ambientColor = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
    light.diffuseColor = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
    *mLightCBMapped = light;
}


void DX12Renderer::CreateDevice()
{
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    debug->EnableDebugLayer();
#endif

    CreateDXGIFactory1(IID_PPV_ARGS(&mFactory));

    ComPtr<IDXGIAdapter1> adapter;
    mFactory->EnumAdapters1(0, &adapter);

    D3D12CreateDevice(
        adapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&mDevice)
    );
}

void DX12Renderer::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    mDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue));
    mDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&mCommandAllocator)
    );

    mDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mCommandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&mCommandList)
    );

    mCommandList->Close();
}

void DX12Renderer::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount = FrameCount;
    desc.Width = 0;
    desc.Height = 0;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    mFactory->CreateSwapChainForHwnd(
        mCommandQueue.Get(),
        hwnd,
        &desc,
        nullptr,
        nullptr,
        &swapChain
    );

    swapChain.As(&mSwapChain);
    mCurrentBackBuffer = mSwapChain->GetCurrentBackBufferIndex();
}

void DX12Renderer::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = FrameCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    mDevice->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&mRtvHeap));
    mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV
    );

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

    mDevice->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&mDsvHeap));
}

void DX12Renderer::CreateRenderTargets()
{
    auto handle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < FrameCount; i++)
    {
        mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mRenderTargets[i]));
        mDevice->CreateRenderTargetView(
            mRenderTargets[i].Get(),
            nullptr,
            handle
        );
        handle.ptr += mRtvDescriptorSize;
    }
}

void DX12Renderer::CreateDepthStencil()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 800;
    desc.Height = 600;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;


    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;


    mDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&mDepthStencil)
    );

    mDevice->CreateDepthStencilView(
        mDepthStencil.Get(),
        nullptr,
        mDsvHeap->GetCPUDescriptorHandleForHeapStart()
    );
}

void DX12Renderer::CreateFence()
{
    mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence));
    mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void DX12Renderer::Render()
{
    mCommandAllocator->Reset();
    mCommandList->Reset(mCommandAllocator.Get(), mPipelineState.Get());

    mCommandList->RSSetViewports(1, &mViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    auto rtv = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += mCurrentBackBuffer * mRtvDescriptorSize;

    auto dsv = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

    FLOAT clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        dsv,
        D3D12_CLEAR_FLAG_DEPTH,
        1.0f,
        0,
        0,
        nullptr
    );

    mCommandList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());


    ObjectConstants obj;
    obj.world = XMMatrixTranspose(mWorld);
    obj.view = XMMatrixTranspose(mView);
    obj.projection = XMMatrixTranspose(mProjection);
    *mObjectCBMapped = obj;

    LightConstants light;
    light.lightDir = XMFLOAT3(0.5f, -1.0f, 0.5f);
    light.ambientColor = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
    light.diffuseColor = XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f);
    *mLightCBMapped = light;

    mCommandList->SetGraphicsRootConstantBufferView(0, mObjectCB->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(1, mLightCB->GetGPUVirtualAddress());



    mCommandList->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
    );
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetIndexBuffer(&mIndexBufferView);

    mCommandList->DrawIndexedInstanced(mIndexCount, 1, 0, 0, 0);

    mCommandList->Close();
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(1, lists);

    mSwapChain->Present(1, 0);
    WaitForGPU();
    mCurrentBackBuffer = mSwapChain->GetCurrentBackBufferIndex();
}

void DX12Renderer::WaitForGPU()
{
    mFenceValue++;
    mCommandQueue->Signal(mFence.Get(), mFenceValue);

    if (mFence->GetCompletedValue() < mFenceValue)
    {
        mFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
        WaitForSingleObject(mFenceEvent, INFINITE);
    }
}

void DX12Renderer::BuildRootSignature()
{
    D3D12_ROOT_PARAMETER params[2] = {};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> error;

    D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized,
        &error
    );

    mDevice->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)
    );
}

void DX12Renderer::BuildShadersAndPSO()
{
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;

    D3DCompile(
        Shaders::VertexShader.c_str(),
        Shaders::VertexShader.size(),
        nullptr, nullptr, nullptr,
        "main", "vs_5_0",
        0, 0, &vs, nullptr
    );

    D3DCompile(
        Shaders::PixelShader.c_str(),
        Shaders::PixelShader.size(),
        nullptr, nullptr, nullptr,
        "main", "ps_5_0",
        0, 0, &ps, nullptr
    );

    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { layout, _countof(layout) };
    pso.pRootSignature = mRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    pso.SampleMask = UINT_MAX;


    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer.FrontCounterClockwise = FALSE;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blend = {};
    blend.AlphaToCoverageEnable = FALSE;
    blend.IndependentBlendEnable = FALSE;

    D3D12_RENDER_TARGET_BLEND_DESC rtBlend = {};
    rtBlend.BlendEnable = FALSE;
    rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    blend.RenderTarget[0] = rtBlend;

    D3D12_DEPTH_STENCIL_DESC depth = {};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depth.StencilEnable = FALSE;

    pso.RasterizerState = rasterizer;
    pso.BlendState = blend;
    pso.DepthStencilState = depth;

    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    mDevice->CreateGraphicsPipelineState(
        &pso,
        IID_PPV_ARGS(&mPipelineState)
    );
}

void DX12Renderer::BuildCubeGeometry()
{
    Vertex vertices[] =
    {
        {{-1,-1,-1},{1,0,0,1},{0,0,-1}},
        {{-1, 1,-1},{0,1,0,1},{0,0,-1}},
        {{ 1, 1,-1},{0,0,1,1},{0,0,-1}},
        {{ 1,-1,-1},{1,1,0,1},{0,0,-1}},

        {{-1,-1,1},{1,0,1,1},{0,0,1}},
        {{ 1,-1,1},{0,1,1,1},{0,0,1}},
        {{ 1, 1,1},{1,1,1,1},{0,0,1}},
        {{-1, 1,1},{1,0,1,1},{0,0,1}},

        {{-1,-1,1},{1,0,0,1},{-1,0,0}},
        {{-1, 1,1},{0,1,0,1},{-1,0,0}},
        {{-1, 1,-1},{0,0,1,1},{-1,0,0}},
        {{-1,-1,-1},{1,1,0,1},{-1,0,0}},

        {{ 1,-1,-1},{1,0,0,1},{1,0,0}},
        {{ 1, 1,-1},{0,1,0,1},{1,0,0}},
        {{ 1, 1,1},{0,0,1,1},{1,0,0}},
        {{ 1,-1,1},{1,1,0,1},{1,0,0}},

        {{-1,1,-1},{1,0,0,1},{0,1,0}},
        {{-1,1,1},{0,1,0,1},{0,1,0}},
        {{ 1,1,1},{0,0,1,1},{0,1,0}},
        {{ 1,1,-1},{1,1,0,1},{0,1,0}},

        {{-1,-1,1},{1,0,0,1},{0,-1,0}},
        {{-1,-1,-1},{0,1,0,1},{0,-1,0}},
        {{ 1,-1,-1},{0,0,1,1},{0,-1,0}},
        {{ 1,-1,1},{1,1,0,1},{0,-1,0}},
    };

    uint16_t indices[] =
    {
        0,1,2, 0,2,3,
        4,5,6, 4,6,7,
        8,9,10, 8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23
    };

    mIndexCount = _countof(indices);

    UINT vbSize = sizeof(vertices);
    UINT ibSize = sizeof(indices);

    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buf = {};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = vbSize;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    mDevice->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &buf, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mVertexBuffer)
    );

    void* data;
    mVertexBuffer->Map(0, nullptr, &data);
    memcpy(data, vertices, vbSize);
    mVertexBuffer->Unmap(0, nullptr);

    mVertexBufferView = { mVertexBuffer->GetGPUVirtualAddress(), vbSize, sizeof(Vertex) };

    buf.Width = ibSize;

    mDevice->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &buf, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mIndexBuffer)
    );

    mIndexBuffer->Map(0, nullptr, &data);
    memcpy(data, indices, ibSize);
    mIndexBuffer->Unmap(0, nullptr);

    mIndexBufferView = { mIndexBuffer->GetGPUVirtualAddress(), ibSize, DXGI_FORMAT_R16_UINT };

}

#include "parcer.h"

void DX12Renderer::BuildObj(const std::string& path)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    if (!LoadOBJ(path, vertices, indices))
        throw std::runtime_error("Failed to load OBJ");

    mIndexCount = (UINT)indices.size();

    UINT vbSize = sizeof(Vertex) * (UINT)vertices.size();
    UINT ibSize = sizeof(uint32_t) * (UINT)indices.size();

    // === HEAP ===
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    // === VERTEX BUFFER ===
    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    mDevice->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mVertexBuffer)
    );

    void* data = nullptr;
    mVertexBuffer->Map(0, nullptr, &data);
    memcpy(data, vertices.data(), vbSize);
    mVertexBuffer->Unmap(0, nullptr);

    mVertexBufferView.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVertexBufferView.SizeInBytes = vbSize;
    mVertexBufferView.StrideInBytes = sizeof(Vertex);

    // === INDEX BUFFER ===
    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = ibSize;

    mDevice->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&mIndexBuffer)
    );

    mIndexBuffer->Map(0, nullptr, &data);
    memcpy(data, indices.data(), ibSize);
    mIndexBuffer->Unmap(0, nullptr);

    mIndexBufferView.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    mIndexBufferView.SizeInBytes = ibSize;
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
}


struct ObjectCB
{
    XMMATRIX world;
    XMMATRIX view;
    XMMATRIX proj;
};

struct LightCB
{
    XMFLOAT3 lightDir;
    float pad;
    XMFLOAT4 ambient;
    XMFLOAT4 diffuse;
};

void DX12Renderer::BuildConstantBuffers()
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buf = {};
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Width = 256;
    buf.Height = 1;
    buf.DepthOrArraySize = 1;
    buf.MipLevels = 1;
    buf.SampleDesc.Count = 1;
    buf.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    mDevice->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &buf, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mObjectCB)
    );

    mObjectCB->Map(0, nullptr, (void**)&mObjectCBMapped);

    mDevice->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE,
        &buf, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&mLightCB)
    );

    mLightCB->Map(0, nullptr, (void**)&mLightCBMapped);
}
