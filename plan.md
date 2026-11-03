# yams: Polish and Feature Plan

Audit date: 2026-10-25. Roughly 4,200 LOC C (ncursesw, chafa, ffmpeg, playerctl), meson/Make/Nix build, cmocka tests.
Priorities: P0 = bugs first, P1 = health, testing, config, P2 = larger features.

## Follow-up (2026-11-03)

- [x] **Art-mode switch abort** — `extract_frame` hardened: single-threaded decode, packet
  timebase, flush on `EAGAIN`, dimension validation. ASAN and Valgrind runs over H264,
  VP9, and attached-art MP3 media are clean.
- [x] **Synced lyrics** — LRC timestamps are parsed and the position is interpolated between
  metadata updates; the active line is highlighted and auto-scrolled into view.
- [x] **Stale/partial metadata** — `r` restarts the metadata stream and forces a lyric
  reparse; track identity now includes the file path; data-art markers split across reads
  are carried into the next read instead of dropping the whole metadata block.
- [x] **Instant pause/play** — status flips optimistically before playerctl confirms, and
  control commands use `posix_spawnp` instead of `fork`/`exec`.
- [x] **Responsive selector** — geometry moved to `compute_selector_layout`; the selector
  resizes in place on `KEY_RESIZE`; art geometry is clamped to the real terminal.
- [x] **Deterministic layout tests** — real `newterm`/`newwin`/panel instances across
  4x10 through 60x240, plus stream data-art regression tests.

## P0: Bug fixes and correctness

- [x] **1. Corrupt art cache poisoning**
Context: `download_to_cache` (src/art.c:389-412) loops on `avio_read` and exits the loop
on any value <= 0. A network error or short body is indistinguishable from EOF, so a
partial/failed download is written straight to the final cache path, and then acts as a
permanent cache hit from `render_art_to_buffer` (src/art.c:527-538). `unlink` only happens
on fwrite failure (src/art.c:404).
Fix: treat `n < 0` as failure, write to a temp path (same dir, distinct name) and rename
into place only after a clean EOF, `unlink` on every early exit, and optionally sanity
check minimum size or re-open with avformat before committing.

- [x] **2. Unchecked asprintf calls in art.c**
Context: 9 call sites return unchecked: src/art.c:382 (`get_cached_path`),
417, 419, 421 (`format_size`), 443, 453, 457, 477, 487, 492 (cache info and clear).
On failure glibc returns -1 and leaves the pointer undefined, so `free(path)` and
`snprintf(..., result)` become UB. Note `get_cache_dir` (src/art.c:337-346) checks
`ret > 0` but silently leaks/drops on failure and falls through, acceptable but inconsistent.
Fix: central `xasprintf` helper or per-site checks; also fix the two unchecked
`asprintf`/`stat` paths in `get_cache_info` and `clear_art_cache`.

- [x] **3. Unchecked allocations in player list handling**
Context: src/ui.c:374-379, `fetch_player_list` reallocs into the same `list` variable
(leaks everything on failure), `strdup(line)` is unchecked and `*count` increments anyway.
src/ui.c:404-407, `show_player_selector` mallocs `labels` and strdups items unchecked.
Fix: temp pointer for realloc, bail cleanly on OOM, free already-collected entries.

- [x] **4. Non-async-signal-safe exit handler**
Context: `handle_exit` (src/main.c:43-54) is installed via `signal()` for SIGINT/SIGTERM
(src/main.c:569-570) and calls `fprintf` (DBG), `free_art`, `endwin` and `exit(0)` (from
`cleanup_ui`, src/terminal.c:39-45). All non-async-signal-safe, plus `exit(0)` runs from
signal context, so `_exit` vs `exit` behavior and pending stdio are undefined. `cleanup_ui`
also ignores its `sig` argument.
Fix: `sigaction` handlers only set a sig_atomic_t flag; cleanup runs in the main loop
before returning. Keep a tiny async-safe `_exit` fallback if cleanup itself faults.

