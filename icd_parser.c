/*
 * File: icd_parser.c
 * Author: Kiarash Mebadi <kiyarash.mebadi@gmail.com>
 * Company: Azarakhsh Maham Shargh
 * Description: Parses SCL/ICD files and exposes data required to build the dynamic model.
 */

#include "icd_parser.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "iec61850_common.h"

typedef struct DOEntry {
    char lnType[64];
    char lnClass[32];
    char doName[64];
    char doType[64];
    char cdc[16];
    struct DOEntry* next;
} DOEntry;

typedef struct DAEntry {
    char doType[64];
    char daPath[128];  // Example: "Oper.ctlVal" or just "stVal"
    char fc[8];
    char bType[32];
    char typeId[64];
    uint8_t trgOps;
    struct DAEntry* next;
} DAEntry;

typedef struct LNEntry {
    char name[64];
    char lnClass[16];
    struct LNEntry* next;
} LNEntry;


typedef struct LNInstEntry {
    char ldInst[64];
    char prefix[64];
    char lnClass[16];
    char lnInst[16];
    char lnType[64];
    char lnName[64];
    int isLn0;
    struct LNInstEntry* next;
} LNInstEntry;
typedef struct FcdaEntry {
    char ldInst[64];
    char prefix[64];
    char lnClass[16];
    char lnInst[16];
    char doName[64];
    char daName[64];
    char fc[8];
    struct FcdaEntry* next;
} FcdaEntry;

typedef struct DataSetEntryDef {
    char ldInst[64];
    char lnName[64];
    char name[96];
    FcdaEntry* members;
    struct DataSetEntryDef* next;
} DataSetEntryDef;

typedef struct ReportEntry {
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
    struct ReportEntry* next;
} ReportEntry;

static DOEntry* do_list = NULL;
static DAEntry* da_list = NULL;
static LNEntry* ln_list = NULL;
static LNInstEntry* ln_instances = NULL;
static DataSetEntryDef* dataset_list = NULL;
static ReportEntry* report_list = NULL;
static char selected_ied_name[64] = "";
static char selected_ap_name[64] = "";

static void set_selected_ied(const char* name)
{
    if (!name || !*name)
        return;
    if (selected_ied_name[0] == '\0') {
        snprintf(selected_ied_name, sizeof(selected_ied_name), "%s", name);
        selected_ap_name[0] = '\0';
    }
}

static void set_selected_ap(const char* name)
{
    if (!name || !*name)
        return;
    if (selected_ap_name[0] == '\0')
        snprintf(selected_ap_name, sizeof(selected_ap_name), "%s", name);
}

const char* icd_get_selected_ied_name(void)
{
    return selected_ied_name;
}


static xmlNode* find_node(xmlNode* root, const char* name, const char* attr, const char* value) {
    for (xmlNode* node = root; node; node = node->next) {
        if (node->type == XML_ELEMENT_NODE && !xmlStrcmp(node->name, (const xmlChar*)name)) {
            if (attr && value) {
                xmlChar* val = xmlGetProp(node, (const xmlChar*)attr);
                if (val && !xmlStrcmp(val, (const xmlChar*)value)) {
                    xmlFree(val);
                    return node;
                }
                if (val) xmlFree(val);
            } else {
                return node;
            }
        }
    }
    return NULL;
}

static bool da_entry_exists(const char* doType, const char* daPath) {
    for (DAEntry* e = da_list; e; e = e->next) {
        if (!strcmp(e->doType, doType) && !strcmp(e->daPath, daPath))
            return true;
    }
    return false;
}

static bool xml_attr_true(xmlChar* value) {
    if (!value)
        return false;
    bool res = (!xmlStrcmp(value, (const xmlChar*)"true")) || (!xmlStrcmp(value, (const xmlChar*)"TRUE")) ||
               (!xmlStrcmp(value, (const xmlChar*)"1"));
    return res;
}

static void add_da_entry(const char* doType, const char* daPath, const char* fc,
                         const char* bType, const char* typeId, uint8_t trgOps) {
    if (!doType || !daPath)
        return;

    if (da_entry_exists(doType, daPath))
        return;

    DAEntry* e = calloc(1, sizeof(DAEntry));
    strncpy(e->doType, doType, sizeof(e->doType) - 1);
    strncpy(e->daPath, daPath, sizeof(e->daPath) - 1);
    if (fc)
        strncpy(e->fc, fc, sizeof(e->fc) - 1);
    if (bType)
        strncpy(e->bType, bType, sizeof(e->bType) - 1);
    if (typeId)
        strncpy(e->typeId, typeId, sizeof(e->typeId) - 1);
    e->trgOps = trgOps;
    e->next = da_list;
    da_list = e;
}

static void add_ln_entry(const char* name, const char* lnClass) {
    if (!name || !*name || !lnClass || !*lnClass)
        return;

    for (LNEntry* e = ln_list; e; e = e->next) {
        if (!strcmp(e->name, name))
            return;
    }

    LNEntry* e = calloc(1, sizeof(LNEntry));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    strncpy(e->lnClass, lnClass, sizeof(e->lnClass) - 1);
    e->lnClass[sizeof(e->lnClass) - 1] = '\0';
    e->next = ln_list;
    ln_list = e;
}

