// SPDX-License-Identifier: MIT
#define ZNP_BACKHAUL 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "znp_bh.c"
uint8_t ZnpBh_neighbor(uint16_t addr, const uint8_t ieee[8]) {
    (void)addr;
    return ieee[0] ? 0 : BH_ADDRESS;
}
static uint32_t clockMs, confirmCount;
static int locks, failAlloc, failQueue, failConfirm;
static uint8_t confirmedHandle, confirmedStatus, delivered[127], meta[32];
static uint8_t events[32], eventCount;
static BhNetwork network;
#ifdef ZNP_SDK_BROADCAST_FIXTURE
/* Only the ABI shell is modeled. The broadcast branch below is extracted
 * verbatim from the SDK callback used by the firmware build. */
typedef struct {
    struct {
        struct {
            struct {
                struct {
                    uint16_t shortAddr;
                } addr;
            } dstAddr;
        } mac;
        struct {
            uint8_t p[127];
        } msdu;
    } dataInd;
} FixtureCallback;
static uint8_t callbackFreed;
#define SUCCESS 0
static uint8_t nwk_broadcastSend(uint8_t *event) {
    (void)event;
    return failQueue ? 1 : 0;
}
static void MAP_mac_msg_deallocate(uint8_t **event) {
    assert(!callbackFreed);
    callbackFreed = 1;
    ZnpBh_freed(*event - 12, 12);
    free(*event - 12);
    *event = NULL;
}
static void sdkCallback(void *event) {
    FixtureCallback *pData = event, *msgPtr = event;
#include "sdk-broadcast-branch.inc"
    ZnpBh_queue(event, failQueue ? 1 : 0);
}
#endif
uintptr_t ZnpBh_lock(void) {
    return locks++;
}
void ZnpBh_unlock(uintptr_t value) {
    assert(locks);
    locks = value;
}
uint32_t ZnpBh_now(void) {
    return clockMs;
}
void ZnpBh_network(BhNetwork *out) {
    *out = network;
}
void *ZnpBh_allocate(const uint8_t m[32], const uint8_t *raw, uint8_t len, uint32_t timestamp) {
    assert(!locks && timestamp == clockMs);
    memcpy(meta, m, 32);
    memcpy(delivered, raw, len);
    if (failAlloc)
        return NULL;
    void *event = (uint8_t *)calloc(1, 256) + 12;
#ifdef ZNP_SDK_BROADCAST_FIXTURE
    FixtureCallback *p = event;
    p->dataInd.mac.dstAddr.addr.shortAddr = r16(m + 2);
    memcpy(p->dataInd.msdu.p, raw, len);
#endif
    return event;
}
void ZnpBh_deliver(void *event) {
    assert(!locks);
    if (eventCount < sizeof(events))
        events[eventCount++] = 0xc3;
#ifdef ZNP_SDK_BROADCAST_FIXTURE
    callbackFreed = 0;
    sdkCallback(event);
    if (callbackFreed)
        return;
#else
    ZnpBh_queue(event, failQueue ? 1 : 0);
#endif
    ZnpBh_freed((uint8_t *)event - 12, 12);
    free((uint8_t *)event - 12);
}
uint8_t ZnpBh_confirm(uint8_t handle, uint8_t status) {
    assert(!locks);
    if (failConfirm)
        return 1;
    if (eventCount < sizeof(events))
        events[eventCount++] = 0xc4;
    ++confirmCount;
    confirmedHandle = handle;
    confirmedStatus = status;
    return 0;
}
static uint8_t input[200], output[200], frame[39];
static void call(uint8_t cmd, uint8_t len, uint8_t expected) {
    memset(output, 0xa5, sizeof(output));
    ZnpBh_command(cmd, len, input, output);
    assert(output[0] == expected);
    assert(!locks);
}
static void reset(void) {
    memset(peers, 0, sizeof(peers));
    memset(stats, 0, sizeof(stats));
    memset(tracked, 0, sizeof(tracked));
    memset(seen, 0, sizeof(seen));
    memset(input, 0, sizeof(input));
    seenNext = 0;
    timerReady = 1;
    eventCount = 0;
    clockMs = confirmCount = locks = failAlloc = failQueue = failConfirm = 0;
    memset(&network, 0, sizeof(network));
    network.pan = 0xba0e;
    network.channel = 15;
    network.state = 9;
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x48;
    frame[1] = 2;
    frame[4] = 0x91;
    frame[5] = 0xd1;
    frame[6] = 30;
    frame[7] = 1;
    input[0] = 1;
    w16(input + 8, 0x1234);
    w16(input + 10, network.pan);
    input[20] = 1;
    call(0xc1, 28, BH_OK);
    input[8] = 1;
    call(0xc5, 9, BH_OK);
}
static void sendFrame(uint8_t handle, uint16_t destination) {
    uint8_t status = 255;
    assert(ZnpBh_tx(destination, network.pan, handle, 1, frame, sizeof(frame), &status) ==
           (destination != 0xffff));
    assert(!status);
}
static void take(void) {
    call(0xc2, 8, BH_OK);
    assert(output[20] == sizeof(frame));
}
static void finish(uint32_t ticket, uint8_t status) {
    w32(input + 8, ticket);
    input[12] = status;
    call(0xc4, 13, BH_OK);
    ZnpBh_tick();
}
static void receive(uint32_t seq, uint8_t broadcast, uint8_t expected) {
    memset(input + 8, 0, sizeof(input) - 8);
    w32(input + 8, seq);
    input[12] = broadcast;
    w16(input + 13, 0x1234);
    w16(input + 15, broadcast ? 0xffff : network.self);
    w16(input + 17, network.pan);
    input[19] = sizeof(frame);
    memcpy(input + 20, frame, sizeof(frame));
    call(0xc3, 20 + sizeof(frame), expected);
}
static void bindAdditional(unsigned slot) {
    memset(input, 0, sizeof(input));
    input[0] = slot + 1;
    w16(input + 8, 0x1234 + slot);
    w16(input + 10, network.pan);
    input[20] = slot + 1;
    call(0xc1, 28, slot < BH_PEERS ? BH_OK : BH_BUSY);
    if (slot < BH_PEERS) {
        input[8] = 1;
        call(0xc5, 9, BH_OK);
    }
}
static void fanout(void) {
    reset();
    for (unsigned i = 1; i < BH_PEERS; i++)
        bindAdditional(i);
    assert(ZnpBh_bound() == 8);
    bindAdditional(8);
    assert(ZnpBh_bound() == 8);
    sendFrame(99, 0xffff);
    assert(total() == 8);
    for (unsigned i = 0; i < 8; i++) {
        input[0] = i + 1;
        take();
        assert(r32(output + 9) == 1);
        finish(1, 0);
    }
    assert(total() == 0 && confirmCount == 0);
    for (unsigned i = 0; i < 8; i++)
        sendFrame(20 + i, 0x1234 + i);
    assert(total() == 8);
    input[0] = 1;
    input[8] = 0;
    call(0xc5, 9, BH_OK);
    ZnpBh_tick();
    assert(confirmCount == 1 && confirmedStatus == 0xf0 && total() == 7);
    for (unsigned i = 1; i < 8; i++) {
        input[0] = i + 1;
        take();
        finish(2, 0);
        assert(confirmedHandle == 20 + i && !confirmedStatus);
    }
    assert(confirmCount == 8 && !total());
    // A Satellite broadcast fans out to the other seven, with no echo to its origin.
    input[0] = 1;
    input[8] = 1;
    call(0xc5, 9, BH_OK);
    frame[7]++;
    receive(1, 1, BH_OK);
    assert(count(&peers[0]) == 0 && total() == 7);
    receive(2, 1, BH_DUPLICATE);
    assert(total() == 7);
    for (unsigned i = 1; i < 8; i++) {
        input[0] = i + 1;
        take();
        finish(3, 0);
    }
    assert(!total() && confirmCount == 8);
    // Rebinding one peer does not reset another peer's queue, counter or lease.
    sendFrame(70, 0x1235);
    memset(input, 0, sizeof(input));
    input[0] = 33;
    w16(input + 8, 0x1234);
    w16(input + 10, network.pan);
    input[20] = 1;
    call(0xc1, 28, BH_OK);
    assert(count(&peers[1]) == 1 && peers[1].nextId == 4 && peers[1].online);
    input[0] = 1;
    call(0xc2, 8, BH_SESSION);
    input[0] = 2;
    take();
    finish(4, 0);
    assert(confirmedHandle == 70 && !confirmedStatus);
}
int main(void) {
    // C4 completion followed by a reply: enqueue native MAC confirm
    // before C3 can enqueue the APS ACK, with NO timer tick between RPCs.
    reset();
    sendFrame(7, peers[0].peer);
    take();
    w32(input + 8, 1);
    input[12] = 0;
    call(0xc4, 13, BH_OK);
    receive(1, 0, BH_OK);
    assert(eventCount == 2 && events[0] == 0xc4 && events[1] == 0xc3 && confirmCount == 1);
    ZnpBh_tick();
    assert(confirmCount == 1);
    // If native confirm enqueue fails, keep its ticket for the timer and
    // reject an early reply without allocation or falsely acknowledging it.
    reset();
    sendFrame(8, peers[0].peer);
    take();
    failConfirm = 1;
    w32(input + 8, 1);
    input[12] = 0;
    call(0xc4, 13, BH_OK);
    receive(1, 0, BH_BUSY);
    assert(!eventCount && !stats[BH_ALLOC] && count(&peers[0]) == 1);
    failConfirm = 0;
    ZnpBh_tick();
    receive(2, 0, BH_OK);
    assert(eventCount == 2 && events[0] == 0xc4 && events[1] == 0xc3 && confirmCount == 1);
    puts("PASS radio ACK order: native confirm before RX without timer; enqueue failure, bounded "
         "retry and exactly-once completion");
    reset();
    sendFrame(7, peers[0].peer);
    take();
    assert(r32(output + 9) == 1 && r16(output + 14) == 0);
    assert(!memcmp(output + 21, frame, sizeof(frame)));
    finish(1, 0);
    assert(confirmCount == 1 && confirmedHandle == 7 && !confirmedStatus && !count(&peers[0]));
    call(0xc4, 13, BH_DUPLICATE);
    ZnpBh_tick();
    assert(confirmCount == 1);
    reset();
    sendFrame(9, peers[0].peer);
    take();
    finish(1, 1);
    assert(confirmedStatus == 0xf0);
    reset();
    for (unsigned i = 0; i < 4; i++)
        sendFrame(i, peers[0].peer);
    uint8_t status;
    assert(ZnpBh_tx(peers[0].peer, network.pan, 88, 1, frame, 39, &status) && status == 0xf0);
    for (unsigned i = 1; i <= 4; i++) {
        take();
        assert(r32(output + 9) == i);
        finish(i, 0);
    }
    assert(confirmCount == 4 && stats[BH_OVERFLOW] == 1);
    reset();
    sendFrame(5, peers[0].peer);
    clockMs = BH_DEADLINE;
    ZnpBh_tick();
    assert(confirmCount == 1 && confirmedStatus == 0xf0 && stats[BH_TIMEOUT] == 1);
    reset();
    sendFrame(5, peers[0].peer);
    take();
    clockMs = BH_DEADLINE;
    finish(1, 0);
    assert(confirmCount == 1 && confirmedStatus == 0xf0); // Late ACK before timer tick.
    reset();
    sendFrame(6, peers[0].peer);
    input[8] = 0;
    call(0xc5, 9, BH_OK);
    ZnpBh_tick();
    assert(confirmCount == 1 && confirmedStatus == 0xf0);
    assert(ZnpBh_tx(peers[0].peer, network.pan, 6, 1, frame, 39, &status) && status == 0xf0);
    assert(!ZnpBh_tx(0x5678, network.pan, 6, 1, frame, 39, &status));
    reset();
    sendFrame(3, peers[0].peer);
    take();
    failConfirm = 1;
    finish(1, 0);
    assert(!confirmCount && count(&peers[0]) == 1);
    failConfirm = 0;
    ZnpBh_tick();
    assert(confirmCount == 1 && !count(&peers[0]));
    reset();
    sendFrame(3, peers[0].peer);
    input[0] ^= 1;
    call(0xc2, 8, BH_SESSION);
    input[0] ^= 1;
    call(0xc7, 8, BH_BUSY);
    clockMs = 1501;
    ZnpBh_tick();
    call(0xc7, 8, BH_OK);
    assert(confirmedStatus == 0xf0 && !peers[0].bound);
    reset();
    receive(1, 0, BH_OK);
    assert(stats[BH_RX] == 1 && stats[BH_ALLOC] == 1 && stats[BH_FREE] == 1 && !stats[BH_LIVE]);
    assert(!memcmp(delivered, frame, 39) && r16(meta) == peers[0].peer &&
           r16(meta + 2) == peers[0].self);
    receive(1, 0, BH_DUPLICATE);
    receive(2, 0, BH_OK);
    assert(stats[BH_RX] == 2);
    reset();
    failAlloc = 1;
    receive(1, 0, BH_MEMORY);
    assert(!stats[BH_LIVE]);
    failAlloc = 0;
    receive(1, 0, BH_DUPLICATE);
    receive(2, 0, BH_OK);
    reset();
    failQueue = 1;
    receive(1, 0, BH_QUEUE);
    assert(!stats[BH_RX] && stats[BH_ALLOC] == stats[BH_FREE]);
    reset();
    failQueue = 1;
    receive(1, 1, BH_QUEUE);
    assert(stats[BH_QUEUE_FAIL] == 1 && !stats[BH_RX] && stats[BH_ALLOC] == stats[BH_FREE] &&
           !stats[BH_LIVE]);
    reset();
    frame[6] = 1;
    receive(1, 1, BH_OK); // Radius 1 uses the ordinary OSAL queue.
    assert(stats[BH_RX] == 1 && !stats[BH_QUEUE_FAIL] && stats[BH_ALLOC] == stats[BH_FREE]);
    reset();
    receive(1, 1, BH_OK);
    receive(2, 1, BH_DUPLICATE);
    frame[7]++;
    receive(3, 1, BH_OK);
    reset();
    sendFrame(99, 0xffff);
    take();
    finish(1, 0);
    assert(!confirmCount);
    receive(2, 1, BH_DUPLICATE);
    clockMs = 9001;
    input[8] = 1;
    call(0xc5, 9, BH_OK);
    receive(3, 1, BH_OK);
    reset();
    sendFrame(4, peers[0].peer);
    take();
    network.pan++;
    call(0xc4, 13, BH_ADDRESS);
    ZnpBh_tick();
    assert(confirmedStatus == 0xf0);
    reset();
    clockMs = 0xffffff00;
    input[8] = 1;
    call(0xc5, 9, BH_OK);
    sendFrame(8, peers[0].peer);
    clockMs += 999;
    ZnpBh_tick();
    assert(!confirmCount);
    clockMs++;
    ZnpBh_tick();
    assert(confirmCount == 1);
    reset();
    frame[1] = 0;
    assert(ZnpBh_tx(peers[0].peer, network.pan, 1, 1, frame, 39, &status) && status == 0xf0);
    reset();
    assert(ZnpBh_tx(peers[0].peer, network.pan, 1, 0, frame, 39, &status) && status == 0xf0);
    // Every malformed command length, including frames ending at the MT limit,
    // must remain bounded under ASan/UBSan and must not allocate or send a packet.
    reset();
    ZnpBh_timerReady(0);
    call(0xc1, 28, BH_UNSUPPORTED);
    ZnpBh_timerReady(1);
    uint8_t fuzz[255];
    memset(fuzz, 0x55, sizeof(fuzz));
    for (unsigned cmd = 0xc0; cmd <= 0xc7; cmd++)
        for (unsigned len = 0; len < 255; len++) {
            ZnpBh_command(cmd, len, fuzz, output);
            assert(!locks && !stats[BH_LIVE]);
        }
    fanout();
    puts("PASS radio: eight independent sessions, ninth rejected, broadcast fanout, isolated "
         "disconnect/rebind; admission, ownership, queue bounds, deadlines, reconnect guards, "
         "broadcast dedup, malformed inputs");
    return 0;
}
