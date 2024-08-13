﻿using Nefarius.DsHidMini.IPC.Models;

namespace Nefarius.DsHidMini.IPC.Exceptions;

public sealed class DsHidMiniInteropExclusiveAccessException : Exception
{
    internal DsHidMiniInteropExclusiveAccessException() : base(
        "Another client is already connected to the driver, can't continue initialization.")
    {
    }
}

public sealed class DsHidMiniInteropUnavailableException : Exception
{
    internal DsHidMiniInteropUnavailableException() : base(
        "Driver IPC unavailable, make sure that at least one compatible controller is connected and operational.")
    {
    }
}

public sealed class DsHidMiniInteropReplyTimeoutException : Exception
{
    internal DsHidMiniInteropReplyTimeoutException() : base("Operation timed out while waiting for a request reply.")
    {
    }
}

public sealed class DsHidMiniInteropUnexpectedReplyException : Exception
{
    private readonly unsafe DSHM_IPC_MSG_HEADER* _header;

    internal unsafe DsHidMiniInteropUnexpectedReplyException(DSHM_IPC_MSG_HEADER* header) : base(
        "A request reply was malformed.")
    {
        _header = header;
    }

    public override unsafe string Message =>
        $"Type: {_header->Type}, Target: {_header->Target}, Command: {_header->Command.Driver}, TargetIndex: {_header->TargetIndex}, Size: {_header->Size}";
}