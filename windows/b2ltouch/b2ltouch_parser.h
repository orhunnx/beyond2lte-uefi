#ifndef B2LTOUCH_PARSER_H
#define B2LTOUCH_PARSER_H

#define B2L_SEC_TS_EVENT_SIZE 8U
#define B2L_SEC_TS_MAX_COORD  4095U
#define B2L_SEC_TS_MAX_TOUCH_ID 15U

#define B2L_SEC_TS_ACTION_PRESS   1U
#define B2L_SEC_TS_ACTION_MOVE    2U
#define B2L_SEC_TS_ACTION_RELEASE 3U

typedef unsigned char  B2L_U8;
typedef unsigned short B2L_U16;

typedef enum _B2L_SEC_TS_PARSE_RESULT {
    B2lSecTsIgnored = 0,
    B2lSecTsUpdated,
    B2lSecTsInvalid
} B2L_SEC_TS_PARSE_RESULT;

typedef struct _B2L_SEC_TS_UPDATE {
    B2L_U8  Action;
    B2L_U8  TouchId;
    B2L_U16 X;
    B2L_U16 Y;
    B2L_U8  Pressure;
} B2L_SEC_TS_UPDATE;

B2L_SEC_TS_PARSE_RESULT
B2lSecTsParseEvent(
    const B2L_U8 Event[B2L_SEC_TS_EVENT_SIZE],
    B2L_SEC_TS_UPDATE *Update
    );

B2L_U8
B2lSecTsRemainingEvents(
    const B2L_U8 Event[B2L_SEC_TS_EVENT_SIZE]
    );

#endif
