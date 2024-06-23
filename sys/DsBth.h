#pragma once

#define BTHPS3_SIXAXIS_HID_INPUT_REPORT_SIZE        0x32
#define BTHPS3_SIXAXIS_HID_OUTPUT_REPORT_SIZE       0x32

#define FILE_DEVICE_BUSENUM             FILE_DEVICE_BUS_EXTENDER
#define BUSENUM_IOCTL(_index_)          CTL_CODE(FILE_DEVICE_BUSENUM, _index_, METHOD_BUFFERED, FILE_READ_DATA)
#define BUSENUM_W_IOCTL(_index_)        CTL_CODE(FILE_DEVICE_BUSENUM, _index_, METHOD_BUFFERED, FILE_WRITE_DATA)
#define BUSENUM_R_IOCTL(_index_)        CTL_CODE(FILE_DEVICE_BUSENUM, _index_, METHOD_BUFFERED, FILE_READ_DATA)
#define BUSENUM_RW_IOCTL(_index_)       CTL_CODE(FILE_DEVICE_BUSENUM, _index_, METHOD_BUFFERED, FILE_WRITE_DATA | FILE_READ_DATA)

#define IOCTL_BTHPS3_BASE 0x801

/**************************************************************/
/* I/O control codes for function-to-bus-driver communication */
/**************************************************************/

// 
// Read from control channel
// 
#define IOCTL_BTHPS3_HID_CONTROL_READ           BUSENUM_R_IOCTL (IOCTL_BTHPS3_BASE + 0x200)

// 
// Write to control channel
// 
#define IOCTL_BTHPS3_HID_CONTROL_WRITE          BUSENUM_W_IOCTL (IOCTL_BTHPS3_BASE + 0x201)

// 
// Read from interrupt channel
// 
#define IOCTL_BTHPS3_HID_INTERRUPT_READ         BUSENUM_R_IOCTL (IOCTL_BTHPS3_BASE + 0x202)

// 
// Write to interrupt channel
// 
#define IOCTL_BTHPS3_HID_INTERRUPT_WRITE        BUSENUM_W_IOCTL (IOCTL_BTHPS3_BASE + 0x203)


EVT_WDF_REQUEST_COMPLETION_ROUTINE DsBth_HidControlWriteRequestCompletionRoutine;

NTSTATUS DsBth_SendDisconnectRequest(PDEVICE_CONTEXT Context);

EVT_WDF_TIMER DsBth_EvtControlReadTimerFunc;
EVT_WDF_TIMER DsBth_EvtStartupDelayTimerFunc;
EVT_WDF_TIMER DsBth_EvtPostStartupTimerFunc;

VOID CALLBACK
DsBth_DisconnectEventCallback(
	_In_ PVOID   lpParameter,
	_In_ BOOLEAN TimerOrWaitFired
);

EVT_DMF_ContinuousRequestTarget_BufferOutput DsBth_HidInterruptReadContinuousRequestCompleted;

EVT_DMF_ContinuousRequestTarget_BufferInput DsBth_HidControlWriteContinuousRequestCompleted;

NTSTATUS
DsBth_SelfManagedIoInit(
	WDFDEVICE Device
);

NTSTATUS
DsBth_SelfManagedIoSuspend(
	WDFDEVICE Device
);

NTSTATUS
DsBth_D0Entry(
	WDFDEVICE Device,
	WDF_POWER_DEVICE_STATE PreviousState
);
