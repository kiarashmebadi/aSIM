#pragma once

/*
 * File: model_iec.h
 * Author: Kiarash Mebadi <kiyarash.mebadi@gmail.com>
 * Company: Azarakhsh Maham Shargh
 * Description: Declarations for building the dynamic IEC 61850 model and running the server.
 */

#include "iec61850_server.h"

typedef struct {
    IedModel* model;
    IedServer server;
    struct {
        char name[64];
        LogicalDevice* ld;
    } ld_cache[64];
    size_t ld_count;
} ServerCtx;

int build_model_from_icd(ServerCtx* ctx);
int start_server(ServerCtx* ctx, int tcp_port);
void dump_model(IedModel* model); // optional debug helper
