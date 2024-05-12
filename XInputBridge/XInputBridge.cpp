// XInputBridge.cpp : Defines the exported functions for the DLL.
//

#include "framework.h"
#include "XInputBridge.h"
#include "UniUtil.h"


//
// Dead-Zone value to stop jittering
// 
#define DS3_AXIS_ANTI_JITTER_OFFSET		10

#define DS3_VID							0x054C
#define DS3_PID							0x0268
#define SXS_MODE_GET_FEATURE_REPORT_ID	0xF2
#define SXS_MODE_GET_FEATURE_BUFFER_LEN	0x40
#define DS3_DEVICES_MAX					8
#define LOGGER_NAME						"XInputBridge"
#define XI_SYSTEM_LIB_NAME				"XInput1_3.dll"


static constexpr uint8_t K_INVALID_X_INPUT_USER_ID = 0xff; // XUSER_INDEX_ANY


//
// Type of the device behind a give user index
// 
enum XI_DEVICE_TYPE
{
	XI_DEVICE_TYPE_NOT_CONNECTED = 0,
	XI_DEVICE_TYPE_DS3,
	XI_DEVICE_TYPE_XUSB,
};

//
// Device state information of each user index to improve performance
// 
struct XI_DEVICE_STATE
{
	bool isInitialized = false;

	bool isConnected = false;

	hid_device* oldDeviceHandle = nullptr;

	DWORD PacketNumber;

	DS3_RAW_INPUT_REPORT LastReport;

	CRITICAL_SECTION lock;

	//
	// Path of backing DS3 or XUSB device
	// 
	std::string SymbolicLink;

	std::atomic<XI_DEVICE_TYPE> Type = XI_DEVICE_TYPE_NOT_CONNECTED;

	union
	{
		hid_device* DeviceHandle = nullptr;
		DWORD UserIndex;
	} Backend;

	void SetOnlineAsXusb(const std::string& Symlink, DWORD UserIndex)
	{
		SymbolicLink = Symlink;
		Backend.UserIndex = UserIndex;
		Type = XI_DEVICE_TYPE_XUSB;
	}
};

//
// Keep track on device states for better lookup performance
// 
static std::vector<XI_DEVICE_STATE> G_DEVICE_STATES(DS3_DEVICES_MAX);

static HCMNOTIFICATION G_DS3_NOTIFICATION_HANDLE = nullptr;
static HCMNOTIFICATION G_XUSB_NOTIFICATION_HANDLE = nullptr;

#define NAMEOF(name) #name
#define CALL_FPN_SAFE(fpn, ...)	((fpn)) ? (fpn)(__VA_ARGS__) : ERROR_DEVICE_NOT_CONNECTED
#define CALL_FPN_SAFE_NO_RETURN(fpn, ...)	((fpn)) ? (fpn)(__VA_ARGS__) : void(0)

//
// Applies to both Xbox 360 and Xbox One controllers
// 
// {EC87F1E3-C13B-4100-B5F7-8B84D54260CB}
DEFINE_GUID(XUSB_INTERFACE_CLASS_GUID,
	0xEC87F1E3, 0xC13B, 0x4100, 0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB);

static decltype(XInputGetState)* G_fpnXInputGetState = nullptr;
static decltype(XInputSetState)* G_fpnXInputSetState = nullptr;
static decltype(XInputGetCapabilities)* G_fpnXInputGetCapabilities = nullptr;
static decltype(XInputEnable)* G_fpnXInputEnable = nullptr;
static decltype(XInputGetDSoundAudioDeviceGuids)* G_fpnXInputGetDSoundAudioDeviceGuids = nullptr;
static decltype(XInputGetBatteryInformation)* G_fpnXInputGetBatteryInformation = nullptr;
static decltype(XInputGetKeystroke)* G_fpnXInputGetKeystroke = nullptr;
static decltype(XInputGetStateEx)* G_fpnXInputGetStateEx = nullptr;
static decltype(XInputWaitForGuideButton)* G_fpnXInputWaitForGuideButton = nullptr;
static decltype(XInputCancelGuideButtonWait)* G_fpnXInputCancelGuideButtonWait = nullptr;
static decltype(XInputPowerOffController)* G_fpnXInputPowerOffController = nullptr;


