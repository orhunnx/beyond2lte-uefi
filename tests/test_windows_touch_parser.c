#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../windows/b2ltouch/b2ltouch_parser.h"

static void
BuildEvent(
    B2L_U8 Event[B2L_SEC_TS_EVENT_SIZE],
    B2L_U8 Action,
    B2L_U8 TouchId,
    B2L_U16 X,
    B2L_U16 Y,
    B2L_U8 Pressure,
    B2L_U8 TouchType,
    B2L_U8 Remaining
    )
{
    memset(Event, 0, B2L_SEC_TS_EVENT_SIZE);
    Event[0] = (B2L_U8)((Action << 6) | (TouchId << 2));
    Event[1] = (B2L_U8)(X >> 4);
    Event[2] = (B2L_U8)(Y >> 4);
    Event[3] = (B2L_U8)(((X & 0x0F) << 4) | (Y & 0x0F));
    Event[6] = (B2L_U8)((Pressure & 0x3F) | ((TouchType >> 2) << 6));
    Event[7] = (B2L_U8)((Remaining & 0x3F) | ((TouchType & 0x03) << 6));
}

int
main(void)
{
    B2L_U8 Event[B2L_SEC_TS_EVENT_SIZE];
    B2L_SEC_TS_POINTER Pointer = { 0 };

    BuildEvent(Event, 1, 2, 0x123, 0xABC, 17, 0, 31);
    assert(B2lSecTsRemainingEvents(Event) == 31);
    assert(B2lSecTsParseEvent(Event, &Pointer) == B2lSecTsUpdated);
    assert(Pointer.Active == 1 && Pointer.TouchId == 2);
    assert(Pointer.X == 0x123 && Pointer.Y == 0xABC);

    BuildEvent(Event, 2, 2, 4095, 0, 0, 0, 0);
    assert(B2lSecTsParseEvent(Event, &Pointer) == B2lSecTsUpdated);
    assert(Pointer.X == B2L_SEC_TS_MAX_COORD && Pointer.Y == 0);

    BuildEvent(Event, 1, 3, 10, 20, 1, 0, 0);
    assert(B2lSecTsParseEvent(Event, &Pointer) == B2lSecTsIgnored);
    BuildEvent(Event, 3, 2, 4000, 100, 0, 0, 0);
    assert(B2lSecTsParseEvent(Event, &Pointer) == B2lSecTsUpdated);
    assert(Pointer.Active == 0 && Pointer.X == 4000 && Pointer.Y == 100);

    BuildEvent(Event, 0, 1, 10, 20, 1, 0, 0);
    assert(B2lSecTsParseEvent(Event, &Pointer) == B2lSecTsInvalid);
    Event[0] = 1;
    assert(B2lSecTsParseEvent(Event, &Pointer) == B2lSecTsIgnored);
    assert(B2lSecTsParseEvent(NULL, &Pointer) == B2lSecTsInvalid);
    assert(B2lSecTsRemainingEvents(NULL) == 0);

    puts("Windows sec_ts parser tests: PASS");
    return 0;
}
