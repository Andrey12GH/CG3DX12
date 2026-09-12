#pragma once

#include "parcer.h"

#include <Windows.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct ObjectConstants
{
    DirectX::XMMATRIX world;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX projection;
};

struct LightConstants
{
    DirectX::XMFLOAT3 lightDir;
    float padding = 0.0f;
    DirectX::XMFLOAT4 ambientColor;
    DirectX::XMFLOAT4 diffuseColor;
};

struct MaterialConstants
{
    DirectX::XMFLOAT4 diffuseColor;
    DirectX::XMFLOAT2 uvScale;
    DirectX::XMFLOAT2 uvOffset;
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
    static constexpr UINT FrameCount = 2;
    static constexpr UINT ConstantBufferAlignment = 256;

    int mWidth = 0;
    int mHeight = 0;

    ComPtr<ID3D12Device> mDevice;
    ComPtr<IDXGIFactory4> mFactory;
    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;

    ComPtr<IDXGISwapChain3> mSwapChain;
    ComPtr<ID3D12Resource> mRenderTargets[FrameCount];
    ComPtr<ID3D12Resource> mDepthStencil;

    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    UINT mRtvDescriptorSize = 0;
    UINT mSrvDescriptorSize = 0;
    UINT mCurrentBackBuffer = 0;

    ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue = 0;
    HANDLE mFenceEvent = nullptr;

    ComPtr<ID3D12RootSignature> mRootSignature;
    ComPtr<ID3D12PipelineState> mPipelineState;

    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Resource> mIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView{};

    ObjModel mModel;
    std::vector<ComPtr<ID3D12Resource>> mTextures;

    ComPtr<ID3D12Resource> mObjectCB;
    ComPtr<ID3D12Resource> mLightCB;
    ComPtr<ID3D12Resource> mMaterialCB;
    ObjectConstants* mObjectCBMapped = nullptr;
    LightConstants* mLightCBMapped = nullptr;
    uint8_t* mMaterialCBMapped = nullptr;

    DirectX::XMMATRIX mWorld;
    DirectX::XMMATRIX mView;
    DirectX::XMMATRIX mProjection;

    DirectX::XMFLOAT2 mTextureTiling{ 4.0f, 4.0f };
    DirectX::XMFLOAT2 mTextureScrollSpeed{ 0.08f, 0.035f };
    DirectX::XMFLOAT2 mTextureOffset{};
    std::chrono::steady_clock::time_point mStartTime;

    D3D12_VIEWPORT mViewport{};
    D3D12_RECT mScissorRect{};

    void CreateDevice();
    void CreateCommandObjects();
    void CreateSwapChain(HWND hwnd);
    void CreateDescriptorHeaps();
    void CreateRenderTargets();
    void CreateDepthStencil();
    void CreateFence();

    void BuildRootSignature();
    void BuildShadersAndPSO();
    void BuildObj(const std::string& path);
    void BuildTextureResources();
    void BuildConstantBuffers();

    void WaitForGPU();
};
