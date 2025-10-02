/*
 * File: main.c
 * Author: Kiarash Mebadi <kiyarash.mebadi@gmail.com>
 * Company: Azarakhsh Maham Shargh
 * Description: Entry point that loads the ICD file, builds the model, and starts the MMS server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "icd_parser.h"
#include "model_iec.h"

#define DEFAULT_PORT 102

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <model.cid> [tcp_port] [--ied NAME] [--ap ACCESSPOINT]\n", argv[0]);
        return 1;
    }

    const char* cid_path = argv[1];

    int tcp_port = DEFAULT_PORT;
    int argi = 2;
    if (argi < argc && argv[argi][0] != '-') {
        tcp_port = atoi(argv[argi]);
        argi++;
    }

    const char* ied_name = NULL;
    const char* ap_name = NULL;
    while (argi < argc) {
        if (strcmp(argv[argi], "--ied") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "Missing value for --ied\n");
                return 1;
            }
            ied_name = argv[argi + 1];
            argi += 2;
        }
        else if (strcmp(argv[argi], "--ap") == 0) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "Missing value for --ap\n");
                return 1;
            }
            ap_name = argv[argi + 1];
            argi += 2;
        }
        else {
            fprintf(stderr, "Unknown argument: %s\n", argv[argi]);
            return 1;
        }
    }

    if (ied_name)
        icd_set_active_ied(ied_name, ap_name);

    if (!icd_load(cid_path)) {
        fprintf(stderr, "❌ Failed to load CID/ICD file: %s\n", cid_path);
        return 3;
    }

    ServerCtx ctx = {0};
    if (build_model_from_icd(&ctx) != 0) {
        fprintf(stderr, "❌ Failed to build model from ICD\n");
        return 4;
    }
    // dump_model(ctx.model); // uncomment for debugging if you need to inspect the model tree

    return start_server(&ctx, tcp_port);
}