static void collect_ln_nodes(xmlNode* node) {
    for (xmlNode* cur = node; cur; cur = cur->next) {
        if (cur->type != XML_ELEMENT_NODE)
            continue;

        if (!xmlStrcmp(cur->name, (const xmlChar*)"LN") || !xmlStrcmp(cur->name, (const xmlChar*)"LN0")) {
            xmlChar* prefixAttr = xmlGetProp(cur, (const xmlChar*)"prefix");
            xmlChar* classAttr  = xmlGetProp(cur, (const xmlChar*)"lnClass");
            xmlChar* instAttr   = xmlGetProp(cur, (const xmlChar*)"inst");

            const char* prefix = prefixAttr ? (const char*)prefixAttr : "";
            const char* lnClass = classAttr ? (const char*)classAttr : "";
            const char* inst = instAttr ? (const char*)instAttr : "";

            char name[64];
            if (!xmlStrcmp(cur->name, (const xmlChar*)"LN0"))
                snprintf(name, sizeof(name), "LLN0");
            else
                snprintf(name, sizeof(name), "%s%s%s", prefix, lnClass, inst);

            add_ln_entry(name, lnClass);

            if (prefixAttr) xmlFree(prefixAttr);
            if (classAttr) xmlFree(classAttr);
            if (instAttr) xmlFree(instAttr);
        }

        if (cur->children)
            collect_ln_nodes(cur->children);
    }
}

static void compose_ln_name(bool isLn0, const char* prefix, const char* lnClass, const char* inst, char out[64])
{
    out[0] = '\0';
    if (isLn0) {
        strncpy(out, "LLN0", 63);
        out[63] = '\0';
        return;
    }
    if (prefix && *prefix)
        strncat(out, prefix, 63 - strlen(out));
    if (lnClass && *lnClass)
        strncat(out, lnClass, 63 - strlen(out));
    if (inst && *inst)
        strncat(out, inst, 63 - strlen(out));
}

static uint32_t parse_uint_attr(xmlChar* attr, uint32_t defaultValue)
{
    if (!attr)
        return defaultValue;
    uint32_t value = defaultValue;
    char* end = NULL;
    unsigned long parsed = strtoul((const char*)attr, &end, 10);
    if (end && *end == '\0')
        value = (uint32_t)parsed;
    xmlFree(attr);
    return value;
}

static uint8_t parse_trgops_node(xmlNode* node)
{
    uint8_t trgOps = 0;
    if (!node)
        return trgOps;
    xmlChar* dchgAttr = xmlGetProp(node, (const xmlChar*)"dchg");
    if (xml_attr_true(dchgAttr)) trgOps |= TRG_OPT_DATA_CHANGED;
    if (dchgAttr) xmlFree(dchgAttr);

    xmlChar* qchgAttr = xmlGetProp(node, (const xmlChar*)"qchg");
    if (xml_attr_true(qchgAttr)) trgOps |= TRG_OPT_QUALITY_CHANGED;
    if (qchgAttr) xmlFree(qchgAttr);

    xmlChar* dupdAttr = xmlGetProp(node, (const xmlChar*)"dupd");
    if (xml_attr_true(dupdAttr)) trgOps |= TRG_OPT_DATA_UPDATE;
    if (dupdAttr) xmlFree(dupdAttr);

    xmlChar* periodAttr = xmlGetProp(node, (const xmlChar*)"period");
    if (xml_attr_true(periodAttr)) trgOps |= TRG_OPT_INTEGRITY;
    if (periodAttr) xmlFree(periodAttr);

    xmlChar* giAttr = xmlGetProp(node, (const xmlChar*)"gi");
    if (xml_attr_true(giAttr)) trgOps |= TRG_OPT_GI;
    if (giAttr) xmlFree(giAttr);

    return trgOps;
}

static uint8_t parse_optfields_node(xmlNode* node)
{
    uint8_t opt = 0;
    if (!node)
        return opt;
    xmlChar* seqAttr = xmlGetProp(node, (const xmlChar*)"seqNum");
    if (xml_attr_true(seqAttr)) opt |= RPT_OPT_SEQ_NUM;
    if (seqAttr) xmlFree(seqAttr);

    xmlChar* tsAttr = xmlGetProp(node, (const xmlChar*)"timeStamp");
    if (xml_attr_true(tsAttr)) opt |= RPT_OPT_TIME_STAMP;
    if (tsAttr) xmlFree(tsAttr);

    xmlChar* rcAttr = xmlGetProp(node, (const xmlChar*)"reasonCode");
    if (xml_attr_true(rcAttr)) opt |= RPT_OPT_REASON_FOR_INCLUSION;
    if (rcAttr) xmlFree(rcAttr);

    xmlChar* dsAttr = xmlGetProp(node, (const xmlChar*)"dataSet");
    if (xml_attr_true(dsAttr)) opt |= RPT_OPT_DATA_SET;
    if (dsAttr) xmlFree(dsAttr);

    xmlChar* drAttr = xmlGetProp(node, (const xmlChar*)"dataRef");
    if (xml_attr_true(drAttr)) opt |= RPT_OPT_DATA_REFERENCE;
    if (drAttr) xmlFree(drAttr);

    xmlChar* bufAttr = xmlGetProp(node, (const xmlChar*)"bufOvfl");
    if (xml_attr_true(bufAttr)) opt |= RPT_OPT_BUFFER_OVERFLOW;
    if (bufAttr) xmlFree(bufAttr);

    xmlChar* entryAttr = xmlGetProp(node, (const xmlChar*)"entryID");
    if (xml_attr_true(entryAttr)) opt |= RPT_OPT_ENTRY_ID;
    if (entryAttr) xmlFree(entryAttr);

    xmlChar* confAttr = xmlGetProp(node, (const xmlChar*)"configRef");
    if (xml_attr_true(confAttr)) opt |= RPT_OPT_CONF_REV;
    if (confAttr) xmlFree(confAttr);

    return opt;
}

static void add_report_entry(const char* ldInst, const char* lnName, ReportEntry* entry)
{
    if (ldInst)
        snprintf(entry->ldInst, sizeof(entry->ldInst), "%s", ldInst);
    if (lnName)
        snprintf(entry->lnName, sizeof(entry->lnName), "%s", lnName);
    entry->next = report_list;
    report_list = entry;
}

