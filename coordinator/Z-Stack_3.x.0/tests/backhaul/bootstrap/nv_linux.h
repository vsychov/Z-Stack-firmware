// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef void *NVS_Handle;
typedef struct {
    size_t sectorSize, regionSize;
} NVS_Attrs;
#define NVS_HANDLE ((void *)1)
#define NVOCMP_ASSERT(cond, message) assert(cond);
#define NVOCMP_ALERT(cond, message)                                                                \
    do {                                                                                           \
        (void)(cond);                                                                              \
    } while (0);
#define NVOCMP_FLASHACCESS(err)                                                                    \
    do {                                                                                           \
        (void)(err);                                                                               \
    } while (0);
void NV_LINUX_init(void);
void NV_LINUX_save(void);
void NV_LINUX_read(uint8_t page, uint16_t offset, void *data, uint16_t length);
int32_t NV_LINUX_write(uint8_t page, uint16_t offset, const void *data, uint16_t length);
int32_t NV_LINUX_erase(uint8_t page);
