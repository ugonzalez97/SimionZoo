#include "Process.h"
#include <Windows.h>
#include <memory>

CProcess::CProcess()
{
	m_handle = INVALID_HANDLE_VALUE;
}

CProcess::~CProcess()
{
	if (m_handle != INVALID_HANDLE_VALUE)
	{
		TerminateProcess(m_handle, 0);
		CloseHandle(m_handle);
	}
}

bool CProcess::spawn(const char* commandLine)
{
	STARTUPINFO startupInfo;
	PROCESS_INFORMATION processInformation;

	memset(&startupInfo, 0, sizeof(STARTUPINFO));
	startupInfo.wShowWindow = true;

	bool bSuccess = ( TRUE== CreateProcess(NULL,(char*) commandLine, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL
		, &startupInfo, &processInformation));
	if (bSuccess)
		m_handle = processInformation.hProcess;

	return bSuccess;
}