static void collect_report_control(xmlNode* rcNode, const char* ldInst, const char* lnName)
{
    if (!rcNode)
        return;

    xmlChar* nameAttr = xmlGetProp(rcNode, (const xmlChar*)"name");
    if (!nameAttr)
        return;

    ReportEntry* entry = calloc(1, sizeof(ReportEntry));
    if (!entry) {
        xmlFree(nameAttr);
        return;
    }

    snprintf(entry->name, sizeof(entry->name), "%s", (const char*)nameAttr);
    xmlFree(nameAttr);

    xmlChar* dsAttr = xmlGetProp(rcNode, (const xmlChar*)"datSet");
    if (dsAttr) {
        snprintf(entry->dataSet, sizeof(entry->dataSet), "%s", (const char*)dsAttr);
        xmlFree(dsAttr);
    }

    xmlChar* rptAttr = xmlGetProp(rcNode, (const xmlChar*)"rptID");
    if (rptAttr) {
        snprintf(entry->rptId, sizeof(entry->rptId), "%s", (const char*)rptAttr);
        xmlFree(rptAttr);
    }

    xmlChar* confAttr = xmlGetProp(rcNode, (const xmlChar*)"confRev");
    entry->confRev = parse_uint_attr(confAttr, 0);

    xmlChar* bufAttr = xmlGetProp(rcNode, (const xmlChar*)"buffered");
    entry->buffered = xml_attr_true(bufAttr);
    if (bufAttr) xmlFree(bufAttr);

    xmlChar* intgAttr = xmlGetProp(rcNode, (const xmlChar*)"intgPd");
    entry->intgPd = parse_uint_attr(intgAttr, 0);

    xmlChar* bufTimeAttr = xmlGetProp(rcNode, (const xmlChar*)"bufTime");
    entry->bufTime = parse_uint_attr(bufTimeAttr, 0);
    if (entry->bufTime == 0) {
        xmlChar* bufTmAttr = xmlGetProp(rcNode, (const xmlChar*)"bufTm");
        entry->bufTime = parse_uint_attr(bufTmAttr, 0);
    }

    for (xmlNode* child = rcNode->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(child->name, (const xmlChar*)"TrgOps") == 0)
            entry->trgOps = parse_trgops_node(child);
        else if (xmlStrcmp(child->name, (const xmlChar*)"OptFields") == 0)
            entry->optFields = parse_optfields_node(child);
        else if (xmlStrcmp(child->name, (const xmlChar*)"RptEnabled") == 0) {
            xmlChar* maxAttr = xmlGetProp(child, (const xmlChar*)"max");
            entry->rptEnabledMax = (uint16_t)parse_uint_attr(maxAttr, 0);
        }
    }

    add_report_entry(ldInst, lnName, entry);
}

static void register_ln_instance(const char* ldInst, bool isLn0, const char* prefix,
        const char* lnClass, const char* inst, const char* lnType, const char* lnName)
{
    if (!lnName)
        return;

    LNInstEntry* e = (LNInstEntry*)calloc(1, sizeof(LNInstEntry));
    if (!e)
        return;

    if (ldInst)
        snprintf(e->ldInst, sizeof(e->ldInst), "%s", ldInst);
    if (prefix)
        snprintf(e->prefix, sizeof(e->prefix), "%s", prefix);
    if (lnClass)
        snprintf(e->lnClass, sizeof(e->lnClass), "%s", lnClass);
    if (inst)
        snprintf(e->lnInst, sizeof(e->lnInst), "%s", inst);
    if (lnType)
        snprintf(e->lnType, sizeof(e->lnType), "%s", lnType);
    snprintf(e->lnName, sizeof(e->lnName), "%s", lnName);
    e->isLn0 = isLn0 ? 1 : 0;

    e->next = ln_instances;
    ln_instances = e;
}

static DataSetEntryDef* dataset_create(const char* ldInst, const char* lnName, const char* dsName)
{
    DataSetEntryDef* ds = calloc(1, sizeof(DataSetEntryDef));
    if (!ds)
        return NULL;
    if (ldInst) snprintf(ds->ldInst, sizeof(ds->ldInst), "%s", ldInst);
    if (lnName) snprintf(ds->lnName, sizeof(ds->lnName), "%s", lnName);
    if (dsName) snprintf(ds->name, sizeof(ds->name), "%s", dsName);
    ds->next = dataset_list;
    dataset_list = ds;
    return ds;
}

static void dataset_add_fcda(DataSetEntryDef* ds, const char* ldInst, const char* prefix,
        const char* lnClass, const char* lnInst, const char* doName, const char* daName, const char* fc)
{
    if (!ds)
        return;
    FcdaEntry* entry = calloc(1, sizeof(FcdaEntry));
    if (!entry)
        return;
    if (ldInst) snprintf(entry->ldInst, sizeof(entry->ldInst), "%s", ldInst);
    if (prefix) snprintf(entry->prefix, sizeof(entry->prefix), "%s", prefix);
    if (lnClass) snprintf(entry->lnClass, sizeof(entry->lnClass), "%s", lnClass);
    if (lnInst) snprintf(entry->lnInst, sizeof(entry->lnInst), "%s", lnInst);
    if (doName) snprintf(entry->doName, sizeof(entry->doName), "%s", doName);
    if (daName) snprintf(entry->daName, sizeof(entry->daName), "%s", daName);
    if (fc) snprintf(entry->fc, sizeof(entry->fc), "%s", fc);
    entry->next = ds->members;
    ds->members = entry;
}

