#include "Driver.h"
#include "Driver.tmh"


#pragma code_seg("INIT")
NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
)
{
	WDF_DRIVER_CONFIG config;
	WDF_OBJECT_ATTRIBUTES attributes;

	//
	// Initialize WPP Tracing
	//
	WPP_INIT_TRACING(DriverObject, RegistryPath);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

	//
	// Register a cleanup callback so that we can call WPP_CLEANUP when
	// the framework driver object is deleted during driver unload.
	//
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DSHM_DRIVER_CONTEXT);
	attributes.EvtCleanupCallback = dshidminiEvtDriverContextCleanup;

	WDF_DRIVER_CONFIG_INIT(
		&config,
		dshidminiEvtDeviceAdd
	);

	WDFDRIVER driver = NULL;

	NTSTATUS status = WdfDriverCreate(
		DriverObject,
		RegistryPath,
		&attributes,
		&config,
		&driver
	);

	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed with status %!STATUS!", status);
		WPP_CLEANUP(DriverObject);
		return status;
	}

	if (!NT_SUCCESS(status = InitIPC()))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "InitIPC failed with status %!STATUS!", status);
		WPP_CLEANUP(DriverObject);
		return status;
	}

	const PDSHM_DRIVER_CONTEXT context = DriverGetContext(driver);

	if (!NT_SUCCESS(status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &context->SlotsLock)))
	{
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfWaitLockCreate failed with status %!STATUS!", status);
		WPP_CLEANUP(DriverObject);
		return status;
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

	return status;
}
#pragma code_seg()

#pragma code_seg("PAGED")
void dshidminiEvtDriverContextCleanup(WDFOBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);

	DestroyIPC();

	WPP_CLEANUP(WdfDriverWdmGetDriverObject( (WDFDRIVER) DriverObject));
}
#pragma code_seg()

//
// DllMain initializes and disposes ETW
// 
BOOL APIENTRY DllMain(
	HMODULE hModule,
	DWORD ul_reason_for_call,
	LPVOID lpReserved
)
{
	UNREFERENCED_PARAMETER(lpReserved);

	DisableThreadLibraryCalls(hModule);

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		EventRegisterNefarius_DsHidMini_Driver();
		break;
	case DLL_PROCESS_DETACH:
		EventUnregisterNefarius_DsHidMini_Driver();
		break;
	}

	return TRUE;
}
