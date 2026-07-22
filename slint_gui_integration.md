# Slint GUI Integration — Session Notes

This documents the work done to add a Slint-based GUI (`sst_gui`) to
SST-Systems_Stress_Tester, alongside the existing console version, plus
the build-system and environment fixes required to get it running on
NixOS + Wayland.

## Summary

| Area | Status |
|---|---|
| Ninja build migration | ✅ Done |
| `CPUStressTest` dynamic thread pool (real, not fixed-count) | ✅ Done |
| Per-thread load tracking (GUI thread bars) | ✅ Done |
| Thread add/remove event notifications | ✅ Done |
| NixOS Wayland runtime fix (`LD_LIBRARY_PATH`) | ✅ Done |
| GUI builds and launches | ✅ Confirmed |
| Window icon | ⚠️ Doesn't render (Wayland limitation — see below) |
| Full clean run-through (Start → Idle) verification | ⏳ Not yet confirmed |
| `.desktop` launcher icon | ⏳ Optional, not committed to repo (machine-local) |

---

## 1. Build System: Unix Makefiles → Ninja

**Problem:** Slint's Rust-side build (via Corrosion/Cargo, pulling in
`tikv-jemalloc-sys`) failed under the default `Unix Makefiles` CMake
generator with:

```
make[3]: *** No rule to make target 's'.  Stop.
```

**Root cause:** GNU Make's jobserver protocol leaks a malformed
`MAKEFLAGS` value (`--jobserver-fds=6,7 --jobserver-auth=6,7 s`) through
Cargo into the nested `make` invocation used by `tikv-jemalloc-sys`'s
build script. The trailing `s` gets read as a literal target name
instead of the `-s` (silent) flag it was meant to be. This is a known
rough edge in how Cargo's jobserver client interacts with nested GNU
Make builds — not a bug in this project's code.

**Fix:** Switch the CMake generator to Ninja, which doesn't implement
the GNU Make jobserver protocol at all, so the malformed flag can never
be generated or leaked.

- `Scripts/build_release.sh` and `Scripts/build_profiling.sh`:
  - `cmake -DCMAKE_BUILD_TYPE=... ..` → `cmake -GNinja -DCMAKE_BUILD_TYPE=... ..`
  - `make` → `cmake --build .` (generator-agnostic; works whether the
    dir was configured with Ninja or Makefiles)
  - Added a generator-mismatch check at the top of each script that
    wipes the build dir if it was previously configured with a
    different generator, since CMake won't silently re-generate across
    generators in an existing build directory.
- `shell.nix`: added `ninja` to `packages`.

---

## 2. NixOS + Wayland Runtime Fix

**Problem:** After a successful build, running `./build/sst_gui` failed
immediately with:

```
Error from Winit backend: Error initializing winit event loop: os error ...
The wayland library could not be loaded
```

**Root cause:** Slint's winit backend `dlopen()`s `libwayland-client.so`
and related libs (`libxkbcommon`, GL) at **runtime**, not link time.
NixOS doesn't place shared libraries on a standard system-wide search
path the way most distros do — everything lives under `/nix/store/...`
hashes — so the dynamic loader can't find them unless explicitly told
where to look.

**Fix (`shell.nix`):**
- Added runtime deps to `packages`: `wayland`, `libxkbcommon`, `libGL`,
  `freetype` (`fontconfig` was already present for the build-time
  `pkg-config` dependency).
- Added an `LD_LIBRARY_PATH` export in `shellHook`, built via
  `pkgs.lib.makeLibraryPath [...]`, prepended to the existing value.

**Gotcha hit along the way:** `shellHook` in this file is a plain
`"..."` Nix string, not a `''...''` multi-line string. Inside a plain
double-quoted Nix string, `${...}` is Nix interpolation — so a literal
shell variable reference like `$LD_LIBRARY_PATH` (meant to *append* to
the existing value at shell-init time) has to be escaped as
`\$LD_LIBRARY_PATH`, or Nix tries to interpret it as a Nix variable
lookup and fails to evaluate.

**Gotcha #2 hit along the way:** `shellHook` only runs once, at
`nix-shell` *entry*. Editing `shell.nix` mid-session and re-running
commands in the same still-open shell does nothing — the shell has to
be fully exited and re-entered (`exit` then `nix-shell`) for changes to
take effect.

**Note:** `SLINT_BACKEND=winit-x11` was tried as a possible workaround
and still hit the same Wayland-load error — winit apparently still
probes for Wayland internally regardless of the backend hint, so this
confirmed the issue was purely a library-path problem, not a backend
selection problem.

---

## 3. `CPUStressTest`: From Fixed Threads to a Real Dynamic Pool

The original implementation had a `manageThreadPool()` method that was
never actually wired up — `start()` just spawned a fixed `numCores`
threads directly, so the "CPU threads (scales with load)" framing in
the planned GUI would have been misleading.

### 3.1 Bug found while wiring it up: shrink path would hang forever