static void process_ln_for_datasets(xmlNode* lnNode, const char* ldInst)
{
    bool isLn0 = (xmlStrcmp(lnNode->name, (const xmlChar*)"LN0") == 0);
    xmlChar* prefixAttr = xmlGetProp(lnNode, (const xmlChar*)"prefix");
    xmlChar* classAttr  = xmlGetProp(lnNode, (const xmlChar*)"lnClass");
    xmlChar* instAttr   = xmlGetProp(lnNode, (const xmlChar*)"inst");

    const char* prefix = prefixAttr ? (const char*)prefixAttr : "";
    const char* lnClass = classAttr ? (const char*)classAttr : "";
    const char* inst = instAttr ? (const char*)instAttr : "";

    char lnName[64];
    compose_ln_name(isLn0, prefix, lnClass, inst, lnName);

    xmlChar* lnTypeAttr = xmlGetProp(lnNode, (const xmlChar*)"lnType");
    const char* lnTypeId = lnTypeAttr ? (const char*)lnTypeAttr : "";

    register_ln_instance(ldInst, isLn0, prefix, lnClass, inst, lnTypeId, lnName);

    if (lnTypeAttr) xmlFree(lnTypeAttr);

    for (xmlNode* child = lnNode->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE)
            continue;

        if (xmlStrcmp(child->name, (const xmlChar*)"DataSet") == 0) {
            xmlChar* dsNameAttr = xmlGetProp(child, (const xmlChar*)"name");
            if (!dsNameAttr)
                continue;
            DataSetEntryDef* ds = dataset_create(ldInst ? ldInst : "", lnName, (const char*)dsNameAttr);
            for (xmlNode* fcda = child->children; fcda; fcda = fcda->next) {
                if (fcda->type != XML_ELEMENT_NODE)
                    continue;
                if (xmlStrcmp(fcda->name, (const xmlChar*)"FCDA") != 0)
                    continue;
                xmlChar* ldAttr = xmlGetProp(fcda, (const xmlChar*)"ldInst");
                xmlChar* prefixAttrFc = xmlGetProp(fcda, (const xmlChar*)"prefix");
                xmlChar* lcAttr = xmlGetProp(fcda, (const xmlChar*)"lnClass");
                xmlChar* liAttr = xmlGetProp(fcda, (const xmlChar*)"lnInst");
                xmlChar* doAttr = xmlGetProp(fcda, (const xmlChar*)"doName");
                xmlChar* daAttr = xmlGetProp(fcda, (const xmlChar*)"daName");
                xmlChar* fcAttr = xmlGetProp(fcda, (const xmlChar*)"fc");
                dataset_add_fcda(ds,
                    ldAttr ? (const char*)ldAttr : "",
                    prefixAttrFc ? (const char*)prefixAttrFc : "",
                    lcAttr ? (const char*)lcAttr : "",
                    liAttr ? (const char*)liAttr : "",
                    doAttr ? (const char*)doAttr : "",
                    daAttr ? (const char*)daAttr : "",
                    fcAttr ? (const char*)fcAttr : "");
                if (ldAttr) xmlFree(ldAttr);
                if (prefixAttrFc) xmlFree(prefixAttrFc);
                if (lcAttr) xmlFree(lcAttr);
                if (liAttr) xmlFree(liAttr);
                if (doAttr) xmlFree(doAttr);
                if (daAttr) xmlFree(daAttr);
                if (fcAttr) xmlFree(fcAttr);
            }
            xmlFree(dsNameAttr);
        }
        else if (xmlStrcmp(child->name, (const xmlChar*)"ReportControl") == 0) {
            collect_report_control(child, ldInst, lnName);
        }
    }

    if (prefixAttr) xmlFree(prefixAttr);
    if (classAttr) xmlFree(classAttr);
    if (instAttr) xmlFree(instAttr);
}

static xmlNode* find_active_ied(xmlNode* root)
{
    if (!root)
        return NULL;

    bool hadPreference = (selected_ied_name[0] != '\0');
    char requested[64] = {0};
    if (hadPreference)
        snprintf(requested, sizeof(requested), "%s", selected_ied_name);

    xmlNode* firstMatch = NULL;
    for (xmlNode* ied = root->children; ied; ied = ied->next) {
        if (ied->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(ied->name, (const xmlChar*)"IED") != 0)
            continue;

        xmlChar* nameAttr = xmlGetProp(ied, (const xmlChar*)"name");
        const char* name = nameAttr ? (const char*)nameAttr : "";
        if (!firstMatch && name && *name)
            firstMatch = ied;

        if (!selected_ied_name[0] && name && *name)
            set_selected_ied(name);

        if (selected_ied_name[0] && name && *name && strcmp(selected_ied_name, name) == 0) {
            if (nameAttr) xmlFree(nameAttr);
            return ied;
        }
        if (nameAttr) xmlFree(nameAttr);
    }

    if (firstMatch) {
        xmlChar* nameAttr = xmlGetProp(firstMatch, (const xmlChar*)"name");
        const char* fallback = nameAttr ? (const char*)nameAttr : "";
        if (fallback && *fallback) {
            if (hadPreference && strcmp(requested, fallback) != 0)
                fprintf(stderr, "Requested IED '%s' not found. Using '%s'.\n", requested, fallback);
            snprintf(selected_ied_name, sizeof(selected_ied_name), "%s", fallback);
            selected_ap_name[0] = '\0';
        }
        if (nameAttr) xmlFree(nameAttr);
    } else if (hadPreference) {
        fprintf(stderr, "Requested IED '%s' not found in SCL file.\n", requested);
        selected_ied_name[0] = '\0';
    }

    return firstMatch;
}

