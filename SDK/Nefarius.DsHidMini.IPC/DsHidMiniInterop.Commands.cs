﻿using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Net.NetworkInformation;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using Nefarius.DsHidMini.IPC.Exceptions;
using Nefarius.DsHidMini.IPC.Models;
using Nefarius.DsHidMini.IPC.Models.Public;

namespace Nefarius.DsHidMini.IPC;

public partial class DsHidMiniInterop
{
    /// <summary>
    ///     Attempts to read the <see cref="DS3_RAW_INPUT_REPORT" /> from a given device instance.
    /// </summary>
    /// <remarks>
    ///     If <paramref name="timeout" /> is null, this method returns the last known input report copy immediately. If
    ///     you use this call in a busy loop, you should set a timeout so this call becomes event-based, meaning the call will
    ///     only return when the driver signaled that new data is available, otherwise you will just burn through CPU for no
    ///     good reason. A new input report is typically available each average 5 milliseconds, depending on the connection
    ///     (wired or wireless) so a timeout of 20 milliseconds should be a good recommendation.
    /// </remarks>
    /// <param name="deviceIndex">The one-based device index.</param>
    /// <param name="report">The <see cref="DS3_RAW_INPUT_REPORT" /> to populate.</param>
    /// <param name="timeout">Optional timeout to wait for a report update to arrive. Default invocation returns immediately.</param>
    /// <exception cref="DsHidMiniInteropAccessDeniedException">
    ///     Driver process interaction failed due to missing permissions;
    ///     this operation requires elevated privileges.
    /// </exception>
    /// <exception cref="DsHidMiniInteropUnexpectedReplyException">The driver returned unexpected or malformed data.</exception>
    /// <exception cref="Win32Exception">Handle duplication failed.</exception>
    /// <exception cref="DsHidMiniInteropReplyTimeoutException">The driver didn't respond within an expected period.</exception>
    /// <exception cref="DsHidMiniInteropConcurrencyException">A different thread is currently performing a data exchange.</exception>
    /// <exception cref="DsHidMiniInteropUnavailableException">
    ///     No driver instance is available. Make sure that at least one
    ///     device is connected and that the driver is installed and working properly. Call <see cref="IsAvailable" /> prior to
    ///     avoid this exception.
    /// </exception>
    /// <returns>
    ///     TRUE if <paramref name="report" /> got filled in or FALSE if the given <paramref name="deviceIndex" /> is not
    ///     occupied.
    /// </returns>
    [SuppressMessage("ReSharper", "UnusedMember.Global")]
    public unsafe bool GetRawInputReport(int deviceIndex, ref DS3_RAW_INPUT_REPORT report, TimeSpan? timeout = null)
    {
        if (_hidView is null)
        {
            throw new DsHidMiniInteropUnavailableException();
        }

        ValidateDeviceIndex(deviceIndex);

        ref IPC_HID_INPUT_REPORT_MESSAGE message = ref Unsafe.AsRef<IPC_HID_INPUT_REPORT_MESSAGE>(_hidView);

        if (timeout.HasValue)
        {
            _inputReportEvent ??= GetHidReportWaitHandle(deviceIndex);

            _inputReportEvent.WaitOne(timeout.Value);
        }

        //
        // Device is/got disconnected
        // 
        if (message.SlotIndex == 0)
        {
            return false;
        }

        //
        // Index mismatch is not supposed to happen
        // 
        if (message.SlotIndex != deviceIndex)
        {
            throw new DsHidMiniInteropUnexpectedReplyException();
        }

        report = message.InputReport;

        return true;
    }

