# Plan: keep the sekz fork in sync with upstream

Decision: upstream PRs are not worth the effort. We only pull from upstream, merge, and fix conflicts on our own branches.

## Current status (checked 2026-10-04)

- Remotes: `origin` = `sekz/ODR-AudioEnc`, `upstream` = `Opendigitalradio/odr-audioenc` (read-only).
- `sekz/master` is based on the tip of `upstream/master` (v3.6.0): 0 behind, 12 ahead. No conflict.
- `upstream/next` is 21 commits ahead of `upstream/master`.
- A test merge of `sekz/master` into `upstream/next` has one conflict: `src/odr-audioenc.cpp`.
  - Upstream rewrote about 160 lines there.
  - Our change replaces `{0}` with `{}` initializers.
- `src/VLCInput.h` merged cleanly, but both sides changed VLC logging, so it needs a build check.

## Rules

- No "Generated with Claude Code", no `Co-Authored-By`, and no session lines in commits or PR bodies.
- Do not rewrite history on `sekz/master`. Use merge commits.
- Do not push to any other branch without confirmation.

## Checklist

### A. master branch
- [ ] A1. Fetch `upstream/master` and confirm it is an ancestor of `sekz/master`.
- [ ] A2. If upstream has moved, merge `upstream/master` into `sekz/master` and resolve conflicts.
- [ ] A3. Build (autotools) and run the available tests. Record the result.
- [ ] A4. Push `sekz/master` after confirmation.

### B. next branch
- [ ] B1. Create `sekz/next` from `upstream/next` (exact copy). Push it as the new base.
- [ ] B2. Create a working branch `merge-master-into-next` from `sekz/next`.
- [ ] B3. Merge `sekz/master` into it.
- [ ] B4. Resolve `src/odr-audioenc.cpp`:
  - Keep upstream's new logic from `next`.
  - Re-apply the `{}` initializer fixes only where the code still exists.
- [ ] B5. Check `src/VLCInput.h` for duplicated or conflicting logging.
- [ ] B6. Build and run the tests. Fix any breakage.
- [ ] B7. Fast-forward `sekz/next` to the result and push after confirmation.

### C. Ongoing sync
- [ ] C1. Repeat A and B whenever upstream `master` or `next` changes.

## Open questions (please decide before executing)

1. Cleanup: remove junk from the fork (`configure~`, the `validate_final` binary, `test-minimal.cpp`, `validate_*.cpp`)?
   - Recommended: yes, as a separate commit on `sekz/master` before step B.
2. Should `sekz/next` carry all of our features, or only the fixes needed to build?
3. Is it OK to push `sekz/next` and `merge-master-into-next` to `origin`?
