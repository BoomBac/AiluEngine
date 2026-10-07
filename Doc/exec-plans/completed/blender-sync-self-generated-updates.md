# Blender export self-generated updates

## Purpose / scope

Prevent idle incremental texture/mesh reimports after FullSync ends, while preserving real user edits.
The supplied log has material update sequences 3000/3006 after a complete 1,363-mesh/182-material batch.

## Architecture constraints / relevant files

Main-thread timer and depsgraph handlers own Blender export/change tracking. Host texture loading is synchronous;
the FullSync summary ends one batch, not the persistent incremental session. Do not add threads or timed edit loss.
Tools/Blender/ailu_live_link/{dirty_tracker,object_sync}.py and native timer regression.

## Current milestone / progress

- [x] Inspect supplied log and source packet/apply order.
- [x] Reproduce idle resends and isolate exporter side effects.
- [x] Suppress/drain exporter-generated updates; verify real edits and idle after incremental export.
- [x] Debug build/package/install and full-scene idle acceptance.

## Decision log / surprises

The host summary is printed after applying FULL_SYNC_END. Subsequent type21 material packets are new incremental
messages, and their local texture imports are synchronous. The supplied log alone cannot establish whether the
user also edited the scene; a separate native idle reproduction proves an exporter-induced loop exists.

Full and incremental exports now enter a nestable main-thread scope: update pending genuine edits before entry,
suppress depsgraph change tracking during generation, and update the source view layer before leaving suppression.
This drains GUID/bake/scene-restore notifications without a grace period that might drop later user edits.
Exception cleanup restores suppression depth. Existing material/image dependency tracking remains active.

## Validation / outcomes

- Before: a 32px native procedural plane exported object/material/mesh nine times during 1.5 seconds of idle after
  Full Sync. Native UI timer reproduced the self-triggering loop with no user action.
- Fixed source: zero idle packets, one normal object/material/mesh update after moving the object, then idle.
- Installed version: zero idle packets; transform and shader roughness edits each send one update and then stay
  idle. Export exception fixture restores depth to zero. Installed module paths/content verified against source.
- Native channel/UV/image/group dependency regression and Debug build PASS. Package/install PASS; 13 Python files
  verified, old add-on/preferences preserved under Saved/BlenderAddonInstallation/20261010-003212.
- Full Hidden Alley default-quality native bake/mesh/idle test PASS: 199.125 seconds, 2,967 full-batch messages,
  zero idle incremental packets; a subsequent transform edit sends exactly one object/material/mesh update,
  followed by zero additional idle packets. Test uses a recording peer, not Editor;
  the original .blend is not saved. Source scene contains an unavailable facade roughness image, so this test
  does not establish visual equivalence or repair the missing source asset.

Evidence: Saved/BlenderTextureValidation/sync-idle-{before,after,installed,full-scene}.json and associated logs,
sync-idle-native-regression.log, sync-idle-build.log, Saved/BlenderAddonInstallation/sync-idle-install.log.

Outcome: exporter-induced idle reimports are repaired and the installed native regression passes. Restart the
user's existing Blender instance to load the updated module. Engine-side appearance and GPU upload completion
remain separate acceptance; this change does not redefine FullSync end as GPU-idle or stop legitimate editing.
