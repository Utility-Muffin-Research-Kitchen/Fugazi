/* Fugazi global-preset ownership. See preset_ownership.h. */

#include "preset_ownership.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

fz_fault_hooks fz_faults;

/* --------------------------------------------------------------- utilities */

static bool fault(int *counter)
{
    if (*counter > 0) { (*counter)--; return true; }
    return false;
}

static bool path_cat(char *out, size_t out_sz, const char *a, const char *b)
{
    int n = snprintf(out, out_sz, "%s%s", a, b);
    return n >= 0 && (size_t)n < out_sz;
}

fz_result fz_preset_backup_path(const char *global_path, char *out, size_t out_sz)
{
    return path_cat(out, out_sz, global_path, FZ_BACKUP_SUFFIX) ? FZ_OK : FZ_ERR_PATH;
}

static bool is_regular_file(const char *path, off_t *size_out)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    if (size_out) *size_out = st.st_size;
    return true;
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Write content to path via a same-directory temp file and rename(). Same
   directory keeps the rename inside one filesystem, which is what makes it
   atomic on FAT32. On any failure the temp file is removed and path is
   untouched. */
static fz_result write_atomic(const char *path, const char *content)
{
    char tmp[2048];
    if (!path_cat(tmp, sizeof(tmp), path, ".fugazi-tmp")) return FZ_ERR_PATH;

    if (fault(&fz_faults.fail_next_write)) { unlink(tmp); return FZ_ERR_IO; }

    FILE *f = fopen(tmp, "w");
    if (!f) return FZ_ERR_IO;
    size_t len = strlen(content);
    bool ok = fwrite(content, 1, len, f) == len;
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) { unlink(tmp); return FZ_ERR_IO; }

    if (fault(&fz_faults.fail_next_rename) || rename(tmp, path) != 0) {
        unlink(tmp);
        return FZ_ERR_IO;
    }
    return FZ_OK;
}

static fz_result rename_checked(const char *from, const char *to)
{
    if (fault(&fz_faults.fail_next_rename)) return FZ_ERR_IO;
    return rename(from, to) == 0 ? FZ_OK : FZ_ERR_IO;
}

static fz_result unlink_checked(const char *path)
{
    if (fault(&fz_faults.fail_next_unlink)) return FZ_ERR_IO;
    return unlink(path) == 0 ? FZ_OK : FZ_ERR_IO;
}

/* ------------------------------------------------------------ reference parse */

static const char *skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Copy the reference target out of a "#reference ..." line, stripping the
   config parser's surrounding quotes and any whitespace. Returns false if the
   line is not a reference directive. */
static bool parse_reference(const char *line, char *out, size_t out_sz)
{
    const char *p = skip_space(line);
    if (*p != '#') return false;
    p++;
    if (strncmp(p, "reference", 9) != 0) return false;
    p += 9;
    /* "#references" and "#reference_foo" are not this directive. */
    if (*p != ' ' && *p != '\t' && *p != '"') return false;
    p = skip_space(p);

    char quote = 0;
    if (*p == '"' || *p == '\'') { quote = *p; p++; }

    size_t n = 0;
    while (*p && n + 1 < out_sz) {
        if (quote && *p == quote) break;
        if (!quote && (*p == ' ' || *p == '\t')) break;
        out[n++] = *p++;
    }
    out[n] = '\0';
    /* Trailing whitespace inside quotes is still whitespace. */
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) out[--n] = '\0';
    return n > 0;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls >= lf && strcmp(s + ls - lf, suffix) == 0;
}

/* True when target names Fugazi's preset. Suffix match on the legacy absolute
   form, never a substring search: ".../fugazi/fugazi.glslp.bak" is not ours. */
static bool reference_is_fugazi(const char *target, bool *legacy_out)
{
    if (strcmp(target, FZ_REFERENCE_RELATIVE) == 0) {
        *legacy_out = false;
        return true;
    }
    if (target[0] == '/' && ends_with(target, FZ_REFERENCE_LEGACY_SUFFIX)) {
        *legacy_out = true;
        return true;
    }
    return false;
}

/* Fugazi owns a preset only when it holds exactly one recognized #reference
   and nothing else but blank lines and comments. Anything more -- a second
   reference, a real preset directive, a NUL byte -- is somebody else's file. */
static bool content_is_owned(const char *buf, size_t len, bool *legacy_out)
{
    if (memchr(buf, '\0', len) != NULL) return false;

    bool owned = false;
    int references = 0;
    const char *p = buf, *end = buf + len;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        char line[1024];
        if (line_len >= sizeof(line)) return false;
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        char target[1024];
        const char *t = skip_space(line);
        if (*t == '\0' || *t == '\r') {
            /* blank */
        } else if (parse_reference(line, target, sizeof(target))) {
            if (++references > 1) return false;
            bool legacy = false;
            if (!reference_is_fugazi(target, &legacy)) return false;
            *legacy_out = legacy;
            owned = true;
        } else if (*t == '#') {
            /* comment */
        } else {
            return false; /* a real directive: not a bare Fugazi reference */
        }

        if (!nl) break;
        p = nl + 1;
    }
    return owned && references == 1;
}

