#include "b2ltouch_parser.h"

#define SEC_TS_COORDINATE_EVENT          0U
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
    B2L_SEC_TS_UPDATE *Update
    )
{
    B2L_U8 Action;
    B2L_U8 EventId;
    B2L_U8 Pressure;
    B2L_U8 TouchId;
    B2L_U8 TouchType;
    B2L_U16 X;
    B2L_U16 Y;

    if ((Event == 0) || (Update == 0)) {
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

    if ((Action != B2L_SEC_TS_ACTION_PRESS) &&
        (Action != B2L_SEC_TS_ACTION_MOVE) &&
        (Action != B2L_SEC_TS_ACTION_RELEASE)) {
        return B2lSecTsIgnored;
    }
    if ((Action != B2L_SEC_TS_ACTION_RELEASE) &&
        !B2lIsPointerTouchType(TouchType)) {
        return B2lSecTsIgnored;
    }

    Update->Action = Action;
    Update->TouchId = TouchId;
    Update->X = X;
    Update->Y = Y;
    Update->Pressure = (Action == B2L_SEC_TS_ACTION_RELEASE) ?
        0U : ((Pressure == 0U) ? 1U : Pressure);
    return B2lSecTsUpdated;
}

B2L_U8
B2lSecTsRemainingEvents(
    const B2L_U8 Event[B2L_SEC_TS_EVENT_SIZE]
    )
{
    return (Event == 0) ? 0U : (B2L_U8)(Event[7] & 0x3FU);
}
