/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    Ioctl.c

Abstract:

    USB device driver for OSR USB-FX2 Learning Kit

Environment:

    Kernel mode only

--*/

#include <osrusbfx2.h>

#if defined(EVENT_TRACING)
#include "ioctl.tmh"
#endif

VOID
OsrFxEvtIoDeviceControl(
    _In_ WDFQUEUE   Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t     OutputBufferLength,
    _In_ size_t     InputBufferLength,
    _In_ ULONG      IoControlCode
    )
/*++

Routine Description:

    This event is called when the framework receives IRP_MJ_DEVICE_CONTROL
    requests from the system.

Arguments:

    Queue - Handle to the framework queue object that is associated
            with the I/O request.
    Request - Handle to a framework request object.

    OutputBufferLength - length of the request's output buffer,
                        if an output buffer is available.
    InputBufferLength - length of the request's input buffer,
                        if an input buffer is available.

    IoControlCode - the driver-defined or system-defined I/O control code
                    (IOCTL) that is associated with the request.
Return Value:

    VOID

--*/
{
    WDFDEVICE           device;
    PDEVICE_CONTEXT     pDevContext;
    size_t              bytesReturned = 0;
    PSWITCH_STATE       switchState = NULL;
    BOOLEAN             requestPending = FALSE;
    NTSTATUS            status = STATUS_INVALID_DEVICE_REQUEST;

    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBufferLength);

    //
    // If your driver is at the top of its driver stack, EvtIoDeviceControl is called
    // at IRQL = PASSIVE_LEVEL.
    //
    _IRQL_limited_to_(PASSIVE_LEVEL);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL, "--> OsrFxEvtIoDeviceControl\n");
    //
    // initialize variables
    //
    device = WdfIoQueueGetDevice(Queue);
    pDevContext = GetDeviceContext(device);

    switch(IoControlCode) {

    case IOCTL_OSRUSBFX2_GET_CONFIG_DESCRIPTOR: {

        PUSB_CONFIGURATION_DESCRIPTOR   configurationDescriptor = NULL;
        USHORT                          requiredSize = 0;

        //
        // First get the size of the config descriptor
        //
        status = WdfUsbTargetDeviceRetrieveConfigDescriptor(
                                    pDevContext->UsbDevice,
                                    NULL,
                                    &requiredSize);

        if (status != STATUS_BUFFER_TOO_SMALL) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                "WdfUsbTargetDeviceRetrieveConfigDescriptor failed 0x%x\n", status);
            break;
        }

        //
        // Get the buffer - make sure the buffer is big enough
        //
        status = WdfRequestRetrieveOutputBuffer(Request,
                                        (size_t)requiredSize,  // MinimumRequired
                                        &configurationDescriptor,
                                        NULL);
        if(!NT_SUCCESS(status)){
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                "WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);
            break;
        }

        status = WdfUsbTargetDeviceRetrieveConfigDescriptor(
                                        pDevContext->UsbDevice,
                                        configurationDescriptor,
                                        &requiredSize);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                "WdfUsbTargetDeviceRetrieveConfigDescriptor failed 0x%x\n", status);
            break;
        }

        bytesReturned = requiredSize;

        }
        break;

    case IOCTL_OSRUSBFX2_RESET_DEVICE:

        status = ResetDevice(device);
        break;

    case IOCTL_OSRUSBFX2_REENUMERATE_DEVICE:

        //
        // Otherwise, call our function to reenumerate the
        //  device
        //
        status = ReenumerateDevice(pDevContext);

        bytesReturned = 0;
        break;

    case IOCTL_OSRUSBFX2_READ_SWITCHES:

        status = WdfRequestRetrieveOutputBuffer(Request,
                            sizeof(SWITCH_STATE),
                            &switchState,
                            NULL);// BufferLength

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                "User's output buffer is too small for this IOCTL, expecting a SWITCH_STATE\n");
            bytesReturned = sizeof(SWITCH_STATE);
            break;

        }

        //
        // Call our routine to get the state of the switches
        //
        status = GetSwitchState(pDevContext, switchState);

        //
        // If successful, return the user their data
        //
        if (NT_SUCCESS(status)) {

            bytesReturned = sizeof(SWITCH_STATE);

        } else {
            //
            // Don't return any data
            //
            bytesReturned = 0;
        }
        break;

    case IOCTL_OSRUSBFX2_GET_INTERRUPT_MESSAGE:

        //
        // Forward the request to an interrupt message queue and dont complete
        // the request until an interrupt from the USB device occurs.
        //
        status = WdfRequestForwardToIoQueue(Request, pDevContext->InterruptMsgQueue);
        if (NT_SUCCESS(status)) {
            requestPending = TRUE;
        }

        break;

    default :
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    if (requestPending == FALSE) {
        WdfRequestCompleteWithInformation(Request, status, bytesReturned);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL, "<-- OsrFxEvtIoDeviceControl\n");

    return;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
