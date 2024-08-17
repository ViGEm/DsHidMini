﻿using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;

using Nefarius.DsHidMini.IPC.Models.Public;

namespace Nefarius.DsHidMini.IPC.Models;

[StructLayout(LayoutKind.Explicit)]
[SuppressMessage("ReSharper", "InconsistentNaming")]
internal struct DSHM_IPC_MSG_COMMAND
{
    [FieldOffset(0)]
    public DSHM_IPC_MSG_CMD_DRIVER Driver;

    [FieldOffset(0)]
    public DSHM_IPC_MSG_CMD_DEVICE Device;
}

[StructLayout(LayoutKind.Sequential)]
[SuppressMessage("ReSharper", "InconsistentNaming")]
[SuppressMessage("ReSharper", "UnusedMember.Global")]
internal struct DSHM_IPC_MSG_HEADER
{
    // What request-behavior is expected (request, request-reply, ...)
    public DSHM_IPC_MSG_TYPE Type;

    // What component is this message targeting (driver, device, ...)
    public DSHM_IPC_MSG_TARGET Target;

    // What command is this message carrying
    public DSHM_IPC_MSG_COMMAND Command;

    // One-based index of which device is this message for
    // Set to 0 if driver is targeted
    public uint TargetIndex;

    // The size of the entire message (header + payload) in bytes
    // A size of 0 is invalid
    public uint Size;
}

[StructLayout(LayoutKind.Sequential)]
[SuppressMessage("ReSharper", "InconsistentNaming")]
[SuppressMessage("ReSharper", "UnusedMember.Global")]
internal unsafe struct DSHM_IPC_MSG_PAIR_TO_REQUEST
{
    public DSHM_IPC_MSG_HEADER Header;

    public fixed byte Address[6];
}

[StructLayout(LayoutKind.Sequential)]
[SuppressMessage("ReSharper", "InconsistentNaming")]
[SuppressMessage("ReSharper", "UnusedMember.Global")]
internal struct DSHM_IPC_MSG_PAIR_TO_REPLY
{
    public DSHM_IPC_MSG_HEADER Header;

    public UInt32 WriteStatus;
    
    public UInt32 ReadStatus;
}

[StructLayout(LayoutKind.Sequential)]
[SuppressMessage("ReSharper", "InconsistentNaming")]
[SuppressMessage("ReSharper", "UnusedMember.Global")]
internal struct DSHM_IPC_MSG_SET_PLAYER_INDEX_REQUEST
{
    public DSHM_IPC_MSG_HEADER Header;

    public byte PlayerIndex;
}

[StructLayout(LayoutKind.Sequential)]
[SuppressMessage("ReSharper", "InconsistentNaming")]
[SuppressMessage("ReSharper", "UnusedMember.Global")]
internal struct DSHM_IPC_MSG_SET_PLAYER_INDEX_REPLY
{
    public DSHM_IPC_MSG_HEADER Header;

    public UInt32 NtStatus;
}

[SuppressMessage("ReSharper", "InconsistentNaming")]
[StructLayout(LayoutKind.Sequential)]
internal struct DSHM_IPC_MSG_GET_HID_WAIT_HANDLE_RESPONSE
{
    public DSHM_IPC_MSG_HEADER Header;

    public UInt32 ProcessId;

    public IntPtr WaitHandle;  // HANDLE is typically represented as IntPtr in C#
}

[StructLayout(LayoutKind.Sequential)]
[SuppressMessage("ReSharper", "InconsistentNaming")]
internal struct IPC_HID_INPUT_REPORT_MESSAGE
{
    public UInt32 SlotIndex;

    public Ds3RawInputReport InputReport;
}
