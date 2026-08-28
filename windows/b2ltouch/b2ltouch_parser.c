#include "b2ltouch_parser.h"

#define SEC_TS_COORDINATE_EVENT          0U
#define SEC_TS_COORDINATE_ACTION_PRESS   1U
#define SEC_TS_COORDINATE_ACTION_MOVE    2U
#define SEC_TS_COORDINATE_ACTION_RELEASE 3U

#define SEC_TS_TOUCHTYPE_NORMAL 0U
#define SEC_TS_TOUCHTYPE_GLOVE  3U
#define SEC_TS_TOUCHTYPE_STYLUS 4U

static int
B2lIsPointerTouchType(B2L_U8 TouchType)
{
    return (TouchType == SEC_TS_TOUCHTYPE_NORMAL) ||
           (TouchType == SEC_TS_TOUCHTYPE_GLOVE) ||
           (TouchType == SEC_TS_TOUCHTYPE_STYLUS);
}

B2L_SEC_TS_PARSE_RESULT
B2lSecTsParseEvent(
    const B2L_U8 Event[B2L_SEC_TS_EVENT_SIZE],
    B2L_SEC_TS_POINTER *Pointer
    )
{
    B2L_U8 Action;
    B2L_U8 EventId;
    B2L_U8 Pressure;
    B2L_U8 TouchId;
    B2L_U8 TouchType;
    B2L_U16 X;
    B2L_U16 Y;

    if ((Event == 0) || (Pointer == 0)) {
        return B2lSecTsInvalid;
    }

    EventId = Event[0] & 0x03U;
    if (EventId != SEC_TS_COORDINATE_EVENT) {
        return B2lSecTsIgnored;
    }

    Action = (Event[0] >> 6) & 0x03U;
    TouchId = (Event[0] >> 2) & 0x0FU;
    TouchType = (B2L_U8)(((Event[6] >> 6) << 2) | (Event[7] >> 6));
    if ((TouchId == 0U) || (Action == 0U)) {
        return B2lSecTsInvalid;
    }

    X = (B2L_U16)(((B2L_U16)Event[1] << 4) | (Event[3] >> 4));
    Y = (B2L_U16)(((B2L_U16)Event[2] << 4) | (Event[3] & 0x0FU));
    Pressure = Event[6] & 0x3FU;

    if (Action == SEC_TS_COORDINATE_ACTION_PRESS) {
        if (Pointer->Active || !B2lIsPointerTouchType(TouchType)) {
            return B2lSecTsIgnored;
        }
        Pointer->Active = 1U;
        Pointer->TouchId = TouchId;
        Pointer->X = X;
        Pointer->Y = Y;
        Pointer->Pressure = (Pressure == 0U) ? 1U : Pressure;
        return B2lSecTsUpdated;
    }

    if (!Pointer->Active || (TouchId != Pointer->TouchId)) {
        return B2lSecTsIgnored;
    }

    if (Action == SEC_TS_COORDINATE_ACTION_RELEASE) {
        Pointer->Active = 0U;
        Pointer->TouchId = 0U;
        Pointer->X = X;
        Pointer->Y = Y;
        Pointer->Pressure = 0U;
        return B2lSecTsUpdated;
    }

    if ((Action == SEC_TS_COORDINATE_ACTION_MOVE) &&
        B2lIsPointerTouchType(TouchType)) {
        Pointer->X = X;
        Pointer->Y = Y;
        Pointer->Pressure = (Pressure == 0U) ? 1U : Pressure;
        return B2lSecTsUpdated;
    }

    return B2lSecTsIgnored;
}

B2L_U8
B2lSecTsRemainingEvents(
    const B2L_U8 Event[B2L_SEC_TS_EVENT_SIZE]
    )
{
    return (Event == 0) ? 0U : (B2L_U8)(Event[7] & 0x3FU);
}
