# AiluEngine Volumetric Fog Exec Plan

This package is intentionally split for long-context agents.

## Read order

```text
00_Architecture_Overview.md
02_Progress.md
01_Milestone_Index.md
Milestones/<current milestone>.md
Milestones/Validation.md   # only when validating
```

## Editing policy

- `00_Architecture_Overview.md`: stable; edit only when architecture assumptions change.
- `01_Milestone_Index.md`: routing only; rarely changes.
- `Milestones/*.md`: implementation instructions; normally static.
- `02_Progress.md`: mutable state; update after every implementation/validation session.
- `Milestones/Validation.md`: shared validation rules.

Do not load all milestone files into context at once.
