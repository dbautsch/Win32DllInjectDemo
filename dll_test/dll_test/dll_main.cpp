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

#include "dll_main.h"

#include <process.h>
#include <algorithm>
#include <sstream>

using namespace std;

#define UNLOAD_MENU_ID			0x666

WindowWatcher * pWatcher	= NULL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL,
					DWORD     fdwReason,
					LPVOID    lpvReserved)
{
	switch (fdwReason)
	{
		case DLL_PROCESS_ATTACH:	//	DLL has been attached to process, initialize Watcher class
		{
			pWatcher		= new WindowWatcher();

			if (pWatcher->Error())
			{
				MessageBox(0, L"DLL : Could not create an instance of Watcher class..", L"", 0);
				delete pWatcher;
				pWatcher	= NULL;
			}
			else
			{
				MessageBox(0, L"DLL : Injected correctly.", L"", 0);
			}

			pWatcher->SignalFinishedEvent();

			break;
		}

		case DLL_PROCESS_DETACH:	//	DLL has been detached from remote process, send a notification to injector process
		{
			if (pWatcher != NULL)
			{
				pWatcher->SignalFinishedEvent();
				delete pWatcher;
				pWatcher	= NULL;
			}
			break;
		}
	}

	return TRUE;
}

WindowWatcher::WindowWatcher()
{
	bError			= false;
	pwszItemCaption = NULL;

	hStopCtrlEvent	= OpenEvent(EVENT_ALL_ACCESS, FALSE, L"hStopCtrlEvent");

	if (hStopCtrlEvent == NULL)
	{
		bError		= true;
		MessageBox(0, L"DLL : Could not open controlling event.", L"Error", 0);
		return;
	}

	hFinishedEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, L"hFinishedEvent");

	if (hFinishedEvent == NULL)
	{
		bError		= true;
		MessageBox(0, L"DLL : Could not open finish signalling event.", L"Error", 0);
		return;
	}

	uptrThread		= _beginthreadex(NULL,
									 0,
									 WorkingThreadProc,
									 (void*) this,
									 0,
									 NULL);
											  
	if (uptrThread <= 0)
	{
		bError		= true;
		MessageBox(0, L"DLL : Could not create working thread.", L"Error", 0);
		return;
	}
}

WindowWatcher::~WindowWatcher()
{
	SetEvent(hFinishedEvent);

	if (hStopCtrlEvent)
		CloseHandle(hStopCtrlEvent);

	if (hFinishedEvent)
		CloseHandle(hFinishedEvent);
}

LRESULT CALLBACK WindowWatcher::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	/*!
	*	This is a custom window proc of remote process. It is used to control (unload) DLL (with
	*	use of main menu user can easly disable and unload DLL).
	*/

	if (pWatcher == NULL)
		return DefWindowProc(hWnd, uMsg, wParam, lParam);

	bool bProcessMessage		= true;

	if (uMsg == WM_COMMAND)
	{
		DWORD dwId				= LOWORD(wParam);

		if (dwId == 2)
		{
			bProcessMessage		= false;
			MessageBox(hWnd, L"This function has been disabled !", L"Hahahha", MB_ICONWARNING);
		}
		else if (dwId == UNLOAD_MENU_ID)
		{
			//	remove injected code
			SetEvent(pWatcher->hStopCtrlEvent);
			return 0;
		}
	}

	if (pWatcher->OriginalWindowProc != NULL && bProcessMessage)
	{
		return CallWindowProc(pWatcher->OriginalWindowProc, hWnd, uMsg, wParam, lParam);
	}
	else
	{
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
}

bool WindowWatcher::Error()
{
	return bError;
}

