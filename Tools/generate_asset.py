#!/usr/bin/env python3
"""
Generate a .alasset (or .almap) file alongside a given source file.

Usage:
    python generate_asset.py path/to/file.ext

This creates path/to/file.alasset with the correct fields inferred from the file extension.

    python generate_asset.py Engine/Res/Textures/blue_noise.jpg
    →  Engine/Res/Textures/blue_noise.alasset  (Ailu::Render::Texture2D)

Supported extensions:
    .fbx          → Ailu::Render::Mesh
    .png, .jpg, .jpeg, .tga, .hdr, .dds, .exr
                  → Ailu::Render::Texture2D
    .hlsl         → Ailu::Render::Shader
    .wav, .mp3, .ogg, .flac
                  → Ailu::Render::AudioClip

Options:
    --type TYPE    Override auto-detected asset type.
    --name NAME    Override _asset_name (default: filename stem).
    --compute      For .hlsl files: use ComputeShader instead of Shader.
    --srgb / --no-srgb   For Texture2D: set import _is_srgb explicitly.
    --no-default-db      Skip default assetdb conflict avoidance.
    --output NAME  Override output filename stem.
"""

import argparse
import json
import os
import re
import sys
import uuid

from pathlib import Path

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
import generate_guid  # type: ignore

# ---- Extension → asset type mapping ----
_EXT_TO_TYPE: dict[str, dict] = {
    ".fbx": {
        "type": "Mesh",
        "type_string": "Ailu::Render::Mesh",
    },
    ".gltf": {
        "type": "Mesh",
        "type_string": "Ailu::Render::Mesh",
    },
    ".glb": {
        "type": "Mesh",
        "type_string": "Ailu::Render::Mesh",
    },
    ".obj": {
        "type": "Mesh",
        "type_string": "Ailu::Render::Mesh",
    },
    ".png": {
        "type": "Texture2D",
        "type_string": "Ailu::Render::Texture2D",
    },
    ".jpg": {
        "type": "Texture2D",
        "type_string": "Ailu::Render::Texture2D",
    },
    ".jpeg": {
        "type": "Texture2D",
        "type_string": "Ailu::Render::Texture2D",
    },
    ".tga": {
        "type": "Texture2D",
        "type_string": "Ailu::Render::Texture2D",
    },
    ".hdr": {
        "type": "Texture2D",
        "type_string": "Ailu::Render::Texture2D",
    },
    ".dds": {
        "type": "Texture2D",
        "type_string": "Ailu::Render::Texture2D",
    },
    ".exr": {
        "type": "Texture2D",
        "type_string": "Ailu::Render::Texture2D",
    },
    ".hlsl": {
        "type": "Shader",
        "type_string": "Ailu::Render::Shader",
    },
    ".wav": {
        "type": "AudioClip",
        "type_string": "Ailu::Render::AudioClip",
    },
    ".mp3": {
        "type": "AudioClip",
        "type_string": "Ailu::Render::AudioClip",
    },
    ".ogg": {
        "type": "AudioClip",
        "type_string": "Ailu::Render::AudioClip",
    },
    ".flac": {
        "type": "AudioClip",
        "type_string": "Ailu::Render::AudioClip",
    },
    ".ttf": {
        "type": "Font",
        "type_string": "Ailu::Render::Font",
    },
    ".otf": {
        "type": "Font",
        "type_string": "Ailu::Render::Font",
    },
}

