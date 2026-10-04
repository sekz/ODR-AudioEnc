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

## Key finding: `VLCInput.h` was broken on `sekz/master` (FIXED)

- `src/VLCInput.h` had been replaced by a mock class, but `VLCInput.cpp` needs the real one. `--enable-vlc` failed to compile (for example `no member named 'get_icy_text'`).
- Fixed in `e2c0b49`: the real header is restored (identical to upstream), and the mock lives in `tests/mocks/VLCInputMock.h`. The CMake test build selects it with `MOCK_BUILD` in `enhanced_stream.h`.

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

### A. master branch (done)
- [x] A1/A2. Upstream `master` has not moved (`20d3b59`), so there was nothing to merge.
- [x] A3. Restored the real `src/VLCInput.h`; the mock moved to `tests/mocks/VLCInputMock.h` (`e2c0b49`).
- [x] A4. Verified a clean autotools build with `--enable-vlc`, and a real 3-second encode. Before the fix the same build failed.
- [x] A5. Push `sekz/master`.

### B. next branch (done)
- [x] B1. `sekz/next` created from `upstream/next` (`7c14c4f`) as the base.
- [x] B2/B3. Merged `sekz/master` into `merge-master-into-next` (`de49f4c`).
- [x] B4. Resolved `src/odr-audioenc.cpp`: kept upstream's rename `info` -> `aac_info` and our `{}` initializer (`AACENC_InfoStruct aac_info = {};`).
- [x] B5. `VLCInput.cpp` (upstream's new logging) and the restored `VLCInput.h` build together.
- [x] B6. Autotools build with `--enable-vlc` passes; a 3-second encode at 72 kbps produced 27000 bytes as expected.
- [x] B7. `sekz/next` carries all fork features (decision 1) and is pushed.

### C. Ongoing sync
- [ ] C1. Repeat A and B whenever upstream `master` or `next` changes.

## Known issues (not fixed, unrelated to the sync)

- The CMake unit tests do not build, on `origin/master` before our changes as well:
  - `tests/test_thai_metadata.cpp:59,291`: `\x` escape with no hex digits.
  - `tests/test_api_interface.cpp`: the gmock `MockEnhancedStreamProcessor` marks methods `override`, but they are not virtual in `EnhancedStreamProcessor`.
  - So none of the 4 CMake tests have been run. These need a separate fix.
- `contrib/ClockTAI.cpp` still warns `fill_bulletin` defined but not used.

## Decisions made

1. `sekz/next` carries all of our features.
2. Pushing all branches to `origin` is approved.
3. The `VLCInput.h` fix (restore the real header, rename the mock) is approved and done.
