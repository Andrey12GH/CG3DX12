#pragma once

#include <d3d12.h>
#include <wrl.h>

#include <array>

class GBuffer
{
public:
    enum Attachment : UINT
    {
        AlbedoSpecular = 0,
        Normal = 1,
        WorldPosition = 2,
        AttachmentCount = 3
    };

    void Initialize(ID3D12Device* device, UINT width, UINT height);

    void BeginGeometryPass(
        ID3D12GraphicsCommandList* commandList,
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView);
    void EndGeometryPass(ID3D12GraphicsCommandList* commandList);

    ID3D12DescriptorHeap* GetShaderVisibleHeap() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvStart() const;

    static DXGI_FORMAT GetFormat(Attachment attachment);

private:
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, AttachmentCount> mTextures;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    UINT mRtvDescriptorSize = 0;
    bool mShaderReadable = false;
};
