#pragma once

/*
 * File: mapping.h
 * Author: Kiarash Mebadi <kiyarash.mebadi@gmail.com>
 * Company: Azarakhsh Maham Shargh
 * Description: Structures and helpers for mapping IEC 61850 paths to Modbus registers.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum { MB_COIL, MB_DI, MB_IREG, MB_HREG } MbType;
typedef enum { CDC_SPS, CDC_DPS, CDC_SPC, CDC_DPC, CDC_MV, CDC_UNKNOWN } CdcType;

typedef struct {
    char iec_path[160];   // e.g. LD0/MMXU1.Pos.stVal or LD0/MMXU1.Amp.mag.f
    char fc[8];           // ST / MX / CO
    CdcType cdc;          // SPS/DPS/SPC/MV
    MbType  mb_type;      // COIL/DI/IREG/HREG
    uint16_t mb_addr;
    uint8_t  mb_unit;
    int      enabled;
    char     desc[128];   // optional human readable description

    // Decomposed IEC path components for model construction
    char ld[40];          // Logical device, e.g. LD0
    char ln[40];          // Logical node, e.g. MMXU1
    char do_name[64];     // Data object name, e.g. Pos or Amp
    char da_path[64];     // Data attribute path, e.g. stVal / mag.f / Oper.ctlVal
    int  bit_index;       // Bit index when the mapping references .bitN, otherwise -1
} MapRow;

typedef struct {
    MapRow* rows;
    size_t  count;
} MapTable;

bool load_mapping_csv(const char* path, MapTable* out_tbl, char* errbuf, size_t errlen);
void free_mapping(MapTable* t);
