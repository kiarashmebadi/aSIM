/*
 * File: model_iec.c
 * Author: Kiarash Mebadi <kiyarash.mebadi@gmail.com>
 * Company: Azarakhsh Maham Shargh
 * Description: Builds the dynamic IEC 61850 model and starts the MMS server.
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <stdbool.h>

#include "model_iec.h"
#include "icd_parser.h"

#include "iec61850_common.h"
#include "iec61850_server.h"
#include "iec61850_dynamic_model.h"
#include "iec61850_model.h"

typedef struct {
    char path[128];
    DAInfo info;
} DoDaEntry;

typedef struct {
    DoDaEntry* items;
   size_t count;
   size_t capacity;
} DoDaCollector;

typedef struct {
    ServerCtx* ctx;
    char hostLd[64];
    char hostLn[64];
    char hostLnType[64];
    DataSet* dataset;
} DataSetBuildCtx;

typedef struct {
    ServerCtx* ctx;
    size_t lnCount;
} LnBuildCtx;

typedef struct {
    LogicalNode* ln;
} LnDoBuildCtx;

static LogicalDevice* serverctx_get_ld(ServerCtx* ctx, const char* name)
{
    if (!ctx || !name)
        return NULL;
    for (size_t i = 0; i < ctx->ld_count; ++i) {
        if (strcmp(ctx->ld_cache[i].name, name) == 0)
        {
            return ctx->ld_cache[i].ld;
        }
    }
    return NULL;
}

static LogicalDevice* serverctx_register_ld(ServerCtx* ctx, const char* name)
{
    if (!ctx || !name || !*name)
        return NULL;

    LogicalDevice* existing = serverctx_get_ld(ctx, name);
    if (existing)
    {
        return existing;
    }

    LogicalDevice* ld = LogicalDevice_create(name, ctx->model);
    if (!ld)
        return NULL;

    if (ctx->ld_count < (sizeof(ctx->ld_cache) / sizeof(ctx->ld_cache[0]))) {
        strncpy(ctx->ld_cache[ctx->ld_count].name, name, sizeof(ctx->ld_cache[ctx->ld_count].name) - 1);
        ctx->ld_cache[ctx->ld_count].name[sizeof(ctx->ld_cache[ctx->ld_count].name) - 1] = '\0';
        ctx->ld_cache[ctx->ld_count].ld = ld;
        ctx->ld_count++;
    }

    return ld;
}

static void canonical_ld_name(const char* src, char dest[64])
{
    if (src && *src)
        snprintf(dest, 64, "%s", src);
    else
        snprintf(dest, 64, "LD0");
}

static void append_token(char* dest, size_t destSize, const char* token)
{
    if (!dest || destSize == 0 || !token || !*token)
        return;
    size_t len = strlen(dest);
    if (len >= destSize - 1)
        return;
    size_t remaining = destSize - len - 1;
    size_t copyLen = strlen(token);
    if (copyLen > remaining)
        copyLen = remaining;
    memcpy(dest + len, token, copyLen);
    dest[len + copyLen] = '\0';
}

/* ---------- helpers ---------- */

// model_iec.c


static void collector_reserve(DoDaCollector* col, size_t needed) {
    if (col->capacity >= needed)
        return;
    size_t newCap = col->capacity ? col->capacity * 2 : 16;
    while (newCap < needed)
        newCap *= 2;
    DoDaEntry* items = realloc(col->items, newCap * sizeof(DoDaEntry));
    if (!items) {
        fprintf(stderr, "❌ OOM while expanding DA collector\n");
        return;
    }
    col->items = items;
    col->capacity = newCap;
}

static void collector_add(DoDaCollector* col, const char* path, const DAInfo* info) {
    if (!path || !info)
        return;
    collector_reserve(col, col->count + 1);
    if (col->capacity < col->count + 1)
        return;
    DoDaEntry* entry = &col->items[col->count++];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->info = *info;
}

