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

#include "main.h"

#include <sstream>

using namespace std;

#define INJECTED_DLL_PATH_NAME	string("X:\\Projects\\Inject\\BIN\\dll_test.dll")	//<! must be changed, unless you have such directory
#define INJECTED_DLL_NAME		L"dll_test.dll"

int APIENTRY WinMain (HINSTANCE hThisInstance,
					  HINSTANCE hPrevInstance,
					  LPSTR lpCmdLine,
					  int nShowCmd)
{
	Injector i;

	//	try to injcect code into EXE
	if (i.Inject() == false)
	{
		MessageBox(0, L"EXE : Could not find any instances of notepad.exe Please start one.", L"", 0);
		return -1;
	}

	return 0;
}

Injector::Injector()
{

}

Injector::~Injector()
{

}

bool Injector::FindProcess(const wstring & wstr, PROCESSENTRY32 * pPE32)
{
	/*!
	*	Find a process with given image name.
	*	\param pPE32 Process information (if success).
	*/

	HANDLE hSnapshot		= CreateToolhelp32Snapshot(TH32CS_SNAPALL, 0);
	bool bFound				= false;

	if (hSnapshot == INVALID_HANDLE_VALUE)
		return false;

	PROCESSENTRY32 pe32;
	memset(&pe32, 0, sizeof(PROCESSENTRY32));
	pe32.dwSize				= sizeof(PROCESSENTRY32);

	if (Process32First(hSnapshot, &pe32))
	{
		if (wstring(pe32.szExeFile) == wstr)
		{
			memcpy(pPE32, &pe32, sizeof(PROCESSENTRY32));
			bFound			= true;
		}
		else
		{
			while (Process32Next(hSnapshot, &pe32))
			{
				if (wstring(pe32.szExeFile) == wstr)
				{
					memcpy(pPE32, &pe32, sizeof(PROCESSENTRY32));
					bFound	= true;
					break;
				}
			}
		}
	}
	else
	{
		OutputDebugStringWSTR(wstring(L"Process32First() failed. Code = ") + ErrorCodeString(GetLastError()));
	}

	CloseHandle(hSnapshot);

	return bFound;
}

bool Injector::Inject()
{
	/*!
	*	Inject dll into given exe.
	*	Returns true on success.
	*/

	PROCESSENTRY32 pe32;

	//	find selected process
	if (FindProcess(L"notepad.exe", &pe32) == false)
	{
		return false;
	}

	//	open process
	HANDLE hProcess	= OpenProcess(GENERIC_ALL, FALSE, pe32.th32ProcessID);

	if (hProcess == NULL)
	{
		OutputDebugStringWSTR(wstring(L"Could not open selected process. Code = ") + ErrorCodeString(GetLastError()));
		return false;
	}

	//	allocate memory buffer
	void * pBuf1	= VirtualAllocEx(hProcess, 0, sizeof(char) * (INJECTED_DLL_PATH_NAME.length() + 1), MEM_COMMIT, PAGE_EXECUTE_READWRITE);

	if (pBuf1 == NULL)
	{
		OutputDebugStringWSTR(wstring(L"Could not allocate memory in the process. Code = ") + ErrorCodeString(GetLastError()));
		CloseHandle(hProcess);

		return false;
	}

	size_t tWritten;

	//	write name of the DLL into EXE process memory, so that it can execute LoadLibrary with success
	if (WriteProcessMemory(hProcess, pBuf1, (void*)INJECTED_DLL_PATH_NAME.c_str(), sizeof(char) * (INJECTED_DLL_PATH_NAME.length() + 1), &tWritten) == FALSE)
	{
		OutputDebugStringWSTR(wstring(L"Could not write to buffer in the process. Code = ") + ErrorCodeString(GetLastError()));

		VirtualFreeEx(hProcess, pBuf1, sizeof(char*) * (INJECTED_DLL_PATH_NAME.length() + 1), MEM_DECOMMIT);
		CloseHandle(hProcess);

		return false;
	}

	HMODULE hKernel32		= LoadLibrary(L"kernel32.dll");
	FARPROC LoadLibraryPtr	= NULL;
	FARPROC FreeLibraryPtr	= NULL;

	if (hKernel32 != NULL)
	{
		//	get the function pointers for LoadLibrary and FreeLibrary
		LoadLibraryPtr		= GetProcAddress(hKernel32, "LoadLibraryA");
		FreeLibraryPtr		= GetProcAddress(hKernel32, "FreeLibrary");
	}

	if (hKernel32 == NULL || LoadLibraryPtr == NULL || FreeLibraryPtr == NULL)
	{
		OutputDebugStringWSTR(wstring(L"Could not create remote thread in the selected process. Code = ") + ErrorCodeString(GetLastError()));

		VirtualFreeEx(hProcess, pBuf1, sizeof(char*) * (INJECTED_DLL_PATH_NAME.length() + 1), MEM_DECOMMIT);
		CloseHandle(hProcess);

		return false;
	}

	//	create control events (named)
	HANDLE hStopCtrlEvent	= CreateEvent(NULL, TRUE, FALSE, L"hStopCtrlEvent");
	HANDLE hFinishedEvent	= CreateEvent(NULL, FALSE, FALSE, L"hFinishedEvent");

	//	execute LoadLibrary with use of CreateRemoteThread in the remote process
	HANDLE hNewThread	= CreateRemoteThread(hProcess,
											 NULL,
											 0,
											 (LPTHREAD_START_ROUTINE) LoadLibraryPtr,
											 pBuf1,
											 0,
											 0);

	if (hNewThread == NULL)
	{
		OutputDebugStringWSTR(wstring(L"Could not create remothe thread in the selected process. Code = ") + ErrorCodeString(GetLastError()));

		VirtualFreeEx(hProcess, pBuf1, sizeof(char*) * (INJECTED_DLL_PATH_NAME.length() + 1), MEM_DECOMMIT);
		CloseHandle(hProcess);
		FreeLibrary(hKernel32);

		return false;
	}

	//	waiting for LoadLibrary to finish
	WaitForSingleObject(hNewThread, INFINITE);

	//	waiting for injected code "LoadLibrary" code to finish (signalled through an event)
	WaitForSingleObject(hFinishedEvent, INFINITE);

	MessageBox(0, L"EXE : DLL has finished.", L"", 0);

	//	wait until the new thread has finished
	ResetEvent(hFinishedEvent);
	WaitForSingleObject(hFinishedEvent, INFINITE);

	CloseHandle(hNewThread);

	VirtualFreeEx(hProcess, pBuf1, sizeof(char*) * (INJECTED_DLL_PATH_NAME.length() + 1), MEM_DECOMMIT);

	UnloadDLL(hProcess, FreeLibraryPtr);

	CloseHandle(hStopCtrlEvent);
	CloseHandle(hFinishedEvent);

	FreeLibrary(hKernel32);

	MessageBox(0, L"EXE : DLL has been unloaded.", L"", 0);

	return true;
}

