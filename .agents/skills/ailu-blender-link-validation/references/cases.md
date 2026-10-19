# Recommended case tiers

**Quick** (on changes): controlled cube/transform (basis, negative/nonuniform scale, hierarchy); two-slot mesh (UV, normals, winding/tangents); nested collection instance GUID; constant/texture/material (linear vs sRGB, alpha); perspective/orthographic camera and lights. Begin with source-to-wire then source-to-Engine.

**Native integration**: fresh Blender and Editor process, full production data path; fresh installed-addon case for package/registration changes; recorded-packet replay to isolate importer regressions.

**Visual integration**: controlled camera with projected landmarks; GBuffer occupancy/normals and texture checker or per-corner color markers; alpha clip/blend; final scene as semantic ROI, not absolute Cycles RGB.

**Stateful integration**: incremental transform/geometry/material/image/add/delete/visibility; idle produces no self-generated update; collection instance object stability; reconnect after changes, missing End and injected error; save/reopen copied assets and rejoin source.

**Extended**: BMW27 (mesh instances, floor/car, cameras, UV/glass, visibility) and Classroom (nested instances, empty mesh and complex bake, long fullsync and performance). Supply actual paths from runtime config. `Test/BlenderLinkPerformance.py` presently has hardcoded external file paths; missing scenes must not be marked PASS.

**Gate**: every Milestone has explicit acceptance. Do not proceed to another milestone with current milestone FAIL/INCONCLUSIVE.

**Evaluated material bindings:** use [material-bindings.md](material-bindings.md) for sparse tables, converted curves, GN replacement, instance/bake bindings and bounded native mutations.