//
// https://github.com/DJm00n/RawInputDemo/blob/master/RawInputLib/RawInputDeviceHid.cpp#L275-L339
// 
static bool XUSB_SymlinkToUserIndex(_In_ PCWSTR Symlink, _Inout_ PDWORD UserIndex)
{
	constexpr DWORD desiredAccess = (GENERIC_WRITE | GENERIC_READ);
	constexpr DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

	HANDLE handle = CreateFileW(
		Symlink,
		desiredAccess,
		shareMode,
		nullptr,
		OPEN_EXISTING,
		0,
		nullptr
	);

	const auto guard = sg::make_scope_guard([handle]() noexcept
	{
		if (handle != INVALID_HANDLE_VALUE)
			CloseHandle(handle);
	});

	std::array<uint8_t, 3> gamepadStateRequest0101{ 0x01, 0x01, 0x00 };
	std::array<uint8_t, 3> ledStateData{};
	DWORD len = 0;

	// https://github.com/nefarius/XInputHooker/issues/1
	// https://gist.github.com/mmozeiko/b8ccc54037a5eaf35432396feabbe435
	constexpr DWORD IOCTL_XUSB_GET_LED_STATE = 0x8000E008;

	if (!DeviceIoControl(handle,
		IOCTL_XUSB_GET_LED_STATE,
		gamepadStateRequest0101.data(),
		gamepadStateRequest0101.size(),
		ledStateData.data(),
		ledStateData.size(),
		&len,
		nullptr
	))
	{
		// GetLastError()
		return false;
	}

	// https://www.partsnotincluded.com/xbox-360-controller-led-animations-info/
	// https://github.com/paroj/xpad/blob/5978d1020344c3288701ef70ea9a54dfc3312733/xpad.c#L1382-L1402
	constexpr uint8_t XINPUT_LED_TO_PORT_MAP[] =
	{
		K_INVALID_X_INPUT_USER_ID, // All off
		K_INVALID_X_INPUT_USER_ID, // All blinking, then previous setting
		0, // 1 flashes, then on
		1, // 2 flashes, then on
		2, // 3 flashes, then on
		3, // 4 flashes, then on
		0, // 1 on
		1, // 2 on
		2, // 3 on
		3, // 4 on
		K_INVALID_X_INPUT_USER_ID, // Rotate
		K_INVALID_X_INPUT_USER_ID, // Blink, based on previous setting
		K_INVALID_X_INPUT_USER_ID, // Slow blink, based on previous setting
		K_INVALID_X_INPUT_USER_ID, // Rotate with two lights
		K_INVALID_X_INPUT_USER_ID, // Persistent slow all blink
		K_INVALID_X_INPUT_USER_ID, // Blink once, then previous setting
	};

	const uint8_t ledState = ledStateData[2];

	*UserIndex = XINPUT_LED_TO_PORT_MAP[ledState];

	return true;
}

//
// Gets called when a device of interest got attached or removed
// 
static DWORD CALLBACK DeviceNotificationCallback(
	_In_ HCMNOTIFICATION hNotify,
	_In_opt_ PVOID Context,
	_In_ CM_NOTIFY_ACTION Action,
	_In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
	_In_ DWORD EventDataSize
)
{
	UNREFERENCED_PARAMETER(hNotify);
	UNREFERENCED_PARAMETER(Context);
	UNREFERENCED_PARAMETER(EventDataSize);

	const std::shared_ptr<spdlog::logger> logger = spdlog::get(LOGGER_NAME)->clone(__FUNCTION__);

	switch (Action)
	{
	case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
		if (IsEqualGUID(GUID_DEVINTERFACE_DSHIDMINI, EventData->u.DeviceInterface.ClassGuid))
		{
			logger->info("New DS3 device arrived: {}", ConvertWideToANSI(EventData->u.DeviceInterface.SymbolicLink));
		}

		if (IsEqualGUID(XUSB_INTERFACE_CLASS_GUID, EventData->u.DeviceInterface.ClassGuid))
		{
			const auto symlink = ConvertWideToANSI(EventData->u.DeviceInterface.SymbolicLink);

			logger->info("New XUSB device arrived: {}", symlink);

			DWORD userIndex = K_INVALID_X_INPUT_USER_ID;
			if (XUSB_SymlinkToUserIndex(EventData->u.DeviceInterface.SymbolicLink, &userIndex))
			{
				logger->info("User index: {}", userIndex);

				const auto item = std::ranges::find_if(G_DEVICE_STATES, [](const XI_DEVICE_STATE& element)
				{
					return element.Type == XI_DEVICE_TYPE_NOT_CONNECTED;
				});

				if (item != G_DEVICE_STATES.end())
				{
					item->SetOnlineAsXusb(symlink, userIndex);
				}
				else
				{
					logger->warn("No free slot for {}", symlink);
				}
			}
			else
			{
				logger->error("User index lookup failed");
			}
		}
		break;
	case CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL:
		if (IsEqualGUID(GUID_DEVINTERFACE_DSHIDMINI, EventData->u.DeviceInterface.ClassGuid))
		{
			const std::string symlink = ConvertWideToANSI(EventData->u.DeviceInterface.SymbolicLink);
			logger->info("DS3 device got removed: {}", symlink);

			const auto item = std::ranges::find_if(G_DEVICE_STATES, [symlink](const XI_DEVICE_STATE& element)
			{
				return absl::EqualsIgnoreCase(element.SymbolicLink, symlink);
			});

			if (item != G_DEVICE_STATES.end())
			{
				item->Type = XI_DEVICE_TYPE_NOT_CONNECTED;
			}
		}

		if (IsEqualGUID(XUSB_INTERFACE_CLASS_GUID, EventData->u.DeviceInterface.ClassGuid))
		{
			const std::string symlink = ConvertWideToANSI(EventData->u.DeviceInterface.SymbolicLink);
			logger->info("XUSB device got removed: {}", symlink);

			const auto item = std::ranges::find_if(G_DEVICE_STATES, [symlink](const XI_DEVICE_STATE& element)
			{
				return absl::EqualsIgnoreCase(element.SymbolicLink, symlink);
			});

			if (item != G_DEVICE_STATES.end())
			{
				item->Type = XI_DEVICE_TYPE_NOT_CONNECTED;
				item->Backend.UserIndex = K_INVALID_X_INPUT_USER_ID;
			}
		}
		break;
	default:
		return ERROR_SUCCESS;
	}

	return ERROR_SUCCESS;
}