/* Classify one file. Missing -> absent(false). Oversized, malformed, or
   somebody else's -> not owned. Unreadable or not a regular file -> invalid. */
typedef enum { FILE_ABSENT, FILE_OWNED, FILE_FOREIGN, FILE_INVALID } file_kind;

static file_kind classify(const char *path, bool *legacy_out)
{
    if (!exists(path)) return FILE_ABSENT;

    off_t size = 0;
    if (!is_regular_file(path, &size)) return FILE_INVALID;
    if (size > FZ_MAX_INSPECT_BYTES) return FILE_FOREIGN; /* capped before read */

    FILE *f = fopen(path, "rb");
    if (!f) return FILE_INVALID;

    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); return FILE_INVALID; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';

    bool legacy = false;
    bool owned = content_is_owned(buf, got, &legacy);
    free(buf);

    if (owned) { *legacy_out = legacy; return FILE_OWNED; }
    return FILE_FOREIGN;
}

/* ------------------------------------------------------------------ public */

fz_result fz_preset_inspect(const char *global_path, const char *backup_path,
                            fz_status *out)
{
    if (!global_path || !backup_path || !out) return FZ_ERR_PATH;

    out->state = FZ_STATE_ABSENT;
    out->legacy_reference = false;

    bool legacy = false;
    file_kind kind = classify(global_path, &legacy);
    bool has_backup = exists(backup_path);

    switch (kind) {
    case FILE_ABSENT:
        /* A backup with no global is a half-finished removal, not a conflict:
           the predecessor is still recoverable and nothing is being displaced. */
        out->state = FZ_STATE_ABSENT;
        break;
    case FILE_OWNED:
        out->state = has_backup ? FZ_STATE_OWNED_BACKUP : FZ_STATE_OWNED;
        out->legacy_reference = legacy;
        break;
    case FILE_FOREIGN:
        out->state = has_backup ? FZ_STATE_FOREIGN_BACKUP : FZ_STATE_FOREIGN;
        break;
    case FILE_INVALID:
        out->state = FZ_STATE_INVALID;
        break;
    }
    return FZ_OK;
}

fz_result fz_preset_install(const char *global_path, const char *backup_path,
                            bool allow_replace)
{
    fz_status st;
    fz_result r = fz_preset_inspect(global_path, backup_path, &st);
    if (r != FZ_OK) return r;

    static const char *content =
        "# Generated by Fugazi. Edits are overwritten on Apply.\n"
        "#reference \"" FZ_REFERENCE_RELATIVE "\"\n";

    switch (st.state) {
    case FZ_STATE_ABSENT:
    case FZ_STATE_OWNED:
    case FZ_STATE_OWNED_BACKUP:
        /* Ours (or nobody's) to rewrite. A legacy absolute reference migrates
           to the relative form here, which is the "next safe rewrite". */
        return write_atomic(global_path, content);

    case FZ_STATE_FOREIGN: {
        if (!allow_replace) return FZ_NEEDS_CONFIRM;
        /* Preserve the predecessor by moving it aside, then install. If the
           install fails, move it straight back: the user keeps their file. */
        r = rename_checked(global_path, backup_path);
        if (r != FZ_OK) return r;
        r = write_atomic(global_path, content);
        if (r != FZ_OK) {
            if (rename_checked(backup_path, global_path) != FZ_OK) {
                /* Both names cannot be lost: the predecessor is still at the
                   backup path, which is exactly what the resolver reads. */
            }
            return FZ_ERR_IO;
        }
        return FZ_OK;
    }

    case FZ_STATE_FOREIGN_BACKUP:
        /* Installing would destroy the preserved predecessor. Fail closed. */
        return FZ_CONFLICT;

    case FZ_STATE_INVALID:
    default:
        return FZ_ERR_IO;
    }
}

fz_result fz_preset_remove(const char *global_path, const char *backup_path)
{
    fz_status st;
    fz_result r = fz_preset_inspect(global_path, backup_path, &st);
    if (r != FZ_OK) return r;

    switch (st.state) {
    case FZ_STATE_ABSENT:
        return FZ_ABSENT;
    case FZ_STATE_OWNED:
        return unlink_checked(global_path);
    case FZ_STATE_OWNED_BACKUP:
        /* One rename: there is no instant where both copies are gone. */
        return rename_checked(backup_path, global_path);
    case FZ_STATE_FOREIGN:
    case FZ_STATE_FOREIGN_BACKUP:
    case FZ_STATE_INVALID:
    default:
        return FZ_NOT_OWNED;
    }
}

fz_result fz_preset_keep_current(const char *global_path, const char *backup_path)
{
    fz_status st;
    fz_result r = fz_preset_inspect(global_path, backup_path, &st);
    if (r != FZ_OK) return r;
    if (st.state != FZ_STATE_FOREIGN_BACKUP) return FZ_NOT_OWNED;
    return unlink_checked(backup_path);
}

fz_result fz_preset_restore_previous(const char *global_path, const char *backup_path)
{
    fz_status st;
    fz_result r = fz_preset_inspect(global_path, backup_path, &st);
    if (r != FZ_OK) return r;
    if (st.state != FZ_STATE_FOREIGN_BACKUP) return FZ_NOT_OWNED;
    return rename_checked(backup_path, global_path);
}
