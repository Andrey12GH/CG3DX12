#include <Windows.h>
#include "dx12app.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pCmdLine, int nCmdShow)
{
    DX12App app(hInstance);

    if (!app.Initialize(800, 600))
    {
        MessageBox(nullptr, L"Failed to initialize DX12 app", L"Error", MB_OK);
        return -1;
    }

    return app.Run();
}