static void process_access_point(xmlNode* ap)
{
    for (xmlNode* server = ap->children; server; server = server->next) {
        if (server->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(server->name, (const xmlChar*)"Server") != 0)
            continue;
        for (xmlNode* ld = server->children; ld; ld = ld->next) {
            if (ld->type != XML_ELEMENT_NODE)
                continue;
            if (xmlStrcmp(ld->name, (const xmlChar*)"LDevice") != 0)
                continue;
            xmlChar* instAttr = xmlGetProp(ld, (const xmlChar*)"inst");
            const char* ldInst = instAttr ? (const char*)instAttr : "";
            for (xmlNode* ln = ld->children; ln; ln = ln->next) {
                if (ln->type != XML_ELEMENT_NODE)
                    continue;
                if (xmlStrcmp(ln->name, (const xmlChar*)"LN") == 0 ||
                    xmlStrcmp(ln->name, (const xmlChar*)"LN0") == 0)
                    process_ln_for_datasets(ln, ldInst);
            }
            if (instAttr) xmlFree(instAttr);
        }
    }
}

static void collect_dataset_nodes(xmlNode* iedNode)
{
    if (!iedNode)
        return;

    bool hadPreference = (selected_ap_name[0] != '\0');
    char requested[64] = {0};
    if (hadPreference)
        snprintf(requested, sizeof(requested), "%s", selected_ap_name);

    xmlNode* firstAp = NULL;
    char firstApName[64] = {0};

    for (xmlNode* ap = iedNode->children; ap; ap = ap->next) {
        if (ap->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(ap->name, (const xmlChar*)"AccessPoint") != 0)
            continue;

        xmlChar* apNameAttr = xmlGetProp(ap, (const xmlChar*)"name");
        const char* apName = apNameAttr ? (const char*)apNameAttr : "";
        if (!firstAp) {
            firstAp = ap;
            if (apName && *apName)
                snprintf(firstApName, sizeof(firstApName), "%s", apName);
            else
                firstApName[0] = '\0';
        }

        if (!selected_ap_name[0] && apName && *apName)
            set_selected_ap(apName);

        bool apMatches = (!selected_ap_name[0]) ||
                         (apName && *apName && strcmp(selected_ap_name, apName) == 0);
        if (apNameAttr) xmlFree(apNameAttr);

        if (!apMatches)
            continue;

        process_access_point(ap);
        return;
    }

    if (firstAp) {
        if (hadPreference && strcmp(requested, firstApName) != 0) {
            const char* fallbackLabel = firstApName[0] ? firstApName : "<unnamed>";
            fprintf(stderr, "Requested AccessPoint '%s' not found. Using '%s'.\n", requested, fallbackLabel);
        }
        snprintf(selected_ap_name, sizeof(selected_ap_name), "%s", firstApName);
        process_access_point(firstAp);
    } else if (hadPreference) {
        fprintf(stderr, "Requested AccessPoint '%s' not found in IED '%s'.\n", requested, selected_ied_name);
        selected_ap_name[0] = '\0';
    }
}


static void collect_da_type(xmlNode* templates, const char* doTypeId, const char* daTypeId,
                            const char* prefix, const char* inheritedFc, uint8_t inheritedTrgOps);

static void collect_do_type(xmlNode* templates, const char* doTypeId, const char* prefix) {
    if (!templates || !doTypeId)
        return;

    xmlNode* doTypeNode = find_node(templates->children, "DOType", "id", doTypeId);
    if (!doTypeNode)
        return;

    for (xmlNode* child = doTypeNode->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE)
            continue;

        if (xmlStrcmp(child->name, (const xmlChar*)"DA") == 0) {
            xmlChar* nameAttr = xmlGetProp(child, (const xmlChar*)"name");
            xmlChar* fcAttr   = xmlGetProp(child, (const xmlChar*)"fc");
            xmlChar* bTypeAttr= xmlGetProp(child, (const xmlChar*)"bType");
            xmlChar* typeAttr = xmlGetProp(child, (const xmlChar*)"type");
            xmlChar* dchgAttr = xmlGetProp(child, (const xmlChar*)"dchg");
            xmlChar* qchgAttr = xmlGetProp(child, (const xmlChar*)"qchg");
            xmlChar* dupdAttr = xmlGetProp(child, (const xmlChar*)"dupd");

            if (!nameAttr || !bTypeAttr) {
                if (nameAttr) xmlFree(nameAttr);
                if (fcAttr) xmlFree(fcAttr);
                if (bTypeAttr) xmlFree(bTypeAttr);
                if (typeAttr) xmlFree(typeAttr);
                if (dchgAttr) xmlFree(dchgAttr);
                if (qchgAttr) xmlFree(qchgAttr);
                if (dupdAttr) xmlFree(dupdAttr);
                continue;
            }

            char path[256];
            const char* nameStr = (const char*)nameAttr;
            if (prefix && prefix[0])
                snprintf(path, sizeof(path), "%s.%s", prefix, nameStr);
            else
                snprintf(path, sizeof(path), "%s", nameStr);

            const char* fcStr = fcAttr ? (const char*)fcAttr : NULL;
            const char* bTypeStr = (const char*)bTypeAttr;
            uint8_t trgOps = 0;
            if (xml_attr_true(dchgAttr)) trgOps |= TRG_OPT_DATA_CHANGED;
            if (xml_attr_true(qchgAttr)) trgOps |= TRG_OPT_QUALITY_CHANGED;
            if (xml_attr_true(dupdAttr)) trgOps |= TRG_OPT_DATA_UPDATE;

            const char* typeStr = typeAttr ? (const char*)typeAttr : NULL;

            add_da_entry(doTypeId, path, fcStr, bTypeStr, typeStr, trgOps);

            if (typeAttr)
                collect_da_type(templates, doTypeId, (const char*)typeAttr, path, fcStr, trgOps);

            xmlFree(nameAttr);
            if (fcAttr) xmlFree(fcAttr);
            xmlFree(bTypeAttr);
            if (typeAttr) xmlFree(typeAttr);
            if (dchgAttr) xmlFree(dchgAttr);
            if (qchgAttr) xmlFree(qchgAttr);
            if (dupdAttr) xmlFree(dupdAttr);
        }
        else if (xmlStrcmp(child->name, (const xmlChar*)"SDO") == 0) {
            xmlChar* nameAttr = xmlGetProp(child, (const xmlChar*)"name");
            xmlChar* typeAttr = xmlGetProp(child, (const xmlChar*)"type");
            if (!nameAttr || !typeAttr) {
                if (nameAttr) xmlFree(nameAttr);
                if (typeAttr) xmlFree(typeAttr);
                continue;
            }

            char path[256];
            const char* nameStr = (const char*)nameAttr;
            if (prefix && prefix[0])
                snprintf(path, sizeof(path), "%s.%s", prefix, nameStr);
            else
                snprintf(path, sizeof(path), "%s", nameStr);

            collect_do_type(templates, (const char*)typeAttr, path);

            xmlFree(nameAttr);
            xmlFree(typeAttr);
        }
    }
}

