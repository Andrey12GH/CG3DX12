#pragma once

#include "rendering_system.h"

// The application-facing name is kept for compatibility with DX12App.
// All rendering work is implemented by the homework's RenderingSystem class.
class DX12Renderer final : public RenderingSystem
{
public:
    DX12Renderer() = default;
    ~DX12Renderer() = default;
};
