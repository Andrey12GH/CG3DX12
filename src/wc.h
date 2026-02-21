#pragma once
#include <Windows.h>

class WindowClass {
private:
	WNDCLASS wc;
	HWND hWnd;
	HINSTANCE hInstance;
public:
	WindowClass(HINSTANCE hInstance_, WNDPROC winProc_);
	void CreateWinEx();
	void ShowWin();
	HWND GetHWND();
};