ResetPipe(
    _In_ WDFUSBPIPE             Pipe
    )
/*++

Routine Description:

    This routine resets the pipe.

Arguments:

    Pipe - framework pipe handle

Return Value:

    NT status value

--*/
{
    NTSTATUS          status;

    //
    //  This routine synchronously submits a URB_FUNCTION_RESET_PIPE
    // request down the stack.
    //
    status = WdfUsbTargetPipeResetSynchronously(Pipe,
                                WDF_NO_HANDLE, // WDFREQUEST
                                NULL // PWDF_REQUEST_SEND_OPTIONS
                                );

    if (NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL, "ResetPipe - success\n");
        status = STATUS_SUCCESS;
    }
    else {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "ResetPipe - failed\n");
    }

    return status;
}

VOID
StopAllPipes(
    IN PDEVICE_CONTEXT DeviceContext
    )
{
    WdfIoTargetStop(WdfUsbTargetPipeGetIoTarget(DeviceContext->InterruptPipe),
                                 WdfIoTargetCancelSentIo);
}

NTSTATUS
StartAllPipes(
    IN PDEVICE_CONTEXT DeviceContext
    )
{
    NTSTATUS status;

    status = WdfIoTargetStart(WdfUsbTargetPipeGetIoTarget(DeviceContext->InterruptPipe));
    if (!NT_SUCCESS(status)) {
        return status;
    }

    return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
ResetDevice(
    _In_ WDFDEVICE Device
    )
/*++

Routine Description:

    This routine calls WdfUsbTargetDeviceResetPortSynchronously to reset the device if it's still
    connected.

Arguments:

    Device - Handle to a framework device

Return Value:

    NT status value

--*/
{
    PDEVICE_CONTEXT pDeviceContext;
    NTSTATUS status;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL, "--> ResetDevice\n");

    pDeviceContext = GetDeviceContext(Device);

    //
    // A NULL timeout indicates an infinite wake
    //
    status = WdfWaitLockAcquire(pDeviceContext->ResetDeviceWaitLock, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "ResetDevice - could not acquire lock\n");
        return status;
    }

    StopAllPipes(pDeviceContext);

    status = WdfUsbTargetDeviceResetPortSynchronously(pDeviceContext->UsbDevice);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "ResetDevice failed - 0x%x\n", status);
    }

    status = StartAllPipes(pDeviceContext);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL, "Failed to start all pipes - 0x%x\n", status);
    }

    WdfWaitLockRelease(pDeviceContext->ResetDeviceWaitLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_IOCTL, "<-- ResetDevice\n");
    return status;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
ReenumerateDevice(
    _In_ PDEVICE_CONTEXT DevContext
    )
/*++

Routine Description

    This routine re-enumerates the USB device.

Arguments:

    pDevContext - One of our device extensions

Return Value:

    NT status value

--*/
{
    NTSTATUS status;
    WDF_USB_CONTROL_SETUP_PACKET    controlSetupPacket;
    WDF_REQUEST_SEND_OPTIONS        sendOptions;
    GUID                            activity;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,"--> ReenumerateDevice\n");

    WDF_REQUEST_SEND_OPTIONS_INIT(
                                  &sendOptions,
                                  WDF_REQUEST_SEND_OPTION_TIMEOUT
                                  );

    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(
                                         &sendOptions,
                                         DEFAULT_CONTROL_TRANSFER_TIMEOUT
                                         );

    WDF_USB_CONTROL_SETUP_PACKET_INIT_VENDOR(&controlSetupPacket,
                                        BmRequestHostToDevice,
                                        BmRequestToDevice,
                                        USBFX2LK_REENUMERATE, // Request
                                        0, // Value
                                        0); // Index


    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
                                        DevContext->UsbDevice,
                                        WDF_NO_HANDLE, // Optional WDFREQUEST
                                        &sendOptions,
                                        &controlSetupPacket,
                                        NULL, // MemoryDescriptor
                                        NULL); // BytesTransferred

    if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                    "ReenumerateDevice: Failed to Reenumerate - 0x%x \n", status);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,"<-- ReenumerateDevice\n");

    //
    // Send event to eventlog
    //

    activity = DeviceToActivityId(WdfObjectContextGetObject(DevContext));
    EventWriteDeviceReenumerated(&activity,
                                 DevContext->DeviceName,
                                 DevContext->Location,
                                 status);

    return status;

}