# ---- Asset templates ----
_ASSET_TEMPLATES: dict[str, dict] = {
    "Mesh": {
        "extension": ".alasset",
        "body": {
            "_file": "",
            "_inner_file_name": "",
            "_import_setting": {
                "_is_recalculate_normals": False,
                "_import_flag": 1,
                "_is_combine_mesh": False,
                "_mesh_name": "",
                "_animation_stack_index": 0,
            },
        },
    },
    "SkeletonMesh": {
        "extension": ".alasset",
        "body": {
            "_file": "",
            "_inner_file_name": "",
            "_import_setting": {
                "_is_recalculate_normals": False,
                "_import_flag": 1,
                "_is_combine_mesh": False,
                "_mesh_name": "",
                "_animation_stack_index": 0,
            },
        },
    },
    "Texture2D": {
        "extension": ".alasset",
        "body": {
            "_file": "",
            "_import_setting": {
                "_is_srgb": True,
                "_generate_mipmap": True,
                "_is_readable": False,
            },
        },
    },
    "Shader": {
        "extension": ".alasset",
        "body": {
            "_file": "",
            "_vs_entry": "VSMain",
            "_ps_entry": "PSMain",
        },
    },
    "ComputeShader": {
        "extension": ".alasset",
        "body": {
            "_file": "",
            "_kernel": "CSMain",
        },
    },
    "Material": {
        "extension": ".alasset",
        "body": {
            "_shader_guid": "",
            "_keywords": [],
            "_uint_properties": [
                {"_name": "_MaterialID", "_value": 0},
                {"_name": "_cull", "_value": 2},
                {"_name": "_surface", "_value": 0},
            ],
            "_float_properties": [
                {"_name": "_AlphaCulloff", "_value": 0.0},
                {"_name": "_Anisotropy", "_value": 0.0},
                {"_name": "_IOR", "_value": 0.0},
                {"_name": "_MaterialID", "_value": 0.0},
                {"_name": "_MetallicValue", "_value": 0.0},
                {"_name": "_RoughnessValue", "_value": 1.0},
                {"_name": "_SamplerMask", "_value": 0.0},
                {"_name": "_Transmission", "_value": 0.0},
            ],
            "_vector_properties": [
                {"_name": "_AlbedoValue", "_value": [1.0, 1.0, 1.0, 1.0]},
                {"_name": "_EmissionValue", "_value": [0.0, 0.0, 0.0, -1.0]},
                {"_name": "_SpecularValue", "_value": [0.0, 0.0, 0.0, 0.0]},
            ],
            "_int_vector_properties": [],
            "_texture_properties": [
                {"_name": "_AlbedoTex", "_texture_guid": ""},
                {"_name": "_EmissionTex", "_texture_guid": ""},
                {"_name": "_NormalTex", "_texture_guid": ""},
                {"_name": "_RoughnessMetallicTex", "_texture_guid": ""},
                {"_name": "_SpecularTex", "_texture_guid": ""},
            ],
        },
    },
    "AnimationClip": {
        "extension": ".alasset",
        "body": {
            "_frame_count": 0,
            "_clip_name": "",
            "_duration": 0.0,
            "_frame_rate": 30.0,
            "_tracks": [],
        },
    },
    "AudioClip": {
        "extension": ".alasset",
        "body": {
            "_file": "",
        },
    },
    "Font": {
        "extension": ".alasset",
        "body": {
            "_file": "",
        },
    },
    "Scene": {
        "extension": ".almap",
        "body": {
            "_entities": [],
        },
    },
}


def InferTextureIsSRGB(source: str) -> bool:
    """Infer whether a texture contains color data from its extension and filename."""
    source_path = Path(source)
    if source_path.suffix.lower() in {".hdr", ".exr"}:
        return False

    stem = source_path.stem.lower()
    normalized_stem = re.sub(r"[^a-z0-9]+", "_", stem)
    tokens = set(filter(None, normalized_stem.split("_")))
    if tokens.intersection({"n", "m", "ao", "orm"}):
        return False
    if any(marker in stem for marker in (
        "normal", "roughness", "metallic", "mask", "height", "depth", "lut", "noise", "weather"
    )):
        return False
    return True


def generate_guid_safe(existing_guids: set[str] | None) -> str:
    """Generate a GUID not in existing_guids."""
    while True:
        g = str(uuid.uuid4())
        if existing_guids is None or g not in existing_guids:
            return g


def GetAssetDatabaseInfo(path: str) -> tuple[str, str, str] | None:
    """Return the asset database, resource root, and URI scheme for a project path."""
    project_root = os.path.normpath(generate_guid._project_root())
    path_abs = os.path.normpath(os.path.abspath(path))
    try:
        relative_path = os.path.relpath(path_abs, project_root)
    except ValueError:
        return None

    path_parts = Path(relative_path).parts
    if not path_parts or path_parts[0].lower() not in {"engine", "editor"}:
        return None

    scope_name = path_parts[0]
    scope = scope_name.lower()
    resource_root = os.path.join(project_root, scope_name, "Res")
    assetdb_path = os.path.join(resource_root, "assetdb.alasset")
    return assetdb_path, resource_root, f"{scope}://"


