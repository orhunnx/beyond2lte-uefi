#include "b2lmax77705.h"

// Only the notification worker calls UCM. MfdLock protects producers and
// diagnostic state, never an OS callback. A physical detach is a queue entry,
// not a boolean that a later attach can overwrite.
static UCM_DATA_ROLE
B2lMaxUcmDataRole(UCHAR Role)
{
    return Role == B2L_MAX_DATA_DFP ? UcmDataRoleDfp : UcmDataRoleUfp;
}

static UCM_TYPEC_CURRENT
B2lMaxUcmCurrent(const B2L_MAX_CONNECTOR_EVENT *Event)
{
    // Samsung CCI reports the partner's Rp only while local power role is
    // SINK. Do not invent an advertised source current from charger limits
    // or copy the 1500mA value in the Microsoft example.
    if (Event->PowerRole == B2L_MAX_CC_SINK) {
        switch ((Event->CcStatus0 >> 4) & 3U) {
        case 1U: return UcmTypeCCurrentDefaultUsb;
        case 2U: return UcmTypeCCurrent1500mA;
        case 3U: return UcmTypeCCurrent3000mA;
        default: break;
        }
    }
    return UcmTypeCCurrentInvalid;
}

NTSTATUS
B2lMaxEvtSetDataRole(UCMCONNECTOR Connector, UCM_DATA_ROLE DataRole)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(
        (WDFDEVICE)WdfObjectGetParentObject((WDFOBJECT)Connector));
    UCM_DATA_ROLE Current = UcmDataRoleInvalid;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;

    WdfWaitLockAcquire(Context->MfdLock, NULL);
    if (!Context->ConnectorRunning || !Context->DataRoleValid ||
        !Context->PowerRoleValid || Context->WaterDetected) {
        Status = STATUS_DEVICE_NOT_READY;
    } else {
        Current = B2lMaxUcmDataRole(Context->DataRole);
        if (DataRole == Current) Status = STATUS_SUCCESS;
    }
    Context->ConnectorSwapStatus = Status;
    Context->DiagnosticDirty = TRUE;
    WdfWaitLockRelease(Context->MfdLock);
    // 0x37/APCMD acknowledges a swap request, not the completed DR transition.
    // Until PDMSG/PS_RDY/swap rejection are ported, reject a different role.
    // Never manufacture DATAROLE or return success for an unperformed swap.
    if (NT_SUCCESS(Status)) UcmConnectorDataDirectionChanged(Connector, TRUE, Current);
    return Status;
}

NTSTATUS
B2lMaxCreateConnector(PB2L_MAX_CONTEXT Context)
{
    UCM_CONNECTOR_TYPEC_CONFIG TypeC;
    UCM_CONNECTOR_CONFIG Config;
    WDF_OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    UCM_CONNECTOR_TYPEC_CONFIG_INIT(&TypeC, UcmTypeCOperatingModeDrp,
        UcmTypeCCurrentDefaultUsb);
    TypeC.EvtSetDataRole = B2lMaxEvtSetDataRole;
    UCM_CONNECTOR_CONFIG_INIT(&Config, 0ULL); // one physical connector
    Config.TypeCConfig = &TypeC;
    // PdConfig stays NULL: PD contracts/capabilities are not implemented yet.
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Context->Device;
    Status = UcmConnectorCreate(Context->Device, &Config, &Attributes, &Context->Connector);
    Context->ConnectorStatus = Status;
    return Status;
}

VOID
B2lMaxQueueConnectorLocked(PB2L_MAX_CONTEXT Context)
{
    B2L_MAX_CONNECTOR_EVENT Event;
    ULONG Index;

    if (!Context->ConnectorRunning) return;
    RtlZeroMemory(&Event, sizeof(Event));
    Event.Attached = (BOOLEAN)(Context->PowerRoleValid && Context->DataRoleValid &&
        !Context->WaterDetected && NT_SUCCESS(Context->MfdFault));
    if (Event.Attached) {
        Event.DataRole = Context->DataRole;
        Event.PowerRole = Context->PowerRole;
        Event.CcStatus0 = Context->CcStatus0;
    }
    if ((Event.Attached == Context->ConnectorLast.Attached) &&
        (Event.DataRole == Context->ConnectorLast.DataRole) &&
        (Event.PowerRole == Context->ConnectorLast.PowerRole) &&
        ((Event.CcStatus0 & 0x30U) == (Context->ConnectorLast.CcStatus0 & 0x30U))) return;
    Context->ConnectorLast = Event;
    if (Context->ConnectorCount == B2L_MAX_CONNECTOR_EVENTS) {
        // Lost ordering cannot be reconstructed by status polling. Latch a
        // notification failure and tear down the OS connection in the worker.
        Context->ConnectorOverflow = TRUE;
        Context->ConnectorStatus = STATUS_BUFFER_OVERFLOW;
    } else if (!Context->ConnectorOverflow) {
        Index = (Context->ConnectorRead + Context->ConnectorCount) % B2L_MAX_CONNECTOR_EVENTS;
        Context->ConnectorEvents[Index] = Event;
        ++Context->ConnectorCount;
        ++Context->ConnectorQueued;
    }
    Context->DiagnosticDirty = TRUE;
    WdfWorkItemEnqueue(Context->ConnectorWork);
}