//
// Performs startup tasks too dangerous to do in DllMain
// 
static DWORD WINAPI InitAsync(
	_In_ LPVOID lpParameter
)
{
	const std::shared_ptr<spdlog::logger> logger = spdlog::get(LOGGER_NAME)->clone(__FUNCTION__);

	UNREFERENCED_PARAMETER(lpParameter);

	CHAR systemDir[MAX_PATH] = {};

	if (GetSystemDirectoryA(systemDir, MAX_PATH) == 0)
	{
		logger->error("GetSystemDirectoryA failed: {:#x}", GetLastError());
		return GetLastError();
	}

	CHAR fullXiPath[MAX_PATH] = {};

	if (PathCombineA(fullXiPath, systemDir, XI_SYSTEM_LIB_NAME) == nullptr)
	{
		logger->error("PathCombineA failed: {:#x}", GetLastError());
		return GetLastError();
	}

	const HMODULE xiLib = LoadLibraryA(fullXiPath);

	if (xiLib == nullptr)
	{
		logger->error("LoadLibraryA failed: {:#x}", GetLastError());
		return GetLastError();
	}

	//
	// Grab the function pointers from the OS-provided exports
	// 

	G_fpnXInputGetState = reinterpret_cast<decltype(XInputGetState)*>(GetProcAddress(xiLib,
		NAMEOF(XInputGetState)
	));
	G_fpnXInputSetState = reinterpret_cast<decltype(XInputSetState)*>(GetProcAddress(xiLib,
		NAMEOF(XInputSetState)
	));
	G_fpnXInputGetCapabilities = reinterpret_cast<decltype(XInputGetCapabilities)*>(GetProcAddress(xiLib,
		NAMEOF(XInputGetCapabilities)
	));
	G_fpnXInputEnable = reinterpret_cast<decltype(XInputEnable)*>(GetProcAddress(xiLib,
		NAMEOF(XInputEnable)
	));
	G_fpnXInputGetDSoundAudioDeviceGuids = reinterpret_cast<decltype(XInputGetDSoundAudioDeviceGuids)*>(GetProcAddress(xiLib,
		NAMEOF(XInputGetDSoundAudioDeviceGuids)
	));
	G_fpnXInputGetBatteryInformation = reinterpret_cast<decltype(XInputGetBatteryInformation)*>(GetProcAddress(xiLib,
		NAMEOF(XInputGetBatteryInformation)
	));
	G_fpnXInputGetKeystroke = reinterpret_cast<decltype(XInputGetKeystroke)*>(GetProcAddress(xiLib,
		NAMEOF(XInputGetKeystroke)
	));
	G_fpnXInputGetStateEx = reinterpret_cast<decltype(XInputGetStateEx)*>(GetProcAddress(xiLib,
		MAKEINTRESOURCEA(100)
	));
	G_fpnXInputWaitForGuideButton = reinterpret_cast<decltype(XInputWaitForGuideButton)*>(GetProcAddress(xiLib,
		MAKEINTRESOURCEA(101)
	));
	G_fpnXInputCancelGuideButtonWait = reinterpret_cast<decltype(XInputCancelGuideButtonWait)*>(GetProcAddress(xiLib,
		MAKEINTRESOURCEA(102)
	));
	G_fpnXInputPowerOffController = reinterpret_cast<decltype(XInputPowerOffController)*>(GetProcAddress(xiLib,
		MAKEINTRESOURCEA(103)
	));

	//
	// ^ TODO: maybe cover the exports of XInput1_4 as well?
	// 

	//
	// Register notifications for device arrival/removal
	// 

	CM_NOTIFY_FILTER ds3Filter = {};
	ds3Filter.cbSize = sizeof(CM_NOTIFY_FILTER);
	ds3Filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
	ds3Filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_DSHIDMINI;

	//
	// Register DsHidMini device interface
	// 
	CONFIGRET ret = CM_Register_Notification(&ds3Filter, nullptr, DeviceNotificationCallback, &G_DS3_NOTIFICATION_HANDLE);

	if (ret != CR_SUCCESS)
	{
		logger->error("CM_Register_Notification (DS3) failed: {:#x}", ret);
	}

	CM_NOTIFY_FILTER xusbFilter = {};
	xusbFilter.cbSize = sizeof(CM_NOTIFY_FILTER);
	xusbFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
	xusbFilter.u.DeviceInterface.ClassGuid = XUSB_INTERFACE_CLASS_GUID;

	//
	// Register X360/XBONE device interface
	// 
	ret = CM_Register_Notification(&xusbFilter, nullptr, DeviceNotificationCallback, &G_XUSB_NOTIFICATION_HANDLE);

	if (ret != CR_SUCCESS)
	{
		logger->error("CM_Register_Notification (XUSB) failed: {:#x}", ret);
	}

	return ERROR_SUCCESS;

}

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

void ScpLibInitialize()
{
	CHAR dllPath[MAX_PATH];

	GetModuleFileNameA((HINSTANCE)&__ImageBase, dllPath, MAX_PATH);
	PathRemoveFileSpecA(dllPath);
	const auto dllDir = std::string(dllPath);

	auto logger = spdlog::basic_logger_mt(
		LOGGER_NAME,
		dllDir + "\\XInputBridge.log"
	);

#if _DEBUG
	spdlog::set_level(spdlog::level::debug);
	logger->flush_on(spdlog::level::debug);
#else
	logger->flush_on(spdlog::level::info);
#endif

	set_default_logger(logger);

	for (auto& state : G_DEVICE_STATES)
	{
		InitializeCriticalSection(&state.lock);
		state.isInitialized = true;
	}

	//
	// Call stuff that must not be done in DllMain in the background
	// 
	const HANDLE hThread = CreateThread(nullptr, 0, InitAsync, nullptr, 0, nullptr);

	if (hThread == nullptr)
	{
		logger->error("Failed to create thread");
	}
}

