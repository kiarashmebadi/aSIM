#define _DEFAULT_SOURCE

/*
 * File: mapping.c
 * Author: Kiarash Mebadi <kiyarash.mebadi@gmail.com>
 * Company: Azarakhsh Maham Shargh
 * Description: Parses CSV mapping files that tie IEC 61850 paths to Modbus addresses.
 */

#include "mapping.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

static void trim(char* s){
    if(!s) return;
    char* p = s;
    while(*p && isspace((unsigned char)*p)) p++;
    if(p != s) memmove(s, p, strlen(p) + 1);
    int i = (int)strlen(s) - 1;
    for(; i >= 0 && isspace((unsigned char)s[i]); --i) s[i] = 0;
}

static CdcType cdc_from(const char* s){
    if(!s) return CDC_UNKNOWN;
    if(!strcasecmp(s, "SPS")) return CDC_SPS;
    if(!strcasecmp(s, "DPS")) return CDC_DPS;
    if(!strcasecmp(s, "SPC")) return CDC_SPC;
    if(!strcasecmp(s, "DPC")) return CDC_DPC;
    if(!strcasecmp(s, "MV"))  return CDC_MV;
    return CDC_UNKNOWN;
}

static MbType mb_from(const char* s){
    if(!s) return MB_IREG;
    if(!strcasecmp(s, "COIL")) return MB_COIL;
    if(!strcasecmp(s, "DISCRETE_INPUT")) return MB_DI;
    if(!strcasecmp(s, "INPUT_REGISTER")) return MB_IREG;
    if(!strcasecmp(s, "HOLDING_REGISTER")) return MB_HREG;
    return MB_IREG;
}

// mapping.c
static void trim_inplace(char* s){
    if(!s) return;
    // left trim
    char* p=s; while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if(p!=s) memmove(s,p,strlen(p)+1);
    // right trim
    int i=(int)strlen(s)-1;
    while(i>=0 && (s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n')) s[i--]=0;
}

// Normalize non-standard slash characters to the regular '/'
static void normalize_slashes(char* s){
    for(char* p=s; *p; ++p){
        unsigned char c=(unsigned char)*p;
        if(c=='\\' || c==0xEF /*UTF8 lead?*/){ /* treat exotic backslashes as forward slashes */
            *p='/';
        }
        if(*p=='\xE2' && p[1]=='\x88' && p[2]=='\x95'){ /* ∕ division slash */
            *p='/'; p[1]=' '; p[2]=' '; // simplify by neutralizing extra bytes
        }
        if(*p=='\xEF' && p[1]=='\xBC' && p[2]=='\x8F'){ /* ／ fullwidth solidus */
            *p='/'; p[1]=' '; p[2]=' ';
        }
    }
}

static int parse_bit_suffix(char* da_path)
{
    if (!da_path)
        return -1;

    size_t len = strlen(da_path);
    if (len == 0)
        return -1;

    char* lastDot = strrchr(da_path, '.');
    char* suffix = lastDot ? lastDot + 1 : da_path;

    if (strncasecmp(suffix, "bit", 3) == 0 && isdigit((unsigned char)suffix[3])) {
        int bit = atoi(suffix + 3);
        if (lastDot)
            *lastDot = '\0';
        else
            da_path[0] = '\0';
        return bit;
    }

    return -1;
}

static int split_iec_path(MapRow* r) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", r->iec_path);
    trim_inplace(buf);
    normalize_slashes(buf);

    // Find the first separator between the LN part and DO part ('.' or '$')
    char* firstDot = strchr(buf, '.');
    char* firstDollar = strchr(buf, '$');

    char* sep = NULL;
    int sep_is_dollar = 0;

    if (firstDot && firstDollar)
        sep = (firstDot < firstDollar ? firstDot : firstDollar);
    else if (firstDot)
        sep = firstDot;
    else if (firstDollar) {
        sep = firstDollar;
        sep_is_dollar = 1;
    } else {
        // No DO/DA section after the LN
        return -1;
    }

    // Extract LD/LN portion before the separator
    char ldln[128]={0};
    size_t ldln_len = (size_t)(sep - buf);
    if (ldln_len >= sizeof(ldln)) ldln_len = sizeof(ldln)-1;
    memcpy(ldln, buf, ldln_len); ldln[ldln_len]=0;
    trim_inplace(ldln);

    // Split LD/LN; if there is '/', treat previous segment as LD, otherwise only LN
    char ldln_copy[128];
    snprintf(ldln_copy, sizeof(ldln_copy), "%s", ldln);

    char* sections[8] = {0};
    int section_count = 0;
    for (char* tok = strtok(ldln_copy, "/"); tok && section_count < 8; tok = strtok(NULL, "/")) {
        sections[section_count++] = tok;
    }

    r->ld[0] = '\0';
    r->ln[0] = '\0';

    if (section_count == 0) {
        return -1;
    } else if (section_count == 1) {
        snprintf(r->ln, sizeof(r->ln), "%s", sections[0]);
    } else {
        snprintf(r->ln, sizeof(r->ln), "%s", sections[section_count - 1]);
        snprintf(r->ld, sizeof(r->ld), "%s", sections[section_count - 2]);
    }

    trim_inplace(r->ld);
    trim_inplace(r->ln);

    // Part after the separator holds DO and DA information
    const char* rest = sep + 1;

    if (!sep_is_dollar) {
        // Dot notation: LD/LN.DO.DA[.sub...]
        const char* dot2 = strchr(rest, '.');
        if (dot2) {
            size_t do_len = (size_t)(dot2 - rest);
            if (do_len >= sizeof(r->do_name)) do_len = sizeof(r->do_name)-1;
            memcpy(r->do_name, rest, do_len);
            r->do_name[do_len]=0;

            snprintf(r->da_path, sizeof(r->da_path), "%s", dot2 + 1);
        } else {
            snprintf(r->do_name, sizeof(r->do_name), "%s", rest);
            r->da_path[0]=0;
        }
    } else {
        // Dollar notation: LD/LN$FC$DO$DAbranch$DAname[.sub...]
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "%s", rest);
        char* saveptr=NULL;
        char* tok = strtok_r(tmp, "$", &saveptr);
        if (!tok) return -2;

        if (!strcasecmp(tok,"ST") || !strcasecmp(tok,"MX") || !strcasecmp(tok,"SP") ||
            !strcasecmp(tok,"SV") || !strcasecmp(tok,"CF") || !strcasecmp(tok,"DC") ||
            !strcasecmp(tok,"EX") || !strcasecmp(tok,"CO")) {
            tok = strtok_r(NULL, "$", &saveptr);
            if (!tok) return -3; // DO name must follow
        }
        snprintf(r->do_name, sizeof(r->do_name), "%s", tok);

        char daBuf[160]={0};
        int first=1;
        for (tok = strtok_r(NULL, "$", &saveptr); tok; tok = strtok_r(NULL, "$", &saveptr)) {
            if (!first) strncat(daBuf, ".", sizeof(daBuf)-strlen(daBuf)-1);
            strncat(daBuf, tok, sizeof(daBuf)-strlen(daBuf)-1);
            first=0;
        }
        snprintf(r->da_path, sizeof(r->da_path), "%s", daBuf);
    }

    trim_inplace(r->do_name);
    trim_inplace(r->da_path);

    r->bit_index = parse_bit_suffix(r->da_path);

    return 0;
}



