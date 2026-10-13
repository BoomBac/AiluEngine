# Third-Party Repository and Dependency Management

`Engine/Ext` contains third-party source dependencies and SDKs used by AiluEngine. Versions must be reproducible on a new machine without placing large binary SDKs in Git history.

## Dependency categories

| Type | Use for | Repository treatment |
|---|---|---|
| Git Submodule | A dependency with its own Git repository and commit history | Pin an exact commit in the parent repository. |
| Vendor | Small, stable source or header-only code | Keep the source and its license in AiluEngine. |
| Package | Large SDKs and prebuilt `.lib`, `.dll`, and `.pdb` files | Keep version, source, and SHA-256 verification data in `Scripts/packages.json`; install under `Engine/Ext/Packages/`, which is ignored by Git. |
| Git LFS | A small set of binaries that must be versioned with source | Enable it only for specifically named files. Do not use it as a general SDK store. |

## Current dependency layout

```text
Engine/Ext/
├─ imgui/             # Submodule, pinned commit
├─ ImGuizmo/          # Submodule, pinned commit
├─ implot/            # Submodule, pinned commit
├─ tracy/             # Submodule, pinned commit
├─ lua/               # Submodule, pinned commit
├─ sol2/              # Submodule, pinned commit
├─ box2d/             # Submodule, pinned commit
├─ DirectXTex/        # Submodule, pinned commit
├─ stb/               # Vendor source
└─ Packages/          # Local SDK/package install; ignored by Git
   ├─ dxc/
   ├─ fbxsdk/
   ├─ fidelityfx-sdk-2.3.0/
   └─ streamline-sdk-2.14.1/
```

The package lock is `Scripts/packages.json`. `Scripts/setup_ext.ps1` installs and verifies the pinned DXC package, imports the FBX and FidelityFX SDKs from local SDK folders, trims the NVIDIA Streamline package to its public headers and x64 runtime, and verifies each resulting package tree. The large SDK packages stay under the Git-ignored `Engine/Ext/Packages/` directory.

```powershell
.\Scripts\setup_ext.ps1
```

On another machine, provide the source checkout or FBX SDK folder explicitly:

```powershell
.\Scripts\setup_ext.ps1 -SourceRepository 'D:\src\AiluEngineOldd'
.\Scripts\setup_ext.ps1 -FbxSdkRoot 'C:\Program Files\Autodesk\FBX\FBX SDK\2020.2.1'
.\Scripts\setup_ext.ps1 -FsrSdkRoot 'D:\SDKs\FidelityFX-SDK-2.3.0'
.\Scripts\setup_ext.ps1 -StreamlineSdkRoot 'D:\SDKs\streamline-sdk-v2.14.1'
```

Install Autodesk FBX SDK 2020.2.1 under its license. Use the official AMD FidelityFX SDK 2.3.0 and NVIDIA Streamline SDK 2.14.1 packages; the lock records their source and tree checksums. The NVIDIA archive is additionally checked against its release SHA-256. These local SDKs are ignored by Git; follow the corresponding vendor license and do not commit or redistribute their runtime binaries. DXC 1.7.2308 is pinned to the [Microsoft release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.7.2308).

To compile both temporal upscaling backends, configure with `AILU_WITH_FSR=ON` and `AILU_WITH_STREAMLINE=ON`. Their default SDK roots point at the versioned package directories above; both compile options remain off by default.

## Submodule workflow

Clone all source dependencies:

```bash
git clone --recurse-submodules https://github.com/BoomBac/AiluEngine.git
```

For an existing clone:

```bash
git submodule update --init --recursive
git submodule status
```

A normal update checks out the exact commit recorded by AiluEngine. A `+` prefix in `git submodule status` means the working submodule commit differs from the parent pin; `-` means it is uninitialized; `U` means a conflict.

Before editing a self-maintained submodule, switch from detached HEAD to its maintenance branch. Commit and push inside the submodule first, then update and push the gitlink in AiluEngine:

```bash
cd Engine/Ext/imgui
git switch docking
# edit files
git add .
git commit -m "Update imgui"
git push origin docking

cd ../../..
git add Engine/Ext/imgui
git commit -m "Update imgui submodule"
git push
```

The `.gitmodules` branch setting guides `git submodule update --remote`; it does not replace the exact commit pin in the parent. Ensure every pinned commit remains fetchable from the submodule remote.

## Vendor, Package, and LFS rules

- Vendor small, stable source and keep its upstream license.
- Keep large SDKs and binary packages out of ordinary Git history.
- Every Package entry identifies its version, source, destination, and SHA-256 verification data.
- Install packages under `Engine/Ext/Packages/`; keep machine-specific SDK copies out of tracked source directories.
- Use Git LFS only for a few reviewed binaries that must travel with source changes.
- Keep generated archives such as `AiluEngine_Source.zip` out of Git; track the script that creates them.
- Keep project assets such as scenes and textures in the repository when they are part of the project and reasonably sized. Use LFS only when a specific asset needs it.

## New-machine restore

1. Clone AiluEngine with recursive submodules.
2. Install Autodesk FBX SDK 2020.2.1 and download the vendor SDK packages under their licenses.
3. Run `Scripts/setup_ext.ps1`, passing `-FbxSdkRoot`, `-FsrSdkRoot`, and `-StreamlineSdkRoot` for SDK sources that are not already installed.
4. Configure and build AiluEngine normally.

The restore flow uses Git for pinned source, the main repository for small vendored source and project assets, and the package setup script for large binary dependencies.
