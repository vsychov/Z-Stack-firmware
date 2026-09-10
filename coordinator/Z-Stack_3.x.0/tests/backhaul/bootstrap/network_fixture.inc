// SPDX-License-Identifier: MIT
// Network/RF fixture only; persistent storage is supplied by each test.
uint8_t *NLME_GetExtAddr(void) {
    return own;
}
uint8_t ZnpBh_bound(void) {
    return bound;
}
associated_devices_t *AssocGetWithShort(uint16_t addr) {
    for (unsigned i = 0; i < 16; i++)
        if (devices[i].ext[0] && devices[i].shortAddr == addr)
            return &devices[i];
    return NULL;
}
associated_devices_t *AssocGetWithExt(uint8_t *ext) {
    for (unsigned i = 0; i < 16; i++)
        if (devices[i].ext[0] && !memcmp(ext, devices[i].ext, 8))
            return &devices[i];
    return NULL;
}
associated_devices_t *AssocAddNew(uint16_t addr, uint8_t *ext, uint8_t relation) {
    (void)relation;
    for (unsigned i = 0; i < 16; i++)
        if (!devices[i].ext[0]) {
            devices[i].shortAddr = addr;
            memcpy(devices[i].ext, ext, 8);
            return &devices[i];
        }
    return NULL;
}
uint8_t NLME_DirectJoinRequest(uint8_t *ext, uint8_t cap) {
    assert(cap == 0x8e);
    return AssocAddNew(0x1000 + ext[0], ext, NEIGHBOR) ? 0 : 1;
}
uint8_t NLME_DirectJoinRequestWithAddr(uint8_t *ext, uint16_t addr, uint8_t cap) {
    assert(cap == 0x8e);
    return AssocAddNew(addr, ext, NEIGHBOR) ? 0 : 1;
}
void NLME_UpdateNV(uint8_t mask) {
    (void)mask;
}
uint16_t APSME_SearchTCLinkKeyEntry(uint8_t *ext, uint8_t *found, APSME_TCLinkKeyNVEntry_t *out) {
    *found = 0;
    for (unsigned i = 0; i < 16; i++) {
        APSME_TCLinkKeyNVEntry_t e;
        if (nvReadEx(ZCD_NV_EX_TCLK_TABLE, i, 0, sizeof(e), &e) || !e.extAddr[0]) {
            memset(out, 0, sizeof(*out));
            return i;
        }
        if (!memcmp(e.extAddr, ext, 8)) {
            *out = e;
            *found = 1;
            return i;
        }
    }
    return 16;
}
void SSP_ReadNwkActiveKey(nwkActiveKeyItems *out) {
    *out = active;
}
void ZDSecMgrGenerateKeyFromSeed(uint8_t *ext, uint8_t shift, uint8_t *out) {
    for (unsigned i = 0; i < 16; i++)
        out[i] = 0x80 + ext[i % 8] + shift;
}
uint8_t zgReadStartupOptions(void) {
    uint8_t o = 0;
    osal_nv_read(ZCD_NV_STARTUP_OPTION, 0, 1, &o);
    return o;
}