wstring Injector::ErrorCodeString(DWORD dwErr)
{
	wstringstream wstr;

	wstr << dwErr;

	return wstr.str();
}

void Injector::OutputDebugStringWSTR(const wstring & wstr)
{
	//	debugging purposes only

	OutputDebugString(wstr.c_str());
}

void Injector::UnloadDLL(HANDLE hProcess, FARPROC FreeLibraryPtr)
{
	/*!
	*	Unload DLL from remote process. The problem is on x64 systems you can't get return value from
	*	remote LoadLibrary call, so you can't just unload in an easy way (GetExitCodeProcess returns DWORD,
	*	so the return value would be truncated).
	*
	*	The solution : iterate over all loaded modules into remote process, compare them by name,
	*	and unload our DLL.
	*/

	HMODULE hMod		= NULL;

	if (FindDLLModule(hProcess, &hMod) == false)
	{
		//	the process is probably gone - user has closed it
		return;
	}

	//	free library
	HANDLE hNewThread	= CreateRemoteThread(hProcess,
											 NULL,
											 0,
											 (LPTHREAD_START_ROUTINE) FreeLibraryPtr,
											 (void*) hMod,
											 0,
											 NULL);

	WaitForSingleObject(hNewThread, INFINITE);
	CloseHandle(hNewThread);
}

bool Injector::FindDLLModule(HANDLE hProcess, HMODULE * phm)
{
	/*!
	*	Find DLL module in the remote process.
	*	\param hProcess Handle of the remote process.
	*	\param phm Hmodule handle of the DLL loaded into remote process
	*	(or NULL if not found).
	*/

	*phm				= NULL;
	DWORD dwProcessID	= GetProcessId(hProcess);
	HANDLE hSnap		= CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, dwProcessID);

	if (hSnap != NULL)
	{
		MODULEENTRY32 me32;

		memset(&me32, 0, sizeof(MODULEENTRY32));

		me32.dwSize		= sizeof(MODULEENTRY32);

		if (Module32First(hSnap, &me32))
		{
			if (_wcsicmp(me32.szModule, INJECTED_DLL_NAME) == 0)
			{
				CloseHandle(hSnap);
				*phm		= me32.hModule;

				return true;
			}

			while (Module32Next(hSnap, &me32))
			{
				if (_wcsicmp(me32.szModule, INJECTED_DLL_NAME) == 0)
				{
					CloseHandle(hSnap);
					*phm = me32.hModule;

					return true;
				}
			}
		}

		CloseHandle(hSnap);
	}

	return false;
}