static void collect_da_callback(const char* path, const DAInfo* info, void* ctx) {
    collector_add((DoDaCollector*)ctx, path, info);
}

static int path_depth(const char* path) {
    int depth = 0;
    if (!path) return depth;
    for (const char* p = path; *p; ++p)
        if (*p == '.') depth++;
    return depth;
}

static int compare_entries_by_depth(const void* a, const void* b) {
    const DoDaEntry* ea = (const DoDaEntry*)a;
    const DoDaEntry* eb = (const DoDaEntry*)b;
    int da = path_depth(ea->path);
    int db = path_depth(eb->path);
    if (da != db)
        return (da < db) ? -1 : 1;
    return strcmp(ea->path, eb->path);
}

static const DoDaEntry* collector_find(const DoDaCollector* col, const char* path) {
    if (!col || !path)
        return NULL;
    for (size_t i = 0; i < col->count; ++i) {
        if (strcmp(col->items[i].path, path) == 0)
            return &col->items[i];
    }
    return NULL;
}

static FunctionalConstraint fc_from_string(const char* fcStr) {
    if (!fcStr || !*fcStr)
        return IEC61850_FC_NONE;
    FunctionalConstraint fc = FunctionalConstraint_fromString(fcStr);
    if (fc == IEC61850_FC_NONE)
        fc = IEC61850_FC_ST;
    return fc;
}

static DataAttributeType type_from_btype(const char* bType) {
    if (!bType || !*bType)
        return IEC61850_CONSTRUCTED;
    if (!strcasecmp(bType, "BOOLEAN")) return IEC61850_BOOLEAN;
    if (!strcasecmp(bType, "INT8")) return IEC61850_INT8;
    if (!strcasecmp(bType, "INT16")) return IEC61850_INT16;
    if (!strcasecmp(bType, "INT32")) return IEC61850_INT32;
    if (!strcasecmp(bType, "INT64")) return IEC61850_INT64;
    if (!strcasecmp(bType, "INT8U")) return IEC61850_INT8U;
    if (!strcasecmp(bType, "INT16U")) return IEC61850_INT16U;
    if (!strcasecmp(bType, "INT24U")) return IEC61850_INT24U;
    if (!strcasecmp(bType, "INT32U")) return IEC61850_INT32U;
    if (!strcasecmp(bType, "FLOAT32")) return IEC61850_FLOAT32;
    if (!strcasecmp(bType, "FLOAT64")) return IEC61850_FLOAT64;
    if (!strcasecmp(bType, "ENUM")) return IEC61850_ENUMERATED;
    if (!strcasecmp(bType, "DBPOS")) return IEC61850_ENUMERATED;
    if (!strcasecmp(bType, "QUALITY")) return IEC61850_QUALITY;
    if (!strcasecmp(bType, "TIMESTAMP")) return IEC61850_TIMESTAMP;
    if (!strcasecmp(bType, "CHECK")) return IEC61850_CHECK;
    if (!strcasecmp(bType, "OCTET64")) return IEC61850_OCTET_STRING_64;
    if (!strcasecmp(bType, "OCTET6")) return IEC61850_OCTET_STRING_6;
    if (!strcasecmp(bType, "OCTET8")) return IEC61850_OCTET_STRING_8;
    if (!strcasecmp(bType, "VISSTRING32")) return IEC61850_VISIBLE_STRING_32;
    if (!strcasecmp(bType, "VISSTRING64")) return IEC61850_VISIBLE_STRING_64;
    if (!strcasecmp(bType, "VISSTRING65")) return IEC61850_VISIBLE_STRING_65;
    if (!strcasecmp(bType, "VISSTRING129")) return IEC61850_VISIBLE_STRING_129;
    if (!strcasecmp(bType, "VISSTRING255")) return IEC61850_VISIBLE_STRING_255;
    if (!strcasecmp(bType, "UNICODE255")) return IEC61850_UNICODE_STRING_255;
    if (!strcasecmp(bType, "ENTRYTIME")) return IEC61850_ENTRY_TIME;
    if (!strcasecmp(bType, "OPTFlds")) return IEC61850_OPTFLDS;
    if (!strcasecmp(bType, "TRGOPS")) return IEC61850_TRGOPS;
    if (!strcasecmp(bType, "OBJREF")) return IEC61850_VISIBLE_STRING_129;
    if (!strcasecmp(bType, "STRUCT")) return IEC61850_CONSTRUCTED;
    return IEC61850_VISIBLE_STRING_255;
}


