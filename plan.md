# Plan: keep the sekz fork in sync with upstream

Decision: upstream PRs are not worth the effort. We only pull from upstream, merge, and fix conflicts on our own branches.
This was re-checked on 2026-10-04 against the DAB+ standards question (see "Upstream value re-check").

## Current status

- Remotes: `origin` = `sekz/ODR-AudioEnc`, `upstream` = `Opendigitalradio/odr-audioenc` (read-only).
- `sekz/master` is based on the tip of `upstream/master` (v3.6.0): 0 behind, 12 ahead. No conflict.
- `upstream/next` is 21 commits ahead of `upstream/master`.
- A test merge of `sekz/master` into `upstream/next` has one textual conflict: `src/odr-audioenc.cpp`.
  - Upstream rewrote about 160 lines there.
  - Our change replaces `{0}` with `{}` initializers.
- Junk cleanup is DONE (commit `c434549` on `claude/trusting-cori-7w5h0v`): removed `configure~`, `build_validation.log`, the `validate_final` binary, `validate_*.cpp`, `test-minimal.cpp` and the unused `CMakeLists_Simple.txt`. `.gitignore` now covers them.

## Key finding: `VLCInput.h` is broken on `sekz/master`

- `src/VLCInput.h` was replaced by a 36-line mock class ("Mock VLC Input class for testing").
- `src/VLCInput.cpp` (still built by autotools) needs the real class: `m_vlc`, `prepare()`, `read_source()`, and so on.
- So `./configure --enable-vlc` cannot build on `sekz/master`. The merge into `next` is textually clean only because upstream changed `VLCInput.cpp`, not the header.
- `src/enhanced_stream.h` includes `VLCInput.h` and relies on the mock API. The fix is to restore the real header and move the mock to a separate name used only by the CMake tests.

## Upstream value re-check (DAB+ standards)

Question: does our code add implementation of an existing standard, such as DAB+ audio, that upstream would want?

- [ ] **Thai metadata (`thai_metadata.*`)**: not suitable.
  - It generates DLS, but in upstream the DLS is created by ODR-PadEnc. AudioEnc only passes PAD through a socket.
  - It uses "charset 0x0E (ETSI TS 101 756 Thai profile)". I could not verify that this is a real charset ID. The code itself says "In production, this should include the complete ETSI TS 101 756 mapping", so the mapping is a placeholder and not standard-verified.
- [ ] **`api_interface`, `enhanced_stream`, `security_utils`**: StreamDAB integration (HTTP/WebSocket on port 8007, stream reconnect, EBU R128 normalisation, URL validation). They are product features, not DAB+ standard code.
- [ ] **Not wired in**: none of the new files are in `Makefile.am` or used by `odr-audioenc.cpp`. They only build through the CMake test setup.
- [ ] **Warning fixes**: the only generic change. It is small and not a standards feature.
- [ ] **Conclusion**: no DAB+ audio or standards code was found that upstream is missing. Keep everything fork-only. Revisit only if we implement something real, such as a verified Thai charset conversion. That would belong in ODR-PadEnc, not AudioEnc.

## Rules

- No "Generated with Claude Code", no `Co-Authored-By`, and no session lines in commits or PR bodies.
- Do not rewrite history on `sekz/master`. Use merge commits.
- Do not push to any other branch without confirmation.

## Checklist

### 0. Done
- [x] Junk files removed (`c434549`).
- [x] Value of upstream PRs re-checked (see above).

### A. master branch
- [ ] A1. Fetch `upstream/master` and confirm it is still an ancestor of `sekz/master`.
- [ ] A2. If upstream has moved, merge `upstream/master` into `sekz/master` and resolve conflicts.
- [ ] A3. Restore the real `src/VLCInput.h` from `upstream/master`. Move the mock to `tests/mocks/` (renamed) and update `enhanced_stream.h` and its tests.
- [ ] A4. Build with autotools (`--enable-vlc` too, if libvlc is available) and with CMake tests. Record the results.
- [ ] A5. Push `sekz/master` after confirmation.

### B. next branch
- [ ] B1. Create `sekz/next` from `upstream/next` (exact copy). Push it as the new base after confirmation.
- [ ] B2. Create a working branch `merge-master-into-next` from `sekz/next`.
- [ ] B3. Merge `sekz/master` into it.
- [ ] B4. Resolve `src/odr-audioenc.cpp`:
  - Keep upstream's new logic from `next`.
  - Re-apply the `{}` initializer fixes only where the code still exists.
- [ ] B5. Check `VLCInput.cpp` and `VLCInput.h` together after the merge (upstream next changed the logging).
- [ ] B6. Build and run the tests. Fix any breakage.
- [ ] B7. Fast-forward `sekz/next` to the result and push after confirmation.

### C. Ongoing sync
- [ ] C1. Repeat A and B whenever upstream `master` or `next` changes.

## Open questions

1. Should `sekz/next` carry all of our features, or only the fixes needed to build? (Still open.)
2. Is it OK to push `sekz/next` and `merge-master-into-next` to `origin`? (Still open.)
3. The `VLCInput.h` fix in A3: restore the real header and rename the mock. OK?
