// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define CONST const
#define SUCCESS 0
#define ZSuccess 0
#define NV_ITEM_UNINIT 9
#define TRUE 1
#define FALSE 0
#define ZG_DEVICETYPE_ROUTER 1
#define DEV_ROUTER 7
#define DEV_ZB_COORD 9
#define NWK_ROUTER 4
#define PARENT 0
#define NEIGHBOR 5
#define DEV_SEC_AUTH_STATUS 8
#define ZG_VERIFIED_KEY 3
#define ZG_UNIQUE_LINK_KEY 2
#define ZCD_STARTOPT_DEFAULT_CONFIG_STATE 1
#define ZCD_STARTOPT_DEFAULT_NETWORK_STATE 2
#define ZCD_STARTOPT_CLEAR_NWK_FRAME_COUNTER 0x80
#define NWK_NV_NIB_ENABLE 1
#define NWK_NV_DEVICELIST_ENABLE 2
#define NWK_NV_ADDRMGR_ENABLE 4
#ifdef ZNP_BOOTSTRAP_SDK_NV
#include <ti/common/nv/nvintf.h>
#include "sdk-nv-ids.inc"
#else
#define ZCD_NV_EX_LEGACY 0
#define NVINTF_SYSID_ZSTACK 1
#define NVINTF_SUCCESS 0
typedef struct {
    uint16_t systemID, itemID, subID;
} NVINTF_itemID_t;
typedef struct {
    uint8_t (*readItem)(NVINTF_itemID_t, uint16_t, uint16_t, void *);
} NVINTF_nvFuncts_t;
enum {
    ZCD_NV_EX_TCLK_TABLE = 10,
    ZCD_NV_NWKKEY,
    ZCD_NV_NWK_ACTIVE_KEY_INFO,
    ZCD_NV_TCLK_JOIN_DEV,
    ZCD_NV_EX_NWK_SEC_MATERIAL_TABLE,
    ZCD_NV_TRUSTCENTER_ADDR,
    ZCD_NV_EXTENDED_PAN_ID,
    ZCD_NV_APS_USE_EXT_PANID,
    ZCD_NV_NIB,
    ZCD_NV_STARTUP_OPTION,
    ZCD_NV_BDBNODEISONANETWORK,
    ZCD_NV_LOGICAL_TYPE,
    ZCD_NV_CONCENTRATOR_ENABLE,
    ZCD_NV_CHANLIST
};
#endif
typedef struct {
    NVINTF_nvFuncts_t nvFps;
} zstack_Config_t;
typedef struct {
    uint16_t nwkDevAddress, nwkPanId, nwkCoordAddress, nwkManagerAddr;
    uint8_t nwkLogicalChannel, nwkUpdateId, nwkKeyLoaded, nwkState, CapabilityFlags, nodeDepth,
        nwkIsConcentrator, beaconOrder, superFrameOrder;
    uint32_t channelList;
    uint8_t nwkCoordExtAddress[8], extendedPANID[8];
} nwkIB_t;
typedef struct {
    uint8_t keySeqNum, key[16];
} nwkKeyDesc;
typedef struct {
    nwkKeyDesc active;
    uint32_t frameCounter;
} nwkActiveKeyItems;
typedef struct {
    uint32_t FrameCounter;
    uint8_t extendedPanID[8];
} nwkSecMaterialDesc_t;
typedef struct {
    uint32_t txFrmCntr, rxFrmCntr;
    uint8_t extAddr[8], keyAttributes, keyType, SeedShift, IcIndex;
} APSME_TCLinkKeyNVEntry_t;
typedef struct {
    uint32_t txFrmCntr, rxFrmCntr;
    uint8_t entryUsed;
} RamKey;
typedef struct {
    uint16_t shortAddr;
    uint8_t ext[8], devStatus, age;
    struct {
        uint32_t inFrmCntr;
        uint8_t inKeySeqNum, txCost, rxLqi;
    } linkInfo;
} associated_devices_t;
static nwkIB_t _NIB;
static uint8_t devState, zgDeviceLogicalType, AIB_apsTrustCenterAddress[8], ZDO_UseExtendedPANID[8];
static RamKey TCLinkKeyRAMEntry[16];
static const unsigned gZDSECMGR_TC_DEVICE_MAX = 16;
uint16_t osal_nv_item_len(uint16_t);
uint16_t osal_nv_item_len_ex(uint16_t, uint16_t);
uint8_t osal_nv_item_init(uint16_t, uint16_t, void *);
uint8_t osal_nv_item_init_ex(uint16_t, uint16_t, uint16_t, void *);
uint8_t osal_nv_read(uint16_t, uint16_t, uint16_t, void *);
uint8_t osal_nv_read_ex(uint16_t, uint16_t, uint16_t, uint16_t, void *);
uint8_t osal_nv_write(uint16_t, uint16_t, void *);
uint8_t osal_nv_write_ex(uint16_t, uint16_t, uint16_t, void *);
uint8_t *NLME_GetExtAddr(void);
associated_devices_t *AssocGetWithShort(uint16_t);
associated_devices_t *AssocGetWithExt(uint8_t *);
associated_devices_t *AssocAddNew(uint16_t, uint8_t *, uint8_t);
uint8_t NLME_DirectJoinRequest(uint8_t *, uint8_t);
uint8_t NLME_DirectJoinRequestWithAddr(uint8_t *, uint16_t, uint8_t);
void NLME_UpdateNV(uint8_t);
uint16_t APSME_SearchTCLinkKeyEntry(uint8_t *, uint8_t *, APSME_TCLinkKeyNVEntry_t *);
void SSP_ReadNwkActiveKey(nwkActiveKeyItems *);
void ZDSecMgrGenerateKeyFromSeed(uint8_t *, uint8_t, uint8_t *);
uint8_t zgReadStartupOptions(void);