static LogicalDevice* get_or_create_ld(ServerCtx* ctx, const char* ldName) {
    if (!ctx)
        return NULL;
    char canonical[64];
    canonical_ld_name(ldName, canonical);
    LogicalDevice* ld = serverctx_get_ld(ctx, canonical);
    if (ld)
        return ld;
    return serverctx_register_ld(ctx, canonical);
}

static LogicalNode* get_or_create_ln(LogicalDevice* ld, const char* lnName) {
    LinkedList ch = ModelNode_getChildren((ModelNode*)ld);
    for (LinkedList it = ch; it; it = it->next) {
        LogicalNode* ln = (LogicalNode*) it->data;
        if (!ln)
            continue;
        const char* childName = ModelNode_getName((ModelNode*)ln);
        if (!childName)
            continue;
        if (strcmp(lnName, childName) == 0)
            return ln;
    }
    return LogicalNode_create(lnName, ld);
}

static void ensure_da_path(ModelNode* doNode, const DoDaCollector* col, const DoDaEntry* entry)
{
    if (!entry)
        return;

    char partial[128];
    partial[0] = '\0';
    ModelNode* current = doNode;
    const char* cursor = entry->path;

    while (cursor && *cursor) {
        const char* dot = strchr(cursor, '.');
        size_t tokenLen = dot ? (size_t)(dot - cursor) : strlen(cursor);
        if (tokenLen >= sizeof(partial))
            tokenLen = sizeof(partial) - 1;
        char token[128];
        memcpy(token, cursor, tokenLen);
        token[tokenLen] = '\0';

        if (partial[0])
            append_token(partial, sizeof(partial), ".");
        append_token(partial, sizeof(partial), token);

        const DoDaEntry* meta = collector_find(col, partial);
        if (!meta)
            break;

        bool isLeaf = (dot == NULL);
        DataAttributeType attrType = IEC61850_CONSTRUCTED;
        if (!strcasecmp(meta->info.bType, "Struct"))
            attrType = IEC61850_CONSTRUCTED;
        else if (isLeaf)
            attrType = type_from_btype(meta->info.bType);

        ModelNode* next = ModelNode_getChild(current, token);
        if (!next) {
            FunctionalConstraint fc = fc_from_string(meta->info.fc);
            next = (ModelNode*) DataAttribute_create(token, current, attrType, fc, meta->info.trgOps, 0, 0);
        }

        current = next;
        if (!dot)
            break;
        cursor = dot + 1;
    }
}

static void build_do_from_icd(ModelNode* doNode, const DOInfo* doInfo)
{
    if (!doNode || !doInfo)
        return;

    DoDaCollector col = {0};
    icd_foreach_da(doInfo->do_type_id, collect_da_callback, &col);
    if (col.count == 0) {
        free(col.items);
        return;
    }

    qsort(col.items, col.count, sizeof(DoDaEntry), compare_entries_by_depth);

    for (size_t i = 0; i < col.count; ++i)
        ensure_da_path(doNode, &col, &col.items[i]);

    free(col.items);
}

static ModelNode* ensure_do_from_icd(LogicalNode* ln, const char* do_name, const DOInfo* doInfo)
{
    ModelNode* existing = ModelNode_getChild((ModelNode*)ln, do_name);
    if (existing)
        return existing;

    if (!doInfo)
    {
        fprintf(stderr, "❌ missing DO info for %s\n", do_name);
        return NULL;
    }

    DataObject* newDo = DataObject_create(do_name, (ModelNode*)ln, 0);
    if (!newDo)
    {
        fprintf(stderr, "❌ DataObject_create failed for %s\n", do_name);
        return NULL;
    }

    build_do_from_icd((ModelNode*)newDo, doInfo);
    return (ModelNode*)newDo;
}