- [x] **5. Fixed-size overlay dialogs crash on small terminals**
Context: help overlay uses hardcoded h=17, w=45 (src/main.c:346) and the seek dialog h=3,
w=30 (src/main.c:135); `newwin` results are unchecked, so a terminal smaller than the
dialog yields NULL passed to `wbkgd`/`box` and crashes. Overlays also have no resize
reaction while open.
Fix: clamp dialog size to current `getmaxyx`, NULL-check `newwin`, and bail or redraw
on failure. Also verify the art selector (src/ui.c:151-336, w=44) has the same clamping.
Update: the art/player selector now uses `compute_selector_layout` and resizes in place.

- [x] **6. Unchecked fork/dup2/library returns**
Context:
- `run_cmd` (src/player_backend_mpris.c:18-44): `fork() == -1` ignored (child block runs
  nothing, parent sets ctrl_pid wrongly on success path only, alloc failure means no op).
- `fetch_player_list` (src/ui.c:343-366): no dup2 checks, child exit status discarded.
- `mpris_has_player` (src/player_backend_mpris.c:135-136): single one-shot `read` of at most
  127 bytes; a long player list (many browsers auto-register MPRIS names) is truncated,
  and short reads can miss lines entirely.
- `sws_scale` result ignored (src/art.c:190).
Fix: check returns, loop reads until EOF in `mpris_has_player`, log or handle child status.

- [x] **7. Main loop robustness**
Context: `select` (src/main.c:635-641) breaks the process silently on any non-EINTR error.
Control commands reaped only opportunistically: `reap_ctrl` (src/player_backend_mpris.c:11-16)
runs only when the next command fires, so `playerctl next` children can linger as zombies
indefinitely. `metadata_pid` exit status is discarded (src/main.c:528), so playerctl format
errors are invisible.
Fix: continue on error after logging (or treat EINTR/EAGAIN specially), install a SIGCHLD
no-handler with waitpid loop, log exit status once for the metadata child.

## P1: Code health

- [x] **8. Remove dead code**
- `is_art_active` (src/art_manager.c:41-43) and `get_last_render_time` (37-39), declared in
  include/art_manager.h but never called anywhere.
- PlayerBackend vestigial fields: `get_metadata`, `get_status`, `set_volume` (declared in
  include/player_backend/player_backend.h, set to NULL in src/player_backend_mpris.c:164-166)
  plus `TrackInfo`/`PlaybackStatus` types. Decide: remove now, or keep only if P2 volume
  feature lands (see item 21).
- include/terminal.h declares `init_terminal`/`cleanup_terminal`, which do not exist; the
  real functions are `init_ui`/`cleanup_ui` (src/terminal.c:9,39). Fix the header names.
- `query_initial_metadata` (src/main.c:62-66) ignores its `fmt` param, pure indirection.

- [x] **9. Unify duplicated logic**
- `is_remote` (src/art_manager.c:29-31) is byte-identical to `is_remote_url` (src/art.c:374-376).
  Keep one in a shared header.
- Data-URI stripping exists twice: `strip_data_art` (src/main.c:68-82) for complete blocks,
  and an inline chunk-boundary variant in `handle_player_data` (src/main.c:444-476) with a
  static `art_skip` flag. Unify into one well-tested function (or move stripping behind the
  backend parse path).
- Metadata snapshot blocks: 5 strncpys in `draw_dashboard` (src/ui.c:43-52) and 9 in
  `render_metadata_pane` (src/panes.c:14-22). Extract a `snapshot_metadata(MetadataSnap *)`
  helper.
- Fallback text generation: `make_fallback` (src/art_manager.c:78-94) vs inline "Retrying
  art..." builder (src/art_manager.c:158-177). One parameterized builder.
- Two list-selection dialogs with the same scroll/select/filter loop: art mode selector
  (src/ui.c:151-336, includes fuzzy filter) and player selector (src/ui.c:386-484). Extract
  one reusable overlay widget (title, items, optional filter, callback on select).
- Subprocess listing: `fetch_player_list` (src/ui.c:338-384) vs `mpris_has_player`
  (player_backend_mpris.c:113-148) both fork/exec playerctl -l. Share one capture helper
  with proper read-until-EOF.