    /// <summary>
    ///     Send a PING to the driver and awaits the reply.
    /// </summary>
    /// <exception cref="DsHidMiniInteropUnavailableException">
    ///     Driver IPC unavailable, make sure that at least one compatible
    ///     controller is connected and operational.
    /// </exception>
    /// <exception cref="DsHidMiniInteropReplyTimeoutException">The driver didn't respond within an expected period.</exception>
    /// <exception cref="DsHidMiniInteropUnexpectedReplyException">The driver returned unexpected or malformed data.</exception>
    [SuppressMessage("ReSharper", "UnusedMember.Global")]
    public unsafe void SendPing()
    {
        if (_commandMutex is null || _cmdView is null)
        {
            throw new DsHidMiniInteropUnavailableException();
        }

        AcquireCommandLock();

        try
        {
            ref DSHM_IPC_MSG_HEADER message = ref Unsafe.AsRef<DSHM_IPC_MSG_HEADER>(_cmdView);

            message.Type = DSHM_IPC_MSG_TYPE.DSHM_IPC_MSG_TYPE_REQUEST_RESPONSE;
            message.Target = DSHM_IPC_MSG_TARGET.DSHM_IPC_MSG_TARGET_DRIVER;
            message.Command.Driver = DSHM_IPC_MSG_CMD_DRIVER.DSHM_IPC_MSG_CMD_DRIVER_PING;
            message.TargetIndex = 0;
            message.Size = (uint)Marshal.SizeOf<DSHM_IPC_MSG_HEADER>();

            if (!SendAndWait())
            {
                throw new DsHidMiniInteropReplyTimeoutException();
            }

            ref DSHM_IPC_MSG_HEADER reply = ref Unsafe.AsRef<DSHM_IPC_MSG_HEADER>(_cmdView);

            //
            // Plausibility check
            // 
            if (reply is
                {
                    Type: DSHM_IPC_MSG_TYPE.DSHM_IPC_MSG_TYPE_REQUEST_REPLY,
                    Target: DSHM_IPC_MSG_TARGET.DSHM_IPC_MSG_TARGET_CLIENT,
                    Command.Driver: DSHM_IPC_MSG_CMD_DRIVER.DSHM_IPC_MSG_CMD_DRIVER_PING, TargetIndex: 0
                }
                && reply.Size == Marshal.SizeOf<DSHM_IPC_MSG_HEADER>())
            {
                return;
            }

            throw new DsHidMiniInteropUnexpectedReplyException(ref reply);
        }
        finally
        {
            _commandMutex.ReleaseMutex();
        }
    }

    /// <summary>
    ///     Writes a new host address to the given device.
    /// </summary>
    /// <exception cref="DsHidMiniInteropUnavailableException">
    ///     Driver IPC unavailable, make sure that at least one compatible
    ///     controller is connected and operational.
    /// </exception>
    /// <returns>A <see cref="SetHostResult" /> containing success (or error) details.</returns>
    /// <remarks>This is synonymous with "pairing" to a new Bluetooth host.</remarks>
    /// <param name="deviceIndex">The one-based device index.</param>
    /// <param name="hostAddress">The new host address.</param>
    /// <exception cref="DsHidMiniInteropInvalidDeviceIndexException">
    ///     The <paramref name="deviceIndex" /> was outside a valid
    ///     range.
    /// </exception>
    /// <exception cref="DsHidMiniInteropConcurrencyException">A different thread is currently performing a data exchange.</exception>
    /// <exception cref="DsHidMiniInteropReplyTimeoutException">The driver didn't respond within an expected period.</exception>
    /// <exception cref="DsHidMiniInteropUnexpectedReplyException">The driver returned unexpected or malformed data.</exception>
    [SuppressMessage("ReSharper", "UnusedMember.Global")]
    public unsafe SetHostResult SetHostAddress(int deviceIndex, PhysicalAddress hostAddress)
    {
        if (_commandMutex is null || _cmdView is null)
        {
            throw new DsHidMiniInteropUnavailableException();
        }

        ValidateDeviceIndex(deviceIndex);

        AcquireCommandLock();

        try
        {
            ref DSHM_IPC_MSG_PAIR_TO_REQUEST request = ref Unsafe.AsRef<DSHM_IPC_MSG_PAIR_TO_REQUEST>(_cmdView);

            request.Header.Type = DSHM_IPC_MSG_TYPE.DSHM_IPC_MSG_TYPE_REQUEST_RESPONSE;
            request.Header.Target = DSHM_IPC_MSG_TARGET.DSHM_IPC_MSG_TARGET_DEVICE;
            request.Header.Command.Device = DSHM_IPC_MSG_CMD_DEVICE.DSHM_IPC_MSG_CMD_DEVICE_PAIR_TO;
            request.Header.TargetIndex = (uint)deviceIndex;
            request.Header.Size = (uint)Marshal.SizeOf<DSHM_IPC_MSG_PAIR_TO_REQUEST>();

            fixed (byte* source = hostAddress.GetAddressBytes())
            fixed (byte* address = request.Address)
            {
                if (source is not null)
                {
                    Buffer.MemoryCopy(source, address, 6, 6);
                }
                else
                {
                    // there might be previous values there we need to zero out
                    Unsafe.InitBlockUnaligned(address, 0, 6);
                }
            }

            if (!SendAndWait())
            {
                throw new DsHidMiniInteropReplyTimeoutException();
            }

            ref DSHM_IPC_MSG_PAIR_TO_REPLY reply = ref Unsafe.AsRef<DSHM_IPC_MSG_PAIR_TO_REPLY>(_cmdView);

            //
            // Plausibility check
            // 
            if (reply.Header.Type == DSHM_IPC_MSG_TYPE.DSHM_IPC_MSG_TYPE_REQUEST_REPLY
                && reply.Header is
                {
                    Target: DSHM_IPC_MSG_TARGET.DSHM_IPC_MSG_TARGET_CLIENT,
                    Command.Device: DSHM_IPC_MSG_CMD_DEVICE.DSHM_IPC_MSG_CMD_DEVICE_PAIR_TO
                }
                && reply.Header.TargetIndex == deviceIndex
                && reply.Header.Size == Marshal.SizeOf<DSHM_IPC_MSG_PAIR_TO_REPLY>())
            {
                return new SetHostResult { WriteStatus = reply.WriteStatus, ReadStatus = reply.ReadStatus };
            }

            throw new DsHidMiniInteropUnexpectedReplyException(ref reply.Header);
        }
        finally
        {
            _commandMutex.ReleaseMutex();
        }
    }