static void do_build_callback(const char* doName, const DOInfo* info, void* ctx)
{
    if (!doName || !info || !ctx)
        return;

    LnDoBuildCtx* buildCtx = (LnDoBuildCtx*)ctx;
    ensure_do_from_icd(buildCtx->ln, doName, info);
}

static void ln_instance_callback(const LNInstanceInfo* info, void* ctx)
{
    if (!info || !ctx)
        return;

    LnBuildCtx* buildCtx = (LnBuildCtx*)ctx;

    char ldName[64];
    if (info->ldInst[0])
        snprintf(ldName, sizeof(ldName), "%s", info->ldInst);
    else
        snprintf(ldName, sizeof(ldName), "LD0");

    LogicalDevice* ld = get_or_create_ld(buildCtx->ctx, ldName);
    const char* lnName = info->lnName[0] ? info->lnName : "LLN0";
    LogicalNode* ln = get_or_create_ln(ld, lnName);
    if (!ln) {
        return;
    }

    LnDoBuildCtx doCtx = { .ln = ln };
    icd_foreach_do(info->lnType, do_build_callback, &doCtx);

    buildCtx->lnCount++;
}

static void ld_precreate_callback(const LNInstanceInfo* info, void* ctx)
{
    if (!info || !ctx)
        return;
    ServerCtx* server = (ServerCtx*)ctx;
    char ldName[64];
    canonical_ld_name(info->ldInst, ldName);
    serverctx_register_ld(server, ldName);
}

/* Ensure that DA paths like "stVal" or "Oper.ctlVal" exist under the DO (without manual creation) */
static void compose_ln_from_parts(const char* prefix, const char* lnClass, const char* inst, char out[64])
{
    out[0] = '\0';
    if (prefix && *prefix)
        append_token(out, 64, prefix);
    if (lnClass && *lnClass)
        append_token(out, 64, lnClass);
    if (inst && *inst)
        append_token(out, 64, inst);
}

static void dataset_member_callback(const FCDAInfo* info, void* ctx)
{
    if (!info || !ctx)
        return;

    DataSetBuildCtx* dctx = (DataSetBuildCtx*)ctx;
    char targetLd[64];
    if (info->ldInst[0]) {
        strncpy(targetLd, info->ldInst, sizeof(targetLd) - 1);
        targetLd[sizeof(targetLd) - 1] = '\0';
    }
    else {
        strncpy(targetLd, dctx->hostLd, sizeof(targetLd) - 1);
        targetLd[sizeof(targetLd) - 1] = '\0';
    }
    if (!targetLd[0]) {
        strncpy(targetLd, "LD0", sizeof(targetLd) - 1);
        targetLd[sizeof(targetLd) - 1] = '\0';
    }

    LogicalDevice* ld = get_or_create_ld(dctx->ctx, targetLd);

    char targetLn[64];
    if (info->lnClass[0] || info->lnInst[0] || info->prefix[0]) {
        compose_ln_from_parts(info->prefix, info->lnClass, info->lnInst, targetLn);
    }
    else {
        strncpy(targetLn, dctx->hostLn, sizeof(targetLn) - 1);
        targetLn[sizeof(targetLn) - 1] = '\0';
    }
    if (!targetLn[0]) {
        strncpy(targetLn, dctx->hostLn, sizeof(targetLn) - 1);
        targetLn[sizeof(targetLn) - 1] = '\0';
    }

    LogicalNode* ln = get_or_create_ln(ld, targetLn);

    char targetLnType[64] = {0};
    if (!icd_find_ln_type_by_parts(targetLd, info->prefix, info->lnClass, info->lnInst, targetLnType))
        icd_find_ln_type_by_name(targetLd, targetLn, targetLnType);
    if (!targetLnType[0] && strcmp(targetLd, dctx->hostLd) == 0 && strcmp(targetLn, dctx->hostLn) == 0)
        snprintf(targetLnType, sizeof(targetLnType), "%s", dctx->hostLnType);

    if (info->doName[0]) {
        DOInfo di = {0};
        if (targetLnType[0] && icd_find_do_info(targetLnType, info->doName, &di))
            ensure_do_from_icd(ln, info->doName, &di);
    }

    char variable[256];
    const char* fc = info->fc[0] ? info->fc : "ST";
    snprintf(variable, sizeof(variable), "%s/%s$%s$%s", targetLd, targetLn,
             fc, info->doName[0] ? info->doName : "");

    if (info->daName[0]) {
        char daBuf[128];
        strncpy(daBuf, info->daName, sizeof(daBuf) - 1);
        daBuf[sizeof(daBuf) - 1] = '\0';
        for (char* p = daBuf; *p; ++p)
            if (*p == '.')
                *p = '$';
        if (info->doName[0] == '\0') {
            size_t len = strlen(variable);
            if (len > 0 && variable[len - 1] == '$')
                variable[len - 1] = '\0';
        }
        strncat(variable, "$", sizeof(variable) - strlen(variable) - 1);
        strncat(variable, daBuf, sizeof(variable) - strlen(variable) - 1);
    }

    DataSetEntry_create(dctx->dataset, variable, -1, NULL);
}

