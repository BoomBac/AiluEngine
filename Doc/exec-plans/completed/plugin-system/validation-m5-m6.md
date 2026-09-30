# M5/M6 acceptance — 2026-09-30

## Implementation

M5: strict seven-field UTF-8 schema, deterministic ordered-root discovery, duplicate isolation, contained relative paths, explicit ID load and autoload. Scan does not activate DLLs. Runtime ABI and descriptor checks precede activation. Editor scans Engine/project Plugins roots before autoload.

M6: unique owner tokens, handles and Engine-owned host records. Scope closes/releases before OnUnload and Destroy; no plugin cleanup callable is retained. ABI v1 unchanged. Menu/panel services and arbitrary delegate cleanup remain follow-ups.

## Validation

Final full `cmake --build --preset build-debug` passed, exit 0.

Five standalone runners passed, exit 0, from F:/AiluEngine/artifacts/out/test/bin/x64/debug: DynamicLibrarySmoke, PluginManagerSmoke, PluginManifestSmoke, PluginRegistrationSmoke and PluginSmoke. Manifest tests cover invalid schema/paths, duplicates, scan without activation, explicit/autoload activation and DLL mismatch rejection. Registration tests cover owner isolation, release before OnUnload/Destroy, stale tokens/handles, affinity and shutdown. PluginSmoke passed 10/10 checks and 100 threaded load/activity/unload iterations with no crashes, hangs or resident modules.

Final live evidence: F:/AiluEngine/artifacts/plugin-validation/m5-m6-complete/.

Test/RunPluginEditorSmoke.ps1 with -TestManifest passed empty, manual threaded load, discovery-only and autoload normal exits. All processes and complete script exited 0. Discovery found one manifest without OnLoad. Loaded cases verified ticks/worker activity, registration release before worker join/destruction, and module absence after Finalize.

All five state files match backup bytes: EngineConfig.json, EditorConfig.json, dock_layout.json, main_widget.json and project editor_session.json. Temporary Plugins fixtures/initially absent parent removed; no test Editor remains. Existing EngineConfig user changes preserved.

Every case has 55 existing non-plugin ERROR lines and zero Plugin errors. Missing assets and allocator warnings remain outside acceptance; no global leak-free Editor claim.

## Harness repairs and limits

Earlier runs passed Editor cases but failed restoration/cleanup. Final harness uses durable byte backups, atomic adjacent-file replacement for mapped layouts and readonly-capable cleanup confined to unique fixtures. Earlier incomplete scripts are not counted as complete acceptance.

No rendering or performance benchmark acceptance was needed. Host records prove metadata cleanup; future services must explicitly detach their resources. Plugins remain responsible for joining workers and waiting for plugin-code jobs.