def GetVirtualAssetPath(path: str, resource_root: str, uri_scheme: str) -> str | None:
    """Convert a resource file path into the engine/editor asset URI format."""
    path_abs = os.path.normpath(os.path.abspath(path))
    resource_root_abs = os.path.normpath(os.path.abspath(resource_root))
    try:
        common_path = os.path.commonpath([path_abs, resource_root_abs])
        if os.path.normcase(common_path) != os.path.normcase(resource_root_abs):
            return None
    except ValueError:
        return None

    relative_path = os.path.relpath(path_abs, resource_root_abs)
    if relative_path == os.curdir or relative_path.startswith(f"..{os.sep}"):
        return None
    return uri_scheme + relative_path.replace(os.sep, "/")


def RegisterAsset(assetdb_path: str, asset_path: str, guid: str, type_string: str) -> None:
    """Insert or update one asset entry in an asset database."""
    if os.path.isfile(assetdb_path):
        with open(assetdb_path, "r", encoding="utf-8") as f:
            database = json.load(f)
    else:
        database = {"assets": []}

    assets = database.get("assets")
    if not isinstance(assets, list):
        raise ValueError(f"Invalid asset database: 'assets' must be an array: {assetdb_path}")

    entry = {
        "guid": guid,
        "path": asset_path,
        "type": type_string,
    }
    existing_entry = next((item for item in assets if item.get("path") == asset_path), None)
    if existing_entry is None:
        assets.append(entry)
    else:
        existing_entry.update(entry)

    with open(assetdb_path, "w", encoding="utf-8") as f:
        json.dump(database, f, indent=2, ensure_ascii=False)
        f.write("\n")