static void collect_da_type(xmlNode* templates, const char* doTypeId, const char* daTypeId,
                            const char* prefix, const char* inheritedFc, uint8_t inheritedTrgOps)
{
    if (!templates || !daTypeId)
        return;

    xmlNode* daTypeNode = find_node(templates->children, "DAType", "id", daTypeId);
    if (!daTypeNode)
        return;

    for (xmlNode* child = daTypeNode->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE)
            continue;

        if (xmlStrcmp(child->name, (const xmlChar*)"BDA") != 0 &&
            xmlStrcmp(child->name, (const xmlChar*)"DA")  != 0)
            continue;

        xmlChar* nameAttr = xmlGetProp(child, (const xmlChar*)"name");
        xmlChar* fcAttr   = xmlGetProp(child, (const xmlChar*)"fc");
        xmlChar* bTypeAttr= xmlGetProp(child, (const xmlChar*)"bType");
        xmlChar* typeAttr = xmlGetProp(child, (const xmlChar*)"type");
        xmlChar* dchgAttr = xmlGetProp(child, (const xmlChar*)"dchg");
        xmlChar* qchgAttr = xmlGetProp(child, (const xmlChar*)"qchg");
        xmlChar* dupdAttr = xmlGetProp(child, (const xmlChar*)"dupd");

        if (!nameAttr) {
            if (fcAttr) xmlFree(fcAttr);
            if (bTypeAttr) xmlFree(bTypeAttr);
            if (typeAttr) xmlFree(typeAttr);
            if (dchgAttr) xmlFree(dchgAttr);
            if (qchgAttr) xmlFree(qchgAttr);
            if (dupdAttr) xmlFree(dupdAttr);
            continue;
        }

        char path[256];
        const char* nameStr = (const char*)nameAttr;
        if (prefix && prefix[0])
            snprintf(path, sizeof(path), "%s.%s", prefix, nameStr);
        else
            snprintf(path, sizeof(path), "%s", nameStr);

        const char* fcStr = fcAttr ? (const char*)fcAttr : inheritedFc;
        const char* bTypeStr = bTypeAttr ? (const char*)bTypeAttr : NULL;
        uint8_t trgOps = inheritedTrgOps;
        if (dchgAttr || qchgAttr || dupdAttr) {
            trgOps = 0;
            if (xml_attr_true(dchgAttr)) trgOps |= TRG_OPT_DATA_CHANGED;
            if (xml_attr_true(qchgAttr)) trgOps |= TRG_OPT_QUALITY_CHANGED;
            if (xml_attr_true(dupdAttr)) trgOps |= TRG_OPT_DATA_UPDATE;
        }

        const char* typeStr = typeAttr ? (const char*)typeAttr : NULL;

        add_da_entry(doTypeId, path, fcStr, bTypeStr, typeStr, trgOps);

        if (typeAttr)
            collect_da_type(templates, doTypeId, (const char*)typeAttr, path, fcStr, trgOps);

        xmlFree(nameAttr);
        if (fcAttr) xmlFree(fcAttr);
        if (bTypeAttr) xmlFree(bTypeAttr);
        if (typeAttr) xmlFree(typeAttr);
        if (dchgAttr) xmlFree(dchgAttr);
        if (qchgAttr) xmlFree(qchgAttr);
        if (dupdAttr) xmlFree(dupdAttr);
    }
}