void ScpLibDestroy()
{
	const std::shared_ptr<spdlog::logger> logger = spdlog::get(LOGGER_NAME)->clone(__FUNCTION__);

	logger->info("Library getting unloaded");

	CM_Unregister_Notification(G_DS3_NOTIFICATION_HANDLE);
	CM_Unregister_Notification(G_XUSB_NOTIFICATION_HANDLE);

	for (auto& state : G_DEVICE_STATES)
	{
		DeleteCriticalSection(&state.lock);
	}
}


#pragma region Rumble helper types

/*
 * Source: https://github.com/RPCS3/rpcs3/blob/5e436984a2b5753ad340d2c97462bf3be6e86237/rpcs3/Input/ds3_pad_handler.cpp#L9-L40
 */

struct ds3_rumble
{
	UCHAR padding = 0x00;
	UCHAR small_motor_duration = 0xFF; // 0xff means forever
	UCHAR small_motor_on = 0x00; // 0 or 1 (off/on)
	UCHAR large_motor_duration = 0xFF; // 0xff means forever
	UCHAR large_motor_force = 0x00; // 0 to 255
};

struct ds3_led
{
	UCHAR duration = 0xFF; // total duration, 0xff means forever
	UCHAR interval_duration = 0xFF; // interval duration in deciseconds
	UCHAR enabled = 0x10;
	UCHAR interval_portion_off = 0x00; // in percent (100% = 0xFF)
	UCHAR interval_portion_on = 0xFF; // in percent (100% = 0xFF)
};

struct ds3_output_report
{
	UCHAR report_id = 0x00;
	UCHAR idk_what_this_is[3] = { 0x02, 0x00, 0x00 };
	ds3_rumble rumble;
	UCHAR padding[4] = { 0x00, 0x00, 0x00, 0x00 };
	UCHAR led_enabled = 0x00; // LED 1 = 0x02, LED 2 = 0x04, etc.
	ds3_led led[4];
	ds3_led led_5; // reserved for another LED
};

#pragma endregion

#pragma region Utility functions

static SHORT ScaleDsToXi(UCHAR value, BOOLEAN invert)
{
	auto intValue = value - 0x80;
	if (intValue == -128)
		intValue = -127;

	const auto wtfValue = intValue * 258.00787401574803149606299212599f; // what the fuck?

	return static_cast<short>(invert ? -wtfValue : wtfValue);
}

static float ClampAxis(float value)
{
	if (value > 1.0f)
		return 1.0f;
	if (value < -1.0f)
		return -1.0f;
	return value;
}

static float ToAxis(UCHAR value)
{
	return ClampAxis((((value & 0xFF) - 0x7F) * 2) / 254.0f);
}

#pragma endregion

#pragma region Device state handling

#if defined(SCPLIB_ENABLE_TELEMETRY)
static nostd::shared_ptr<trace::Tracer> GetTracer()
{
	const auto provider = trace::Provider::GetTracerProvider();
	return provider->GetTracer("XInputBridge", OPENTELEMETRY_SDK_VERSION);
}
#endif

//
// Marks a given user index HID device as not available
// 
static void SetDeviceDisconnected(DWORD UserIndex)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scopedSpan = trace::Scope(GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(UserIndex) }
		}));
#endif

	if (UserIndex >= DS3_DEVICES_MAX)
		return;

	const auto state = &G_DEVICE_STATES[UserIndex];

	EnterCriticalSection(&state->lock);

	state->isConnected = false;
	state->PacketNumber = 0;
	RtlZeroMemory(&state->LastReport, sizeof(DS3_RAW_INPUT_REPORT));

	if (state->oldDeviceHandle)
	{
		hid_close(state->oldDeviceHandle);
		state->oldDeviceHandle = nullptr;
	}

	LeaveCriticalSection(&state->lock);
}

//
// Gets a device handle for a given user index, if it is connected
// 
static bool TryGetDs3DeviceHandle(DWORD UserIndex, hid_device** Handle)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scopedSpan = trace::Scope(GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(UserIndex) }
		}));
#endif

	if (UserIndex >= DS3_DEVICES_MAX)
		return false;

	bool result = false;
	const auto state = &G_DEVICE_STATES[UserIndex];
	hid_device* device = nullptr;
	struct hid_device_info* devices = nullptr;
	DWORD index = 0;

	do
	{
		if (!state->isInitialized)
			break;

		EnterCriticalSection(&state->lock);

		if (state->isConnected)
		{
			if (Handle)
				*Handle = state->oldDeviceHandle;
			result = true;
			break;
		}

		RtlZeroMemory(&state->LastReport, sizeof(DS3_RAW_INPUT_REPORT));
		state->PacketNumber = 0;

		//
		// Look for device of interest
		// 
		devices = hid_enumerate(DS3_VID, DS3_PID);

		if (devices == nullptr)
			return false;

		const struct hid_device_info* currentDevice = devices;

		while (currentDevice)
		{
			if (index++ == UserIndex)
				break;

			currentDevice = currentDevice->next;
		}

		if (currentDevice == nullptr)
		{
			state->oldDeviceHandle = nullptr;
			state->isConnected = false;
			break;
		}

		device = hid_open_path(currentDevice->path);

		if (device == nullptr)
		{
			state->oldDeviceHandle = nullptr;
			state->isConnected = false;
			break;
		}

		if (Handle)
			*Handle = device;

		state->oldDeviceHandle = device;
		state->isConnected = true;
		result = true;

	} while (FALSE);

	if (devices)
		hid_free_enumeration(devices);

	LeaveCriticalSection(&state->lock);

	return result;
}

