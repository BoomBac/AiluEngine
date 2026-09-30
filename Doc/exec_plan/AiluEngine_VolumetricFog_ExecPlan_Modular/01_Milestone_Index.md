# AiluEngine Volumetric Fog — Milestone Index

> Lightweight routing file. Read this after `00_Architecture_Overview.md` and `02_Progress.md`.
> Only open the milestone file corresponding to the current milestone.

# Milestones

- `Milestones/M00_Establish_Baseline_Debug_Views.md` — Milestone 0: Establish Baseline & Debug Views
- `Milestones/M01_Correctness_Fixes.md` — Milestone 1: Correctness Fixes
- `Milestones/M02_Temporal_Reprojection_Reliability.md` — Milestone 2: Temporal Reprojection Reliability
- `Milestones/M03_Medium_Model_Height_Fog.md` — Milestone 3: Medium Model / Height Fog
- `Milestones/M04_Environment_In_Scattering.md` — Milestone 4: Environment In-Scattering
- `Milestones/M05_Transparent_Fog_Sampling.md` — Milestone 5: Transparent Fog Sampling
- `Milestones/M06_Dynamic_Froxel_Resolution.md` — Milestone 6: Dynamic Froxel Resolution
- `Milestones/M07_Optional_Local_Fog_Volumes.md` — Milestone 7: Optional Local Fog Volumes
- `Milestones/M08_Optional_Point_Spot_Volumetric_Lights.md` — Milestone 8: Optional Point / Spot Volumetric Lights

# Shared Validation

Cross-milestone validation rules are stored in:

```text
Milestones/Validation.md
```

# Execution Rule

Normal resume flow:

```text
00_Architecture_Overview.md
    ↓
02_Progress.md
    ↓
01_Milestone_Index.md
    ↓
Milestones/Mxx_*.md   <- current milestone only
    ↓
Milestones/Validation.md when reaching a validation gate
```

Do not preload future milestone files unless a dependency must be checked.