def main():
    parser = argparse.ArgumentParser(
        description="Generate an .alasset file alongside a source file.",
    )
    parser.add_argument(
        "source",
        type=str,
        help="Path to the source file (e.g. Assets/c1.png).",
    )
    parser.add_argument(
        "--type", "-t",
        type=str,
        default=None,
        choices=list(_ASSET_TEMPLATES.keys()),
        help="Override auto-detected asset type.",
    )
    parser.add_argument(
        "--compute",
        action="store_true",
        default=False,
        help="For .hlsl files: treat as ComputeShader instead of Shader.",
    )
    parser.add_argument(
        "--name", "-n",
        type=str,
        default=None,
        help="Override _asset_name (default: filename stem).",
    )
    parser.add_argument(
        "--srgb",
        action="store_true",
        default=None,
        dest="is_srgb",
        help="Set import _is_srgb=true for Texture2D.",
    )
    parser.add_argument(
        "--no-srgb",
        action="store_false",
        default=None,
        dest="is_srgb",
        help="Set import _is_srgb=false for Texture2D.",
    )
    parser.add_argument(
        "--inner-name",
        type=str,
        default=None,
        help="Value for _inner_file_name (Mesh/SkeletonMesh).",
    )
    parser.add_argument(
        "--shader-guid",
        type=str,
        default=None,
        help="Reference shader GUID (Material only).",
    )
    parser.add_argument(
        "--output", "-o",
        type=str,
        default=None,
        help="Override output file path. If a directory, the file is placed inside with the default name.",
    )
    parser.add_argument(
        "--no-default-db",
        action="store_true",
        default=False,
        help="Skip default assetdb conflict avoidance.",
    )
    parser.add_argument(
        "--assetdb",
        type=str,
        action="append",
        default=None,
        help="Additional assetdb path(s) for GUID conflict avoidance.",
    )

    args = parser.parse_args()

    source = os.path.abspath(args.source)
    if not os.path.isfile(source):
        print(f"Error: source file not found: {source}", file=sys.stderr)
        sys.exit(1)

    source_ext = os.path.splitext(source)[1].lower()

    # ---- Determine asset type ----
    if args.type:
        asset_type_key = args.type
        type_info = {"type": asset_type_key}
    elif args.compute and source_ext == ".hlsl":
        asset_type_key = "ComputeShader"
        type_info = {"type": asset_type_key}
    elif source_ext in _EXT_TO_TYPE:
        type_info = _EXT_TO_TYPE[source_ext]
        asset_type_key = type_info["type"]
    else:
        print(
            f"Error: cannot infer asset type from extension '{source_ext}'. "
            f"Use --type to specify one.",
            file=sys.stderr,
        )
        sys.exit(1)

    if asset_type_key not in _ASSET_TEMPLATES:
        print(f"Error: unknown asset type '{asset_type_key}'.", file=sys.stderr)
        sys.exit(1)

    template = _ASSET_TEMPLATES[asset_type_key]
    type_string = (
        type_info.get("type_string")
        if "type_string" in type_info
        else template.get("type_string", f"Ailu::Render::{asset_type_key}")
    )

    # ---- Determine output path ----
    source_dir = os.path.dirname(source)
    asset_name = args.name if args.name else Path(source).stem

    if args.output:
        if os.path.isdir(os.path.abspath(args.output)):
            output_dir = os.path.abspath(args.output)
            output_name = asset_name
        else:
            output_dir = os.path.dirname(os.path.abspath(args.output)) or source_dir
            output_name = Path(args.output).stem
    else:
        output_dir = source_dir
        output_name = asset_name

    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, output_name + template["extension"])

    if os.path.exists(output_path):
        print(f"Warning: overwriting existing file: {output_path}", file=sys.stderr)

    # ---- Generate GUID ----
    existing_guids: set[str] | None = None

    assetdb_info = GetAssetDatabaseInfo(source)
    db_paths: list[str] = []
    if not args.no_default_db and assetdb_info:
        db_paths.append(assetdb_info[0])
    if args.assetdb:
        db_paths.extend(args.assetdb)

    project_root = generate_guid._project_root()
    seen: set[str] = set()
    unique_paths: list[str] = []
    for p in db_paths:
        abs_p = os.path.normpath(os.path.join(project_root, p) if not os.path.isabs(p) else p)
        if abs_p not in seen:
            seen.add(abs_p)
            unique_paths.append(p)
    db_paths = unique_paths

    if db_paths:
        required = set(args.assetdb) if args.assetdb else set()
        existing_guids = generate_guid.collect_guids(db_paths, required_paths=required)
        if existing_guids:
            print(f"Total: {len(existing_guids)} unique GUID(s) loaded, will avoid conflicts.", file=sys.stderr)

    guid = generate_guid_safe(existing_guids)

    # ---- Build asset JSON ----
    import copy
    body = copy.deepcopy(template["body"])

    # Auto-fill from source file.
    if "_file" in body:
        body["_file"] = os.path.basename(source)
    if "_inner_file_name" in body and args.inner_name:
        body["_inner_file_name"] = args.inner_name
    if asset_type_key == "Texture2D":
        is_srgb = args.is_srgb if args.is_srgb is not None else InferTextureIsSRGB(source)
        body["_import_setting"]["_is_srgb"] = is_srgb
    if "_shader_guid" in body and args.shader_guid:
        body["_shader_guid"] = args.shader_guid
    if "_clip_name" in body:
        body["_clip_name"] = asset_name

    asset: dict = {
        "_header": {
            "_format_version": 1,
            "_guid": guid,
            "_asset_type": type_string,
            "_asset_name": asset_name,
        },
    }
    asset.update(body)

    # ---- Write ----
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(asset, f, indent=4, ensure_ascii=False)
        f.write("\n")

    if assetdb_info:
        assetdb_path, resource_root, uri_scheme = assetdb_info
        virtual_asset_path = GetVirtualAssetPath(output_path, resource_root, uri_scheme)
        if virtual_asset_path is None:
            print(
                f"Warning: generated asset is outside the resource root, skipping asset database registration: "
                f"{output_path}",
                file=sys.stderr,
            )
        else:
            RegisterAsset(assetdb_path, virtual_asset_path, guid, type_string)
            print(f"Registered: {assetdb_path}", file=sys.stderr)

    print(f"Created: {output_path}", file=sys.stderr)
    print(f"  GUID:  {guid}", file=sys.stderr)
    print(f"  Type:  {type_string}", file=sys.stderr)
    print(f"  Name:  {asset_name}", file=sys.stderr)

    # Print output path to stdout for scripting.
    print(output_path)


if __name__ == "__main__":
    main()
