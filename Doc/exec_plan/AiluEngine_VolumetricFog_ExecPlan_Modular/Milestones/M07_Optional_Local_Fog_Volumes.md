# Milestone 7 — Optional Local Fog Volumes

> Read `../00_Architecture_Overview.md` and `../02_Progress.md` first.
> Execute only this milestone. Do not implement future milestones before this milestone's acceptance gate passes.

## Purpose

支持局部 sphere / box medium。

第一版允许限制少量 volume。

不要：

```text
every froxel x every volume
```

无限增长。

可先 CPU cull 相机附近 volume，之后再考虑 clustered/tiled volume culling。

---