_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
GetSwitchState(
    _In_ PDEVICE_CONTEXT DevContext,
    _In_ PSWITCH_STATE SwitchState
    )
/*++

Routine Description

    This routine gets the state of the switches on the board

Arguments:

    DevContext - One of our device extensions

Return Value:

    NT status value

--*/
{
    NTSTATUS status;
    WDF_USB_CONTROL_SETUP_PACKET    controlSetupPacket;
    WDF_REQUEST_SEND_OPTIONS        sendOptions;
    WDF_MEMORY_DESCRIPTOR memDesc;
    ULONG    bytesTransferred;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "--> GetSwitchState\n");

    WDF_REQUEST_SEND_OPTIONS_INIT(
                                  &sendOptions,
                                  WDF_REQUEST_SEND_OPTION_TIMEOUT
                                  );

    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(
                                         &sendOptions,
                                         DEFAULT_CONTROL_TRANSFER_TIMEOUT
                                         );

    WDF_USB_CONTROL_SETUP_PACKET_INIT_VENDOR(&controlSetupPacket,
                                        BmRequestDeviceToHost,
                                        BmRequestToDevice,
                                        USBFX2LK_READ_SWITCHES, // Request
                                        0, // Value
                                        0); // Index

    SwitchState->SwitchesAsUChar = 0;

    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memDesc,
                                    SwitchState,
                                    sizeof(SWITCH_STATE));

    status = WdfUsbTargetDeviceSendControlTransferSynchronously(
                                                DevContext->UsbDevice,
                                                NULL, // Optional WDFREQUEST
                                                &sendOptions,
                                                &controlSetupPacket,
                                                &memDesc,
                                                &bytesTransferred);

    if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                "GetSwitchState: Failed to Get switches - 0x%x \n", status);

    } else {
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL,
            "GetSwitchState: Switch mask is 0x%x\n", SwitchState->SwitchesAsUChar);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_IOCTL, "<-- GetSwitchState\n");

    return status;

}


VOID
OsrUsbIoctlGetInterruptMessage(
    _In_ WDFDEVICE Device,
    _In_ NTSTATUS  ReaderStatus
    )
/*++

Routine Description

    This method handles the completion of the pended request for the IOCTL
    IOCTL_OSRUSBFX2_GET_INTERRUPT_MESSAGE.

Arguments:

    Device - Handle to a framework device.

Return Value:

    None.

--*/
{
    NTSTATUS            status;
    WDFREQUEST          request;
    PDEVICE_CONTEXT     pDevContext;
    size_t              bytesReturned = 0;
    PSWITCH_STATE       switchState = NULL;

    pDevContext = GetDeviceContext(Device);

    do {

        //
        // Check if there are any pending requests in the Interrupt Message Queue.
        // If a request is found then complete the pending request.
        //
        status = WdfIoQueueRetrieveNextRequest(pDevContext->InterruptMsgQueue, &request);

        if (NT_SUCCESS(status)) {
            status = WdfRequestRetrieveOutputBuffer(request,
                                                    sizeof(SWITCH_STATE),
                                                    &switchState,
                                                    NULL);// BufferLength

            if (!NT_SUCCESS(status)) {

                TraceEvents(TRACE_LEVEL_ERROR, DBG_IOCTL,
                    "User's output buffer is too small for this IOCTL, expecting a SWITCH_STATE\n");
                bytesReturned = sizeof(SWITCH_STATE);

            } else {

                //
                // Copy the state information saved by the continuous reader.
                //
                if (NT_SUCCESS(ReaderStatus)) {
                    switchState->SwitchesAsUChar = pDevContext->CurrentSwitchState;
                    bytesReturned = sizeof(SWITCH_STATE);
                } else {
                    bytesReturned = 0;
                }
            }

            //
            // Complete the request.  If we failed to get the output buffer then
            // complete with that status.  Otherwise complete with the status from the reader.
            //
            WdfRequestCompleteWithInformation(request,
                                              NT_SUCCESS(status) ? ReaderStatus : status,
                                              bytesReturned);
            status = STATUS_SUCCESS;

        } else if (status != STATUS_NO_MORE_ENTRIES) {
            KdPrint(("WdfIoQueueRetrieveNextRequest status %08x\n", status));
        }

        request = NULL;

    } while (status == STATUS_SUCCESS);

    return;

}


