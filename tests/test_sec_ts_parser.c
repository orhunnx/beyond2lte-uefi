#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../mu-silicium-overlay/Platforms/Samsung/beyond2ltePkg/Drivers/B2LTouchDxe/B2LSecTsParser.h"

static void
BuildCoordinateEvent (
  B2L_U8   event[B2L_SEC_TS_EVENT_SIZE],
  B2L_U8   action,
  B2L_U8   touch_id,
  B2L_U16  x,
  B2L_U16  y,
  B2L_U8   pressure,
  B2L_U8   touch_type,
  B2L_U8   remaining
  )
{
  memset (event, 0, B2L_SEC_TS_EVENT_SIZE);
  event[0] = (B2L_U8)((action << 6) | (touch_id << 2));
  event[1] = (B2L_U8)(x >> 4);
  event[2] = (B2L_U8)(y >> 4);
  event[3] = (B2L_U8)(((x & 0x0F) << 4) | (y & 0x0F));
  event[6] = (B2L_U8)((pressure & 0x3F) | ((touch_type >> 2) << 6));
  event[7] = (B2L_U8)((remaining & 0x3F) | ((touch_type & 0x03) << 6));
}

static void
TestPressMoveRelease (void)
{
  B2L_U8              event[B2L_SEC_TS_EVENT_SIZE];
  B2L_SEC_TS_POINTER  pointer = { 0 };

  BuildCoordinateEvent (event, 1, 2, 0x123, 0xABC, 17, 0, 2);
  assert (B2LSecTsRemainingEvents (event) == 2);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsUpdated);
  assert (pointer.Active == 1);
  assert (pointer.TouchId == 2);
  assert (pointer.X == 0x123);
  assert (pointer.Y == 0xABC);
  assert (pointer.Pressure == 17);

  BuildCoordinateEvent (event, 2, 2, 4095, 0, 0, 0, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsUpdated);
  assert (pointer.X == B2L_SEC_TS_MAX_COORD);
  assert (pointer.Y == 0);
  assert (pointer.Pressure == 1);

  BuildCoordinateEvent (event, 3, 2, 4000, 100, 33, 5, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsUpdated);
  assert (pointer.Active == 0);
  assert (pointer.TouchId == 0);
  assert (pointer.X == 4000);
  assert (pointer.Y == 100);
  assert (pointer.Pressure == 0);
}

static void
TestSinglePointerPolicy (void)
{
  B2L_U8              event[B2L_SEC_TS_EVENT_SIZE];
  B2L_SEC_TS_POINTER  pointer = { 0 };

  BuildCoordinateEvent (event, 1, 1, 10, 20, 5, 3, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsUpdated);

  BuildCoordinateEvent (event, 1, 2, 100, 200, 6, 0, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsIgnored);
  assert (pointer.TouchId == 1);
  assert (pointer.X == 10);

  BuildCoordinateEvent (event, 2, 2, 101, 201, 7, 0, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsIgnored);
  assert (pointer.TouchId == 1);

  BuildCoordinateEvent (event, 3, 1, 11, 21, 0, 3, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsUpdated);

  BuildCoordinateEvent (event, 1, 4, 30, 40, 8, 4, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsUpdated);
}

static void
TestIgnoredAndInvalidEvents (void)
{
  B2L_U8              event[B2L_SEC_TS_EVENT_SIZE] = { 0 };
  B2L_SEC_TS_POINTER  pointer = { 0 };

  event[0] = 1; // Status event, not a coordinate event.
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsIgnored);

  BuildCoordinateEvent (event, 0, 1, 10, 20, 1, 0, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsInvalid);

  BuildCoordinateEvent (event, 1, 0, 10, 20, 1, 0, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsInvalid);

  BuildCoordinateEvent (event, 2, 1, 10, 20, 1, 0, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsIgnored);

  BuildCoordinateEvent (event, 1, 1, 10, 20, 1, 5, 0);
  assert (B2LSecTsParseEvent (event, &pointer) == B2LSecTsIgnored);

  assert (B2LSecTsParseEvent (NULL, &pointer) == B2LSecTsInvalid);
  assert (B2LSecTsParseEvent (event, NULL) == B2LSecTsInvalid);
  assert (B2LSecTsRemainingEvents (NULL) == 0);
}

int
main (void)
{
  TestPressMoveRelease ();
  TestSinglePointerPolicy ();
  TestIgnoredAndInvalidEvents ();
  puts ("sec_ts parser tests: PASS");
  return 0;
}