- [x] **10. Split long functions**
- `extract_frame` (src/art.c:25-204): fail-path teardown repeated 9 times; convert to goto
  cleanup pattern.
- `show_art_mode_selector` (src/ui.c:151-336): extract filter matching, item rendering,
  key handling (most will merge into the shared widget from item 9).
- `handle_keypress` (src/main.c:203-336): the 'p' case is 28 lines of subprocess teardown
  (src/main.c:231-258); move into a backend-owned `restart_stream` function.
- `handle_player_data` (src/main.c:415-523): extract overflow handling (src/main.c:478-501)
  and legacy stripping; both become unit-testable (feeds item 12).
- `draw_dashboard` (src/ui.c:34-149): 4 static "memory" variables and mixed duties; split
  resize check, art lifecycle, status line, and render phases.
- `render_art_to_buffer` (src/art.c:500-618): extract cache resolution and render dispatch.

- [ ] **11. Layering and header hygiene**
- include/ui.h declares `reset_art`/`get_last_metadata_change_time`, implemented in
  art_manager.c, under a comment noting this; move to art_manager.h and include there.
- src/layout.c (low-level window creation) calls `reset_art()`/`expire_art_debounce()`
  (src/layout.c:69-70); better to return a "layout changed" bool and let callers trigger
  art reset.
- metadata.h includes stdbool before the guard; player_backend.h uses `#pragma once`
  while every other header uses include guards; unify.
- Magic key codes mixed with KEY_* enums in `handle_keypress` (src/main.c:314-323),
  including 410 (KEY_RESIZE) which is swallowed while resize is handled out of band via
  SIGWINCH; name them or drop them.

- [ ] **12. Repo/build hygiene**
- `builddir/` is in the working tree but not covered by .gitignore (which covers only
  `build/` and `build-*/`), and contains stale objects for a removed img2ascii feature;
  delete locally and add to .gitignore.
- Generated dev artifacts at repo root: `yams.log`, `valgrind_report.txt` (gitignored but
  present). Fine locally, but the log location itself is wrong: see next points.
- `init_debug` opens `yams.log` in the CWD (include/debug.h:10); move to
  `${XDG_STATE_HOME:-~/.local/state}/yams/` and add the `--log-file` override when CLI
  flags land (item 16).
- Repo URL three-way mismatch: README says moved to GitHub (README lines 11-12), but clone
  URL at README line 63 and the fallback screen URL (src/render_utils.c:184) still point to
  Codeberg. Pick the canonical URL and fix all three.
- tofix.md is deleted in the working tree (`git status: D tofix.md`); either commit the
  deletion or restore it. It contained the plan to move off CLI calls (feeds item 22).

- [ ] **13. Build config drift**
- Version is hardcoded in meson.build:2 (1.0.0) and flake.nix:31 (1.0.0) and never compiled
  into the binary; emit a `#define` via meson (or use `configure_file`) so `--version`
  (item 16) is possible. flake.nix can read the version from meson.build.
- `-DNDEBUG` is tied to the `logging` option (meson.build:29-31) so disabling logging also
  disables assertions; separate them into two concerns. Also note Makefile COMMON_FLAGS
  itself includes `-DNDEBUG` (Makefile line 14), applied only in release targets.
- Makefile quirks worth fixing while touching it: `run` always depends on clean+build
  (lines 47-50), `test` hides meson reconfigure errors with 2>/dev/null (line 58), `install`
  does `rm -rf build` unconditionally (line 115), `ps` target uses `pgrep -n yams` which can
  match the installed release binary instead of the dev one.
- Build config duplicated across Makefile release targets and flake.nix mesonFlags with no
  common source; consider a `meson/wrap`-style canonical script or leave documented.

## P1: Testing

