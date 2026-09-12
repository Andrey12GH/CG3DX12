#include "gbuffer.h"

#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace
{
    D3D12_HEAP_PROPERTIES DefaultHeapProperties()
    {
        D3D12_HEAP_PROPERTIES properties{};
        properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        properties.CreationNodeMask = 1;
        properties.VisibleNodeMask = 1;
        return properties;
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
}

DXGI_FORMAT GBuffer::GetFormat(Attachment attachment)
{
    switch (attachment)
    {
    case AlbedoSpecular:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Normal:
    case WorldPosition:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

void GBuffer::Initialize(ID3D12Device* device, UINT width, UINT height)
{
    if (!device || width == 0 || height == 0)
        throw std::invalid_argument("GBuffer requires a valid device and non-zero dimensions");

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
    rtvHeapDescription.NumDescriptors = AttachmentCount;
    rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(device->CreateDescriptorHeap(
        &rtvHeapDescription, IID_PPV_ARGS(&mRtvHeap))))
    {
        throw std::runtime_error("Failed to create the GBuffer RTV heap");
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescription{};
    srvHeapDescription.NumDescriptors = AttachmentCount;
    srvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(
        &srvHeapDescription, IID_PPV_ARGS(&mSrvHeap))))
    {
        throw std::runtime_error("Failed to create the GBuffer SRV heap");
    }

    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        mSrvHeap->GetCPUDescriptorHandleForHeapStart();

    const D3D12_HEAP_PROPERTIES heapProperties = DefaultHeapProperties();
    for (UINT index = 0; index < AttachmentCount; ++index)
    {
        const Attachment attachment = static_cast<Attachment>(index);
        const DXGI_FORMAT format = GetFormat(attachment);

        D3D12_RESOURCE_DESC textureDescription{};
        textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDescription.Width = width;
        textureDescription.Height = height;
        textureDescription.DepthOrArraySize = 1;
        textureDescription.MipLevels = 1;
        textureDescription.Format = format;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        textureDescription.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = format;

        if (FAILED(device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &textureDescription,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&mTextures[index]))))
        {
            throw std::runtime_error("Failed to create a GBuffer attachment");
        }

        device->CreateRenderTargetView(
            mTextures[index].Get(), nullptr, rtvHandle);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
        srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDescription.Format = format;
        srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDescription.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(
            mTextures[index].Get(), &srvDescription, srvHandle);

        rtvHandle.ptr += mRtvDescriptorSize;
        srvHandle.ptr += srvDescriptorSize;
    }

    mShaderReadable = false;
}

void GBuffer::BeginGeometryPass(
    ID3D12GraphicsCommandList* commandList,
    D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView)
{
    if (mShaderReadable)
    {
        std::array<D3D12_RESOURCE_BARRIER, AttachmentCount> barriers{};
        for (UINT index = 0; index < AttachmentCount; ++index)
        {
            barriers[index] = TransitionBarrier(
                mTextures[index].Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        commandList->ResourceBarrier(AttachmentCount, barriers.data());
        mShaderReadable = false;
    }

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, AttachmentCount> renderTargetViews{};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        mRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < AttachmentCount; ++index)
    {
        renderTargetViews[index] = rtvHandle;
        const FLOAT clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
        commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        rtvHandle.ptr += mRtvDescriptorSize;
    }

    commandList->ClearDepthStencilView(
        depthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->OMSetRenderTargets(
        AttachmentCount, renderTargetViews.data(), FALSE, &depthStencilView);
}

void GBuffer::EndGeometryPass(ID3D12GraphicsCommandList* commandList)
{
    std::array<D3D12_RESOURCE_BARRIER, AttachmentCount> barriers{};
    for (UINT index = 0; index < AttachmentCount; ++index)
    {
        barriers[index] = TransitionBarrier(
            mTextures[index].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    commandList->ResourceBarrier(AttachmentCount, barriers.data());
    mShaderReadable = true;
}

ID3D12DescriptorHeap* GBuffer::GetShaderVisibleHeap() const
{
    return mSrvHeap.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE GBuffer::GetGpuSrvStart() const
{
    return mSrvHeap->GetGPUDescriptorHandleForHeapStart();
}
