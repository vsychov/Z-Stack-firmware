# Optional ZNP frame forwarding

`ZNP_BACKHAUL_ENABLE` adds MT commands for virtual neighbours, secured NWK frame
transfer and persistent router configuration.

## Build and scope

Follow [COMPILE.md](COMPILE.md). The setting is **off by default** and only selects
the `znp_LP_CC1352P7_4_tirtos7_ticlang` project. Both coordinator and satellite use
this ZNP variant; the satellite's logical type is router. The ordinary standalone
router application is not the satellite host interface.

The extension supports eight independently leased neighbours, with four pending
frames per neighbour. Broadcasts fan out to the other live neighbours and also
follow the normal local radio path. Unicast addressed to a bound neighbour uses
the host path; unrelated destinations keep the normal stack path. Only ordinary
secured Zigbee NWK data/command frames with short MAC addresses are supported;
MAC security, IEs and Green Power requests are not transported.

A host admission confirmation means the remote radio admitted the frame to its
NWK queue. It does **not** mean that a sensor received or executed a command.
Normal APS acknowledgements remain authoritative. The native MAC confirmation
is queued before admitting a returning APS ACK, including when a confirmation
allocation must be retried.

## Network bootstrap

A fresh satellite receives an 80-byte profile bound to its own factory IEEE address.
The coordinator reserves a stable short address and derives that satellite's trust
centre link key. The satellite stages the profile in NV, then applies it before
BDB commissioning after a radio reset. A partially written profile blocks
commissioning until it can be committed. Existing networks are rejected by `C9`;
any deliberate migration must be managed explicitly by the host. The radio never
clones the coordinator's IEEE address.

Frame-counter floors are preserved across restarts, including existing higher
satellite counters. Counter exhaustion is rejected. Bootstrap storage uses legacy
NV IDs `0x03F0` and `0x03F1`; these must remain reserved when updating the SDK.
The registry has eight entries and no individual removal command in protocol 2.

## MT interface, protocol 2

Commands use MT SYS SREQ (`CMD0=0x21`) and SRSP (`CMD0=0x61`). All multi-byte
integers and IEEE byte arrays use TI's little-endian representation. There are
no native pointers or ABI-dependent structures on the wire. Every reply begins
with a one-byte status; error replies contain only that status unless stated.

| CMD1 | Request payload | Successful response after status |
| --- | --- | --- |
| `C0` status | empty | protocol u8, revision u32, own short u16, PAN u16, channel u8, device state u8, own IEEE[8], extended PAN[8], bound count u8, online count u8, reserved short `FFFF` |
| `C1` bind | nonce[8], peer short u16, PAN u16, extended PAN[8], peer IEEE[8] | empty |
| `C2` take TX | nonce[8] | nonce[8], id u32, broadcast u8, source MAC u16, destination MAC u16, PAN u16, length u8, secured NWK bytes |
| `C3` admit RX | nonce[8], id u32, broadcast u8, source MAC u16, destination MAC u16, PAN u16, length u8, secured NWK bytes | empty |
| `C4` complete TX | nonce[8], id u32, remote admission result u8 (`0` success, other failure) | empty |
| `C5` lease | nonce[8], online u8 (`0` or `1`) | empty |
| `C6` statistics | empty | bound u8, online u8, pending u8, 15 counters u32 in the order below |
| `C7` unbind | nonce[8] | empty; busy while frames or imported allocations remain |
| `C8` export profile | target IEEE[8] | profile[80]; coordinator only |
| `C9` stage profile | profile[80] | empty; unjoined satellite only |
| `CA` bootstrap state | empty | state u8: `0` absent, `1` staged, `2` committed; `3` reserved for host-managed migration |
| `CB` last NV failure | empty | operation u8, item u16, sub-ID u16, length u16, SDK status u8; no stored values |

Counters: TX, accepted, failed, timeout, overflow, broadcast TX, RX, allocation,
free, live, allocation failure, queue failure, duplicate, reject, confirmation retry.
The response has 15 counters (64 bytes including status and header); counter
values are cumulative and may include background traffic. `live` is a gauge.

Status values: `0` OK, `1` empty, `2` length/command, `3` session, `4` busy,
`5` address/network, `6` memory, `7` queue, `8` duplicate, `9` offline,
`10` unavailable timer; bootstrap adds `11` storage, `12` already joined,
`13` counter limit, `14` registry full, `15` coordinator/network, `16` invalid profile.

Session nonces distinguish peer sessions and their command lifetimes.
The host binds a fresh nonce for each peer session and renews its online lease
within **1500 ms**. Pending TX expires after **1000 ms**, independently of the
lease; a 50 ms timer wakes the MT task to complete expired frames. On disconnect,
mark the peer offline and drain completions before unbinding. Failed native
confirmation allocation retains ownership for a later timer retry.

RX IDs must increase strictly within a session, including after a rejected
admission; retries use a new ID. The host rewrites the session nonce for the
receiving radio. It returns the actual admission result via `C4` before delivering
a reverse-direction frame that may contain the APS ACK. Service all peers within
the lease and TX deadlines above.

Broadcast duplicate detection uses NWK source and sequence for nine seconds in a
bounded 64-entry cache. Unicast is not deduplicated by its eight-bit NWK sequence:
ACKs and retransmissions can legitimately reuse it. This cache suppresses loops;
it does not replace NWK security/replay validation.

## Bootstrap profile layout

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 1 | Profile format `1` |
| 1 | 1 | Channel, 11–26 |
| 2 | 2 | PAN ID |
| 4 | 2 | Satellite short address |
| 6 | 1 | Network update ID |
| 7 | 1 | Active network key sequence |
| 8 | 8 | Coordinator IEEE |
| 16 | 8 | Satellite IEEE |
| 24 | 8 | Extended PAN ID |
| 32 | 16 | Active network key |
| 48 | 16 | Satellite-specific trust centre link key |
| 64 | 4 | Satellite NWK TX counter floor |
| 68 | 4 | Satellite APS TX counter floor |
| 72 | 4 | Coordinator NWK counter |
| 76 | 4 | Coordinator APS counter |

Exported satellite floors include a 4096-counter reserve. After a successful
commit, the staging copy's key fields are wiped; the active keys remain in their
normal Zigbee NV records.

## Tests and licensing

See [COMPILE.md](COMPILE.md#radio-regression-tests) for native and SDK-backed tests
of queue ownership, confirmations, timeouts and NV bootstrap.

The new sources and tests use the repository's MIT license. TI SDK components
retain their own notices and license terms.
