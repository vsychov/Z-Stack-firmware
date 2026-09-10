// SPDX-License-Identifier: MIT
#define ZNP_BACKHAUL 1
#define ZNP_BOOTSTRAP_HOST_TEST 1
#include <assert.h>
#include <stdio.h>
#include "znp_bootstrap.c"
const uint8_t gMAX_NWK_SEC_MATERIAL_TABLE_ENTRIES = 5;
static struct Nv {
    uint16_t id, index, len;
    uint8_t data[256];
} nv[64];
static unsigned writes, failWrite;
static uint8_t own[8], bound, profile[80], reply[100];
static associated_devices_t devices[16];
static nwkActiveKeyItems active;
static struct Nv *find(uint16_t id, uint16_t idx) {
    for (unsigned i = 0; i < 64; i++)
        if (nv[i].id == id && nv[i].index == idx)
            return &nv[i];
    return NULL;
}
uint16_t osal_nv_item_len(uint16_t id) {
    struct Nv *p = find(id, 0);
    return p ? p->len : 0;
}
uint16_t osal_nv_item_len_ex(uint16_t id, uint16_t idx) {
    struct Nv *p = find(id, idx);
    return p ? p->len : 0;
}
uint8_t osal_nv_item_init_ex(uint16_t id, uint16_t idx, uint16_t len, void *initial) {
    // Mirror NVOCMP's bounds/non-NULL create and OSAL's masked error return.
    if (id > 0x3ff || idx > 0x3ff || !initial || !len)
        return NV_ITEM_UNINIT;
    if (find(id, idx))
        return 0;
    for (unsigned i = 0; i < 64; i++)
        if (!nv[i].id) {
            assert(len <= 256);
            nv[i].id = id;
            nv[i].index = idx;
            nv[i].len = len;
            memcpy(nv[i].data, initial, len);
            return NV_ITEM_UNINIT;
        }
    return 1;
}
uint8_t osal_nv_item_init(uint16_t id, uint16_t len, void *p) {
    return osal_nv_item_init_ex(id, 0, len, p);
}
uint8_t osal_nv_read_ex(uint16_t id, uint16_t idx, uint16_t off, uint16_t len, void *out) {
    struct Nv *p = find(id, idx);
    if (!p || off + len > p->len)
        return 1;
    memcpy(out, p->data + off, len);
    return 0;
}
uint8_t osal_nv_read(uint16_t id, uint16_t off, uint16_t len, void *out) {
    return osal_nv_read_ex(id, 0, off, len, out);
}
uint8_t osal_nv_write_ex(uint16_t id, uint16_t idx, uint16_t len, void *data) {
    if (++writes == failWrite)
        return 1;
    struct Nv *p = find(id, idx);
    if (!p || p->len != len)
        return 1;
    memcpy(p->data, data, len);
    return 0;
}
uint8_t osal_nv_write(uint16_t id, uint16_t len, void *data) {
    return osal_nv_write_ex(id, 0, len, data);
}
static uint8_t fixtureNvRead(NVINTF_itemID_t id, uint16_t off, uint16_t len, void *out) {
    return id.itemID == ZCD_NV_EX_LEGACY ? osal_nv_read(id.subID, off, len, out)
                                         : osal_nv_read_ex(id.itemID, id.subID, off, len, out);
}
static zstack_Config_t config = {{fixtureNvRead}};
zstack_Config_t *pZStackCfg = &config;
#include "bootstrap/network_fixture.inc"
static void reset(void) {
    memset(nv, 0, sizeof(nv));
    memset(devices, 0, sizeof(devices));
    memset(TCLinkKeyRAMEntry, 0, sizeof(TCLinkKeyRAMEntry));
    memset(&_NIB, 0, sizeof(_NIB));
    _NIB.nwkPanId = _NIB.nwkDevAddress = 0xffff;
    writes = failWrite = bound = devState = 0;
    zgDeviceLogicalType = 1;
    memset(own, 0, 8);
    own[0] = 2;
}
static void setupProfile(void) {
    memset(profile, 0, 80);
    profile[0] = 1;
    profile[1] = 15;
    put16(profile + 2, 0xba0e);
    put16(profile + 4, 0x1002);
    profile[6] = 5;
    profile[7] = 8;
    profile[8] = 1;
    profile[16] = 2;
    profile[24] = 3;
    memset(profile + 32, 0x42, 16);
    memset(profile + 48, 0x73, 16);
    put32(profile + 64, 4096);
    put32(profile + 68, 8192);
    put32(profile + 72, 700000);
    put32(profile + 76, 600000);
}
int main(void) {
    setupProfile();
    reset();
    assert(stageProfile(profile) == 0);
    unsigned stageWrites = writes;
    nwkSecMaterialDesc_t previous = {9000, {7}};
    assert(!nvPutEx(ZCD_NV_EX_NWK_SEC_MATERIAL_TABLE, 3, sizeof(previous), &previous));
    writes = 0;
    assert(ZnpBh_restoreBootstrap() == 0);
    unsigned restoreWrites = writes;
    nwkActiveKeyItems key;
    assert(!osal_nv_read(ZCD_NV_NWKKEY, 0, sizeof(key), &key));
    assert(key.frameCounter == 9000 && key.frameCounter != get32(profile + 72));
    assert(!memcmp(key.active.key, profile + 32, 16));
    assert(_NIB.nwkDevAddress == 0x1002 && _NIB.nwkPanId == 0xba0e &&
           !memcmp(own, profile + 16, 8));
    Staged st;
    assert(!osal_nv_read(BOOT_NV, 0, sizeof(st), &st));
    assert(st.state == 2);
    for (unsigned i = 32; i < 64; i++)
        assert(st.profile[i] == 0);
    unsigned before = writes;
    assert(ZnpBh_restoreBootstrap() == 0 && writes == before);
    assert(stageProfile(profile) == BOOT_ALREADY_JOINED);
    // Simulate power loss at every persistent write: no half-applied profile is accepted as
    // committed.
    for (unsigned cut = 1; cut <= stageWrites; cut++) {
        reset();
        failWrite = cut;
        assert(stageProfile(profile) != 0);
        failWrite = 0;
        writes = 0;
        assert(!stageProfile(profile));
        assert(!ZnpBh_restoreBootstrap());
    }
    for (unsigned cut = 1; cut <= restoreWrites; cut++) {
        reset();
        assert(!stageProfile(profile));
        writes = 0;
        failWrite = cut;
        assert(ZnpBh_restoreBootstrap() != 0);
        assert(!osal_nv_read(BOOT_NV, 0, sizeof(st), &st) && st.state == 1);
        failWrite = 0;
        writes = 0;
        memset(&_NIB, 0, sizeof(_NIB));
        assert(!ZnpBh_restoreBootstrap());
        assert(!osal_nv_read(BOOT_NV, 0, sizeof(st), &st) && st.state == 2);
    }
    reset();
    profile[16] = 3;
    assert(stageProfile(profile) == BOOT_PROFILE && writes == 0);
    profile[16] = 2;
    devState = 7;
    assert(stageProfile(profile) == BOOT_ALREADY_JOINED && writes == 0);
    devState = 0;
    bound = 1;
    assert(stageProfile(profile) == BH_BUSY);
    bound = 0;
    put32(profile + 64, 0xfffff001);
    assert(stageProfile(profile) == BOOT_PROFILE);
    setupProfile();
    // Eight unique persistent allocations; reconnect reuses address, ninth cannot consume an
    // association/key.
    reset();
    own[0] = 1;
    devState = 9;
    _NIB.nwkDevAddress = 0;
    _NIB.nwkPanId = 0xba0e;
    _NIB.nwkLogicalChannel = 15;
    _NIB.nwkKeyLoaded = 1;
    _NIB.extendedPANID[0] = 3;
    memset(active.active.key, 0x42, 16);
    active.frameCounter = 700000;
    for (unsigned i = 2; i < 10; i++) {
        uint8_t ieee[8] = {i};
        assert(exportProfile(ieee, reply) == 0);
        assert(get16(reply + 4) == 0x1000 + i);
        assert(!memcmp(reply + 16, ieee, 8));
        assert(get32(reply + 64) == 4096);
        assert(exportProfile(ieee, reply) == 0);
    }
    memset(devices, 0, sizeof(devices));
    uint8_t returning[8] = {2};
    assert(exportProfile(returning, reply) == 0 && get16(reply + 4) == 0x1002);
    uint8_t ninth[8] = {10};
    assert(exportProfile(ninth, reply) == BOOT_CAPACITY);
    assert(!AssocGetWithExt(ninth));
    // Failed storage diagnostics identify the write without exposing record bytes.
    reset();
    setupProfile();
    failWrite = 1;
    assert(ZnpBh_provision(0xc9, 80, profile, reply) == 1 && reply[0] == BOOT_STORAGE);
    assert(ZnpBh_provision(0xcb, 0, NULL, reply) == 9 && reply[0] == 0 && reply[1] == 3);
    assert(get16(reply + 2) == 0 && get16(reply + 4) == ZCD_NV_LOGICAL_TYPE &&
           get16(reply + 6) == 1);
    puts("PASS bootstrap: eight identities, stable addresses, foreign-network protection, local "
         "counter floors, every NV write interrupted/recovered, committed keys omitted from "
         "staging");
}