//
// Returns a packet number for a given user index that increments only if the input report has changed
// 
static bool GetPacketNumber(DWORD UserIndex, PDS3_RAW_INPUT_REPORT Report, DWORD* PacketNumber)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	const auto span = GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(UserIndex) }
		});
	auto scopedSpan = trace::Scope(span);
#endif

	if (UserIndex >= DS3_DEVICES_MAX)
		return false;

	if (!PacketNumber || !Report)
		return false;

	const auto state = &G_DEVICE_STATES[UserIndex];

	//
	// Exclude noisy motion stuff from comparison
	// 
	constexpr size_t bytesToCompare = sizeof(DS3_RAW_INPUT_REPORT) - 18;

	//
	// Only increment when a change happened
	// 
	if (memcmp(
		&state->LastReport,
		Report,
		bytesToCompare
	) != 0)
	{
		state->PacketNumber++;
		memcpy(&state->LastReport, Report, sizeof(DS3_RAW_INPUT_REPORT));
	}

#if defined(SCPLIB_ENABLE_TELEMETRY)
	span->SetAttribute("xinput.packetNumber", std::to_string(state->packetNumber));
#endif

	*PacketNumber = state->PacketNumber;

	return true;
}

#pragma endregion

#pragma region Public exports

//
// Public API exports
// 

XINPUTBRIDGE_API DWORD WINAPI XInputGetExtended(
	_In_ DWORD dwUserIndex,
	_Out_ SCP_EXTN* pState
)
{
	DWORD status = ERROR_DEVICE_NOT_CONNECTED;
	hid_device* device = nullptr;

#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scopedSpan = trace::Scope(GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		}));
#endif

	do
	{
		//
		// User might troll us
		// 
		if (pState == nullptr)
			break;

		//
		// Look for device of interest
		// 
		if (!TryGetDs3DeviceHandle(dwUserIndex, &device))
			break;

		UCHAR buf[SXS_MODE_GET_FEATURE_BUFFER_LEN];
		buf[0] = SXS_MODE_GET_FEATURE_REPORT_ID;

		const int res = hid_get_feature_report(device, buf, ARRAYSIZE(buf));

		if (res == 0)
		{
			SetDeviceDisconnected(dwUserIndex);
			break;
		}

		const auto pReport = reinterpret_cast<PDS3_RAW_INPUT_REPORT>(&buf[1]);

		RtlZeroMemory(pState, sizeof(SCP_EXTN));

		//
		// D-Pad
		// 
		pState->SCP_UP = static_cast<float>(pReport->Pressure.Values.Up) / static_cast<float>(UCHAR_MAX);
		pState->SCP_RIGHT = static_cast<float>(pReport->Pressure.Values.Right) / static_cast<float>(UCHAR_MAX);
		pState->SCP_DOWN = static_cast<float>(pReport->Pressure.Values.Down) / static_cast<float>(UCHAR_MAX);
		pState->SCP_LEFT = static_cast<float>(pReport->Pressure.Values.Left) / static_cast<float>(UCHAR_MAX);

		//
		// Start/Select
		// 
		pState->SCP_START = pReport->Buttons.Individual.Start ? 1.0f : 0.0f;
		pState->SCP_SELECT = pReport->Buttons.Individual.Select ? 1.0f : 0.0f;

		//
		// Thumbs
		// 
		pState->SCP_L3 = pReport->Buttons.Individual.L3 ? 1.0f : 0.0f;
		pState->SCP_R3 = pReport->Buttons.Individual.R3 ? 1.0f : 0.0f;

		//
		// Shoulders
		// 
		pState->SCP_L1 = static_cast<float>(pReport->Pressure.Values.L1) / static_cast<float>(UCHAR_MAX);
		pState->SCP_R1 = static_cast<float>(pReport->Pressure.Values.R1) / static_cast<float>(UCHAR_MAX);

		//
		// Face buttons
		// 
		pState->SCP_T = static_cast<float>(pReport->Pressure.Values.Triangle) / static_cast<float>(UCHAR_MAX);
		pState->SCP_C = static_cast<float>(pReport->Pressure.Values.Circle) / static_cast<float>(UCHAR_MAX);
		pState->SCP_X = static_cast<float>(pReport->Pressure.Values.Cross) / static_cast<float>(UCHAR_MAX);
		pState->SCP_S = static_cast<float>(pReport->Pressure.Values.Square) / static_cast<float>(UCHAR_MAX);

		//
		// Triggers
		// 
		pState->SCP_L2 = static_cast<float>(pReport->Pressure.Values.L2) / static_cast<float>(UCHAR_MAX);
		pState->SCP_R2 = static_cast<float>(pReport->Pressure.Values.R2) / static_cast<float>(UCHAR_MAX);

		//
		// PS
		// 
		pState->SCP_PS = pReport->Buttons.Individual.PS ? 1.0f : 0.0f;

		//
		// Thumb axes
		//
		if (pReport->LeftThumbX < (UCHAR_MAX / 2) - DS3_AXIS_ANTI_JITTER_OFFSET
			|| pReport->LeftThumbX > (UCHAR_MAX / 2) + DS3_AXIS_ANTI_JITTER_OFFSET)
			pState->SCP_LX = ToAxis(pReport->LeftThumbX);
		if (pReport->LeftThumbY < (UCHAR_MAX / 2) - DS3_AXIS_ANTI_JITTER_OFFSET
			|| pReport->LeftThumbY > (UCHAR_MAX / 2) + DS3_AXIS_ANTI_JITTER_OFFSET)
			pState->SCP_LY = ToAxis(pReport->LeftThumbY) * -1.0f;
		if (pReport->RightThumbX < (UCHAR_MAX / 2) - DS3_AXIS_ANTI_JITTER_OFFSET
			|| pReport->RightThumbX > (UCHAR_MAX / 2) + DS3_AXIS_ANTI_JITTER_OFFSET)
			pState->SCP_RX = ToAxis(pReport->RightThumbX);
		if (pReport->RightThumbY < (UCHAR_MAX / 2) - DS3_AXIS_ANTI_JITTER_OFFSET
			|| pReport->RightThumbY > (UCHAR_MAX / 2) + DS3_AXIS_ANTI_JITTER_OFFSET)
			pState->SCP_RY = ToAxis(pReport->RightThumbY) * -1.0f;

		status = ERROR_SUCCESS;
	} while (FALSE);

	return status;
}

