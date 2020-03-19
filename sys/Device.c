
#include "driver.h"
#include "device.tmh"
#include <DmfModule.h>
#include <devpkey.h>
#include <ini.h>


EVT_DMF_DEVICE_MODULES_ADD DmfDeviceModulesAdd;


NTSTATUS
dshidminiCreateDevice(
	_Inout_ PWDFDEVICE_INIT DeviceInit
)
{
	WDF_OBJECT_ATTRIBUTES deviceAttributes;
	WDFDEVICE device;
	NTSTATUS status;
	NTSTATUS ntStatus;
	PDMFDEVICE_INIT dmfDeviceInit;
	DMF_EVENT_CALLBACKS dmfCallbacks;
	WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
	PDEVICE_CONTEXT pDevCtx;
	WCHAR enumeratorName[200];
	ULONG bufSize;
	WDF_DEVICE_PROPERTY_DATA devProp;
	DEVPROPTYPE propType;

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

	dmfDeviceInit = DMF_DmfDeviceInitAllocate(DeviceInit);

	WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
	pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = DsHidMini_EvtWdfDeviceSelfManagedIoInit;
	pnpPowerCallbacks.EvtDevicePrepareHardware = DsHidMini_EvtDevicePrepareHardware;
	pnpPowerCallbacks.EvtDeviceD0Entry = DsHidMini_EvtDeviceD0Entry;
	pnpPowerCallbacks.EvtDeviceD0Exit = DsHidMini_EvtDeviceD0Exit;

	// All DMF drivers must call this function even if they do not support PnP Power callbacks.
	// (In this case, this driver does support a PnP Power callback.)
	//
	DMF_DmfDeviceInitHookPnpPowerEventCallbacks(dmfDeviceInit,
		&pnpPowerCallbacks);

	// All DMF drivers must call this function even if they do not support File Object callbacks.
	//
	DMF_DmfDeviceInitHookFileObjectConfig(dmfDeviceInit,
		NULL);

	// All DMF drivers must call this function even if they do not support Power Policy callbacks.
	//
	DMF_DmfDeviceInitHookPowerPolicyEventCallbacks(dmfDeviceInit,
		NULL);

	// This is a filter driver that loads on MSHIDUMDF driver.
	//
	WdfFdoInitSetFilter(DeviceInit);
	// DMF Client drivers that are filter drivers must also make this call.
	//
	DMF_DmfFdoSetFilter(dmfDeviceInit);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

	status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

	if (NT_SUCCESS(status)) {

		status = WdfDeviceQueryProperty(
			device,
			DevicePropertyEnumeratorName,
			ARRAYSIZE(enumeratorName),
			(PVOID)enumeratorName,
			&bufSize
		);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfDeviceQueryProperty failed with status %!STATUS!",
				status
			);
			goto Exit;
		}

		pDevCtx = DeviceGetContext(device);

		if (wcscmp(L"USB", enumeratorName) == 0)
		{
			pDevCtx->ConnectionType = DsHidMiniDeviceConnectionTypeUsb;
		}
		else
		{
			pDevCtx->ConnectionType = DsHidMiniDeviceConnectionTypeBth;
			pDevCtx->Connection.Bth.BthIoTarget = WdfDeviceGetIoTarget(device);

			status = DsHidMini_BthConnectionContextInit(device);

			if (!NT_SUCCESS(status))
			{
				TraceEvents(TRACE_LEVEL_ERROR,
					TRACE_DEVICE,
					"WdfDeviceQueryProperty failed with status %!STATUS!",
					status
				);
				goto Exit;
			}
		}

		WDF_DEVICE_PROPERTY_DATA_INIT(&devProp, &DEVPKEY_Device_InstanceId);
		WDF_OBJECT_ATTRIBUTES_INIT(&deviceAttributes);
		deviceAttributes.ParentObject = device;
		
		status = WdfDeviceAllocAndQueryPropertyEx(
			device,
			&devProp,
			NonPagedPoolNx,
			&deviceAttributes,
			&pDevCtx->InstanceId,
			&propType
		);
		if (!NT_SUCCESS(status))
		{
			TraceEvents(TRACE_LEVEL_ERROR,
				TRACE_DEVICE,
				"WdfDeviceAllocAndQueryPropertyEx failed with status %!STATUS!",
				status
			);
			goto Exit;
		}

		TraceEvents(TRACE_LEVEL_INFORMATION,
			TRACE_DEVICE,
			"!! DEVPKEY_Device_InstanceId: %ws",
			WdfMemoryGetBuffer(pDevCtx->InstanceId, NULL)
		);

		// Create the DMF Modules this Client driver will use.
		//
		dmfCallbacks.EvtDmfDeviceModulesAdd = DmfDeviceModulesAdd;
		DMF_DmfDeviceInitSetEventCallbacks(dmfDeviceInit,
			&dmfCallbacks);

		ntStatus = DMF_ModulesCreate(device,
			&dmfDeviceInit);
		if (!NT_SUCCESS(ntStatus))
		{
			goto Exit;
		}
	}

Exit:

	if (dmfDeviceInit != NULL)
	{
		DMF_DmfDeviceInitFree(&dmfDeviceInit);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

	return status;
}

NTSTATUS DsHidMini_BthConnectionContextInit(
	WDFDEVICE Device
)
{
	NTSTATUS				status = STATUS_SUCCESS;
	PDEVICE_CONTEXT			pDeviceContext;
	WDF_OBJECT_ATTRIBUTES	attribs;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

	pDeviceContext = DeviceGetContext(Device);

	WDF_OBJECT_ATTRIBUTES_INIT(&attribs);
	attribs.ParentObject = Device;

	status = WdfRequestCreate(
		&attribs,
		pDeviceContext->Connection.Bth.BthIoTarget,
		&pDeviceContext->Connection.Bth.HidInterruptReadRequest
	);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DEVICE,
			"WdfRequestCreate failed with status %!STATUS!",
			status
		);
	}

	WDF_OBJECT_ATTRIBUTES_INIT(&attribs);
	attribs.ParentObject = pDeviceContext->Connection.Bth.HidInterruptReadRequest;

	status = WdfMemoryCreate(
		&attribs,
		NonPagedPool,
		DS3_POOL_TAG,
		BTHPS3_SIXAXIS_HID_INPUT_REPORT_SIZE,
		&pDeviceContext->Connection.Bth.HidInterruptReadMemory,
		NULL
	);
	if (!NT_SUCCESS(status))
	{
		TraceEvents(TRACE_LEVEL_ERROR,
			TRACE_DEVICE,
			"WdfMemoryCreate failed with status %!STATUS!",
			status
		);
	}

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

	return status;
}

#pragma code_seg("PAGED")
_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
DmfDeviceModulesAdd(
	_In_ WDFDEVICE Device,
	_In_ PDMFMODULE_INIT DmfModuleInit
)
{
	PDEVICE_CONTEXT deviceContext;
	DMF_MODULE_ATTRIBUTES moduleAttributes;
	DMF_CONFIG_DsHidMini dsHidMiniCfg;

	PAGED_CODE();

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

	deviceContext = DeviceGetContext(Device);

	DMF_CONFIG_DsHidMini_AND_ATTRIBUTES_INIT(
		&dsHidMiniCfg,
		&moduleAttributes
	);

	DMF_DmfModuleAdd(DmfModuleInit,
		&moduleAttributes,
		WDF_NO_OBJECT_ATTRIBUTES,
		&deviceContext->DsHidMiniModule);

	TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}
#pragma code_seg()


