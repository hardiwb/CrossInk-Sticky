---
title: Calendar Blueprint
nav_order: 14
---

# Calendar blueprint

This document tracks the planned evolution of the receive-only Sticky Notes
extension into an offline calendar for the Xteink X3. The existing ESP-NOW
protocol and dated note files remain the compatibility foundation.

## Product boundary

- The Xteink X3 is the primary hardware target because its DS3231 provides a
  persistent, accurate wall clock.
- The shared X3/X4 firmware may still be distributed, but opening Calendar on
  the original Xteink X4 must show the RTC-required warning and must not start
  Wi-Fi or ESP-NOW.
- Calendar operation is offline. Connectivity runs only when the user starts a
  note sync.
- The first release keeps one synchronized entry per date. Multiple events,
  event times, recurrence, and on-device text editing require a later versioned
  storage and wire format.

## Existing foundation

- ESP-NOW v1/v2 receives a validated date and up to 2048 bytes of UTF-8 text.
- Each date is atomically stored under `/.crosspoint/calendar/YYYY-MM-DD.bin`.
- Re-sending a date replaces that date's entry.
- Calendar sleep-screen layout marks dates with retained entries.
- The generated Sticky Notes bitmap remains selectable as the custom sleep
  image.

## Calendar MVP

1. Open on the X3 RTC's current local date without starting the radio.
2. Navigate days, weeks, and months with logical mapped buttons.
3. Load and display the selected date's retained entry.
4. Provide an explicit Sync action that starts the existing ESP-NOW receiver.
5. Refresh the selected date and month markers after a successful sync.
6. Preserve the existing atomic storage, acknowledgement, and sleep-image
   behavior.

## Twenty-four-segment lock-screen frame

The lock screen may include a compact day-progress frame containing 24 equal
bars. The bars share one outer border instead of appearing as 24 separate
rectangles:

- The frame wraps into two joined rows of 12 bars: hours `00` through `11` on
  top and `12` through `23` on the bottom.
- Thin internal dividers separate the bars while the continuous outer border
  keeps the indicator visually unified.
- Past-hour bars are empty.
- Future-hour bars are black.
- The current-hour bar is black from `:00` through `:29`, then dithered light
  gray from `:30` through `:59`.

The initial implementation should draw a snapshot when the device enters
sleep. It must read time through `HalClock`, draw through the existing renderer,
and use the current sleep-screen refresh without allocating another
framebuffer.

### Optional half-hour refresh

A later opt-in mode may update the bar at every local `:00` and `:30`. This
requires a dedicated minimal timer-wake path:

1. Before deep sleep, calculate the interval to the next half-hour boundary and
   arm an ESP32-C3 timer wake alongside the power-button wake.
2. On a timer wake, initialize only the hardware required for the RTC, SD card,
  framebuffer, and display.
3. Read the DS3231, load the pinned Sticky calendar image, redraw the 24-bar
   frame, and perform one sleep-screen refresh.
4. Schedule the next half-hour boundary and return directly to deep sleep
   without opening Home, a reader, networking, or note reception.

Timer wake must be exposed through `HalPowerManager`; app and sleep-screen code
must not call ESP-IDF sleep APIs directly. The feature must fail closed: if RTC
time is unavailable or invalid, do not schedule a repeating refresh loop.

This mode is disabled by default because it causes up to 48 automatic boot,
SD-access, and e-ink refresh cycles per day. It should not be enabled by default
until physical X3 testing measures the battery impact and confirms acceptable
display ghosting.

## Verification gates

- X3: Calendar opens on the correct local date and manual Sync still receives
  and acknowledges v1 and v2 notes.
- X4: Calendar shows the RTC-required warning; radio initialization never
  occurs.
- Sleep-entry snapshot: the correct hour bar and half-hour shade are shown in
  the unified 24-bar frame.
- Timer wake: no boot or Home UI flashes before returning to sleep.
- Timer wake: power-button wake remains functional at all times.
- Invalid RTC state cannot cause a repeated wake loop.
- Measure sleep current, energy per refresh, and 24-hour battery impact on a
  physical X3 before enabling half-hour refresh by default.