bool load_mapping_csv(const char* path, MapTable* out_tbl, char* errbuf, size_t errlen){
    memset(out_tbl, 0, sizeof(*out_tbl));

    FILE* f = fopen(path, "rb");
    if(!f){
        snprintf(errbuf, errlen, "cannot open file");
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        snprintf(errbuf, errlen, "fseek failed");
        fclose(f);
        return false;
    }

    long sz = ftell(f);
    if (sz < 0) {
        snprintf(errbuf, errlen, "ftell failed");
        fclose(f);
        return false;
    }
    rewind(f);

    char* content = (char*)malloc((size_t)sz + 1);
    if (!content) {
        snprintf(errbuf, errlen, "oom");
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(content, 1, (size_t)sz, f);
    fclose(f);
    content[read_bytes] = '\0';

    size_t cap = 128, cnt = 0;
    MapRow* rows = (MapRow*)calloc(cap, sizeof(MapRow));
    if (!rows) {
        free(content);
        snprintf(errbuf, errlen, "oom");
        return false;
    }

    char* line_save = NULL;
    char* line = strtok_r(content, "\r\n", &line_save); // header
    if (!line) {
        snprintf(errbuf, errlen, "empty file");
        free(content);
        free(rows);
        return false;
    }

    // Skip header line
    line = strtok_r(NULL, "\r\n", &line_save);

    while (line) {
        trim(line);
        if (line[0]) {
            char* tok[8] = {0};
            int n = 0;
            char* field_save = NULL;
            for (char* field = strtok_r(line, ",", &field_save);
                 field && n < 8;
                 field = strtok_r(NULL, ",", &field_save)) {
                trim(field);
                tok[n++] = field;
            }

            if (n >= 8) {
                MapRow r = {0};
                r.bit_index = -1;
                snprintf(r.iec_path, sizeof(r.iec_path), "%s", tok[0]);
                snprintf(r.fc, sizeof(r.fc), "%s", tok[1]);
                r.cdc     = cdc_from(tok[2]);
                r.mb_type = mb_from(tok[3]);
                r.mb_addr = (uint16_t) strtoul(tok[4], NULL, 0);
                r.mb_unit = (uint8_t)  strtoul(tok[5], NULL, 0);
                r.enabled = (!strcasecmp(tok[6], "1") || !strcasecmp(tok[6], "true"));
                snprintf(r.desc, sizeof(r.desc), "%s", tok[7]);

                if (r.enabled && split_iec_path(&r) == 0) {
                    if(cnt == cap){
                        cap *= 2;
                        rows = (MapRow*)realloc(rows, cap * sizeof(MapRow));
                    }
                    rows[cnt++] = r;
                }
            }
        }

        line = strtok_r(NULL, "\r\n", &line_save);
    }

    free(content);

    out_tbl->rows = rows;
    out_tbl->count = cnt;
    return (cnt > 0);
}

void free_mapping(MapTable* t){
    if(t && t->rows) free(t->rows);
    if(t) memset(t, 0, sizeof(*t));
}