The original `manageThreadPool()` shrink branch called
`cpuThreads.back().join()` on a worker whose only exit condition was
the **global** `running` flag. Since `running` stays `true` for the
entire test duration, that join would never return the moment load
ever dropped below the shrink threshold (0.25) — effectively hanging
the whole test.

**Fix:** Added a `std::vector<std::atomic<bool>> threadRunning`, one
flag per thread slot. Each worker's loop condition now checks its own
`threadRunning[threadId]` in addition to the global `running` flag, and
`manageThreadPool()` sets that flag `false` *before* joining a thread
it wants to remove.

### 3.2 Per-thread op tracking (for GUI thread bars)

Previously only an aggregate `hashOps` counter existed — no way to show
individual thread load in a GUI.

**Added:**
- `std::vector<std::atomic<uint64_t>> threadOps` — one counter per
  thread slot, incremented alongside the existing global `hashOps` in
  `cpuHashStressTest()`.
- `getThreadLoads()` — polls the *active* thread count, computes each
  thread's op-rate delta since the last poll (mirroring the existing
  `getCurrentSystemLoad()` normalization pattern), returns a
  `std::vector<float>` of `[0.0, 1.0]` values sized to the currently
  active thread count.

### 3.3 Event notifications (for the "thread added/removed" UI toast)

**Added:**
- `pushEvent(text)` / `getLatestEvent(outText, lastSeenSeq)` — a simple
  sequence-numbered event slot. The GUI polls with a `lastSeenSeq` it
  owns; `getLatestEvent` returns `true` only when a new event has
  landed since the caller's last check, avoiding string comparisons or
  missed/duplicate events across poll cycles.

### 3.4 `start()` changed

- **Before:** spawned `numCores` threads immediately.
- **After:** spawns a single worker (thread 0) and starts
  `manageThreadPool()` as a background thread, which grows the pool up
  toward `numCores` based on measured load — actually delivering on the
  "scales with load" behavior.

### 3.5 `threadPoolMutex`

Added to guard `cpuThreads` + `threadRunning` together, since
`manageThreadPool()` mutates both from a background thread while
`waitForCompletion()` and `getThreadLoads()` read/iterate `cpuThreads`
from other threads.

---

## 4. Slint UI (`ui/stress_test_ui.slint`)

Layout mirrors the console output 1:1, laid out as a real dashboard
instead of scrolling terminal text:

