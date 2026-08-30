---
title: Sticky Notes ESP-NOW Sender Protocol
nav_order: 12
---

# Build a Sticky Notes sender

Any ESP32 firmware can send a Sticky Notes sleep screen to CrossInk-Sticky;
BrokenSignal-Pro is not required.

## Receiver workflow

1. On the Xteink, open **Menu > Sticky Notes > Receive Note**.
2. The Xteink listens on ESP-NOW channel 1 for 60 seconds.
3. Broadcast the note packet described below.
4. Wait for the Xteink ACK.

There is no persistent pairing and no shared encryption key. The receiver runs
only from the Receive Note screen, so normal deep sleep is not interrupted.

## Text format

Encode one checklist item per UTF-8 line:

```text
[ ] Buy groceries
[ ] Call Alice
[x] Archive invoices
```

- The entire text must be 1-220 UTF-8 bytes. Count bytes, not characters.
- Separate items with one `LF` byte (`0x0A`, `\n`).
- Do not add `LF` or NUL after the final item.
- Prefix an unfinished item with the exact four bytes `[ ] `.
- Prefix a completed item with the exact four bytes `[x] ` (lowercase `x`).
- Replace CR, LF, or tab inside an item's text with a normal space.
- Do not include the date in the text; encode it in the packet header.

A sender may omit completed items to produce an active-tasks-only note.
Ordinary UTF-8 is accepted, but these prefixes and row separators produce the
intended checklist layout.

## Note packet

Use unencrypted ESP-NOW on Wi-Fi channel 1. The packet is exactly
`16 + messageLength` bytes. Multi-byte integers are unsigned little-endian.

| Offset | Size | Value |
| ---: | ---: | --- |
| 0 | 4 | ASCII `CINT` (`43 49 4E 54`) |
| 4 | 1 | Protocol version `1` |
| 5 | 1 | Packet type `1` (note) |
| 6 | 1 | Reserved `0` |
| 7 | 1 | UTF-8 message length, `1`-`220` |
| 8 | 4 | Non-zero sequence number |
| 12 | 2 | Year, `2024`-`2099` |
| 14 | 1 | Month, `1`-`12` |
| 15 | 1 | Valid day for the month and year |
| 16 | N | Exactly N UTF-8 message bytes, without NUL |

Build the header byte-by-byte instead of relying on a packed struct. For
example, encode a 32-bit sequence number as:

```cpp
packet[8]  = sequence;
packet[9]  = sequence >> 8;
packet[10] = sequence >> 16;
packet[11] = sequence >> 24;
```

The receiver rejects a mismatched packet length, zero sequence, invalid date,
malformed UTF-8, embedded NUL, or unsupported control characters. It preserves
`LF` as a row separator and normalizes CR and tab to spaces.

## ACK packet

After rendering and saving succeeds, the Xteink sends this 16-byte unicast
packet to the sender's source MAC:

| Offset | Size | Value |
| ---: | ---: | --- |
| 0 | 4 | ASCII `CINT` |
| 4 | 1 | Protocol version `1` |
| 5 | 1 | Packet type `2` (ACK) |
| 6 | 1 | Reserved `0` |
| 7 | 1 | Message length `0` |
| 8 | 4 | Sequence number copied from the note |
| 12 | 4 | Reserved zeros |

An ESP-NOW send callback confirms only radio delivery. Report success only when
a valid ACK contains the sequence number of the note being sent.

## Radio and retry requirements

- Use `WIFI_STA`, set channel 1, and initialize unencrypted ESP-NOW.
- Add `FF:FF:FF:FF:FF:FF` as an unencrypted broadcast peer on channel 1.
- Register the receive callback before transmitting.
- Use a new random non-zero sequence for each new note.
- Retry the unchanged packet every 250-500 ms until its ACK arrives.
- Allow at least 10 seconds for rendering and storage before timing out.
- Restore the sender's previous Wi-Fi state/channel when finished.

Repeated packets carrying the same sequence and content are retries, not new
notes. ESP-NOW callback signatures differ between Arduino-ESP32 2.x and 3.x;
use the signatures required by the sender project's installed core.