    /// <summary>
    ///     Overwrites the player slot indicator (player LEDs) of the given device.
    /// </summary>
    /// <param name="deviceIndex">The one-based device index.</param>
    /// <param name="playerIndex">The player index to set to. Valid values include 1 to 7.</param>
    /// <exception cref="DsHidMiniInteropUnavailableException">
    ///     Driver IPC unavailable, make sure that at least one compatible
    ///     controller is connected and operational.
    /// </exception>
    /// <returns></returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     The <paramref name="deviceIndex" /> or <paramref name="playerIndex" />
    ///     were out of range.
    /// </exception>
    /// <exception cref="DsHidMiniInteropConcurrencyException">A different thread is currently performing a data exchange.</exception>
    /// <exception cref="DsHidMiniInteropReplyTimeoutException">The driver didn't respond within an expected period.</exception>
    /// <exception cref="DsHidMiniInteropUnexpectedReplyException">The driver returned unexpected or malformed data.</exception>
    [SuppressMessage("ReSharper", "UnusedMember.Global")]
    public unsafe UInt32 SetPlayerIndex(int deviceIndex, byte playerIndex)
    {
        if (_commandMutex is null || _cmdView is null)
        {
            throw new DsHidMiniInteropUnavailableException();
        }

        ValidateDeviceIndex(deviceIndex);

        if (playerIndex is < 1 or > 7)
        {
            throw new ArgumentOutOfRangeException(nameof(playerIndex),
                "Player index must be between (including) 1 and 7.");
        }

        AcquireCommandLock();

        try
        {
            ref DSHM_IPC_MSG_SET_PLAYER_INDEX_REQUEST request =
                ref Unsafe.AsRef<DSHM_IPC_MSG_SET_PLAYER_INDEX_REQUEST>(_cmdView);

            request.Header.Type = DSHM_IPC_MSG_TYPE.DSHM_IPC_MSG_TYPE_REQUEST_RESPONSE;
            request.Header.Target = DSHM_IPC_MSG_TARGET.DSHM_IPC_MSG_TARGET_DEVICE;
            request.Header.Command.Device = DSHM_IPC_MSG_CMD_DEVICE.DSHM_IPC_MSG_CMD_DEVICE_SET_PLAYER_INDEX;
            request.Header.TargetIndex = (uint)deviceIndex;
            request.Header.Size = (uint)Marshal.SizeOf<DSHM_IPC_MSG_SET_PLAYER_INDEX_REQUEST>();

            request.PlayerIndex = playerIndex;

            if (!SendAndWait())
            {
                throw new DsHidMiniInteropReplyTimeoutException();
            }

            ref DSHM_IPC_MSG_SET_PLAYER_INDEX_REPLY reply =
                ref Unsafe.AsRef<DSHM_IPC_MSG_SET_PLAYER_INDEX_REPLY>(_cmdView);

            //
            // Plausibility check
            // 
            if (reply.Header is
                {
                    Type: DSHM_IPC_MSG_TYPE.DSHM_IPC_MSG_TYPE_REQUEST_REPLY,
                    Target: DSHM_IPC_MSG_TARGET.DSHM_IPC_MSG_TARGET_CLIENT,
                    Command.Device: DSHM_IPC_MSG_CMD_DEVICE.DSHM_IPC_MSG_CMD_DEVICE_SET_PLAYER_INDEX
                }
                && reply.Header.TargetIndex == deviceIndex
                && reply.Header.Size == Marshal.SizeOf<DSHM_IPC_MSG_SET_PLAYER_INDEX_REPLY>())
            {
                return reply.NtStatus;
            }

            throw new DsHidMiniInteropUnexpectedReplyException(ref reply.Header);
        }
        finally
        {
            _commandMutex.ReleaseMutex();
        }
    }
}