static void dataset_callback(const char* ldInst, const char* lnName, const char* dsName, void* ctx)
{
    if (!ctx || !dsName)
        return;

    ServerCtx* server = (ServerCtx*)ctx;
    char hostLd[64];
    if (ldInst && *ldInst)
        snprintf(hostLd, sizeof(hostLd), "%s", ldInst);
    else
        snprintf(hostLd, sizeof(hostLd), "LD0");

    char hostLn[64];
    if (lnName && *lnName)
        snprintf(hostLn, sizeof(hostLn), "%s", lnName);
    else
        snprintf(hostLn, sizeof(hostLn), "LLN0");

    LogicalDevice* ld = get_or_create_ld(server, hostLd);
    LogicalNode* ln = get_or_create_ln(ld, hostLn);
    DataSet* ds = DataSet_create(dsName, ln);
    if (!ds)
        return;

    DataSetBuildCtx buildCtx;
    memset(&buildCtx, 0, sizeof(buildCtx));
    buildCtx.ctx = server;
    snprintf(buildCtx.hostLd, sizeof(buildCtx.hostLd), "%s", hostLd);
    snprintf(buildCtx.hostLn, sizeof(buildCtx.hostLn), "%s", hostLn);
    if (!icd_find_ln_type_by_name(hostLd, hostLn, buildCtx.hostLnType))
        buildCtx.hostLnType[0] = '\0';
    buildCtx.dataset = ds;

    icd_foreach_dataset_fcda(ldInst, lnName, dsName, dataset_member_callback, &buildCtx);
}

static void create_datasets(ServerCtx* ctx)
{
    if (!ctx || !ctx->model)
        return;
    icd_foreach_dataset(dataset_callback, ctx);
}

static void report_callback(const ReportControlInfo* info, void* ctx)
{
    if (!info || !ctx)
        return;

    ServerCtx* server = (ServerCtx*)ctx;

    char ldName[64];
    canonical_ld_name(info->ldInst, ldName);
    LogicalDevice* ld = get_or_create_ld(server, ldName);
    if (!ld)
        return;

    const char* lnName = (info->lnName[0]) ? info->lnName : "LLN0";
    LogicalNode* ln = get_or_create_ln(ld, lnName);
    if (!ln)
        return;

    char datasetRef[256] = {0};
    const char* dataSetStr = NULL;
    if (info->dataSet[0]) {
        if (strchr(info->dataSet, '/'))
            snprintf(datasetRef, sizeof(datasetRef), "%s", info->dataSet);
        else
            snprintf(datasetRef, sizeof(datasetRef), "%s/%s$%s", ldName, lnName, info->dataSet);
        dataSetStr = datasetRef;
    }

    const char* rptIdStr = info->rptId[0] ? info->rptId : NULL;

    ReportControlBlock* rcb = ReportControlBlock_create(info->name,
                                                        ln,
                                                        rptIdStr,
                                                        info->buffered ? true : false,
                                                        dataSetStr,
                                                        info->confRev,
                                                        info->trgOps,
                                                        info->optFields,
                                                        info->bufTime,
                                                        info->intgPd);
    if (!rcb) {
        fprintf(stderr, "❌ Failed to create ReportControlBlock %s\n", info->name);
        return;
    }

    (void)rcb; // suppress unused warning if no further customization
}

