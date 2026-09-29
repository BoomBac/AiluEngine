# AiluEngine

## Third-Party Dependencies

Third-party source and binary dependencies are managed by Submodule, Vendor, and Package. See [Doc/ThirdPartyDependencyManagement.md](Doc/ThirdPartyDependencyManagement.md).

Clone with submodules, then install local SDK packages with `Scripts/setup_ext.ps1`:

```bash
git clone --recurse-submodules https://github.com/BoomBac/AiluEngine.git
```

```powershell
.\Scripts\setup_ext.ps1
```

Install Autodesk FBX SDK 2020.2.1 under its license before setup on a clean machine; pass its installation directory with `-FbxSdkRoot`. See the dependency guide for details.
