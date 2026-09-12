#pragma once

#include "gbuffer.h"
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

struct MaterialConstants
{
    DirectX::XMFLOAT4 diffuseColor;
    DirectX::XMFLOAT2 uvScale;
    DirectX::XMFLOAT2 uvOffset;
};

enum class LightType : uint32_t
{
    Directional = 0,
    Point = 1,
    Spot = 2
};

struct SceneLight
{
    LightType type = LightType::Point;
    DirectX::XMFLOAT3 position{};
    float range = 1.0f;
    DirectX::XMFLOAT3 direction{ 0.0f, -1.0f, 0.0f };
    float spotInnerCos = 0.9f;
    DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float spotOuterCos = 0.75f;
};

struct LightingConstants
{
    DirectX::XMFLOAT3 lightPosition;
    float lightRange;
    DirectX::XMFLOAT3 lightDirection;
    float spotInnerCos;
    DirectX::XMFLOAT3 lightColor;
    float intensity;
    DirectX::XMFLOAT3 cameraPosition;
    float spotOuterCos;
    uint32_t lightType;
    float ambientStrength;
    DirectX::XMFLOAT2 padding{};
};

static_assert(sizeof(LightingConstants) <= 256);

class RenderingSystem
{
public:
    RenderingSystem();
    virtual ~RenderingSystem();

    bool Initialize(HWND hwnd, int width, int height);
    void Update();
    void Render();

protected:
    static constexpr UINT FrameCount = 2;
    static constexpr UINT ConstantBufferAlignment = 256;

private:
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
    GBuffer mGBuffer;

    ComPtr<ID3D12DescriptorHeap> mBackBufferRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mMaterialSrvHeap;
    UINT mRtvDescriptorSize = 0;
    UINT mSrvDescriptorSize = 0;
    UINT mCurrentBackBuffer = 0;

    ComPtr<ID3D12Fence> mFence;
    UINT64 mFenceValue = 0;
    HANDLE mFenceEvent = nullptr;

    ComPtr<ID3D12RootSignature> mGeometryRootSignature;
    ComPtr<ID3D12RootSignature> mLightingRootSignature;
    ComPtr<ID3D12PipelineState> mGeometryPipelineState;
    ComPtr<ID3D12PipelineState> mLightingPipelineState;

    ComPtr<ID3D12Resource> mVertexBuffer;
    ComPtr<ID3D12Resource> mIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView{};
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView{};

    ObjModel mModel;
    std::vector<ComPtr<ID3D12Resource>> mTextures;
    std::vector<SceneLight> mLights;

    ComPtr<ID3D12Resource> mObjectCB;
    ComPtr<ID3D12Resource> mMaterialCB;
    ComPtr<ID3D12Resource> mLightingCB;
    ObjectConstants* mObjectCBMapped = nullptr;
    uint8_t* mMaterialCBMapped = nullptr;
    uint8_t* mLightingCBMapped = nullptr;

    DirectX::XMMATRIX mWorld;
    DirectX::XMMATRIX mView;
    DirectX::XMMATRIX mProjection;
    DirectX::XMFLOAT3 mCameraPosition{ 0.0f, 1.7f, -7.0f };

    DirectX::XMFLOAT2 mTextureTiling{ 1.0f, 1.0f };
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

    void BuildRootSignatures();
    void BuildPipelineStateObjects();
    void BuildObj(const std::string& path);
    void BuildTextureResources();
    void BuildConstantBuffers();
    void BuildLights(float seconds);

    void RenderGeometryPass(D3D12_CPU_DESCRIPTOR_HANDLE dsv);
    void RenderLightingPass(D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv);
    void WaitForGPU();
};