static void create_reports(ServerCtx* ctx)
{
    if (!ctx || !ctx->model)
        return;
    icd_foreach_report(report_callback, ctx);
}

/* ---------- Build the dynamic model using the ICD data ---------- */

int build_model_from_icd(ServerCtx* ctx)
{
    if (!ctx)
        return -1;

    const char* iedName = icd_get_selected_ied_name();
    if (!iedName || !*iedName)
        iedName = "DYN_IED";

    ctx->model = IedModel_create(iedName);
    IedModel_setIedNameForDynamicModel(ctx->model, iedName);

    ctx->ld_count = 0;
    icd_foreach_ln_instance(ld_precreate_callback, ctx);

    LnBuildCtx lnCtx = { .ctx = ctx, .lnCount = 0 };
    icd_foreach_ln_instance(ln_instance_callback, &lnCtx);

    create_datasets(ctx);
    create_reports(ctx);

    fprintf(stdout, "ICD build summary: logical-nodes=%zu\n", lnCtx.lnCount);

    return 0;
}

/* ---------- Server bootstrap and processing loop ---------- */

int start_server(ServerCtx* ctx, int tcp_port) {
    ctx->server = IedServer_create(ctx->model);
    IedServer_setServerIdentity(ctx->server, "Dyn-CSV+ICD", "HLK7688A", "v0.3");
    IedServer_startThreadless(ctx->server, tcp_port);

    if (!IedServer_isRunning(ctx->server)) {
        fprintf(stderr, "❌ Failed to start MMS server on port %d\n", tcp_port);
        return -1;
    }

    printf("✅ MMS server listening on TCP %d ...\n", tcp_port);
    while (1) {
        IedServer_processIncomingData(ctx->server);
        IedServer_performPeriodicTasks(ctx->server);
        usleep(50 * 1000);
    }
    return 0;
}

/* ---------- Debug helper: dump the model tree ---------- */

/* Print the model tree so it can be compared with IEDScout */
static void dump_indent(int n){ while(n--) fputc(' ', stdout); }

static void dump_node(ModelNode* n, int depth) {
    dump_indent(depth);
    const char* name = ModelNode_getName(n);
    printf("%s", name ? name : "(null)");

    LinkedList ch = ModelNode_getChildren(n);
    int childCount = 0;
    for (LinkedList it = ch; it; it = it->next)
        childCount++;
    printf("  [children=%d]", childCount);

  //  if (ModelNode_getNodeType(n) == ModelNodeType_DataAttribute) {
        /* Uncomment to show FC of data attributes */
 //       FunctionalConstraint fc = DataAttribute_getFunctionalConstraint((DataAttribute*)n);
  //      printf("  [DA FC=%d]", (int)fc);
//    }
// For compatibility with older libiec61850 versions:
    printf("  [DA or DO]\n");
    putchar('\n');

    for (LinkedList it = ch; it; it = it->next)
        dump_node((ModelNode*)it->data, depth + 2);
}

void dump_model(IedModel* m){
    printf("===== MODEL DUMP =====\n");
    dump_node((ModelNode*)m, 0);
    printf("======================\n");
}

/* Call from main if you need to inspect the model: dump_model(ctx->model); */
