# Blender threaded transport crash

## Purpose / scope

Remove persistent Python threads from the Blender add-on after the second native crash. Preserve protocol v1,
queue coalescing, diagnostics and reconnect/disconnect behavior; update the already authorized installation.

## Architecture constraints / relevant files

Blender's main-thread timer owns RNA, baking and nonblocking transport. No persistent Python worker may execute
concurrently with operators/Cycles. Tools/Blender/ailu_live_link/{connection,addon}.py and transport regressions.

## Current milestone / progress

- [x] Supplied crash thread is in queue.get/PyException_SetTraceback; main thread is in pyop_call/py_timer_execute.
- [x] Replace the background socket thread with bounded nonblocking connect/handshake/receive/send polling.
- [x] Test real fragmented TCP, partial writes, reconnect, failure cleanup and native Blender bake while connected.
- [x] Build/package/install and document validated scope.

## Decision log / discoveries

Blender 5.0 documentation explicitly forbids persistent Python threads even for background file downloads.
The previous recorders had no live network worker and therefore did not cover this concurrency condition.
https://docs.blender.org/api/5.0/info_gotchas_threading.html
Polling advances between generated packets during Full Sync and on the existing addon timer; writes retain their
partial packet and run within a per-poll time/byte budget. Slow peers retain queue overflow behavior.

## Validation / outcomes

- Standalone real TCP regression PASS (`Saved/BlenderTextureValidation/transport-smoke-03.log`): fragmented ACK,
  rejected handshake and retry, partial 8,388,864+256-byte payload with matching SHA256, remote close, invalid header,
  reconnect, explicit disconnect, coalescing/deletion, queue overflow and refused port. No new Python thread.
- Native Blender UI timer with live TCP and actual channel/UV/Cycles bake regression PASS: four packets received,
  no Python workers; Thread.start is forbidden by the fixture. Source evidence: connected-bake-timer-04.json.
- Initial native runs accidentally resolved the old installed transport instead of workspace source. The thread
  guard caught its Thread.start; fixture now asserts exact module origin. One earlier fixture also expected six
  packets when the bake smoke emits four. Those runs are failures, not acceptance evidence.
- Installed native retest PASS (`connected-bake-installed.json`): actual Roaming Blender 5.0 connection.py matches
  source, four packets received, only MainThread. Normal startup confirms enabled add-on and matching module bytes.
- `cmake --build --preset build-debug` PASS. ZIP rebuilt, 13 modules verified and add-on enabled; backup:
  Saved/BlenderAddonInstallation/20261009-215224/. Install/startup evidence is under Saved/BlenderAddonInstallation/.
- UI tests isolate Blender temporary recovery directories and never save the user's original scene.
- Matching unsupported concurrency is removed; exact supplied native crash is not deterministically reproduced.
  Full Hidden Alley native bake/geometry/Editor import and visual/performance acceptance remain separate.
