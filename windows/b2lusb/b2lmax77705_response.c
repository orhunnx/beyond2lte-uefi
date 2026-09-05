#include "b2lmax77705.h"

// Samsung beyond2lte 55f0426 max77705_irq_execute: the received opcode
// selects the consumer, independently of the queue node's expected opcode.
// Length includes the opcode and is the actual single mailbox read length.
NTSTATUS
B2lMaxDispatchOpcodeResponseLocked(PB2L_MAX_CONTEXT Context,
    const B2L_MAX_OPCODE_REQUEST *Request, const UCHAR *Response, ULONG Length)
{
    NTSTATUS Status = STATUS_SUCCESS;
    switch (Response[0]) {
    case 0x01U: case 0x03U: case 0x05U: case 0x07U: case 0x09U:
    case 0x0BU: case 0x0DU: case 0x0FU: case 0x11U: case 0x13U:
    case 0x15U: case 0x17U:
        // Stock register reads only trigger a write for OPCODE_UPDATE_SEQ.
        // Our current requests are READ/WRITE pairs, never UPDATE requests.
        break;
    case 0x1CU:
        Context->Control3RawValid = FALSE;
        if (Length < 2UL) { Status = STATUS_BUFFER_TOO_SMALL; break; }
        Context->Control3Raw = Response[1];
        Context->Control3RawValid = TRUE;
        // The matching CONTROL3 owner drives its pending CC-open operation.
        // An unrelated owner's response cannot manufacture a new request.
        break;
    case 0x25U:
        Context->SbuValid = FALSE;
        if (Length < 3UL) { Status = STATUS_BUFFER_TOO_SMALL; break; }
        Context->SbuRaw[0] = Response[1];
        Context->SbuRaw[1] = Response[2];
        Context->SbuValid = TRUE;
        break;
    case 0x30U:
        Status = B2lMaxConsumeCurrentPdoLocked(Context, Response, Length,
            Request->PdRequest != NULL ? Request->PdRequest->Generation : Context->PdGeneration);
        break;
    case 0x32U:
        if (Length < 2UL) { Status = STATUS_BUFFER_TOO_SMALL; break; }
        if (Response[1] == 0xFEU || Response[1] == 0xFFU)
            Context->PdRequestRetry = TRUE;
        // Linux retries a selected PDO on a later actual CCI=3 IRQ. There
        // is no selected-PDO producer yet; current PDO is not that selection.
        break;
    case 0x55U:
        Context->AltModeAcknowledged = TRUE;
        // max77705->set_altmode acknowledges the firmware command, not the
        // manager's battery/USB/DP READY or a negotiated DisplayPort mode.
        break;
    case 0x59U:
        Context->SelftestValid = FALSE;
        if (Length < 4UL) { Status = STATUS_BUFFER_TOO_SMALL; break; }
        Context->SelftestCc = Response[1];
        Context->SelftestSbuRaw[0] = Response[2];
        Context->SelftestSbuRaw[1] = Response[3];
        Context->SelftestValid = TRUE;
        break;
    case 0x70U:
        // Stock HWC1 has GRL enabled. Actual GRL response, even if it differs
        // from the expected opcode, invokes forcetrimi exactly once.
        if (!Context->PolicyEnabled) Status = STATUS_CANCELLED;
        else {
            Status = B2lMaxWriteByte(Context, B2L_MAX_DEBUG_ADDRESS, 0x1FU, 0x04U);
            B2lMaxDelayMicroseconds(100000UL);
            if (!NT_SUCCESS(Status) && NT_SUCCESS(Context->MfdFault)) Context->MfdFault = Status;
        }
        Context->GrlStatus = Status;
        break;
    case 0x21U: case 0x22U: // enabled stock AFC/QC battery consumers
    case 0x31U:             // GET_SRCCAP battery power-list consumer
    case 0x48U: case 0x4BU: // VDM/alternate-mode consumers
    case 0xD1U:             // MAX77705_RAM_TEST consumer
        ++Context->ResponseUnhandled;
        Status = STATUS_NOT_SUPPORTED;
        break;
    default:
        // Includes normal write acknowledgements and 0x5d logging-only on
        // this HWC1 build (CONFIG_DIRECT_CHARGING is not enabled).
        break;
    }
    if (Status == STATUS_BUFFER_TOO_SMALL) ++Context->ResponseShort;
    return Status;
}
