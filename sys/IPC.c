#include "Driver.h"
#include "IPC.tmh"


NTSTATUS InitIPC(void)
{
	const WDFDRIVER driver = WdfGetDriver();
	const PDSHM_DRIVER_CONTEXT context = DriverGetContext(driver);

	SECURITY_DESCRIPTOR sd = { 0 };

	if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
	{
		TraceError(
			TRACE_IPC,
			"InitializeSecurityDescriptor failed with error: %lu\n", GetLastError());
		return NTSTATUS_FROM_WIN32(GetLastError());
	}

	SECURITY_ATTRIBUTES sa = { 0 };
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = &sd;

	CHAR* szSD = "D:" // Discretionary ACL
	"(D;OICI;GA;;;BG)" // Deny access to Built-in Guests
	"(D;OICI;GA;;;AN)" // Deny access to Anonymous Logon
	"(A;OICI;GRGWGX;;;AU)" // Allow read/write/execute to Authenticated Users
	"(A;OICI;GA;;;BA)"; // Allow full control to Administrators

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
		szSD,
		SDDL_REVISION_1,
		&(sa.lpSecurityDescriptor),
		NULL
	))
	{
		TraceError(
			TRACE_IPC,
			"ConvertStringSecurityDescriptorToSecurityDescriptor failed with error: %lu\n", GetLastError());
		return NTSTATUS_FROM_WIN32(GetLastError());
	}

	// Create a named event for signaling
	HANDLE hReadEvent = CreateEventA(&sa, FALSE, FALSE, DSHM_IPC_READ_EVENT_NAME);
	if (hReadEvent == NULL)
	{
		TraceError(
			TRACE_IPC,
			"Could not create event (%!WINERROR!).",
			GetLastError()
		);
		return NTSTATUS_FROM_WIN32(GetLastError());
	}

	HANDLE hWriteEvent = CreateEventA(&sa, FALSE, FALSE, DSHM_IPC_WRITE_EVENT_NAME);
	if (hWriteEvent == NULL)
	{
		TraceError(
			TRACE_IPC,
			"Could not create event (%!WINERROR!).",
			GetLastError()
		);
		return NTSTATUS_FROM_WIN32(GetLastError());
	}

	// Create a memory-mapped file
	HANDLE hMapFile = CreateFileMappingA(
		INVALID_HANDLE_VALUE, // use paging file
		&sa, // default security
		PAGE_READWRITE, // read/write access
		0, // maximum object size (high-order DWORD)
		DSHM_IPC_BUFFER_SIZE, // maximum object size (low-order DWORD)
		DSHM_IPC_FILE_MAP_NAME // name of mapping object
	);

	if (hMapFile == NULL)
	{
		TraceError(
			TRACE_IPC,
			"Could not create file mapping object (%!WINERROR!).",
			GetLastError()
		);
		return NTSTATUS_FROM_WIN32(GetLastError());
	}

	// Map a view of the file in the calling process's address space
	PUCHAR pBuf = MapViewOfFile(
		hMapFile, // handle to map object
		FILE_MAP_ALL_ACCESS, // read/write permission
		0,
		0,
		DSHM_IPC_BUFFER_SIZE
	);

	if (pBuf == NULL)
	{
		TraceError(
			TRACE_IPC,
			"Could not map view of file (%!WINERROR!).", GetLastError()
		);
		CloseHandle(hMapFile);
		return NTSTATUS_FROM_WIN32(GetLastError());
	}

	context->IPC.MapFile = hMapFile;
	context->IPC.ReadEvent = hReadEvent;
	context->IPC.WriteEvent = hWriteEvent;
	context->IPC.SharedMemory = (PUCHAR)pBuf;

	return STATUS_SUCCESS;
}

void DestroyIPC(void)
{
	const WDFDRIVER driver = WdfGetDriver();
	const PDSHM_DRIVER_CONTEXT context = DriverGetContext(driver);

	if (context->IPC.SharedMemory)
		UnmapViewOfFile(context->IPC.SharedMemory);

	if (context->IPC.MapFile)
		CloseHandle(context->IPC.MapFile);

	if (context->IPC.ReadEvent)
		CloseHandle(context->IPC.ReadEvent);

	if (context->IPC.WriteEvent)
		CloseHandle(context->IPC.WriteEvent);
}
