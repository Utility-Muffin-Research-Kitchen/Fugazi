#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FZ_I18N_MAX_ENTRIES = 768, FZ_I18N_MAX_BYTES = 1024 * 1024 };
typedef struct { char *key; char *value; } fz_i18n_entry;
static fz_i18n_entry entries[FZ_I18N_MAX_ENTRIES];
static size_t entry_count;

static void clear_entries(void) { size_t i; for (i = 0; i < entry_count; ++i) { free(entries[i].key); free(entries[i].value); } entry_count = 0; }
static int valid_language(const char *s) { size_t n = 0; if (!s || !*s) return 0; for (; *s; ++s, ++n) if (!(('a' <= *s && *s <= 'z') || ('A' <= *s && *s <= 'Z') || ('0' <= *s && *s <= '9') || *s == '_' || *s == '-')) return 0; return n <= 15; }
static const char *normalise_language(const char *s) { return s && (!strcmp(s, "zh") || !strcmp(s, "zh_CN") || !strcmp(s, "zh-CN")) ? "zh_CN" : s; }
static void unescape(char *s) { char *read = s, *write = s; while (*read) { if (read[0] == '\\' && read[1] == 'n') { *write++ = '\n'; read += 2; } else if (read[0] == '\\' && read[1] == 't') { *write++ = '\t'; read += 2; } else *write++ = *read++; } *write = '\0'; }
static int fmt_sig(const char *s, char *out, size_t cap) { size_t n = 0; int count = 0; while (*s) { if (*s++ != '%') continue; if (*s == '%') { ++s; continue; } while (*s && strchr("-+ #0123456789.*'", *s)) ++s; while (*s && strchr("hlLqjzt", *s)) { if (n + 1 >= cap) return -1; out[n++] = *s++; } if (!*s || n + 1 >= cap) return -1; out[n++] = *s++; ++count; } if (n >= cap) return -1; out[n] = '\0'; return count; }
static int fmt_compatible(const char *key, const char *value) { char a[64], b[64]; int na = fmt_sig(key, a, sizeof(a)), nb = fmt_sig(value, b, sizeof(b)); return na >= 0 && na == nb && !strcmp(a, b); }
static int load_table(const char *path) {
    FILE *file = fopen(path, "rb"); long length; char *text, *line, *next;
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 || length > FZ_I18N_MAX_BYTES) { fclose(file); return 0; }
    rewind(file); text = malloc((size_t)length + 1);
    if (!text || fread(text, 1, (size_t)length, file) != (size_t)length) { free(text); fclose(file); return 0; }
    fclose(file); text[length] = '\0';
    for (line = text; line && *line && entry_count < FZ_I18N_MAX_ENTRIES; line = next) {
        char *tab, *end; next = strchr(line, '\n'); if (next) *next++ = '\0'; end = line + strlen(line); if (end > line && end[-1] == '\r') *--end = '\0';
        if (!line[0] || line[0] == '#') continue; tab = strchr(line, '\t'); if (!tab || tab == line || !tab[1] || strchr(tab + 1, '\t')) continue; *tab++ = '\0';
        unescape(line); unescape(tab); if (!fmt_compatible(line, tab)) continue;
        { size_t i; int duplicate = 0; for (i = 0; i < entry_count; ++i) if (!strcmp(entries[i].key, line)) { duplicate = 1; break; } if (duplicate) continue; }
        entries[entry_count].key = strdup(line); entries[entry_count].value = strdup(tab);
        if (entries[entry_count].key && entries[entry_count].value) ++entry_count; else { free(entries[entry_count].key); free(entries[entry_count].value); }
    }
    free(text); return entry_count != 0;
}
void fz_i18n_init(const char *res_dir) {
    const char *language = getenv("UMRK_LANGUAGE"), *override; char path[2048]; clear_entries();
    if (!language || !*language) language = getenv("JAWAKA_LANGUAGE"); language = normalise_language(language);
    if (!valid_language(language) || !strcmp(language, "en")) return;
    override = getenv("USERDATA_PATH");
    if (override && *override && snprintf(path, sizeof(path), "%s/Fugazi/i18n/%s.tsv", override, language) < (int)sizeof(path) && load_table(path)) return;
    override = getenv("FUGAZI_I18N_DIR");
    if (override && *override && snprintf(path, sizeof(path), "%s/%s.tsv", override, language) < (int)sizeof(path) && load_table(path)) return;
    if (res_dir && *res_dir && snprintf(path, sizeof(path), "%s/res/i18n/%s.tsv", res_dir, language) < (int)sizeof(path)) (void)load_table(path);
}
const char *fz_t(const char *key) { size_t i; const char *context; if (!key) return ""; for (i = 0; i < entry_count; ++i) if (!strcmp(entries[i].key, key)) return entries[i].value; context = strchr(key, '|'); return context && context != key ? context + 1 : key; }
