# Blender Link evaluated parent transforms and affine instances

## Purpose
Repair the 52 bone-parented, 30 vertex-parented and two sheared-instance world-transform mismatches found in `D:/Downloads/blender-4.5-splash.blend`, then repeat native Engine validation.

## Scope / Non-goals
Plugin transform export, incremental invalidation, focused Blender smoke and scene retest. No Engine Transform redesign, protocol revision, original scene saves or blanket M3 acceptance.

## Architecture constraints
Blender evaluation on its main thread; retain source object GUIDs and logical parenting. Preserve unrelated dirty work and user processes. Preserve shared instance geometry. Add deterministic helper entities only when a flattened affine instance cannot be represented by a single TRS.

## Relevant files
`Tools/Blender/ailu_live_link/{coordinate,object_sync,dirty_tracker}.py`, `Test/BlenderInstanceSyncSmoke.py`, `Temp/splash_validation/`.

## Current milestone
Complete: plugin implementation, focused regressions, Debug build and installed-add-on native scene retest.

## Progress
- Initial scene run and exact replay confirmed 82 special-parent objects and two sheared instances differ in world matrices. Empty mesh instances are explicitly excluded.
- Plugin conversion implemented in `coordinate.py`, `object_sync.py` and `dirty_tracker.py`; the source scene needs one direct-object and two instance helper parents.
- Fresh Blender source-to-wire regression PASS: 185 direct objects, 114 nonempty instances, 307 checks, zero world mismatches; idle, helper transitions/deletion, reflections and near-singular scales covered. Geometry publication is stubbed in this focused regression.
- Existing native instance smoke and host persistence smoke PASS. `cmake --build --preset build-debug` PASS.
- Independent Engine comparator now reconstructs effective local matrices from read-only TRS and explicitly traverses/validates helper parents. Its unchanged world-matrix criterion still rejects the original failing run (1742 scalar local/world mismatches), so the oracle has not relaxed source-world acceptance.
- Native post-fix run `20261016-162829-c99ba611` PASS using the installed Blender 5.0 add-on. 185 direct objects + 114 nonempty instances + 3 affine helpers + 1 link root; 10,773 independent world/effective-local-matrix, logical hierarchy, GUID/name/visibility checks PASS, zero failures.
- Full Sync export/drain 119.35 s; exactly one ordered Begin/End, current Editor applied End, no Apply/overflow error. Both Blender and Editor exited 0; all ten shared-state entries restored and original source SHA256 unchanged.
- GBuffer normal, GBuffer albedo and final HDR: three consecutive finite frames each, 896x512. Same camera/AA as before; unified Reinhard/sRGB comparison shows corrected snowman face/head/buttons and other parented placements. This is not Blender shader pixel-equivalence acceptance.
- Existing instance/persistence regressions and fresh installed-addon registration PASS. Updated zip and installed `.py` files match repository source byte-for-byte; old installation backed up at `Temp/splash_validation/installed_backup_20261016_162812`. Global preferences were not saved.

## Decision Log
- Export evaluated object-relative transforms from parent-world inverse times child-world, instead of assuming Blender `matrix_local` uses the ordinary object-parent frame for bone/vertex parenting.
- Represent sheared flattened instance affine matrices as two TRS factors using SVD and a deterministic nonrenderable helper parent. Existing OBJECT_UPSERT/INSTANCE_BATCH_UPSERT and Engine hierarchy suffice.

## Surprises & Discoveries
The current Engine file appender filters Info. The scene-specific harness captures only its owned Editor's OutputDebugString through DBWIN for actual End evidence. Keep this harness limitation separate from plugin correctness.

## Validation
`cmake --build --preset build-debug`, Python syntax, existing instance/persistence smokes and `git diff --check` PASS.
Focused source-to-wire scene smoke: 307 checks PASS, including helper movement, shear-to-TRS transition and helper deletion. The final native run uses the installed production exporter rather than geometry stubs.
Evidence: `F:/AiluEngine/artifacts/out/editor/bin/x64/debug/blender_splash_validation/20261016-162829-c99ba611/fix_report.md` and `fix_report.json`, `assertions.json`, `summary.json`, `m2_capture.json`, raw captures and `restoration.json`.
Source `.blend` SHA256: `7f8718cfd89baf59151cc4ba431eeab38b9ff260ffa0054d93293f228a70cc36`.
Updated zip SHA256: `3ce9960be54bdaa7e33826546d878126745aa0119396cfec36ccf8acfcaa7a8c`.

## Outcomes
The 52 bone-parent, 30 vertex-parent and two sheared-instance world-transform mismatches are fixed at the Blender exporter layer. One vertex-parented object's corrected relative transform also requires a helper. Engine Transform, protocol v1 and shared mesh ownership did not change.
This closes the bounded transform-fix task, not the full M3 incremental/reconnect/persistence or complex-material parity suite.
