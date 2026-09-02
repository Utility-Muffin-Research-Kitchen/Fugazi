# Global preset ownership fixtures

Inputs for `make preset-ownership-test`. Every file here is hand-written and
contains no device identifiers, credentials, ROM data, or user configuration.

Fugazi owns a `global.glslp` only when its content is a **single** recognized
`#reference` to the resolved Fugazi preset, plus blank and comment lines, within
the inspection size cap. Ownership is decided from content, never from the file
existing. See RetroArch decision 0009 in `umrk-workspace`.

## Single-file fixtures — expected `inspect()` verdict

| Fixture | Expect | Point |
| --- | --- | --- |
| `owned-relative.glslp` | owned | The form Fugazi writes now: generated-file comment plus a mount-stable relative reference |
| `owned-relative-unquoted.glslp` | owned | RetroArch's own writer may drop the quotes; strip them before comparing |
| `owned-relative-whitespace.glslp` | owned | Leading blank lines, indentation, inner run of spaces, trailing comment — all ignorable |
| `owned-legacy-abs-quoted-sdcard.glslp` | owned, needs migration | Legacy absolute form under `/mnt/sdcard` |
| `owned-legacy-abs-unquoted-sdcard.glslp` | owned, needs migration | Same, unquoted |
| `owned-legacy-abs-quoted-sdcard1.glslp` | owned, needs migration | Same preset under `/media/sdcard1`. The two mount roots swap across reboots, so recognition must key on the path **suffix** `/.config/retroarch/shaders/fugazi/fugazi.glslp`, not the root |
| `unowned-reference.glslp` | unowned | A one-line reference to somebody else's preset. Structurally identical to ours — only the target differs |
| `unowned-reference-near-miss.glslp` | unowned | Contains `fugazi.glslp` as a substring but does not end in the recognized suffix. Guards against a `strstr()` ownership check |
| `unowned-legacy-abs-near-miss.glslp` | unowned | The full legacy absolute path with `.bak` appended. Contains the recognized suffix but does not end in it — the same guard for the legacy form |
| `unowned-double-reference.glslp` | unowned | First reference is ours, second is not. A single recognized reference is required, so two is never owned |
| `unowned-two-fugazi-references.glslp` | unowned | Both references resolve to Fugazi, so only the "exactly one" rule can reject it. Without this fixture the row above passes for the wrong reason — verified by mutation |
| `unowned-reference-plus-directive.glslp` | unowned | Our reference plus a real preset directive. Only blank and comment lines may accompany the reference |
| `unowned-full-preset.glslp` | unowned | An ordinary preset a user saved in RetroArch. The file Apply must never destroy without consent |
| `malformed-empty.glslp` | unowned | Zero bytes. Present but owns nothing |
| `malformed-truncated.glslp` | unowned | Cut off mid-directive |
| `malformed-binary.glslp` | unowned | NUL and control bytes after a valid-looking reference. Must not be parsed as text past the first NUL |
| `oversized.glslp` | unowned, unread | ~135 KiB. Exceeds the inspection cap; the cap must be enforced before the file is read into memory |

The **absent** state has no file by definition and is exercised through
`dirs/absent/`.

None of the unowned fixtures may be modified by inspection. The tests compare
each one byte-for-byte before and after.

## Directory fixtures

| Directory | State |
| --- | --- |
| `dirs/absent/` | No `global.glslp` and no backup. Apply installs Fugazi directly |
| `dirs/owned-plus-backup/` | Fugazi owns the global and preserved one predecessor. Apply updates in place without touching the backup; Remove restores the predecessor byte-for-byte |
| `dirs/conflict-unowned-plus-backup/` | Someone replaced Fugazi's global while a Fugazi backup still exists. Apply must not overwrite either file — this is the three-action resolver case, and it is the only fixture where Apply refuses to install |
| `dirs/search-order/` | A legacy global in an earlier-searched config directory and a more specific preset in the canonical one. It exists to test directory-first search behavior, not to redefine RetroArch's game → content directory → core → global specificity |

`dirs/absent/.gitkeep` exists only because git does not track empty directories;
tests must ignore it.

The backup file is `global.glslp.fugazi-backup` — app-owned recovery state, not
an automatic preset. Its non-shader extension is what keeps RetroArch from
discovering it as a preset, so tests must not rename it to something RetroArch
would load.
