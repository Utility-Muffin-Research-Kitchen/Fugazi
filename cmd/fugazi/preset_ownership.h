/*
 * Fugazi global-preset ownership.
 *
 * RetroArch's automatic global preset is durable user state. Fugazi may replace
 * a preset it recognizes as its own; replacing anyone else's takes explicit
 * consent and preserves exactly one predecessor. Ownership is decided from file
 * CONTENT, never from the file existing -- acting on existence is what destroys
 * a preset the user saved in RetroArch's own UI.
 *
 * No SDL, no GL, no Catastrophe: this compiles and tests natively.
 * See RetroArch decision 0009 in umrk-workspace.
 */
#ifndef FUGAZI_PRESET_OWNERSHIP_H
#define FUGAZI_PRESET_OWNERSHIP_H

#include <stdbool.h>
#include <stddef.h>

/* The reference Fugazi writes now. Relative, so it survives the /mnt/sdcard
   and /media/sdcard1 swap; resolved against the preset's own directory. */
#define FZ_REFERENCE_RELATIVE "../shaders/fugazi/fugazi.glslp"

/* Pre-relative Fugazi wrote an absolute mount path. Recognition keys on this
   suffix so a preset written under one mount root is still ours under the
   other. Must be a suffix match, never a substring search. */
#define FZ_REFERENCE_LEGACY_SUFFIX "/.config/retroarch/shaders/fugazi/fugazi.glslp"

/* Appended to the global preset path. A non-shader extension on purpose:
   RetroArch must never discover the preserved predecessor as a preset. */
#define FZ_BACKUP_SUFFIX ".fugazi-backup"

/* Nothing legitimate approaches this. The cap is enforced before the file is
   read, so an oversized preset costs a stat() rather than a read. */
#define FZ_MAX_INSPECT_BYTES (64 * 1024)

typedef enum {
    FZ_STATE_ABSENT = 0,       /* no global preset; Fugazi is not applied */
    FZ_STATE_OWNED,            /* Fugazi owns the global preset */
    FZ_STATE_OWNED_BACKUP,     /* Fugazi owns it and preserved a predecessor */
    FZ_STATE_FOREIGN,          /* another global preset is active */
    FZ_STATE_FOREIGN_BACKUP,   /* another is active AND a Fugazi backup exists */
    FZ_STATE_INVALID,          /* unreadable or not a regular file */
} fz_state;

typedef struct {
    fz_state state;
    /* Owned through the legacy absolute form. The next safe rewrite migrates it
       to the relative form; merely inspecting it never rewrites anything. */
    bool legacy_reference;
} fz_status;

typedef enum {
    FZ_OK = 0,
    FZ_NEEDS_CONFIRM,  /* foreign preset present and allow_replace was false */
    FZ_CONFLICT,       /* foreign preset AND a backup: resolver required */
    FZ_NOT_OWNED,      /* asked to remove a preset Fugazi does not own */
    FZ_ABSENT,         /* nothing to act on */
    FZ_ERR_PATH,       /* path too long to derive a temp/backup name */
    FZ_ERR_IO,         /* write or rename failed; prior state was restored */
} fz_result;

/* Classify the current state. Never modifies anything on disk. */
fz_result fz_preset_inspect(const char *global_path, const char *backup_path,
                            fz_status *out);

/* Install Fugazi's reference as the global preset.
   ABSENT          -> write it
   OWNED[_BACKUP]  -> rewrite in place, backup untouched (migrates legacy form)
   FOREIGN         -> allow_replace ? preserve predecessor then install
                                    : FZ_NEEDS_CONFIRM
   FOREIGN_BACKUP  -> FZ_CONFLICT; never overwrite, the resolver decides */
fz_result fz_preset_install(const char *global_path, const char *backup_path,
                            bool allow_replace);

/* Disable Fugazi.
   OWNED         -> remove the global preset
   OWNED_BACKUP  -> restore the preserved predecessor over it
   FOREIGN*      -> FZ_NOT_OWNED; Fugazi never deletes someone else's preset */
fz_result fz_preset_remove(const char *global_path, const char *backup_path);

/* The two destructive halves of the FZ_STATE_FOREIGN_BACKUP resolver. Each
   names one file it discards and touches nothing else. Cancel is the absence of
   a call. Both require FZ_STATE_FOREIGN_BACKUP and otherwise return FZ_NOT_OWNED. */
fz_result fz_preset_keep_current(const char *global_path, const char *backup_path);
fz_result fz_preset_restore_previous(const char *global_path, const char *backup_path);

/* Derive "<global_path>" + FZ_BACKUP_SUFFIX. Returns FZ_ERR_PATH on truncation. */
fz_result fz_preset_backup_path(const char *global_path, char *out, size_t out_sz);

/* Test-only fault injection. When a counter is > 0 it is decremented and the
   operation fails as if the filesystem had. Zero in normal use; the production
   build never sets these. */
typedef struct {
    int fail_next_write;
    int fail_next_rename;
    int fail_next_unlink;
} fz_fault_hooks;

extern fz_fault_hooks fz_faults;

#endif /* FUGAZI_PRESET_OWNERSHIP_H */