VOID
B2lMaxEvtConnectorWork(WDFWORKITEM WorkItem)
{
    PB2L_MAX_CONTEXT Context = B2lMaxGetContext(
        (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem));
    B2L_MAX_CONNECTOR_EVENT Event;
    UCM_CONNECTOR_TYPEC_ATTACH_PARAMS Params;
    NTSTATUS Status;
    BOOLEAN Attached, Overflow;
    ULONG Count;

    for (Count = 0UL; Count < B2L_MAX_CONNECTOR_EVENTS; ++Count) {
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        Overflow = Context->ConnectorOverflow;
        if (!Context->ConnectorRunning ||
            ((Context->ConnectorCount == 0UL) && !Overflow)) {
            WdfWaitLockRelease(Context->MfdLock);
            return;
        }
        Attached = Context->ConnectorAttached;
        if (Overflow) {
            RtlZeroMemory(&Event, sizeof(Event));
            Context->ConnectorCount = 0UL;
        } else {
            Event = Context->ConnectorEvents[Context->ConnectorRead];
            Context->ConnectorRead = (Context->ConnectorRead + 1UL) % B2L_MAX_CONNECTOR_EVENTS;
            --Context->ConnectorCount;
        }
        WdfWaitLockRelease(Context->MfdLock);

        Status = STATUS_SUCCESS;
        if (!Event.Attached) {
            if (Attached) Status = UcmConnectorTypeCDetach(Context->Connector);
        } else if (!Attached) {
            UCM_CONNECTOR_TYPEC_ATTACH_PARAMS_INIT(&Params,
                Event.DataRole == B2L_MAX_DATA_DFP ? UcmTypeCPartnerUfp : UcmTypeCPartnerDfp);
            Params.CurrentAdvertisement = B2lMaxUcmCurrent(&Event);
            // Unknown current/charging remains unknown, not nominal charging.
            Status = UcmConnectorTypeCAttach(Context->Connector, &Params);
        } else {
            if (Event.DataRole != Context->ConnectorReported.DataRole) {
                UcmConnectorDataDirectionChanged(Context->Connector, TRUE,
                    B2lMaxUcmDataRole(Event.DataRole));
            }
            if ((Event.CcStatus0 & 0x30U) != (Context->ConnectorReported.CcStatus0 & 0x30U)) {
                Status = UcmConnectorTypeCCurrentAdChanged(Context->Connector, B2lMaxUcmCurrent(&Event));
            }
        }
        // Report local power independently of data role. These void methods
        // are notifications, not evidence of URS child/USB enumeration success.
        if (Event.Attached && (Attached || NT_SUCCESS(Status)) &&
            (!Attached || Event.PowerRole != Context->ConnectorReported.PowerRole)) {
            UcmConnectorPowerDirectionChanged(Context->Connector, TRUE,
                Event.PowerRole == B2L_MAX_CC_SOURCE ? UcmPowerRoleSource : UcmPowerRoleSink);
        }
        WdfWaitLockAcquire(Context->MfdLock, NULL);
        if (Attached && Event.Attached) {
            // A failed current-advertisement notification must not erase the
            // independent data/power notifications already sent above.
            Context->ConnectorReported.DataRole = Event.DataRole;
            Context->ConnectorReported.PowerRole = Event.PowerRole;
        }
        if (NT_SUCCESS(Status)) {
            Context->ConnectorAttached = Event.Attached;
            Context->ConnectorReported = Event;
        }
        if (!Event.Attached && !NT_SUCCESS(Status)) {
            // A failed detach is a barrier. Never notify a later attachment
            // as a role change on the connection that failed to close.
            Context->ConnectorOverflow = TRUE;
            Context->ConnectorCount = 0UL;
            Overflow = TRUE;
        }
        Context->ConnectorStatus = !NT_SUCCESS(Status) ? Status :
            Overflow ? STATUS_BUFFER_OVERFLOW : Status;
        ++Context->ConnectorAttempts;
        Context->DiagnosticDirty = TRUE;
        WdfWaitLockRelease(Context->MfdLock);
        if (Overflow) return;
    }
    // Bounded drain; a newly queued event remains ordered behind this batch.
    WdfWorkItemEnqueue(WorkItem);
}

VOID
B2lMaxStopConnector(PB2L_MAX_CONTEXT Context)
{
    NTSTATUS Status = STATUS_SUCCESS;
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    Context->ConnectorRunning = FALSE;
    WdfWaitLockRelease(Context->MfdLock);
    WdfWorkItemFlush(Context->ConnectorWork);
    if (Context->ConnectorAttached) Status = UcmConnectorTypeCDetach(Context->Connector);
    WdfWaitLockAcquire(Context->MfdLock, NULL);
    if (NT_SUCCESS(Status)) Context->ConnectorAttached = FALSE;
    Context->ConnectorStatus = Status;
    Context->ConnectorCount = Context->ConnectorRead = 0UL;
    WdfWaitLockRelease(Context->MfdLock);
}