- [ ] **14. New test coverage (cmocka)**
Currently covered: parse_block (test_metadata, 450 lines), url_decode and setters (test_utils),
config clamps (test_config), trim/bbox (test_art), cache info/clear (test_art_cache),
compute_layout (test_layout).
Added since the audit: stream/data-art parsing (`tests/test_stream.c`) and real-ncurses
layout/selector geometry (`tests/test_ncurses.c`).
Zero coverage today:
- Art manager retry/debounce state machine (`update_and_render_art` state transitions,
  retry delays 1000/2000/5000/10000/30000 at src/art_manager.c:25, fallback text paths).
- Backend: arg construction in `mpris_spawn_stream` (needs a small injection point for
  execvp), `has_player` list parsing.
- `download_to_cache` failure paths (202 stub or a local file:// url; ties into item 1).

- [ ] **15. Test and build tweaks**
- tests/test_art_cache.c teardown shells out with `system("rm -rf '%s'")` on an mkdtemp path;
  replace with NFTW remove walk (removes system() from tests entirely).
- CI workflow: build (debug + release-dynamic), meson test, valgrind smoke run gated by
  YAMS_EXIT_IMMEDIATELY style env (already exists at src/main.c:563-567, useful for CI
  startup tests).

## P1: Config file and CLI (README "Planned" items)

- [ ] **16. Config file**
Context/impl notes:
- Existing accessors to persist: art mode (utils.c:51-61, clamp 0..ART_MODE_COUNT-1),
  seek interval (utils.c:39-49, clamp 1..60), debounce (config.c:34-42, clamp 0..5000, default 200),
  selected player (config.c:44-53, 64 chars). No file I/O exists today; everything resets
  per launch.
- Path: `${XDG_CONFIG_HOME:-~/.config}/yams/config` plus `--config PATH` override.
- Format: plain `key = value`, parse with no dependency; keys: art_mode, seek_interval,
  art_debounce, player. Unknown keys ignored with a debug log. Write atomically (tmp +
  rename) on preference change, debounced so selector churn does not thrash disk.
- Keep runtime-only state (status messages, retry timers) out of it.
- Update test_config.c to cover load/save round trip and clamping on import.

- [ ] **17. CLI flags and man page**
- `getopt_long`: `--help`, `--version`, `--player NAME`, `--art-mode MODE` (accepts the 24
  names in art_modes, config.c:5-32), `--interval N`, `--cache-dir PATH`, `--config PATH`,
  `--log-file PATH`. Unknown flags should error to stderr and exit nonzero (today argv is
  silently ignored, main() takes no args, src/main.c:560).
- CLI should override config file values; config file overrides defaults.
- Man page `yams.1` installed via `install_data` in meson.build and Makefile install paths;
  source it from the single keymap table (item 19).

## P2: tldr and tealdeer page

- [ ] **18. Ship a yams tldr page in our own directory**
Goal: user runs `tldr yams` or `tldr -a yams` with tealdeer, but the page must not live in
the shared pages/caches that get wiped and re-fetched on every `tldr -u` / `tealdeer update`.
- Page file: `yams.md` in tldr-pages markdown format (front-matter optional, title, info
  sentence, usage lines with > descriptions, example commands).
- Install location, per PREFIX:
  user:   `${XDG_DATA_HOME:-~/.local/share}/yams/tldr/yams.md`
  system: `${PREFIX}/share/yams/tldr/yams.md` (i.e. `/usr/local/share/yams/tldr/`)
- Wire up:
  - tealdeer (>= 1.x): custom dir via `~/.config/tealdeer/config.toml`:
    `[directories] custom_pages = "/usr/local/share/yams/tldr"` (user path when not root).
    Document this in README and print the snippet from `yams --help` or at install time.
  - GNU tldr (blue) client: supports `TLDR_PAGES_DIR`; mention it in README for parity.
  - tldr C client and others vary; do not promise, just document tealdeer as primary.
- Registering the dir does not affect the standard `pages` dir, and updates stay isolated.
- Fallback path so it works with no client: embed a plain-text copy readable via
  `yams --help-tldr` (or just print the man page key section); minimal cost, big benefit.
- meson: `install_data('docs/tldr/yams.md', install_dir: get_option('datadir') / 'yams' / 'tldr')`.
  Makefile needs nothing extra if it just runs `ninja -C build install`.
- Content draft (match on-disk keymap, src/main.c:211-335):
  `yams` start it; `q` quit; `Space` play/pause; `h`/`l` (also arrows) prev/next;
  `j`/`k` seek by interval; `+`/`-` adjust interval 5s steps; `s` set interval numerically;
  `a` art mode selector with type-to-filter; `p` player selector; `r` force art refresh;
  `c` clear art cache; `?` help overlay.
  Keep only ASCII art and simple backtick commands so all clients render it.

## P2: Docs and UX consistency

- [ ] **19. One keymap table**
Context: help overlay (main.c:362-381, HELP_L macro 12 entries), metadata pane hints
(render_utils.c:58-103, omits j/k descriptions entirely), README features, and future
man page and tldr page all list keys independently and already drift.
Fix: a static `{key, desc, pane_visible}` table in config/h, rendered by the overlay, the
pane hints, and exported for the man page/tldr generation (generation optional; manual sync
from one source is fine at this size).

- [ ] **20. README Known Issues, either fix or drop**
- tmux focus-away means exit requires Ctrl+C instead of q: likely ncurses key state and
  bracketed paste handling in init_ui (src/terminal.c:24-26 disables bracketed paste, but
  mouse/paste state after focus events needs checking with tmux enabled).
- Sixel art too small at high resolution: `render_pixel_to_gs` (src/art.c:294-331) uses fixed
  cell geometry (10, 20) at src/art.c:298; query real cell pixels (CSI 16 t or tmux passthrough)
  and scale geometry accordingly.
- [x] New lyrics require prev+next to load: LYRICS is keyed on `song_changed`
  (src/metadata.c:90), so lyric-only updates do not reset `lyric_count`; reparse LYRICS on
  any value change instead of only after title change. Fixed: lyrics reparse on text change
  and on manual refresh; `r` also restarts the metadata stream.
- Last-active player not auto-updating after switching back to Auto: switching to "Auto"
  clears the player name (src/ui.c:460-462) but there is no follow-mode reset beyond
  `maintain_subprocesses` restarts; investigate playerctl `--player` follow semantics.

- [ ] **21. Smaller niceties**
- Volume control: backend vtable slot `set_volume` exists (player_backend.h); wire to
  `playerctl volume N` and add keys (maybe `[`/`]`), requires no external changes.
- Lyrics scroll keys: lyric pane renders from line 0 (src/panes.c:54-59), no scrolling for
  long lyrics; add scroll offset with j/k conflict resolution (only when lyrics pane focused,
  or use PGUP/PGDOWN). Partial: the active synced line now auto-scrolls; manual scroll is
  still open.
- Add `SPDX-License-Identifier: MIT OR Apache-2.0` headers across sources to match the dual
  license (currently only at repo level).
- Confirm `default` keypress behavior: any printable key opens help (src/main.c:330-334),
  which disables it as a filter target if future features need keys; consider removing.

## P2: Deferred / separate initiative

- [ ] **22. Native MPRIS via libgio/GDBus**
Context: current runtime model forks `playerctl metadata --follow` and parses its
format string output (src/main.c:578-585) with a hand-rolled tab-delimited protocol and
`[[END]]` framing, plus fork/exec for every control command (src/player_backend_mpris.c).
The deleted tofix.md planned moving off CLI calls. glib is already a dependency via chafa.
Scope: swap the backend implementation to GDBus (own name watcher org.mpris.MediaPlayer2.*,
PropertiesChanged subscription, seek/play calls via MPRIS interface), keep the PlayerBackend
vtable so it is a drop-in for parse_chunk/spawn_stream. Big win: no subprocess lifecycle,
no truncated-list read, no framing bug class, faster metadata latency.
Cost: highest item by far; also removes playerctl as a runtime dependency (README/docs
dep table change). Only start after P0/P1 land, behind its own decision point.
Partial: control commands now use `posix_spawnp` and no longer fork; the metadata backend
and framing remain on playerctl.
