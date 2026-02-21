#include "dx12app.h"
#include "dx12renderer.h"
#include "wc.h"

#include <Windows.h>
#include <memory>

DX12App::DX12App(HINSTANCE hInstance)
    : mHInstance(hInstance), mHwnd(nullptr), mWidth(800), mHeight(600)
{
}

DX12App::~DX12App()
{
}

bool DX12App::InitWindow(int width, int height)
{
    WindowClass win(mHInstance, WindowProc);
    win.CreateWinEx();
    win.ShowWin();

    mHwnd = win.GetHWND();
    return mHwnd != nullptr;
}

bool DX12App::Initialize(int width, int height)
{
    if (!InitWindow(width, height))
        return false;

    mRenderer = std::make_unique<DX12Renderer>();

    return mRenderer->Initialize(mHwnd, width, height);
}

void DX12App::Update()
{
    if (mRenderer)
        mRenderer->Update();
}

void DX12App::Render()
{
    if (mRenderer)
        mRenderer->Render();
}

int DX12App::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            Update();
            Render();
        }
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK DX12App::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}
