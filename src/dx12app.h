#pragma once
#include <Windows.h>
#include <memory>

class DX12Renderer;

class DX12App
{
public:
    DX12App(HINSTANCE hInstance);
    ~DX12App();

    bool Initialize(int width, int height);
    int Run();

private:
    bool InitWindow(int width, int height);
    void Update();
    void Render();

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE mHInstance;
    HWND mHwnd;

    int mWidth;
    int mHeight;

    std::unique_ptr<DX12Renderer> mRenderer;
};
