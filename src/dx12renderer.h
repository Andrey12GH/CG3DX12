#pragma once
#include <Windows.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <string>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
    XMFLOAT3 normal;
};

struct alignas(256) ObjectConstants
{
    XMMATRIX world;
    XMMATRIX view;
    XMMATRIX projection;
};

struct alignas(256) LightConstants
{
    XMFLOAT3 lightDir;
    float padding;
    XMFLOAT4 ambientColor;
    XMFLOAT4 diffuseColor;
};

class DX12Renderer
{
public:
    DX12Renderer();
    ~DX12Renderer();

    bool Initialize(HWND hwnd, int width, int height);
    void Render();
    void Update();
private:
    // ===== DX12 core =====
    ComPtr<ID3D12Device> mDevice;
    ComPtr<IDXGIFactory4> mFactory;

    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;

    ComPtr<IDXGISwapChain3> mSwapChain;
    static const UINT FrameCount = 2;

    ComPtr<ID3D12Resource> mRenderTargets[FrameCount];
    ComPtr<ID3D12Resource> mDepthStencil;

    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;

    UINT mRtvDescriptorSize;
    UINT mCurrentBackBuffer;

    ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue;
    HANDLE mFenceEvent;

    ComPtr<ID3D12RootSignature> mRootSignature;
    ComPtr<ID3D12PipelineState> mPipelineState;

    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Resource> mIndexBuffer;

    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView;
    D3D12_INDEX_BUFFER_VIEW  mIndexBufferView;

    UINT mIndexCount;

    ComPtr<ID3D12Resource> mObjectCB;
    ComPtr<ID3D12Resource> mLightCB;

    ObjectConstants* mObjectCBMapped;
    LightConstants* mLightCBMapped;

    XMMATRIX mWorld;
    XMMATRIX mView;
    XMMATRIX mProjection;

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;

    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain(HWND hwnd);
    void CreateDescriptorHeaps();
    void CreateRenderTargets();
    void CreateDepthStencil();
    void CreateFence();

    void BuildRootSignature();
    void BuildShadersAndPSO();
    void BuildCubeGeometry();
    void BuildObj(const std::string& path);
    void BuildConstantBuffers();

    void WaitForGPU();
};
