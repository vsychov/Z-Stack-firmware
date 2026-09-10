// SPDX-License-Identifier: MIT
#define ZNP_BACKHAUL 1
#define ZNP_BOOTSTRAP_HOST_TEST 1
#define ZNP_BOOTSTRAP_SDK_NV 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <ti/common/nv/nvocmp.h>
#include "znp_bootstrap.c"
#define NV_OPER_FAILED 10
#define NV_BAD_ITEM_LEN 0x0c
static zstack_Config_t config;
zstack_Config_t *pZStackCfg = &config;
#include "sdk-osal-nv.inc"
const uint8_t gMAX_NWK_SEC_MATERIAL_TABLE_ENTRIES = 5;
static uint8_t own[8], bound;
static associated_devices_t devices[16];
static nwkActiveKeyItems active;
#include "bootstrap/network_fixture.inc"
static uint8_t flash[2][8192];
static unsigned erases;
static const char *flashPath;
void NV_LINUX_init(void) {
    FILE *f = fopen(flashPath, "rb");
    if (f) {
        assert(fread(flash, 1, sizeof(flash), f) == sizeof(flash));
        assert(fgetc(f) == EOF);
        assert(!fclose(f));
    } else
        memset(flash, 0xff, sizeof(flash));
}
void NV_LINUX_save(void) {
    FILE *f = fopen(flashPath, "wb");
    assert(f);
    assert(fwrite(flash, 1, sizeof(flash), f) == sizeof(flash));
    assert(!fclose(f));
}
void NV_LINUX_read(uint8_t page, uint16_t offset, void *data, uint16_t len) {
    assert(page < 2 && offset + len <= 8192);
    memcpy(data, flash[page] + offset, len);
}
int32_t NV_LINUX_write(uint8_t page, uint16_t offset, const void *data, uint16_t len) {
    assert(page < 2 && offset + len <= 8192);
    const uint8_t *in = data;
    for (unsigned i = 0; i < len; i++) {
        assert((flash[page][offset + i] & in[i]) == in[i]);
        flash[page][offset + i] &= in[i];
    }
    return 0;
}
int32_t NV_LINUX_erase(uint8_t page) {
    assert(page < 2);
    memset(flash[page], 0xff, 8192);
    ++erases;
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    flashPath = argv[2];
    NVOCMP_loadApiPtrs(&config.nvFps);
    assert(config.nvFps.initNV(NULL) == NVINTF_SUCCESS);
    uint8_t b = 0x42;
    assert(osal_nv_item_init(0x0f11, 1, &b) == NV_ITEM_UNINIT && !osal_nv_item_len(0x0f11));
    assert(osal_nv_item_init(0x03ef, 1, NULL) == NV_ITEM_UNINIT && !osal_nv_item_len(0x03ef));
    assert(osal_nv_write(0x03ef, 1, &b) != SUCCESS);
    // Prove the SDK read wrapper masks NOTFOUND, but bootstrap does not.
    assert(osal_nv_read(0x03ef, 0, 1, &b) == SUCCESS && b == 0x42);
    assert(nvRead(0x03ef, 0, 1, &b) != SUCCESS);
    if (!osal_nv_item_len(0x03ee))
        assert(config.nvFps.createItem((NVINTF_itemID_t){NVINTF_SYSID_ZSTACK, 0, 0x03ee}, 1, &b) ==
               NVINTF_SUCCESS);
    assert(nvRead(0x03ee, 0, 2, &b) != SUCCESS);
    if (!strncmp(argv[1], "master", 6)) {
        own[0] = 1;
        devState = 9;
        _NIB.nwkDevAddress = 0;
        _NIB.nwkPanId = 0xba0e;
        _NIB.nwkLogicalChannel = 15;
        _NIB.nwkKeyLoaded = 1;
        _NIB.extendedPANID[0] = 3;
        memset(active.active.key, 0x42, 16);
        uint8_t profile[80];
        if (!strcmp(argv[1], "master-reboot")) {
            Registry saved;
            assert(!nvRead(REG_NV, 0, sizeof(saved), &saved));
            assert(saved.magic == BOOT_MAGIC);
            for (unsigned i = 0; i < 8; i++)
                assert(saved.peer[i].ieee[0] == i + 2 && saved.peer[i].address == 0x1002 + i);
        }
        for (unsigned i = 2; i < 10; i++) {
            uint8_t ieee[8] = {i};
            uint8_t result = exportProfile(ieee, profile);
            if (result)
                fprintf(stderr, "SDK master export status=%u peer=%u registry_len=%u\n", result, i,
                        osal_nv_item_len(REG_NV));
            assert(!result);
            assert(get16(profile + 4) == 0x1000 + i);
            assert(!memcmp(profile + 16, ieee, 8));
        }
        uint8_t extra[8] = {10};
        assert(exportProfile(extra, profile) == BOOT_CAPACITY && !AssocGetWithExt(extra));
        // Exercise actual append/compaction repeatedly while retaining all eight records.
        unsigned before = erases;
        for (unsigned i = 0; i < 150; i++) {
            uint8_t ieee[8] = {2 + i % 8};
            assert(!exportProfile(ieee, profile));
        }
        assert(erases > before);
        memset(devices, 0, sizeof(devices));
        uint8_t returning[8] = {2};
        assert(!exportProfile(returning, profile) && get16(profile + 4) == 0x1002);
    } else {
        own[0] = 2;
        _NIB.nwkPanId = _NIB.nwkDevAddress = 0xffff;
        zgDeviceLogicalType = 1;
        uint8_t profile[80] = {1, 15};
        put16(profile + 2, 0xba0e);
        put16(profile + 4, 0x1002);
        profile[8] = 1;
        profile[16] = 2;
        profile[24] = 3;
        memset(profile + 32, 0x42, 16);
        memset(profile + 48, 0x73, 16);
        put32(profile + 64, 4096);
        put32(profile + 68, 8192);
        if (!strncmp(argv[1], "satellite-migrate-", 18)) {
            Staged st;
            memset(&st, 0, sizeof(st));
            assert(sizeof(st) == 88);
            if (!strcmp(argv[1], "satellite-migrate-mark")) {
                assert(!nvRead(BOOT_NV, 0, sizeof(st), &st) && st.state == 2);
                st.state = 3;
                assert(!nvPut(BOOT_NV, sizeof(st), &st));
                nwkSecMaterialDesc_t floor = {25000, {3}};
                assert(!nvPutEx(ZCD_NV_EX_NWK_SEC_MATERIAL_TABLE, 3, sizeof(floor), &floor));
                assert(!ZnpBh_restoreBootstrap()); // Radio leaves ESP migration intent intact.
                assert(!nvRead(BOOT_NV, 0, sizeof(st), &st) && st.state == 3);
                return 0;
            }
            if (!strcmp(argv[1], "satellite-migrate-clear")) {
                assert(!nvRead(BOOT_NV, 0, sizeof(st), &st) && st.state == 3);
                // Model zgInit membership/config reset; actual OSAL/NVOCMP deletion.
                uint8_t options = 2, on = 0;
                assert(!nvPut(ZCD_NV_STARTUP_OPTION, 1, &options));
                assert(!nvPut(ZCD_NV_BDBNODEISONANETWORK, 1, &on));
                assert(!osal_nv_delete(ZCD_NV_NIB, osal_nv_item_len(ZCD_NV_NIB)));
                assert(!osal_nv_item_len(ZCD_NV_NIB));
                return 0; // Power loss before final marker deletion.
            }
            if (!strcmp(argv[1], "satellite-migrate-stage")) {
                assert(!osal_nv_item_len(ZCD_NV_NIB));
                assert(!osal_nv_delete(BOOT_NV, sizeof(st)));
                assert(!osal_nv_item_len(BOOT_NV));
                profile[1] = 11;
                put16(profile + 2, 0xbeef);
                profile[24] = 9;
                memset(profile + 32, 0x75, 16);
                assert(!stageProfile(profile));
                return 0;
            }
            assert(!ZnpBh_restoreBootstrap());
            assert(!nvRead(ZCD_NV_NIB, 0, sizeof(_NIB), &_NIB) && _NIB.nwkPanId == 0xbeef &&
                   _NIB.nwkLogicalChannel == 11 && _NIB.extendedPANID[0] == 9);
            assert(!nvRead(BOOT_NV, 0, sizeof(st), &st) && st.state == 2);
            nwkActiveKeyItems key;
            assert(!nvRead(ZCD_NV_NWKKEY, 0, sizeof(key), &key) && key.frameCounter == 25000 &&
                   key.active.key[0] == 0x75);
            return 0;
        }
        if (!strcmp(argv[1], "satellite-stage")) {
            assert(!stageProfile(profile));
            Staged st;
            assert(!nvRead(BOOT_NV, 0, sizeof(st), &st) && st.state == 1);
            return 0;
        }
        if (!strcmp(argv[1], "satellite-restore"))
            assert(!ZnpBh_restoreBootstrap());
        else
            assert(!nvRead(ZCD_NV_NIB, 0, sizeof(_NIB), &_NIB));
        Staged st;
        assert(osal_nv_item_len(BOOT_NV) == sizeof(st));
        assert(!osal_nv_read(BOOT_NV, 0, sizeof(st), &st));
        assert(st.state == 2);
        for (unsigned i = 32; i < 64; i++)
            assert(!st.profile[i]);
        assert(_NIB.nwkDevAddress == 0x1002 && _NIB.nwkPanId == 0xba0e);
        assert(!ZnpBh_restoreBootstrap());
        assert(stageProfile(profile) == BOOT_ALREADY_JOINED);
    }
    return 0;
}
