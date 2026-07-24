#!/usr/bin/env python3
"""
Generate a GUID in the same format as AiluEngine's Guid::Generate().

Format: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x
(standard UUID v4 lowercase format, e.g. "012efc02-825c-42e9-b512-35c19e7eb1db")

By default, loads all GUIDs from the hardcoded assetdb paths to avoid conflicts:
    Engine/Res/assetdb.alasset
    Editor/Res/assetdb.alasset

Usage:
    python generate_guid.py                              # Generate a single GUID (avoids default assetdb GUIDs)
    python generate_guid.py --no-default-db              # Generate without loading any assetdb
    python generate_guid.py --assetdb <path>             # Also load an additional assetdb
    python generate_guid.py --assetdb <path> --count 5   # Generate 5 GUIDs, avoiding all loaded GUIDs
"""

import argparse
import json
import os
import sys
import uuid

# Hardcoded default assetdb paths, relative to the project root (parent of Tools/).
_DEFAULT_ASSETDB_PATHS = [
    "Engine/Res/assetdb.alasset",
    "Editor/Res/assetdb.alasset",
]


def _project_root() -> str:
    """Return the project root directory (parent of this script's directory)."""
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_existing_guids(assetdb_path: str) -> set[str]:
    """Load all existing GUIDs from an assetdb JSON file.

    Returns an empty set if the file does not exist (non-existent default paths are skipped).
    Exits with an error only if an explicitly specified --assetdb path is missing.
    """
    if not os.path.isfile(assetdb_path):
        return set()

    with open(assetdb_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    guids = set()
    assets = data.get("assets", [])
    for asset in assets:
        g = asset.get("guid")
        if g:
            guids.add(g.lower())
    return guids


def collect_guids(
    assetdb_paths: list[str],
    required_paths: set[str] | None = None,
) -> set[str]:
    """Load and merge GUIDs from multiple assetdb files.

    Args:
        assetdb_paths: List of paths to assetdb JSON files.
        required_paths: Set of paths that must exist (user-specified --assetdb).
            Paths NOT in this set are silently skipped if missing.

    Returns:
        A set of all GUID strings found across all files.
    """
    required_paths = required_paths or set()
    all_guids: set[str] = set()
    for path in assetdb_paths:
        abs_path = os.path.join(_project_root(), path) if not os.path.isabs(path) else path
        if not os.path.isfile(abs_path):
            if path in required_paths or abs_path in required_paths:
                print(f"Error: assetdb file not found: {abs_path}", file=sys.stderr)
                sys.exit(1)
            else:
                print(f"Skipping (not found): {abs_path}", file=sys.stderr)
                continue

        guids = load_existing_guids(abs_path)
        print(f"Loaded {len(guids)} GUID(s) from: {abs_path}", file=sys.stderr)
        all_guids.update(guids)

    return all_guids


def generate_guid(existing: set[str] | None = None) -> str:
    """Generate a single GUID, optionally avoiding a set of existing GUIDs."""
    while True:
        g = str(uuid.uuid4())
        if existing is None or g not in existing:
            return g


def main():
    parser = argparse.ArgumentParser(
        description="Generate GUID(s) in AiluEngine format (UUID v4 lowercase)."
    )
    parser.add_argument(
        "--count", "-c",
        type=int,
        default=1,
        help="Number of GUIDs to generate (default: 1).",
    )
    parser.add_argument(
        "--assetdb",
        type=str,
        action="append",
        default=None,
        help="Path to an additional assetdb JSON file. Can be specified multiple times. "
             "Paths are relative to the project root unless absolute.",
    )
    parser.add_argument(
        "--no-default-db",
        action="store_true",
        default=False,
        help="Do not load the hardcoded default assetdb files "
             "(Engine/Res/assetdb.alasset, Editor/Res/assetdb.alasset).",
    )
    parser.add_argument(
        "--output", "-o",
        type=str,
        default=None,
        help="Optional output file path. GUIDs will be written one per line. "
             "Uses stdout if not specified.",
    )

    args = parser.parse_args()

    if args.count < 1:
        print("Error: --count must be >= 1", file=sys.stderr)
        sys.exit(1)

    # Build the list of assetdb paths to load (deduplicated by normalized absolute path).
    db_paths: list[str] = []
    if not args.no_default_db:
        db_paths.extend(_DEFAULT_ASSETDB_PATHS)
    if args.assetdb:
        db_paths.extend(args.assetdb)

    # Deduplicate by normalized absolute path while preserving order.
    seen: set[str] = set()
    unique_paths: list[str] = []
    for p in db_paths:
        abs_p = os.path.normpath(os.path.join(_project_root(), p) if not os.path.isabs(p) else p)
        if abs_p not in seen:
            seen.add(abs_p)
            unique_paths.append(p)
    db_paths = unique_paths

    existing: set[str] | None = None
    if db_paths:
        # User-specified --assetdb paths are required; default paths are optional.
        required = set(args.assetdb) if args.assetdb else set()
        existing = collect_guids(db_paths, required_paths=required)
        if existing:
            print(f"Total: {len(existing)} unique GUID(s) loaded, will avoid conflicts.", file=sys.stderr)
        else:
            print("No existing GUIDs found — generating without conflict check.", file=sys.stderr)
            existing = None

    guids = [generate_guid(existing) for _ in range(args.count)]

    output_text = "\n".join(guids)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(output_text + "\n")
        print(f"Wrote {args.count} GUID(s) to: {args.output}", file=sys.stderr)
    else:
        print(output_text)


if __name__ == "__main__":
    main()