XINPUTBRIDGE_API DWORD WINAPI XInputGetState(
	_In_ DWORD dwUserIndex,
	_Out_ XINPUT_STATE* pState
)
{
	DWORD status = ERROR_DEVICE_NOT_CONNECTED;
	hid_device* device = nullptr;

#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scopedSpan = trace::Scope(GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		}));
#endif

	do
	{
		//
		// User might troll us
		// 
		if (pState == nullptr)
			break;

		//
		// Look for device of interest
		// 
		if (!TryGetDs3DeviceHandle(dwUserIndex, &device))
		{
			status = CALL_FPN_SAFE(G_fpnXInputGetState, dwUserIndex, pState);
			break;
		}

		UCHAR buf[SXS_MODE_GET_FEATURE_BUFFER_LEN];
		buf[0] = SXS_MODE_GET_FEATURE_REPORT_ID;

		const int res = hid_get_feature_report(device, buf, ARRAYSIZE(buf));

		if (res <= 0)
		{
			SetDeviceDisconnected(dwUserIndex);
			break;
		}

		const auto pReport = reinterpret_cast<PDS3_RAW_INPUT_REPORT>(&buf[1]);

		if (!GetPacketNumber(dwUserIndex, pReport, &pState->dwPacketNumber))
			break;

		RtlZeroMemory(&pState->Gamepad, sizeof(pState->Gamepad));

		//
		// D-Pad translation
		// 
		switch (pReport->Buttons.bButtons[0] & ~0xF)
		{
		case 0x10: // N
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
			break;
		case 0x30: // NE
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
			break;
		case 0x20: // E
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
			break;
		case 0x60: // SE
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
			break;
		case 0x40: // S
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
			break;
		case 0xC0: // SW
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		case 0x80: // W
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		case 0x90: // NW
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		default: // Released
			break;
		}

		//
		// Start/Select
		// 
		if (pReport->Buttons.Individual.Start)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_START;
		if (pReport->Buttons.Individual.Select)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;

		//
		// Thumbs
		// 
		if (pReport->Buttons.Individual.L3)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
		if (pReport->Buttons.Individual.R3)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

		//
		// Shoulders
		// 
		if (pReport->Buttons.Individual.L1)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
		if (pReport->Buttons.Individual.R1)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

		//
		// Face buttons
		// 
		if (pReport->Buttons.Individual.Triangle)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
		if (pReport->Buttons.Individual.Circle)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B;
		if (pReport->Buttons.Individual.Cross)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
		if (pReport->Buttons.Individual.Square)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X;

		//
		// Triggers
		// 
		pState->Gamepad.bLeftTrigger = pReport->Pressure.Values.L2;
		pState->Gamepad.bRightTrigger = pReport->Pressure.Values.R2;

		//
		// Thumb axes
		// 
		pState->Gamepad.sThumbLX = ScaleDsToXi(pReport->LeftThumbX, FALSE);
		pState->Gamepad.sThumbLY = ScaleDsToXi(pReport->LeftThumbY, TRUE);
		pState->Gamepad.sThumbRX = ScaleDsToXi(pReport->RightThumbX, FALSE);
		pState->Gamepad.sThumbRY = ScaleDsToXi(pReport->RightThumbY, TRUE);

		status = ERROR_SUCCESS;
	} while (FALSE);

	return status;
}

