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

## CMake unit tests: fixed

All 4 suites build and pass (security 49, thai 31, enhanced stream 19, API 27), warning-free, stable over repeated runs. The autotools build with `--enable-vlc` also has 0 warnings.

Build/test setup:
- libcurl is found before the `.a` library preference, so it links dynamically (the static `libcurl.a` needs its whole dependency chain).
- Added `src/api_serialization.cpp` and `src/api_websocket.cpp` to the CMake sources.

Real bugs fixed in the code (found by the tests):
- `thai_metadata.cpp`: `remove_control_characters` compared a signed `char` with 32, which deleted every non-ASCII byte, so all Thai metadata was erased. `normalize_whitespace` passed a negative `char` to `isspace` (undefined behaviour).
- `security_utils.cpp`: the same signed-`char` bug in `validate_metadata_field` and `sanitize_metadata` (Thai rejected or stripped); `sanitize_url` did not neutralise markup (now percent-encodes unsafe characters); `validate_hostname` accepted `192.168.1`; regexes were recompiled on every call; `normalize_samples_simd` zero-extended instead of sign-extending, processed only 4 of every 8 samples, and wrapped instead of clipping.
- `api_interface.cpp`: `stop()` could hang forever (blocking `accept`); request bodies were corrupted; authentication, method checks, rate limiting and request counters were never applied; JSON output was not escaped; `url_decode` used an uninitialised value; tokens came from a seeded Mersenne Twister; API key comparison was not constant-time; `SO_REUSEPORT` allowed two processes on one port.
- `enhanced_stream`: data races on `config_` and `metrics_` (now guarded; `get_config()` returns a copy).
- `ClockTAI.cpp`: `fill_bulletin` is now compiled only with `HAVE_CURL`.

Newly implemented (were declared but missing): `SecureBuffer` read/clear/resize/integrity, `MemoryPool`, `AuditLogger`, `PerformanceMonitor` accessors, `ThreadSafeQueue` (message-based), `EnhancedStreamProcessor` update/cycle/reset/statistics/reconnect, `StreamUtils::detect_stream_format`, Thai DLS/metadata validation, MessagePack encoder/decoder, a small JSON reader, and a real RFC 6455 WebSocket server on port + 1 (clients send `{"subscribe": "status"}` etc.).

Test fixes (the tests themselves were wrong): `\xInvalid` escape, a lambda passed through curl's C varargs (undefined behaviour), the HTTP helper ignored its headers and method arguments, `string(n, 'ก')` with a multi-byte literal, a peak-memory expectation that a high-water mark cannot meet, a shared-singleton counter compared as an absolute, and a test that asserted both "healthy" and "disconnected".

## Known issues (not fixed)

These are declared in headers but have no implementation. Nothing calls them (a call would fail to link), so they are latent:
- `InputValidator`: `validate_metadata_length`, `validate_string_length`, `contains_only_safe_chars`, `sanitize_filename`, `escape_html_entities`, `remove_control_characters`
- `MemoryManager::report_memory_usage`; `PerformanceMonitor::trigger_*_optimization` and the private `optimize_*`
- `SIMDProcessor`: `mix_stereo_samples_simd`, `secure_memcpy`, `secure_memset`
- `StreamUtils`: `extract_metadata_from_response`, `measure_stream_latency`
- `StreamDABApiInterface`: `handle_get_stream_info`, `handle_get_statistics`, `handle_get_thai_analysis`, `broadcast_metadata_update`, `broadcast_quality_metrics`, `cleanup_disconnected_clients`
- `ApiUtils`: `url_encode`, `is_valid_api_key`, `is_valid_client_id`, `hash_api_key`; class `SSLContext`

Other limitations:
- `EnhancedStreamProcessor` still reads `config_` without the lock in its connection code, and it needs the mock `VLCInput` (`get_current_title()` and similar do not exist on the real `VLCInput`). It is not part of the autotools build.
- The REST API and WebSocket server have no TLS (`enable_ssl` only checks that paths are set). Use a reverse proxy.
- The Thai charset ID `0x0E` and its mapping table are still unverified against ETSI TS 101 756 (see the upstream value re-check).

## Decisions made

1. `sekz/next` carries all of our features.
2. Pushing all branches to `origin` is approved.
3. The `VLCInput.h` fix (restore the real header, rename the mock) is approved and done.
