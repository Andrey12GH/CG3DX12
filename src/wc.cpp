#include "wc.h"

WindowClass::WindowClass(HINSTANCE hInstance_, WNDPROC winProc_) : hInstance(hInstance_) {
	wc.lpfnWndProc = winProc_;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"MyWindowClass";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

	RegisterClass(&wc);
};

void WindowClass::CreateWinEx() {
	hWnd = CreateWindowEx(
		WS_EX_APPWINDOW,
		L"MyWindowClass",
		L"MyWindowName",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);
};

void WindowClass::ShowWin() {
	ShowWindow(hWnd, SW_SHOW);
};

HWND WindowClass::GetHWND() {
	return hWnd;
};