unsigned __stdcall WindowWatcher::WorkingThreadProc(void * pv)
{
	/*!
	*	This is a worker thread function. Here is the initialization code of
	*	injected DLL, replacing windowproc and such.
	*/

	WindowWatcher * pWW				= (WindowWatcher*) pv;

	if (pWW == NULL)
	{
		//	should never happen
		_endthreadex(0);
		return 0;
	}

	vector < HWND > windowsList;

	//	find main window
	if (EnumWindows(EnumWindowsProc, (LPARAM)&windowsList) == FALSE)
	{
		pWW->bError					= true;
		SetEvent(pWW->hFinishedEvent);

		_endthreadex(0);
		return 0;
	}

	if (windowsList.size() == 0)
	{
		//	window class not found
		pWW->bError					= true;
		SetEvent(pWW->hFinishedEvent);

		_endthreadex(0);
		return 0;
	}
	
	int iIDX						= pWW->GetCurrentProcessIndexFromHWND(windowsList);

	if (iIDX == -1)
	{
		pWW->bError					= true;
		SetEvent(pWW->hFinishedEvent);

		_endthreadex(0);
		return 0;
	}

	//	add menu item for unloading code
	HMENU hm						= GetMenu(windowsList[iIDX]);
	pWW->AddUnloadingMenuItem(hm, windowsList[iIDX]);

	//	save original and replace window proc
	pWW->OriginalWindowProc			= (WNDPROC) GetWindowLongPtr(windowsList[iIDX], -4);

	if (pWW->OriginalWindowProc == NULL)
	{
		//	could not get original window proc
		pWW->bError					= true;
		SetEvent(pWW->hFinishedEvent);

		_endthreadex(0);
		return 0;
	}

	//	replace window proc
	if (SetWindowLongPtr(windowsList[iIDX], -4, (LONG_PTR) pWW->WindowProc) == 0)
	{
		pWW->bError					= true;
		SetEvent(pWW->hFinishedEvent);

		_endthreadex(0);
		return 0;
	}

	WaitForSingleObjectEx(pWW->hStopCtrlEvent, INFINITE, TRUE);

	//	remove incjected code, restore original window proc

	pWW->RemoveUnloadingMenuItem(hm, windowsList[iIDX]);
	SetWindowLongPtr(windowsList[iIDX], -4, (LONG_PTR) pWW->OriginalWindowProc);

	//	notify caller so that he can remove library from the process
	SetEvent(pWW->hFinishedEvent);

	_endthreadex(0);
	return 0;
}

BOOL CALLBACK WindowWatcher::EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	wchar_t wszText[1024];
	wstring wstrClassName;
	vector < HWND > * pWindowsList	= (vector < HWND > *) lParam;

	if (GetClassName(hwnd, wszText, 1024) > 0)
	{
		wstrClassName				= wszText;
		transform(wstrClassName.begin(), wstrClassName.end(), wstrClassName.begin(), tolower);

		if (wstrClassName == wstring(L"notepad"))
		{
			pWindowsList->push_back(hwnd);
		}
	}

	return TRUE;
}

int	WindowWatcher::GetCurrentProcessIndexFromHWND(vector < HWND > & windowsList)
{
	DWORD dwCurrentID		= GetCurrentProcessId();

	for (unsigned i = 0; i < windowsList.size(); ++i)
	{
		DWORD dwProcID;

		if (GetWindowThreadProcessId(windowsList[i], &dwProcID))
		{
			if (dwCurrentID == dwProcID)
			{
				return (int) i;
			}	
		}
	}

	return -1;
}

wstring WindowWatcher::ErrorCodeString(DWORD dwErr)
{
	wstringstream wstr;

	wstr << dwErr;

	return wstr.str();
}

void WindowWatcher::SignalFinishedEvent()
{
	SetEvent(hFinishedEvent);
}

void WindowWatcher::AddUnloadingMenuItem(HMENU h, HWND hWnd)
{
	MENUITEMINFO mi;

	memset(&mi, 0, sizeof(MENUITEMINFO));

	pwszItemCaption	= new wchar_t[64];
	wcscpy_s(pwszItemCaption, 64, L"Unload injected DLL!");

	mi.cbSize		= sizeof(MENUITEMINFO);
	mi.fMask		= MIIM_STRING | MIIM_FTYPE | MIIM_ID;
	mi.fType		= MFT_STRING;
	mi.wID			= UNLOAD_MENU_ID;
	mi.dwTypeData	= pwszItemCaption;
	mi.cch			= (UINT) wcslen(pwszItemCaption);

	if (InsertMenuItem(h, UNLOAD_MENU_ID, FALSE, &mi) == FALSE)
	{
		MessageBox(0, L"DLL : Could not insert menu item!", L"", 0);
	}
	else
	{
		DrawMenuBar(hWnd);
	}
}

void WindowWatcher::RemoveUnloadingMenuItem(HMENU h, HWND hWnd)
{
	if (pwszItemCaption != NULL)
	{
		RemoveMenu(h, UNLOAD_MENU_ID, MF_BYCOMMAND);
		delete [] pwszItemCaption;
		pwszItemCaption	= NULL;

		DrawMenuBar(hWnd);
	}
}