static void parse_icd(xmlDocPtr doc) {
    xmlNode* root = xmlDocGetRootElement(doc);
    xmlNode* templates = find_node(root->children, "DataTypeTemplates", NULL, NULL);
    if (!templates) return;

    // LNodeType → DOType
    for (xmlNode* ln = templates->children; ln; ln = ln->next) {
        if (ln->type != XML_ELEMENT_NODE) continue;
        if (xmlStrcmp(ln->name, (const xmlChar*)"LNodeType") != 0) continue;

        xmlChar* lnClassAttr = xmlGetProp(ln, (const xmlChar*)"lnClass");
        xmlChar* lnTypeAttr  = xmlGetProp(ln, (const xmlChar*)"id");
        const char* lnClass = lnClassAttr ? (const char*)lnClassAttr : NULL;
        const char* lnTypeId = lnTypeAttr ? (const char*)lnTypeAttr : NULL;

        for (xmlNode* doNode = ln->children; doNode; doNode = doNode->next) {
            if (doNode->type != XML_ELEMENT_NODE || xmlStrcmp(doNode->name, (const xmlChar*)"DO") != 0) continue;
            xmlChar* doNameAttr = xmlGetProp(doNode, (const xmlChar*)"name");
            xmlChar* doTypeAttr = xmlGetProp(doNode, (const xmlChar*)"type");

            const char* doName = doNameAttr ? (const char*)doNameAttr : NULL;
            const char* doType = doTypeAttr ? (const char*)doTypeAttr : NULL;

            // Look up the DOType to determine the CDC
            xmlNode* doTypeNode = find_node(templates->children, "DOType", "id", doType);
            if (!doTypeNode) continue;
            xmlChar* cdcAttr = xmlGetProp(doTypeNode, (const xmlChar*)"cdc");
            const char* cdc = cdcAttr ? (const char*)cdcAttr : NULL;

            DOEntry* e = calloc(1, sizeof(DOEntry));
            if (lnTypeId)
                strncpy(e->lnType, lnTypeId, sizeof(e->lnType)-1);
            if (lnClass)
                strncpy(e->lnClass, lnClass, sizeof(e->lnClass)-1);
            if (doName)
                strncpy(e->doName, doName, sizeof(e->doName)-1);
            if (doType)
                strncpy(e->doType, doType, sizeof(e->doType)-1);
            if (cdc)
                strncpy(e->cdc, cdc, sizeof(e->cdc)-1);
            e->next = do_list;
            do_list = e;

            collect_do_type(templates, doType, NULL);

            if (cdcAttr) xmlFree(cdcAttr);
            if (doNameAttr) xmlFree(doNameAttr);
            if (doTypeAttr) xmlFree(doTypeAttr);
        }

        if (lnClassAttr) xmlFree(lnClassAttr);
        if (lnTypeAttr) xmlFree(lnTypeAttr);
    }

    xmlNode* activeIed = find_active_ied(root);
    if (!activeIed) {
        fprintf(stderr, "No IED definition found in SCL file.\n");
        return;
    }

    collect_ln_nodes(activeIed);
    collect_dataset_nodes(activeIed);
}

bool icd_load(const char* path) {
    icd_unload();
    xmlInitParser();
    xmlDoc* doc = xmlReadFile(path, NULL, 0);
    if (!doc) return false;
    parse_icd(doc);
    xmlFreeDoc(doc);
    return true;
}

bool icd_find_do_info(const char* lnTypeId, const char* do_name, DOInfo* out) {
    if (!lnTypeId || !do_name || !out)
        return false;

    for (DOEntry* e = do_list; e; e = e->next) {
        if (!strcmp(e->lnType, lnTypeId) && !strcmp(e->doName, do_name)) {
            snprintf(out->do_type_id, sizeof(out->do_type_id), "%s", e->doType);
            snprintf(out->cdc, sizeof(out->cdc), "%s", e->cdc);
            return true;
        }
    }
    return false;
}

bool icd_find_da_info(const char* do_type_id, const char* da_path, DAInfo* out) {
    for (DAEntry* e = da_list; e; e = e->next) {
        if (!strcmp(e->doType, do_type_id) && !strcmp(e->daPath, da_path)) {
            strncpy(out->fc, e->fc, sizeof(out->fc));
            strncpy(out->bType, e->bType, sizeof(out->bType));
            strncpy(out->typeId, e->typeId, sizeof(out->typeId));
            out->trgOps = e->trgOps;
            return true;
        }
    }
    return false;
}

bool icd_da_exists(const char* do_type_id, const char* da_path) {
    for (DAEntry* e = da_list; e; e = e->next) {
        if (!strcmp(e->doType, do_type_id) && !strcmp(e->daPath, da_path))
            return true;
    }
    return false;
}

void icd_foreach_da(const char* do_type_id,
                    void (*callback)(const char* path, const DAInfo* info, void* ctx),
                    void* ctx)
{
    if (!do_type_id || !callback)
        return;

    for (DAEntry* e = da_list; e; e = e->next) {
        if (strcmp(e->doType, do_type_id) != 0)
            continue;

        DAInfo info = {0};
        strncpy(info.fc, e->fc, sizeof(info.fc));
        strncpy(info.bType, e->bType, sizeof(info.bType));
        strncpy(info.typeId, e->typeId, sizeof(info.typeId));
        info.trgOps = e->trgOps;
        callback(e->daPath, &info, ctx);
    }
}

void icd_foreach_do(const char* lnTypeId,
                    void (*callback)(const char* doName, const DOInfo* info, void* ctx),
                    void* ctx)
{
    if (!lnTypeId || !callback)
        return;

    for (DOEntry* e = do_list; e; e = e->next) {
        if (strcmp(e->lnType, lnTypeId) != 0)
            continue;

        DOInfo info = {0};
        snprintf(info.do_type_id, sizeof(info.do_type_id), "%s", e->doType);
        snprintf(info.cdc, sizeof(info.cdc), "%s", e->cdc);
        callback(e->doName, &info, ctx);
    }
}

void icd_foreach_ln_instance(void (*callback)(const LNInstanceInfo* info, void* ctx),
                             void* ctx)
{
    if (!callback)
        return;

    for (LNInstEntry* e = ln_instances; e; e = e->next) {
        LNInstanceInfo info = {0};
        snprintf(info.ldInst, sizeof(info.ldInst), "%s", e->ldInst);
        snprintf(info.prefix, sizeof(info.prefix), "%s", e->prefix);
        snprintf(info.lnClass, sizeof(info.lnClass), "%s", e->lnClass);
        snprintf(info.lnInst, sizeof(info.lnInst), "%s", e->lnInst);
        snprintf(info.lnType, sizeof(info.lnType), "%s", e->lnType);
        snprintf(info.lnName, sizeof(info.lnName), "%s", e->lnName);
        info.isLn0 = e->isLn0;
        callback(&info, ctx);
    }
}

