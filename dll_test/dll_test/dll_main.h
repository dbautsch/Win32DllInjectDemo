/*!
*	Copyright(c) 2015 Dawid Bautsch, dawid.bautsch [ at ] gmail.com
*
*	Permission is hereby granted, free of charge, to any person obtaining a copy
*	of this software and associated documentation files(the "Software"), to deal
*	in the Software without restriction, including without limitation the rights
*	to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
*	copies of the Software, and to permit persons to whom the Software is
*	furnished to do so, subject to the following conditions :
*
*	The above copyright notice and this permission notice shall be included in
*	all copies or substantial portions of the Software.
*
*	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
*	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
*	THE SOFTWARE.
*/

#ifndef DLL_MAIN_H_INCLUDED
#define DLL_MAIN_H_INCLUDED

#include <windows.h>
#include <string>
#include <vector>

class WindowWatcher
{
private:
	HANDLE						hStopCtrlEvent;

	HANDLE						hFinishedEvent;

	bool						bError;

	WNDPROC						OriginalWindowProc;

	uintptr_t					uptrThread;

	wchar_t					*	pwszItemCaption;

public:
								WindowWatcher();

								~WindowWatcher();

	bool						Error();

	int							GetCurrentProcessIndexFromHWND(std::vector < HWND > & windowsList);

	void						SignalFinishedEvent();

	void						AddUnloadingMenuItem(HMENU h, HWND hWnd);

	void						RemoveUnloadingMenuItem(HMENU h, HWND hWnd);


	static LRESULT CALLBACK		WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	static unsigned __stdcall	WorkingThreadProc(void * pv);

	static BOOL CALLBACK		EnumWindowsProc(HWND hwnd, LPARAM lParam);

	static std::wstring			ErrorCodeString(DWORD dwErr);
};

#endif