XINPUTBRIDGE_API DWORD WINAPI XInputSetState(
	_In_ DWORD dwUserIndex,
	_In_ XINPUT_VIBRATION* pVibration
)
{
	DWORD status = ERROR_DEVICE_NOT_CONNECTED;
	hid_device* device = nullptr;

#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scoped_span = trace::Scope(GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		}));
#endif

	do
	{
		//
		// User might troll us
		// 
		if (pVibration == nullptr)
			break;

		//
		// Look for device of interest
		// 
		if (!TryGetDs3DeviceHandle(dwUserIndex, &device))
		{
			status = CALL_FPN_SAFE(G_fpnXInputSetState, dwUserIndex, pVibration);
			break;
		}

		ds3_output_report outputReport;

		// ReSharper disable CppAssignedValueIsNeverUsed
		outputReport.rumble.small_motor_on = pVibration->wRightMotorSpeed > 0 ? 1 : 0;
		outputReport.rumble.large_motor_force = static_cast<float>(pVibration->wLeftMotorSpeed) / static_cast<float>(
			USHRT_MAX) * static_cast<float>(UCHAR_MAX);

		switch (dwUserIndex)
		{
		case 0:
			outputReport.led_enabled = 0b00000010;
			break;
		case 1:
			outputReport.led_enabled = 0b00000100;
			break;
		case 2:
			outputReport.led_enabled = 0b00001000;
			break;
		case 3:
			outputReport.led_enabled = 0b00010000;
			break;
		case 4:
			outputReport.led_enabled = 0b00010010;
			break;
		case 5:
			outputReport.led_enabled = 0b00010100;
			break;
		case 6:
			outputReport.led_enabled = 0b00011000;
			break;
		default:
			break;
		}
		// ReSharper restore CppAssignedValueIsNeverUsed

		const int res = hid_write(device, &outputReport.report_id, sizeof(outputReport));

		if (res <= 0)
		{
			SetDeviceDisconnected(dwUserIndex);
			break;
		}

		status = ERROR_SUCCESS;
	} while (FALSE);

	return status;
}

XINPUTBRIDGE_API DWORD WINAPI XInputGetCapabilities(
	_In_ DWORD dwUserIndex,
	_In_ DWORD dwFlags,
	_Out_ XINPUT_CAPABILITIES* pCapabilities
)
{
	UNREFERENCED_PARAMETER(dwFlags);
	DWORD status = ERROR_DEVICE_NOT_CONNECTED;

#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scopedSpan = trace::Scope(GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		}));
#endif

	do
	{
		//
		// User might troll us
		// 
		if (pCapabilities == nullptr)
			break;

		//
		// Look for device of interest
		// 
		if (!TryGetDs3DeviceHandle(dwUserIndex, nullptr))
		{
			status = CALL_FPN_SAFE(G_fpnXInputGetCapabilities, dwUserIndex, dwFlags, pCapabilities);
			break;
		}

		RtlZeroMemory(pCapabilities, sizeof(XINPUT_CAPABILITIES));

		pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
		pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
		pCapabilities->Flags += XINPUT_CAPS_FFB_SUPPORTED;

		pCapabilities->Gamepad.wButtons = (
			XINPUT_GAMEPAD_DPAD_UP |
			XINPUT_GAMEPAD_DPAD_DOWN |
			XINPUT_GAMEPAD_DPAD_LEFT |
			XINPUT_GAMEPAD_DPAD_RIGHT |
			XINPUT_GAMEPAD_START |
			XINPUT_GAMEPAD_BACK |
			XINPUT_GAMEPAD_LEFT_THUMB |
			XINPUT_GAMEPAD_RIGHT_THUMB |
			XINPUT_GAMEPAD_LEFT_SHOULDER |
			XINPUT_GAMEPAD_RIGHT_SHOULDER |
			XINPUT_GAMEPAD_A |
			XINPUT_GAMEPAD_B |
			XINPUT_GAMEPAD_X |
			XINPUT_GAMEPAD_Y
		);
		pCapabilities->Gamepad.bLeftTrigger = UCHAR_MAX;
		pCapabilities->Gamepad.bRightTrigger = UCHAR_MAX;
		pCapabilities->Gamepad.sThumbLX = static_cast<SHORT>(0xFFC0);
		pCapabilities->Gamepad.sThumbLY = static_cast<SHORT>(0xFFC0);
		pCapabilities->Gamepad.sThumbRX = static_cast<SHORT>(0xFFC0);
		pCapabilities->Gamepad.sThumbRY = static_cast<SHORT>(0xFFC0);

		pCapabilities->Vibration.wLeftMotorSpeed = UCHAR_MAX;
		pCapabilities->Vibration.wRightMotorSpeed = UCHAR_MAX;

		status = ERROR_SUCCESS;
	} while (FALSE);

	return status;
}

XINPUTBRIDGE_API void WINAPI XInputEnable(
	_In_ BOOL enable
)
{
	UNREFERENCED_PARAMETER(enable);

#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scopedSpan = trace::Scope(GetTracer()->StartSpan(__FUNCTION__));
#endif

	CALL_FPN_SAFE_NO_RETURN(G_fpnXInputEnable, enable);
}

XINPUTBRIDGE_API DWORD WINAPI XInputGetDSoundAudioDeviceGuids(
	DWORD dwUserIndex,
	GUID* pDSoundRenderGuid,
	GUID* pDSoundCaptureGuid
)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	const auto span = GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		});
	auto scopedSpan = trace::Scope(span);
#endif

	return CALL_FPN_SAFE(G_fpnXInputGetDSoundAudioDeviceGuids, dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid);
}

XINPUTBRIDGE_API DWORD WINAPI XInputGetBatteryInformation(
	_In_ DWORD dwUserIndex,
	_In_ BYTE devType,
	_Out_ XINPUT_BATTERY_INFORMATION* pBatteryInformation
)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	const auto span = GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		});
	auto scopedSpan = trace::Scope(span);
#endif

	return CALL_FPN_SAFE(G_fpnXInputGetBatteryInformation, dwUserIndex, devType, pBatteryInformation);
}

