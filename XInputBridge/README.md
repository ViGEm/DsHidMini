# SCP XInput Bridge (Proxy DLL)

## About

This project brings back the [extended XInput API provided by ScpServer/ScpToolkit](https://github.com/nefarius/ScpToolkit/tree/master/ScpXInputBridge) to DsHidMini as a drop-in replacement for the now abandoned [SCP proxy DLL](https://github.com/nefarius/ScpToolkit/tree/9f4076ad6912002687d1824494258607d859c67e/XInput_Scp). In addition to the common XInput functions, it also provides the `XInputGetExtended` API, which returns all possible pressure values.

The library supportes detection and proxying of up to 8 DS3s in [SXS mode](https://docs.nefarius.at/projects/DsHidMini/HID-Device-Modes-Explained/#sxs) and/or any XInput-compatible controller mixed.

### Info fo users

Please read the [Usage documentation](https://docs.nefarius.at/projects/DsHidMini/SCP-XInput-Bridge/).

### Info for developers

This project is actively maintained and considered stable, it can also be used in new projects that need simple access to DS3 pressure values.

### Known limitations

Routing of Wireless Xbox 360 pads using the Microsoft Wireless Dongle will probably not work properly since the dongle hides multiple controllers behind one single device instance, so arrival and removal notifications for every subsequent wireless device will not fire. Testing welcome.

## API Overview

A brief summary of the exported library functions.

### `XInputGetExtended`

Reports back `struct _SCP_EXTN` with pressure values. For implementation details see `ScpTypes.h`. A value of `1.0f` represents fully pressed/engaged and `0.0f` represents default/disengaged. For axes, a value of `-1.0f` is equal to most west/south position, `0.0f` represents the centered/resting position and `1.0f` is equal to most east/north position.

A jitter compensation dead-zone is applied to the thumb axes.

If the provided user index is occupied by an XUSB device it returns `ERROR_DEVICE_NOT_CONNECTED`.

### `XInputGetState`

Reports back [`XINPUT_GAMEPAD` structure](https://docs.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad) with all DS3 buttons and axes mapped identical to the Xbox 360 layout. It does *not* report the PS/Guide button.

A jitter compensation dead-zone is applied to the thumb axes.

If the provided user index is occupied by an XUSB device the request gets proxied to `C:\Windows\System32\XInput1_3.dll`. Otherwise returns `ERROR_DEVICE_NOT_CONNECTED`.

### `XInputSetState`

Converts a vibration request into a DS3 output report. Translates and scales the rumble motor values accordingly (left gets mapped to strong, right gets mapped to weak motor). The player LED gets set to the (zero-based) `dwUserIndex` the function got invoked with (0 sets player 1, 1 sets player 2 and so forth).

If the provided user index is occupied by an XUSB device the request gets proxied to `C:\Windows\System32\XInput1_3.dll`. Otherwise returns `ERROR_DEVICE_NOT_CONNECTED`.

### `XInputGetCapabilities`

Returns the typical Xbox 360 Controller compatible capabilities for every connected player index.

If the provided user index is occupied by an XUSB device the request gets proxied to `C:\Windows\System32\XInput1_3.dll`. Otherwise returns `ERROR_DEVICE_NOT_CONNECTED`.

### `XInputEnable`

Gets proxied to `C:\Windows\System32\XInput1_3.dll`.

### `XInputGetDSoundAudioDeviceGuids`

Gets proxied to `C:\Windows\System32\XInput1_3.dll`.

### `XInputGetBatteryInformation`

Gets proxied to `C:\Windows\System32\XInput1_3.dll`.

### `XInputGetKeystroke`

Gets proxied to `C:\Windows\System32\XInput1_3.dll`.

### `XInputGetStateEx`

Reports back [`XINPUT_GAMEPAD` structure](https://docs.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_gamepad) with all DS3 buttons and axes mapped identical to the Xbox 360 layout. It *does* report the PS/Guide button.

A jitter compensation dead-zone is applied to the thumb axes.

If the provided user index is occupied by an XUSB device the request gets proxied to `C:\Windows\System32\XInput1_3.dll`. Otherwise returns `ERROR_DEVICE_NOT_CONNECTED`.

### `XInputWaitForGuideButton`

Gets proxied to `C:\Windows\System32\XInput1_3.dll`.

### `XInputCancelGuideButtonWait`

Gets proxied to `C:\Windows\System32\XInput1_3.dll`.

### `XInputPowerOffController`

Gets proxied to `C:\Windows\System32\XInput1_3.dll`.

## OpenTelemetry

The library can be built with `/D SCPLIB_ENABLE_TELEMETRY` which will pull in the [OpenTelemetry C++ Client](https://github.com/open-telemetry/opentelemetry-cpp) and register an [OTLP](https://opentelemetry.io/docs/specs/otlp/) exporter you can use in conjunction with e.g. [Jaeger Tracing](https://www.jaegertracing.io/) to visualize performance metrics. OTEL support isn't enabled by default as it inflates resulting binary size and most users will never need this functionality.

### OTEL-enabled binaries

If you need logging and tracing to hunt down errors or performance bottle-necks you can use an OTEL-enabled copy instead. It comes with all necessary prerequisites compiled in and therefore is like 20-50 times larger than the normal build.

The DLL architecture has to match your game/emulator or it will result in errors.

- [x64 (64-Bit Intel/AMD)](https://buildbot.nefarius.at/builds/DsHidMini/latest/bin/x64/OTEL/XInput1_3.dll) (most used)
- [x86 (32-Bit Intel/AMD)](https://buildbot.nefarius.at/builds/DsHidMini/latest/bin/x86/OTEL/XInput1_3.dll)
- [ARM64 (64-Bit ARM)](https://buildbot.nefarius.at/builds/DsHidMini/latest/bin/arm64/OTEL/XInput1_3.dll)

## Sources & 3rd party credits

This application benefits from these awesome projects ❤ (appearance in no special order):

- [araghon007/X1nput](https://github.com/araghon007/X1nput)
- [rpcs3/rpcs3/Input/ds3_pad_handler.cpp](https://github.com/RPCS3/rpcs3/blob/5e436984a2b5753ad340d2c97462bf3be6e86237/rpcs3/Input/ds3_pad_handler.cpp)
- [pcsx2/pcsx2/PAD/Windows/XInputEnum.cpp](https://github.com/PCSX2/pcsx2/blob/6f7890b709d5e3f7f5b824781e493455efc92339/pcsx2/PAD/Windows/XInputEnum.cpp)
- [HIDAPI library for Windows, Linux, FreeBSD and macOS](https://github.com/libusb/hidapi)
- [DJm00n/RawInputDemo](https://github.com/DJm00n/RawInputDemo)
- [OpenXInput](https://github.com/Nemirtingas/OpenXinput)
- [Abseil C++ library](https://abseil.io/)
- [Convenient high-level C++ wrapper around the Windows Registry API](https://github.com/GiovanniDicanio/WinReg)
