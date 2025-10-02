#pragma once

/*
 * File: icd_parser.h
 * Author: Kiarash Mebadi <kiyarash.mebadi@gmail.com>
 * Company: Azarakhsh Maham Shargh
 * Description: Public API for parsing SCL/ICD files and exposing structured information.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char do_type_id[64];  // Example: "SPC_DO"
    char cdc[16];         // Example: "SPC"
} DOInfo;

typedef struct {
    char fc[8];          // Example: "ST"
    char bType[32];       // Example: "BOOLEAN"
    char typeId[64];
    uint8_t trgOps;
} DAInfo;

typedef struct {
    char ldInst[64];
    char prefix[64];
    char lnClass[16];
    char lnInst[16];
    char doName[64];
    char daName[64];
    char fc[8];
} FCDAInfo;

typedef struct {
    char ldInst[64];
    char prefix[64];
    char lnClass[16];
    char lnInst[16];
    char lnType[64];
    char lnName[64];
    int isLn0;
} LNInstanceInfo;

bool icd_load(const char* path);

bool icd_find_do_info(const char* lnTypeId, const char* do_name, DOInfo* out);
bool icd_find_da_info(const char* do_type_id, const char* da_path, DAInfo* out);
bool icd_da_exists(const char* do_type_id, const char* da_path);
bool icd_lookup_ln_class(const char* ln_name, char out[16]);
void icd_foreach_da(const char* do_type_id,
                    void (*callback)(const char* path, const DAInfo* info, void* ctx),
                    void* ctx);
void icd_foreach_do(const char* lnTypeId,
                    void (*callback)(const char* doName, const DOInfo* info, void* ctx),
                    void* ctx);
void icd_foreach_ln_instance(void (*callback)(const LNInstanceInfo* info, void* ctx),
                             void* ctx);
const char* icd_get_selected_ied_name(void);
bool icd_set_active_ied(const char* name, const char* accessPoint);
bool icd_get_first_dataset(const char** ldInst, const char** lnName, const char** dsName);
void icd_foreach_dataset(void (*callback)(const char* ldInst, const char* lnName, const char* dsName, void* ctx),
                        void* ctx);
void icd_foreach_dataset_fcda(const char* ldInst, const char* lnName, const char* dsName,
                              void (*callback)(const FCDAInfo* info, void* ctx),
                              void* ctx);
bool icd_find_ln_type_by_name(const char* ldInst, const char* lnName, char lnTypeOut[64]);
bool icd_find_ln_type_by_parts(const char* ldInst, const char* prefix, const char* lnClass, const char* lnInst, char lnTypeOut[64]);

typedef struct {
    char ldInst[64];
    char lnName[64];
    char name[64];
    char dataSet[96];
    char rptId[128];
    uint32_t confRev;
    uint32_t intgPd;
    uint32_t bufTime;
    uint16_t rptEnabledMax;
    uint8_t trgOps;
    uint8_t optFields;
    int buffered;
} ReportControlInfo;

void icd_foreach_report(void (*callback)(const ReportControlInfo* info, void* ctx), void* ctx);

void icd_unload(void);
