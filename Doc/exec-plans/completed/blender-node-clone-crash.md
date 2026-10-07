# Blender node clone crash

## Purpose / scope

Address the supplied Blender 5.0.1 native access violation in ChannelGraph.output while traversing node inputs.
Keep the source scene unsaved and preserve existing workspace changes; update the already authorized add-on install.

## Architecture constraints / relevant files

Blender RNA and baking run on the main thread. ChannelGraph copies instance settings into an isolated material;
it must never write shared node-type registration metadata. Tools/Blender/ailu_live_link/material_bake.py,
Tools/Blender/tests/material_graph_clone_smoke.py and existing native material bake tests.

## Current milestone / progress

- [x] Inspect supplied native and Python stack; identify unsafe shared node-type setters in the clone path.
- [x] Skip registration and bl_* properties, snapshot inputs before recursive graph construction.
- [x] Run native graph/bake regressions and package/install verification.

## Decision log / discoveries

Blender v5.0.1 rna_nodetree.cc confirms bl_idname/bl_description setters write node->typeinfo.
The pre-fix background clone stress passed 1,488 graphs; the original native crash has not been reproduced.
Removing these setters fixes a concrete unsafe operation, but does not by itself establish the precise crash cause.

## Validation / outcomes

- `graph-clone-old-audit.log`: previous installed code FAIL on writing bl_idname. Current code PASS with actual
  native Blender nodes, retaining instance Math operation and cloning 1,488 real Hidden Alley graphs.
- `node-clone-bake-smoke.log`: native Cycles material/channel/alpha/normal regression PASS.
- `full-sync-timer-graphs.json`: interactive Blender timer Full Sync node-path PASS for all 1,620 material/object
  combinations and 1,412 scene objects. Graph-only fixture substitutes native channel graph construction for
  texture baking and records mesh messages without encoding geometry. It is not full transport/render acceptance.
  Datablock counts (including 1,218 meshes), source scene and view layer restored.
- `node-clone-native-export.json`: all 122 mesh-slot material representatives export with 16px native bakes,
  no exceptions/missing files and original datablock counts restored. Source blend was never explicitly saved.
- Debug build PASS. ZIP rebuilt; installation enabled and all 13 Python files verified. Fresh normal Blender
  startup confirms installed material_bake hash matches source. Backup: Saved/BlenderAddonInstallation/20261009-211058/.
- Exact native crash was not reproduced, including with pre-fix code; unsafe shared-type mutation is confirmed
  and removed. Full interactive 1,620-combination bake/mesh/Editor transport and appearance remain unverified.
- Initial native all-object timer runs were intentionally stopped to correct the recording fixture and bound
  expensive per-object baking; they are not acceptance results.
- Test UI exit wrote Blender's global temporary quit.blend. The discovered 18:23 user autosave was copied to
  Saved/BlenderCrashRecovery/ph_hidden_alley_15076_autosave.blend. Future UI tests isolate temporary_directory;
  `timer-temp-isolation.log` confirms recovery is written under evidence instead. Original scene was not saved.

Evidence logs/JSON are under Saved/BlenderTextureValidation/. Install/startup logs are under Saved/BlenderAddonInstallation/.
Blender shared setter evidence: https://raw.githubusercontent.com/blender/blender/v5.0.1/source/blender/makesrna/intern/rna_nodetree.cc
