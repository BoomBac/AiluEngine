# M2–M4 acceptance — 2026-09-30

## Scope and result

M2 was implemented as the prerequisite for the requested M3/M4 work. M0–M4 are accepted; M5/M6 were not started.

| Gate | Evidence | Result |
| --- | --- | --- |
| M2 lifecycle | PluginManagerSmoke.exe | PASS, exit 0 |
| M3 threaded MVP | PluginSmoke.exe | 10 passed, 0 failed; x100; exit 0 |
| M4 empty Editor | normal WM_CLOSE after 10 frame callbacks | PASS, exit 0 |
| M4 threaded Editor | Tick=11, worker=119; join → Destroy → module absent | PASS, exit 0 |
| Final compile/link | `cmake --build --preset build-debug` | PASS, exit 0 |

M2 tests cover missing DLL/version/descriptor/Create/Destroy, invalid descriptor, null Create, failed OnLoad
cleanup sequence, same-DLL and different-DLL duplicate IDs, module reference rollback, wrong-thread rejection,
Tick reentrancy rejection, reverse shutdown and reinitialization. Source review confirms export pointers and
instance are cleared before FreeLibrary. A small ordered owning vector avoids duplicate map/order state.

M3 checks worker activity on each of 100 cycles, clears borrowed diagnostic function pointers before unload,
and checks GetModuleHandleW absence afterward. Worker startup catches exceptions; stop/join happens in OnUnload,
and a joinable worker at destruction terminates the test. The bad-version DLL exposes no complex ABI exports.
No crash, hang or leaked module was observed in the successful suite.

M4 subscribes an Editor-owned callback after successful base initialization. Finalize unsubscribes it and
shuts down/resets PluginManager before other Editor services and base teardown. Generated EditorApp.gen.h
changes only update reflection macro line numbers through the normal build process.

## Reproduction

Build using `cmake --build --preset build-debug`. From the staged test directory, run DynamicLibrarySmoke.exe,
PluginManagerSmoke.exe and PluginSmoke.exe. The standalone runners initialize logging, not Editor/Graphics.

Run `Test/RunPluginEditorSmoke.ps1` with EditorExecutable, ProjectFile, SmokeLibrary and EvidenceDirectory.
Paths are arguments; the script has no developer-specific absolute paths. It invokes Editor with the project
argument and `--exit-after-frames 10`, adding `--plugin-smoke <DLL>` for the threaded case. It uses normal
WM_CLOSE, not forced termination; timeout is a failed test. Config, layout and automation session bytes are
restored in finally.

Final local evidence is under `F:/AiluEngine/artifacts/plugin-validation/m4-final/`:

- `empty/FileAppender_log.txt`, `stdout.txt`, `stderr.txt`.
- `threaded/FileAppender_log.txt`, `stdout.txt`, `stderr.txt`.
- `error-comparison.json`.

The project descriptor used was `C:/Users/BoomBac/OneDrive/AiluTestProj/empty.json`.

## Evidence limits and discoveries

Both final Editor cases logged 55 non-plugin ERROR lines, 26 unique messages, mostly missing external scene
textures. The threaded case introduced no new error, and both had zero Plugin errors. Existing allocator
warnings remain. M4 acceptance proves plugin lifecycle safety, not globally error-free/leak-free Editor behavior.

The initial script invocation had a quoting error that added a leading space to the DLL path. No plugin loaded;
that immediate-error teardown process failed and was not counted as acceptance. Script quoting was fixed,
Editor test arguments gained pre-initialization path normalization/existence checks, and the fresh final build
passed both live cases. General zero-frame Editor failure teardown was not validated or refactored here.

No production plugin autoload, plugin UI, manifest discovery, owner registration, Blender Live Link or
JobSystem plugin ownership was added.