- **Header** — title, live status badge (Idle/Running), Start/Stop
  button (replaces the console's "Press Enter to continue" prompt).
- **Metric row** — four `MetricCard` tiles: elapsed/total time, core
  count, hash ops (pre-formatted as e.g. "4.2M" in C++, not in Slint),
  RAM bandwidth.
- **CPU panel** — one `ThreadBar` per *active* thread, growing/shrinking
  live as `manageThreadPool()` adds/removes threads, since it's a
  `for load[i] in thread-loads` loop over a `[float]` model. Includes a
  yellow toast-style tag for the latest thread-pool event, auto-hidden
  after ~3 seconds.
- **Memory panel** — single bar, allocated vs. target.
- **Duration slider** — lets the user pick test duration before
  starting, rather than the hardcoded `TEST_DURATION` constant used in
  the console version.

**Color scheme:** mirrored directly from the existing `ConsoleColors`
ANSI palette rather than generic UI defaults —

| Role | Color | Matches (console code) |
|---|---|---|
| Title/banner | Magenta | `"=== System Stress Test Starting ==="` |
| Time | Cyan | `displayTimeProgress()` |
| Memory / good bandwidth | Green | `displayMemoryStatus()`, `>20000 MB/s` case |
| Thread pool event | Yellow | `"Adding/Removing thread..."` log lines |
| Stop / danger | Red | low-bandwidth case, `bad_alloc` handler |
| Core count / info | Blue | `"Detected N CPU cores"` line |

**Design principle kept throughout:** no logic lives in `.slint` — every
value is an `in property` set from C++, every user action is a
`callback` consumed by C++ (`start-clicked`, `stop-clicked`,
`duration-changed`). Formatting (op-count → "4.2M", bandwidth →
threshold color) stays in C++/`CPUStressTest`/`MemoryStressTest` rather
than being reimplemented in Slint.

### 4.1 Warning fixed: padding on non-layout element

```
warning: padding only has effect on layout elements
```

`MetricCard` had `padding: 12px;` set directly on the component's outer
`Rectangle`, which has no effect — padding only works on layout
elements (`VerticalLayout`, `HorizontalLayout`, etc.) in Slint. The
inner `VerticalLayout` already had its own `padding: 12px;`, so the
redundant outer one was removed.

---

## 5. `src/main_gui.cpp`

New file — the Slint↔C++ glue layer, `StressTestController`:

- Owns a `CPUStressTest` and `MemoryStressTest` instance plus the
  generated `StressTestWindow` handle.
- `onStart()` / `onStop()` map directly to `start()`/`stop()` on both
  test objects; `onStop()` hands `waitForCompletion()` off to a
  detached background thread so the UI thread never blocks on join.
- A `slint::Timer` polls every 150ms (`tick()`), pushing live values
  (`elapsed_seconds`, `hash_ops_display`, `bandwidth_mbps`,
  `memory_allocated_mb`, etc.) into the window's properties.
- `updateThreadLoads()` — originally a placeholder returning a flat
  `0.75f` for every bar (before `CPUStressTest::getThreadLoads()`
  existed); now calls the real per-thread API.
- `checkThreadEvents()` — polls `CPUStressTest::getLatestEvent()`,
  shows/auto-hides the yellow event toast.
- `formatOpsCount()` / `bandwidthColor()` — local helpers mirroring the
  console version's number formatting and bandwidth color thresholds,
  just targeting Slint types (`slint::SharedString`, `slint::Color`)
  instead of `std::cout` + ANSI codes.

---

## 6. Icon

Attempted to set a window icon via:

```slint
icon: @image-url("icon.png");
```

**Result: no visible change**, even after fixing file-path issues
(typo'd filename, then a nonexistent path, then a real screenshot file
copied into `ui/`).

**Root cause:** winit — which Slint uses under the hood — explicitly
does **not** support setting the window icon on Wayland; `set_window_icon()`
is a documented no-op on that backend (works on X11/Windows/macOS).
Since the app runs under Wayland here, the property is silently
dropped, not failing to apply due to any code issue.

**Correct approach on Wayland:** the compositor looks up
taskbar/dock/alt-tab icons from an installed `.desktop` file matching
the window's `app_id`, not from anything the app sets at runtime.
Steps (machine-local, not part of the repo):

```bash
mkdir -p ~/.local/share/icons/hicolor/256x256/apps
cp ui/icon.png ~/.local/share/icons/hicolor/256x256/apps/sst-gui.png

mkdir -p ~/.local/share/applications
cat > ~/.local/share/applications/sst-gui.desktop << 'EOF'
[Desktop Entry]
Name=System Stress Tester
Exec=/path/to/SST-Systems_Stress_Tester/build/sst_gui
Icon=sst-gui
Type=Application
Categories=Utility;
StartupWMClass=sst_gui
EOF

update-desktop-database ~/.local/share/applications
gtk-update-icon-cache ~/.local/share/icons/hicolor 2>/dev/null
```

Important: the icon will only appear when launched via the `.desktop`
entry (`gtk-launch sst-gui` or the app menu) — launching directly from
a terminal (`./build/sst_gui`) will never show it, regardless of any of
the above, since the compositor only does `.desktop`→window matching
for windows it considers "launched as an application."

`StartupWMClass` was guessed as `sst_gui` (winit's default app_id
derivation from the binary name) — not yet verified against the actual
running window's `WM_CLASS`. If the icon still doesn't appear after
using `gtk-launch`, confirm the real class with `xprop WM_CLASS`
(click the window) and correct the `.desktop` file to match.

---

## 7. Known Open Items

1. **Full clean run verification not yet done.** One screenshot showed
   `Elapsed: 47 / 18s` (elapsed exceeding the selected duration) and
   `Hash ops: 0` while all 8 thread bars showed full/green — an
   inconsistent combination worth re-checking with a clean Start → Idle
   cycle now that the Wayland fix is in and the build is stable. Could
   be stale display state from a prior run rather than a real bug, but
   unconfirmed either way.
2. **`.desktop` file / icon cache files are machine-local**, intentionally
   not committed to the repo (they live under `~/.local/share/`, outside
   the project directory).
3. **`shellHook` auto-builds both `build/` and `profiling/` on every
   `nix-shell` entry** — this is why plain shell entry takes several
   minutes (6–10 min observed). Intentional per the original `shell.nix`
   design, just noting it as the reason shell startup feels slow, in
   case it's ever worth changing to build-on-demand instead.
4. **`kernel_security_bypass.sh`** also runs automatically on every
   shell entry per the existing `shellHook` wiring (needed for `perf`
   profiling — adjusts `perf_event_paranoid`/`kptr_restrict`). Pre-existing
   design, not something introduced in this session, just noted here for
   completeness since it's part of the same `shellHook` chain that was
   modified.

---

## 8. Build & Run Quick Reference

```bash
# Enter the dev shell (auto-builds both build/ and profiling/ per shellHook)
nix-shell

# Manual rebuild after editing source (fast path, no full shell re-entry)
cd build && ninja && cd ..

# Run the GUI
./build/sst_gui

# Run the original console version (unaffected by any of this work)
./build/sst_main   # or whatever the console binary target is named
```

## 9. Git

Committed and pushed to `origin/slint_UI`:

```
10 files changed, 704 insertions(+), 53 deletions(-)
 modified:   .gitignore, CMakeLists.txt, Scripts/build_profiling.sh,
             Scripts/build_release.sh, include/CPUStressTest.hpp,
             shell.nix, src/CPUStressTest.cpp
 new file:   src/main_gui.cpp, ui/icon.png, ui/stress_test_ui.slint
```

`.cache/` (clangd index) added to `.gitignore` — machine-local tooling
metadata, not source.