bool icd_find_ln_type_by_name(const char* ldInst, const char* lnName, char lnTypeOut[64])
{
    if (!lnName || !lnTypeOut)
        return false;

    for (LNInstEntry* e = ln_instances; e; e = e->next) {
        if (ldInst && *ldInst && strcmp(e->ldInst, ldInst) != 0)
            continue;
        if (strcmp(e->lnName, lnName) == 0) {
            snprintf(lnTypeOut, 64, "%s", e->lnType);
            return true;
        }
    }
    return false;
}

bool icd_find_ln_type_by_parts(const char* ldInst, const char* prefix, const char* lnClass, const char* lnInst, char lnTypeOut[64])
{
    if (!lnTypeOut)
        return false;

    for (LNInstEntry* e = ln_instances; e; e = e->next) {
        if (ldInst && *ldInst && strcmp(e->ldInst, ldInst) != 0)
            continue;
        if (prefix && *prefix) {
            if (strcmp(e->prefix, prefix) != 0)
                continue;
        } else if (e->prefix[0]) {
            continue;
        }
        if (lnClass && *lnClass) {
            if (strcmp(e->lnClass, lnClass) != 0)
                continue;
        }
        if (lnInst && *lnInst) {
            if (strcmp(e->lnInst, lnInst) != 0)
                continue;
        } else if (e->lnInst[0]) {
            continue;
        }
        snprintf(lnTypeOut, 64, "%s", e->lnType);
        return true;
    }
    return false;
}

void icd_foreach_dataset(void (*callback)(const char* ldInst, const char* lnName, const char* dsName, void* ctx),
                        void* ctx)
{
    if (!callback)
        return;
    for (DataSetEntryDef* ds = dataset_list; ds; ds = ds->next) {
        callback(ds->ldInst, ds->lnName, ds->name, ctx);
    }
}

void icd_foreach_dataset_fcda(const char* ldInst, const char* lnName, const char* dsName,
                              void (*callback)(const FCDAInfo* info, void* ctx),
                              void* ctx)
{
    if (!callback)
        return;
    for (DataSetEntryDef* ds = dataset_list; ds; ds = ds->next) {
        if (ldInst && *ldInst && strcmp(ds->ldInst, ldInst) != 0)
            continue;
        if (lnName && *lnName && strcmp(ds->lnName, lnName) != 0)
            continue;
        if (dsName && *dsName && strcmp(ds->name, dsName) != 0)
            continue;
        for (FcdaEntry* fcda = ds->members; fcda; fcda = fcda->next) {
            FCDAInfo info = {0};
            snprintf(info.ldInst, sizeof(info.ldInst), "%s", fcda->ldInst);
            snprintf(info.prefix, sizeof(info.prefix), "%s", fcda->prefix);
            snprintf(info.lnClass, sizeof(info.lnClass), "%s", fcda->lnClass);
            snprintf(info.lnInst, sizeof(info.lnInst), "%s", fcda->lnInst);
            snprintf(info.doName, sizeof(info.doName), "%s", fcda->doName);
            snprintf(info.daName, sizeof(info.daName), "%s", fcda->daName);
            snprintf(info.fc, sizeof(info.fc), "%s", fcda->fc);
            callback(&info, ctx);
        }
    }
}

bool icd_lookup_ln_class(const char* ln_name, char out[16]) {
    if (!ln_name || !out)
        return false;

    for (LNEntry* e = ln_list; e; e = e->next) {
        if (!strcmp(e->name, ln_name)) {
            strncpy(out, e->lnClass, 15);
            out[15] = '\0';
            return true;
        }
    }
    return false;
}

void icd_foreach_report(void (*callback)(const ReportControlInfo* info, void* ctx), void* ctx)
{
    if (!callback)
        return;

    for (ReportEntry* e = report_list; e; e = e->next) {
        ReportControlInfo info = {0};
        snprintf(info.ldInst, sizeof(info.ldInst), "%s", e->ldInst);
        snprintf(info.lnName, sizeof(info.lnName), "%s", e->lnName);
        snprintf(info.name, sizeof(info.name), "%s", e->name);
        snprintf(info.dataSet, sizeof(info.dataSet), "%s", e->dataSet);
        snprintf(info.rptId, sizeof(info.rptId), "%s", e->rptId);
        info.confRev = e->confRev;
        info.intgPd = e->intgPd;
        info.bufTime = e->bufTime;
        info.rptEnabledMax = e->rptEnabledMax;
        info.trgOps = e->trgOps;
        info.optFields = e->optFields;
        info.buffered = e->buffered;
        callback(&info, ctx);
    }
}

bool icd_set_active_ied(const char* name, const char* accessPoint)
{
    if (!name || !*name)
        return false;

    snprintf(selected_ied_name, sizeof(selected_ied_name), "%s", name);
    if (accessPoint && *accessPoint)
        snprintf(selected_ap_name, sizeof(selected_ap_name), "%s", accessPoint);
    else
        selected_ap_name[0] = '\0';
    return true;
}

void icd_unload() {
    while (do_list) {
        DOEntry* tmp = do_list;
        do_list = do_list->next;
        free(tmp);
    }
    while (da_list) {
        DAEntry* tmp = da_list;
        da_list = da_list->next;
        free(tmp);
    }
    while (ln_list) {
        LNEntry* tmp = ln_list;
        ln_list = ln_list->next;
        free(tmp);
    }
    while (ln_instances) {
        LNInstEntry* tmp = ln_instances;
        ln_instances = ln_instances->next;
        free(tmp);
    }
    while (dataset_list) {
        DataSetEntryDef* ds = dataset_list;
        dataset_list = dataset_list->next;
        FcdaEntry* fcda = ds->members;
        while (fcda) {
            FcdaEntry* ftmp = fcda;
            fcda = fcda->next;
            free(ftmp);
        }
        free(ds);
    }
    while (report_list) {
        ReportEntry* r = report_list;
        report_list = report_list->next;
        free(r);
    }
}
