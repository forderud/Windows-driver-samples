/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    bulkrwr.c

Abstract:

    This file has routines to perform reads and writes.
    The read and writes are targeted bulk to endpoints.

Environment:

    Kernel mode

--*/

#include <osrusbfx2.h>


#if defined(EVENT_TRACING)
#include "bulkrwr.tmh"
#endif

#pragma warning(disable:4267)



VOID
EvtRequestWriteCompletionRoutine(
    _In_ WDFREQUEST                  Request,
    _In_ WDFIOTARGET                 Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
    _In_ WDFCONTEXT                  Context
    )
/*++

Routine Description:

    This is the completion routine for writes
    If the irp completes with success, we check if we
    need to recirculate this irp for another stage of
    transfer.

Arguments:

    Context - Driver supplied context
    Device - Device handle
    Request - Request handle
    Params - request completion params

Return Value:
    None

--*/
{
    NTSTATUS    status;
    size_t      bytesWritten = 0;
    GUID        activity = RequestToActivityId(Request);
    PWDF_USB_REQUEST_COMPLETION_PARAMS usbCompletionParams;

    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Context);

    status = CompletionParams->IoStatus.Status;

    //
    // For usb devices, we should look at the Usb.Completion param.
    //
    usbCompletionParams = CompletionParams->Parameters.Usb.Completion;

    bytesWritten =  usbCompletionParams->Parameters.PipeWrite.Length;

    if (NT_SUCCESS(status)){
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE,
                    "Number of bytes written: %I64d\n", (INT64)bytesWritten);
    } else {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE,
            "Write failed: request Status 0x%x UsbdStatus 0x%x\n",
                status, usbCompletionParams->UsbdStatus);
    }

    //
    // Log write stop event, using IRP activtiy ID if available or request
    // handle otherwise
    //
    EventWriteWriteStop(&activity, 
                        WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)),
                        bytesWritten, 
                        status, 
                        usbCompletionParams->UsbdStatus);


    WdfRequestCompleteWithInformation(Request, status, bytesWritten);

    return;
}
