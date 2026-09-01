/*
 * Native tests for Fugazi's global-preset ownership.
 *
 * The point of these tests is not that Apply works -- it is that a preset
 * Fugazi does not own is never lost. Every unowned fixture is compared
 * byte-for-byte before and after, including in the failure-injection cases.
 *
 * Build and run: make preset-ownership-test
 */

#include "preset_ownership.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static int checks;
static const char *fixture_dir = "tests/fixtures/global-preset";
static char work[1024];

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("  FAIL: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    failures++;
}

static void check(bool cond, const char *fmt, ...)
{
    checks++;
    if (cond) return;
    va_list ap;
    va_start(ap, fmt);
    fputs("  FAIL: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    failures++;
}

/* ------------------------------------------------------------------ helpers */

static char *read_all(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (len_out) *len_out = got;
    return buf;
}

static bool same_bytes(const char *a, const char *b)
{
    size_t la = 0, lb = 0;
    char *ba = read_all(a, &la);
    char *bb = read_all(b, &lb);
    bool eq = ba && bb && la == lb && memcmp(ba, bb, la) == 0;
    free(ba); free(bb);
    return eq;
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static void copy_file(const char *from, const char *to)
{
    size_t len = 0;
    char *buf = read_all(from, &len);
    if (!buf) { fail("cannot read %s", from); return; }
    FILE *f = fopen(to, "wb");
    if (!f) { free(buf); fail("cannot write %s", to); return; }
    fwrite(buf, 1, len, f);
    fclose(f);
    free(buf);
}

static void fixture(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s/%s", fixture_dir, name);
}

/* A clean working directory for one case. */
static void reset_work(char *global, size_t gn, char *backup, size_t bn)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s'", work, work);
    if (system(cmd) != 0) fail("cannot reset work dir");
    snprintf(global, gn, "%s/global.glslp", work);
    snprintf(backup, bn, "%s/global.glslp%s", work, FZ_BACKUP_SUFFIX);
}

static void place(const char *fixture_name, const char *dest)
{
    char src[1024];
    fixture(src, sizeof(src), fixture_name);
    copy_file(src, dest);
}

static const char *state_name(fz_state s)
{
    switch (s) {
    case FZ_STATE_ABSENT:         return "ABSENT";
    case FZ_STATE_OWNED:          return "OWNED";
    case FZ_STATE_OWNED_BACKUP:   return "OWNED_BACKUP";
    case FZ_STATE_FOREIGN:        return "FOREIGN";
    case FZ_STATE_FOREIGN_BACKUP: return "FOREIGN_BACKUP";
    case FZ_STATE_INVALID:        return "INVALID";
    }
    return "?";
}

/* ------------------------------------------------- 1. ownership recognition */

static void expect_state(const char *fixture_name, fz_state want, bool want_legacy)
{
    char global[1024], backup[1024];
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place(fixture_name, global);

    fz_status st;
    fz_preset_inspect(global, backup, &st);
    check(st.state == want, "%s: state %s, want %s",
          fixture_name, state_name(st.state), state_name(want));
    if (want == FZ_STATE_OWNED)
        check(st.legacy_reference == want_legacy,
              "%s: legacy_reference %d, want %d",
              fixture_name, (int)st.legacy_reference, (int)want_legacy);

    /* Inspection must never write. */
    char src[1024];
    fixture(src, sizeof(src), fixture_name);
    check(same_bytes(src, global), "%s: inspection modified the file", fixture_name);
}

static void test_recognition(void)
{
    puts("ownership recognition");

    expect_state("owned-relative.glslp",                    FZ_STATE_OWNED, false);
    expect_state("owned-relative-unquoted.glslp",           FZ_STATE_OWNED, false);
    expect_state("owned-relative-whitespace.glslp",         FZ_STATE_OWNED, false);
    expect_state("owned-legacy-abs-quoted-sdcard.glslp",    FZ_STATE_OWNED, true);
    expect_state("owned-legacy-abs-unquoted-sdcard.glslp",  FZ_STATE_OWNED, true);
    expect_state("owned-legacy-abs-quoted-sdcard1.glslp",   FZ_STATE_OWNED, true);

    expect_state("unowned-reference.glslp",                 FZ_STATE_FOREIGN, false);
    expect_state("unowned-reference-near-miss.glslp",       FZ_STATE_FOREIGN, false);
    expect_state("unowned-legacy-abs-near-miss.glslp",      FZ_STATE_FOREIGN, false);
    expect_state("unowned-double-reference.glslp",          FZ_STATE_FOREIGN, false);
    /* Both references resolve to Fugazi, so only the "exactly one" rule
       rejects this. Without it the file above passes for the wrong reason. */
    expect_state("unowned-two-fugazi-references.glslp",     FZ_STATE_FOREIGN, false);
    expect_state("unowned-reference-plus-directive.glslp",  FZ_STATE_FOREIGN, false);
    expect_state("unowned-full-preset.glslp",               FZ_STATE_FOREIGN, false);
    expect_state("malformed-empty.glslp",                   FZ_STATE_FOREIGN, false);
    expect_state("malformed-truncated.glslp",               FZ_STATE_FOREIGN, false);
    expect_state("malformed-binary.glslp",                  FZ_STATE_FOREIGN, false);
    expect_state("oversized.glslp",                         FZ_STATE_FOREIGN, false);

    /* Absent, and a directory where a preset should be. */
    char global[1024], backup[1024];
    reset_work(global, sizeof(global), backup, sizeof(backup));
    fz_status st;
    fz_preset_inspect(global, backup, &st);
    check(st.state == FZ_STATE_ABSENT, "absent: state %s", state_name(st.state));

    if (mkdir(global, 0755) == 0) {
        fz_preset_inspect(global, backup, &st);
        check(st.state == FZ_STATE_INVALID,
              "directory at the preset path: state %s", state_name(st.state));
    }
}

/* -------------------------------------------------------- 2. paired states */

static void test_paired_states(void)
{
    puts("paired global + backup states");
    char global[1024], backup[1024];
    fz_status st;

    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("owned-relative.glslp", global);
    place("unowned-full-preset.glslp", backup);
    fz_preset_inspect(global, backup, &st);
    check(st.state == FZ_STATE_OWNED_BACKUP, "owned+backup: %s", state_name(st.state));

    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-reference.glslp", global);
    place("unowned-full-preset.glslp", backup);
    fz_preset_inspect(global, backup, &st);
    check(st.state == FZ_STATE_FOREIGN_BACKUP, "foreign+backup: %s", state_name(st.state));

    /* A backup with no global is a half-finished removal, not a conflict. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-full-preset.glslp", backup);
    fz_preset_inspect(global, backup, &st);
    check(st.state == FZ_STATE_ABSENT, "backup only: %s", state_name(st.state));
}

/* -------------------------------------------------------------- 3. install */

static void test_install(void)
{
    puts("install");
    char global[1024], backup[1024], keep[1024];
    fz_status st;

    /* Absent -> installs, no backup invented. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    check(fz_preset_install(global, backup, false) == FZ_OK, "absent: install failed");
    fz_preset_inspect(global, backup, &st);
    check(st.state == FZ_STATE_OWNED, "absent: post-state %s", state_name(st.state));
    check(!file_exists(backup), "absent: install created a backup");

    /* Legacy owned -> rewritten to the relative form, no backup touched. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("owned-legacy-abs-quoted-sdcard.glslp", global);
    check(fz_preset_install(global, backup, false) == FZ_OK, "legacy: install failed");
    fz_preset_inspect(global, backup, &st);
    check(st.state == FZ_STATE_OWNED && !st.legacy_reference,
          "legacy: not migrated to the relative form");
    check(!file_exists(backup), "legacy: migration created a backup");

    /* Foreign, no consent -> untouched. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-full-preset.glslp", global);
    snprintf(keep, sizeof(keep), "%s/keep", work);
    copy_file(global, keep);
    check(fz_preset_install(global, backup, false) == FZ_NEEDS_CONFIRM,
          "foreign: install without consent did not ask");
    check(same_bytes(keep, global), "foreign: refused install still modified the file");
    check(!file_exists(backup), "foreign: refused install created a backup");

    /* Foreign, with consent -> predecessor preserved byte-for-byte. */
    check(fz_preset_install(global, backup, true) == FZ_OK, "foreign: consented install failed");
    fz_preset_inspect(global, backup, &st);
    check(st.state == FZ_STATE_OWNED_BACKUP, "foreign: post-state %s", state_name(st.state));
    check(same_bytes(keep, backup), "foreign: predecessor not preserved exactly");

    /* Owned + backup -> rewrite in place, backup untouched. */
    check(fz_preset_install(global, backup, false) == FZ_OK, "owned+backup: reinstall failed");
    check(same_bytes(keep, backup), "owned+backup: reinstall disturbed the backup");

    /* Foreign + backup -> conflict, both files untouched. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-reference.glslp", global);
    place("unowned-full-preset.glslp", backup);
    char keep_g[1024], keep_b[1024];
    snprintf(keep_g, sizeof(keep_g), "%s/keep_g", work);
    snprintf(keep_b, sizeof(keep_b), "%s/keep_b", work);
    copy_file(global, keep_g);
    copy_file(backup, keep_b);
    check(fz_preset_install(global, backup, true) == FZ_CONFLICT,
          "foreign+backup: install did not fail closed");
    check(same_bytes(keep_g, global), "foreign+backup: global was modified");
    check(same_bytes(keep_b, backup), "foreign+backup: backup was modified");
}

/* --------------------------------------------------------------- 4. remove */

static void test_remove(void)
{
    puts("remove");
    char global[1024], backup[1024], keep[1024];
    fz_status st;

    reset_work(global, sizeof(global), backup, sizeof(backup));
    check(fz_preset_remove(global, backup) == FZ_ABSENT, "absent: remove did not report absent");

    /* Owned, no backup -> gone. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("owned-relative.glslp", global);
    check(fz_preset_remove(global, backup) == FZ_OK, "owned: remove failed");
    check(!file_exists(global), "owned: preset still present after remove");

    /* Owned + backup -> predecessor restored byte-for-byte, backup consumed. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("owned-relative.glslp", global);
    place("unowned-full-preset.glslp", backup);
    snprintf(keep, sizeof(keep), "%s/keep", work);
    copy_file(backup, keep);
    check(fz_preset_remove(global, backup) == FZ_OK, "owned+backup: remove failed");
    check(same_bytes(keep, global), "owned+backup: predecessor not restored exactly");
    check(!file_exists(backup), "owned+backup: backup survived its own restore");

    /* Foreign -> refused, untouched. This is the case that protects a user who
       replaced Fugazi's preset from inside RetroArch. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-full-preset.glslp", global);
    copy_file(global, keep);
    check(fz_preset_remove(global, backup) == FZ_NOT_OWNED, "foreign: remove was not refused");
    check(same_bytes(keep, global), "foreign: refused remove modified the file");

    /* Foreign + backup -> also refused; the resolver owns this state. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-reference.glslp", global);
    place("unowned-full-preset.glslp", backup);
    check(fz_preset_remove(global, backup) == FZ_NOT_OWNED,
          "foreign+backup: remove was not refused");
    check(file_exists(global) && file_exists(backup),
          "foreign+backup: refused remove deleted something");
    (void)st;
}

/* ------------------------------------------------------------- 5. resolver */

static void test_resolver(void)
{
    puts("conflict resolver");
    char global[1024], backup[1024], keep_g[1024], keep_b[1024];

    /* Keep current: discards only the backup. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-reference.glslp", global);
    place("unowned-full-preset.glslp", backup);
    snprintf(keep_g, sizeof(keep_g), "%s/keep_g", work);
    copy_file(global, keep_g);
    check(fz_preset_keep_current(global, backup) == FZ_OK, "keep current failed");
    check(same_bytes(keep_g, global), "keep current altered the current preset");
    check(!file_exists(backup), "keep current left the backup behind");

    /* Restore previous: replaces only the current global. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-reference.glslp", global);
    place("unowned-full-preset.glslp", backup);
    snprintf(keep_b, sizeof(keep_b), "%s/keep_b", work);
    copy_file(backup, keep_b);
    check(fz_preset_restore_previous(global, backup) == FZ_OK, "restore previous failed");
    check(same_bytes(keep_b, global), "restore previous did not restore exactly");
    check(!file_exists(backup), "restore previous left the backup behind");

    /* Neither action applies outside the conflict state. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("owned-relative.glslp", global);
    check(fz_preset_keep_current(global, backup) == FZ_NOT_OWNED,
          "keep current ran outside the conflict state");
    check(fz_preset_restore_previous(global, backup) == FZ_NOT_OWNED,
          "restore previous ran outside the conflict state");
}

/* ------------------------------------------------------ 6. injected failure */

static void test_faults(void)
{
    puts("injected filesystem failure");
    char global[1024], backup[1024], keep[1024];
    fz_status st;

    /* Write fails on a fresh install: nothing is left behind. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    fz_faults.fail_next_write = 1;
    check(fz_preset_install(global, backup, false) == FZ_ERR_IO, "absent: write fault not reported");
    fz_faults.fail_next_write = 0;
    check(!file_exists(global), "absent: failed install left a preset");

    /* Rename fails on a fresh install: no temp file survives as ownership. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    fz_faults.fail_next_rename = 1;
    check(fz_preset_install(global, backup, false) == FZ_ERR_IO, "absent: rename fault not reported");
    fz_faults.fail_next_rename = 0;
    check(!file_exists(global), "absent: failed install left a preset");
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.fugazi-tmp", global);
    check(!file_exists(tmp), "a stale temp file survived a failed install");

    /* Replacing a foreign preset, first rename fails: the user's file stays put. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-full-preset.glslp", global);
    snprintf(keep, sizeof(keep), "%s/keep", work);
    copy_file(global, keep);
    fz_faults.fail_next_rename = 1;
    check(fz_preset_install(global, backup, true) == FZ_ERR_IO, "foreign: rename fault not reported");
    fz_faults.fail_next_rename = 0;
    check(same_bytes(keep, global), "foreign: file lost when the backup rename failed");
    check(!file_exists(backup), "foreign: a backup appeared despite the failure");

    /* Replacing a foreign preset, the write after the backup rename fails.
       The predecessor must still be recoverable -- either back in place or at
       the backup path, never gone. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("unowned-full-preset.glslp", global);
    copy_file(global, keep);
    fz_faults.fail_next_write = 1;
    check(fz_preset_install(global, backup, true) == FZ_ERR_IO, "foreign: write fault not reported");
    fz_faults.fail_next_write = 0;
    /* "Restore the prior state" means the preset is back where RetroArch reads
       it. Leaving it parked at the backup path is recoverable but still stops
       the user's shader from applying, so it does not count. */
    check(file_exists(global) && same_bytes(keep, global),
          "foreign: predecessor not restored to the preset path after a failed write");
    check(!file_exists(backup),
          "foreign: a failed install left the predecessor parked at the backup path");
    fz_preset_inspect(global, backup, &st);
    check(st.state != FZ_STATE_OWNED && st.state != FZ_STATE_OWNED_BACKUP,
          "foreign: a failed install still reported Fugazi as installed");

    /* Restore fails during remove: the backup is not destroyed. */
    reset_work(global, sizeof(global), backup, sizeof(backup));
    place("owned-relative.glslp", global);
    place("unowned-full-preset.glslp", backup);
    copy_file(backup, keep);
    fz_faults.fail_next_rename = 1;
    check(fz_preset_remove(global, backup) == FZ_ERR_IO, "remove: rename fault not reported");
    fz_faults.fail_next_rename = 0;
    check(same_bytes(keep, backup), "remove: backup lost when the restore failed");
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    if (argc > 1) fixture_dir = argv[1];
    snprintf(work, sizeof(work), "%s", "build/preset-ownership-work");

    DIR *d = opendir(fixture_dir);
    if (!d) {
        fprintf(stderr, "fixtures not found at %s\n", fixture_dir);
        return 2;
    }
    closedir(d);

    test_recognition();
    test_paired_states();
    test_install();
    test_remove();
    test_resolver();
    test_faults();

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", work);
    if (system(cmd) != 0) fputs("note: could not clean the work dir\n", stderr);

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