XINPUTBRIDGE_API DWORD WINAPI XInputGetKeystroke(
	DWORD dwUserIndex,
	DWORD dwReserved,
	PXINPUT_KEYSTROKE pKeystroke
)
{
	UNREFERENCED_PARAMETER(dwUserIndex);
	UNREFERENCED_PARAMETER(dwReserved);
	UNREFERENCED_PARAMETER(pKeystroke);

#if defined(SCPLIB_ENABLE_TELEMETRY)
	const auto span = GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		});
	auto scopedSpan = trace::Scope(span);
#endif

	return CALL_FPN_SAFE(G_fpnXInputGetKeystroke, dwUserIndex, dwReserved, pKeystroke);
}

XINPUTBRIDGE_API DWORD WINAPI XInputGetStateEx(
	_In_ DWORD dwUserIndex,
	_Out_ XINPUT_STATE* pState
)
{
	DWORD status = ERROR_DEVICE_NOT_CONNECTED;
	hid_device* device = nullptr;

#if defined(SCPLIB_ENABLE_TELEMETRY)
	auto scopedSpan = trace::Scope(GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		}));
#endif

	do
	{
		//
		// User might troll us
		// 
		if (pState == nullptr)
			break;

		//
		// Look for device of interest
		// 
		if (!TryGetDs3DeviceHandle(dwUserIndex, &device))
		{
			status = CALL_FPN_SAFE(G_fpnXInputGetStateEx, dwUserIndex, pState);
			break;
		}

		UCHAR buf[64];
		buf[0] = SXS_MODE_GET_FEATURE_REPORT_ID;

		const int res = hid_get_feature_report(device, buf, ARRAYSIZE(buf));

		if (res <= 0)
		{
			SetDeviceDisconnected(dwUserIndex);
			break;
		}

		const auto pReport = reinterpret_cast<PDS3_RAW_INPUT_REPORT>(&buf[1]);

		if (!GetPacketNumber(dwUserIndex, pReport, &pState->dwPacketNumber))
			break;

		RtlZeroMemory(&pState->Gamepad, sizeof(pState->Gamepad));

		//
		// D-Pad translation
		// 
		switch (pReport->Buttons.bButtons[0] & ~0xF)
		{
		case 0x10: // N
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
			break;
		case 0x30: // NE
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
			break;
		case 0x20: // E
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
			break;
		case 0x60: // SE
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
			break;
		case 0x40: // S
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
			break;
		case 0xC0: // SW
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		case 0x80: // W
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		case 0x90: // NW
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
			break;
		default: // Released
			break;
		}

		//
		// Start/Select
		// 
		if (pReport->Buttons.Individual.Start)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_START;
		if (pReport->Buttons.Individual.Select)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;

		//
		// Thumbs
		// 
		if (pReport->Buttons.Individual.L3)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
		if (pReport->Buttons.Individual.R3)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;

		//
		// Shoulders
		// 
		if (pReport->Buttons.Individual.L1)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
		if (pReport->Buttons.Individual.R1)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

		//
		// Face buttons
		// 
		if (pReport->Buttons.Individual.Triangle)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
		if (pReport->Buttons.Individual.Circle)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_B;
		if (pReport->Buttons.Individual.Cross)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
		if (pReport->Buttons.Individual.Square)
			pState->Gamepad.wButtons |= XINPUT_GAMEPAD_X;

		//
		// Triggers
		// 
		pState->Gamepad.bLeftTrigger = pReport->Pressure.Values.L2;
		pState->Gamepad.bRightTrigger = pReport->Pressure.Values.R2;

		//
		// Thumb axes
		// 
		pState->Gamepad.sThumbLX = ScaleDsToXi(pReport->LeftThumbX, FALSE);
		pState->Gamepad.sThumbLY = ScaleDsToXi(pReport->LeftThumbY, TRUE);
		pState->Gamepad.sThumbRX = ScaleDsToXi(pReport->RightThumbX, FALSE);
		pState->Gamepad.sThumbRY = ScaleDsToXi(pReport->RightThumbY, TRUE);

		//
		// PS/Guide
		// 
		pState->Gamepad.wButtons |= XINPUT_GAMEPAD_GUIDE;

		status = ERROR_SUCCESS;
	} while (FALSE);

	return status;
}

XINPUTBRIDGE_API DWORD WINAPI XInputWaitForGuideButton(
	_In_ DWORD dwUserIndex,
	_In_ DWORD dwFlag,
	_In_ LPVOID pVoid
)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	const auto span = GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		});
	auto scopedSpan = trace::Scope(span);
#endif

	return CALL_FPN_SAFE(G_fpnXInputWaitForGuideButton, dwUserIndex, dwFlag, pVoid);
}

XINPUTBRIDGE_API DWORD WINAPI XInputCancelGuideButtonWait(
	_In_ DWORD dwUserIndex
)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	const auto span = GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		});
	auto scopedSpan = trace::Scope(span);
#endif

	return CALL_FPN_SAFE(G_fpnXInputCancelGuideButtonWait, dwUserIndex);
}

XINPUTBRIDGE_API DWORD WINAPI XInputPowerOffController(
	_In_ DWORD dwUserIndex
)
{
#if defined(SCPLIB_ENABLE_TELEMETRY)
	const auto span = GetTracer()->StartSpan(__FUNCTION__, {
		{ "xinput.userIndex", std::to_string(dwUserIndex) }
		});
	auto scopedSpan = trace::Scope(span);
#endif

	return CALL_FPN_SAFE(G_fpnXInputPowerOffController, dwUserIndex);
}

#pragma endregion
