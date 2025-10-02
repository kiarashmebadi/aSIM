# aSIM IEC 61850 Dynamic Server

## Overview
This project builds a **dynamic IEC 61850 server** based on an SCL/ICD file. The
primary goal is to ingest an ICD description, construct the logical device /
logical node hierarchy at runtime, and expose an MMS server that behaves like
the original device. The code is written in C and uses the
[`libiec61850`](https://www.mz-automation.de) SDK.

## Key Features
- 📦 **ICD-driven model creation** (`icd_parser.c`, `model_iec.c`) – logical
  devices, logical nodes, data objects, data attributes, datasets, and report
  control blocks are created dynamically from the SCL file.
- 🧾 **Report Control Blocks** – buffered and unbuffered RCBs are parsed and
  created so that tools such as IEDScout can subscribe to dataset reports.
- 🔁 **Runtime MMS server** (`start_server`) – exposes the generated model over
  the standard 61850 MMS interface. Multiple ICDs can be loaded by selecting
  `--ied` and `--ap` on the command line.
- 🔌 **Modbus mapping groundwork** (`mapping.c`, `mapping.h`) – CSV mapping files
  are parsed so that IEC 61850 attributes can be linked to Modbus coils,
  discrete inputs, and registers. This is the foundation for future
  IEC↔Modbus data exchange.
- 🧪 **Test plan** (`docs/report_test_plan.md`) – step-by-step instructions for
  enabling datasets, activating reports, and validating triggers using the SDK
  sample client or IEDScout.

## Project Structure
```
├── main.c                 # CLI entry point, builds model & starts server
├── model_iec.c/.h         # Dynamic model builder and MMS server wrapper
├── icd_parser.c/.h        # XML parser for ICD/SCL (libxml2 based)
├── mapping.c/.h           # CSV mapping loader for IEC→Modbus links
├── docs/report_test_plan.md
└── README.md              # You are here
```

## Building
```bash
make
```
Prerequisites:
- `libiec61850` SDK (headers + libraries)
- `libxml2`, `jansson`, `pthread`

Edit the `Makefile` if your SDK paths differ.

## Running a Server
```bash
./iec61850_csv_server <ICD file> [tcp_port] [--ied NAME] [--ap ACCESSPOINT]

# Example
./iec61850_csv_server IED_E01MAIN.cid 15000 --ied IED_E01MAIN --ap S1
```
The optional `--ied` / `--ap` filters limit parsing to one device inside a
larger SCL file.

## Testing Reports
Follow `docs/report_test_plan.md` for a detailed walkthrough. In short:
1. Start the server (choose a port >=102 if running as non-root).
2. Use the SDK sample client or IEDScout to enable the desired RCB
   (e.g. `brcbRelayDin`).
3. Modify the dataset members (either programmatically or via IEDScout) and
   observe generated reports.

## Modbus Integration Roadmap
- `mapping.c` already parses CSV rows with IEC paths, FC, CDC, and Modbus
  targets.
- Next steps involve:
  - polling Modbus registers/coils and updating MMS attributes using
    `IedServer_updateAttributeValue`.
  - propagating control commands from MMS to Modbus using the stored mapping.

## Credits
- Author: **Kiarash Mebadi** <kiyarash.mebadi@gmail.com>
- Company: **Azarakhsh Maham Shargh**

Feel free to open issues or PRs if you extend the Modbus bridge or add new ICD
features!
