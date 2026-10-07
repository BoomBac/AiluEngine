# Descriptor page growth crash

## Purpose and scope

Repair the import-time DescriptorPage::GetDescriptorHandle crash and validate old allocations after staging-page
growth. Keep MainHeap/bindless capacities and existing descriptor lifetime/fence policy unchanged.

## Architecture constraints and relevant files

GPUVisibleDescriptorAllocation borrows a page owned by GPUVisibleDescriptorAllocator. Page storage must remain
address-stable until allocator teardown. Growth and cache mutation retain their existing locks. Relevant files:
Engine/{Inc,Src}/RHI/DX12/DescriptorManager.*, Test/DescriptorPageGrowthSmoke.cpp.

## Current milestone and progress

- [x] Source diagnosis: vector reserve(10) is not pointer stability after the eleventh page; static first-allocation
  flag also failed to reserve for subsequent allocator instances.
- [x] Meaningful DX12 regression over more than ten pages, including old-page descriptor copies.
- [x] Debug build and validation evidence; preserve unrelated dirty work and user runtime state.

## Decision log

- Use deque storage for staging pages. No runtime migration of shader-visible MainHeap is necessary.

## Surprises and discoveries

- Supplied stack uses Engine.dll; no active Editor process was found. Repository FileAppender_log.txt is historical
  (September 28) and cannot establish the page count or module path of the user's crash.

## Validation and outcomes

`cmake --build --preset build-debug` PASS. Private allocator implementation is compiled directly into the smoke
target because allocator/page types are not exported from Engine; no new public DLL API was introduced.

Native DX12 smoke PASS in two independent allocator instances, each with 16 staging pages and 16,369 descriptors.
Old-page copy/handle resolution runs concurrently with growth (46,067 and 42,123 calls). All 16,369 descriptors
were released per instance; subsequent reuse preserved the page address and advanced allocation generation.
D3D12 ERROR/CORRUPTION count 0; exit code 0. This exercises real descriptor copies and bind-handle resolution,
not GPU draw output, shader-visible ring wraparound or full Hidden Alley import/visual acceptance.

Evidence: F:/AiluEngine/artifacts/out/editor/bin/x64/debug/DescriptorPageGrowthValidation-20261009-02/
(`validation.txt`, `run_summary.json`, `restoration.json`). All nine configuration/session/log files restored.
The source repair removes an established matching defect. Exact full-scene runtime acceptance remains separate;
restart the rebuilt Debug Editor before importing again.
