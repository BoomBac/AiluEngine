"""Compare an independent bpy reference with a read-only Engine automation snapshot."""
import copy
import json
import math
import sys
from pathlib import Path


reference_path, snapshot_path, output_dir = map(Path, sys.argv[1:4])
reference = json.loads(reference_path.read_text(encoding="utf-8"))
snapshot = json.loads(snapshot_path.read_text(encoding="utf-8"))
expected = reference["objects"] + reference["instances"]
expected_by_guid = {item["guid"]: item for item in expected}
inspected_by_guid = {item["entity_guid"]: item for item in snapshot["inspections"]}
queried_by_guid = {item["entity_guid"]: item for item in snapshot["queried_entities"]}
failures = []
checks = []
unsupported = []


def fail(path, wanted, actual, detail="mismatch"):
    failures.append({"field": path, "expected": wanted, "actual": actual, "reason": detail})


def numeric(path, wanted, actual, tolerance=1e-4):
    if isinstance(actual, dict) and isinstance(actual.get("value"), list):
        actual = actual["value"]
    if isinstance(wanted, (int, float)) and isinstance(actual, (int, float)):
        if not math.isfinite(actual) or abs(float(wanted) - float(actual)) > tolerance:
            fail(path, wanted, actual, f"absolute error exceeds {tolerance}")
        else:
            checks.append(path)
        return
    if isinstance(wanted, list) and isinstance(actual, list) and len(wanted) == len(actual):
        for index, (want_value, actual_value) in enumerate(zip(wanted, actual)):
            numeric(f"{path}[{index}]", want_value, actual_value, tolerance)
        return
    fail(path, wanted, actual, "shape or type mismatch")


def quaternion(path, wanted, actual):
    if not isinstance(actual, list) or len(actual) != 4:
        fail(path, wanted, actual, "expected quaternion [x,y,z,w]")
        return
    wanted_length = math.sqrt(sum(value * value for value in wanted))
    actual_length = math.sqrt(sum(value * value for value in actual))
    if wanted_length <= 0.0 or actual_length <= 0.0:
        fail(path, wanted, actual, "zero-length quaternion")
        return
    dot = abs(sum(a * b for a, b in zip(wanted, actual)) / (wanted_length * actual_length))
    angle = 2.0 * math.acos(min(1.0, max(0.0, dot)))
    if angle > 1e-4:
        fail(path, wanted, actual, f"quaternion angular error {angle} rad exceeds 0.0001 rad")
    else:
        checks.append(path)


def triangle_multiset(indices):
    if not isinstance(indices, list) or len(indices) % 3:
        return None
    triangles = []
    for start in range(0, len(indices), 3):
        a, b, c = indices[start:start + 3]
        # Ignore BVH's triangle reordering but preserve each triangle's winding.
        triangles.append(min((a, b, c), (b, c, a), (c, a, b)))
    return sorted(triangles)


def component(item, stable_type):
    return next((entry for entry in item.get("components", []) if entry.get("type") == stable_type), None)


for expected_item in expected:
    guid = expected_item["guid"]
    engine_item = inspected_by_guid.get(guid)
    if engine_item is None:
        fail(f"entities[{guid}]", "present", None, "source object or nested instance is absent from Engine snapshot")
        continue
    checks.append(f"entities[{guid}].present")
    if engine_item.get("name") != expected_item["name"]:
        fail(f"entities[{guid}].name", expected_item["name"], engine_item.get("name"))
    if engine_item.get("visible") != expected_item["visible"]:
        fail(f"entities[{guid}].visible", expected_item["visible"], engine_item.get("visible"))
    world_transform = engine_item.get("world_transform")
    if not isinstance(world_transform, dict):
        fail(f"entities[{guid}].world_transform", expected_item["world_transform"], world_transform,
             "world transform is missing or has not been updated")
    else:
        numeric(f"entities[{guid}].world_matrix", expected_item["world_matrix"], engine_item.get("world_matrix"))
        for field in ("position",):
            numeric(f"entities[{guid}].world_{field}", expected_item["world_transform"][field],
                    world_transform.get(field))
    expected_parent_guid = expected_item["parent_guid"] or (snapshot.get("link_roots", [{}])[0].get("entity_guid", ""))
    if engine_item.get("parent_guid", "") != expected_parent_guid:
        fail(f"entities[{guid}].parent_guid", expected_parent_guid, engine_item.get("parent_guid", ""))
    transform = component(engine_item, "Ailu.ECS.TransformComponent")
    if transform is None:
        fail(f"entities[{guid}].components", "Ailu.ECS.TransformComponent", None)
    else:
        props = transform.get("properties", {})
        wanted = expected_item["transform"]
        numeric(f"entities[{guid}].position", wanted["position"], props.get("_local_transform._position"))
        numeric(f"entities[{guid}].scale", wanted["scale"], props.get("_local_transform._scale"))
        quaternion(f"entities[{guid}].rotation", wanted["rotation"], props.get("_local_transform._rotation"))

    object_type = expected_item["object_type"]
    if object_type == "MESH":
        mesh_component = component(engine_item, "Ailu.ECS.StaticMeshComponent")
        mesh_data = engine_item.get("mesh_data")
        if mesh_component is None or mesh_data is None:
            fail(f"entities[{guid}].mesh", "StaticMeshComponent with readable mesh data", mesh_data)
            continue
        source_mesh = expected_item["mesh"]
        for field in ("vertices", "normals", "tangents", "uv0", "submeshes"):
            if field == "submeshes":
                source_submeshes = source_mesh[field]
                engine_submeshes = mesh_data.get(field)
                if not isinstance(engine_submeshes, list) or len(source_submeshes) != len(engine_submeshes):
                    fail(f"entities[{guid}].mesh.submeshes.count", len(source_submeshes),
                         len(engine_submeshes) if isinstance(engine_submeshes, list) else engine_submeshes)
                    continue
                for slot, (source_indices, engine_indices) in enumerate(zip(source_submeshes, engine_submeshes)):
                    if triangle_multiset(source_indices) != triangle_multiset(engine_indices):
                        fail(f"entities[{guid}].mesh.submeshes[{slot}].triangles",
                             {"count": len(source_indices) // 3, "winding_preserved": True},
                             {"count": len(engine_indices) // 3 if isinstance(engine_indices, list) else None,
                              "indices": engine_indices})
                    else:
                        checks.append(f"entities[{guid}].mesh.submeshes[{slot}].triangles")
                continue
            tolerance = 1e-5 if field == "uv0" else 1e-4
            numeric(f"entities[{guid}].mesh.{field}", source_mesh[field], mesh_data.get(field), tolerance)
        numeric(f"entities[{guid}].mesh.vertex_count", source_mesh["vertex_count"], mesh_data.get("vertex_count"), 0)
        numeric(f"entities[{guid}].mesh.triangle_count", source_mesh["triangle_count"], mesh_data.get("triangle_count"), 0)
        engine_slots = mesh_data.get("material_slots", [])
        if len(engine_slots) != len(source_mesh["materials"]):
            fail(f"entities[{guid}].mesh.material_slots.count", len(source_mesh["materials"]), len(engine_slots))
        for slot_index, source_material in enumerate(source_mesh["materials"]):
            if slot_index >= len(engine_slots):
                continue
            actual_material = engine_slots[slot_index]
            base = source_material["base_color"]
            numeric(f"entities[{guid}].mesh.material_slots[{slot_index}].base_color", base,
                    actual_material.get("base_color"), 1e-4)
            numeric(f"entities[{guid}].mesh.material_slots[{slot_index}].roughness", source_material["roughness"],
                    actual_material.get("roughness"), 1e-4)
            numeric(f"entities[{guid}].mesh.material_slots[{slot_index}].metallic", source_material["metallic"],
                    actual_material.get("metallic"), 1e-4)
            if actual_material.get("name") != source_material["name"]:
                fail(f"entities[{guid}].mesh.material_slots[{slot_index}].name", source_material["name"],
                     actual_material.get("name"))
            if actual_material.get("source_guid") != source_material["guid"]:
                fail(f"entities[{guid}].mesh.material_slots[{slot_index}].source_guid",
                     source_material["guid"], actual_material.get("source_guid"))
    elif object_type == "LIGHT":
        light = component(engine_item, "Ailu.ECS.LightComponent")
        if light is None:
            fail(f"entities[{guid}].light", "Ailu.ECS.LightComponent", None)
            continue
        props = light.get("properties", {})
        type_map = {"SUN": "kDirectional", "POINT": "kPoint", "SPOT": "kSpot", "AREA": "kArea"}
        if props.get("_type") != type_map[expected_item["light"]["type"]]:
            fail(f"entities[{guid}].light.type", type_map[expected_item["light"]["type"]], props.get("_type"))
        source_light = expected_item["light"]
        expected_color = [value * source_light["energy"] for value in source_light["color"]] + [1.0]
        numeric(f"entities[{guid}].light.color", expected_color, props.get("_light._light_color"), 1e-4)
        params = {"SUN": [0.0, 0.0, 0.0, source_light["radius"],],
                  "POINT": [source_light["range"], source_light["radius"], 0.0, 0.0],
                  "SPOT": [source_light["range"], source_light["inner_angle_degrees"],
                           source_light["outer_angle_degrees"], 0.0],
                  "AREA": [source_light["range"], source_light["width"], source_light["height"], 0.0]}
        numeric(f"entities[{guid}].light.parameters", params[source_light["type"]],
                props.get("_light._light_param"), 1e-4)
    elif object_type == "CAMERA":
        camera = component(engine_item, "Ailu.ECS.CCamera")
        if camera is None:
            fail(f"entities[{guid}].camera", "Ailu.ECS.CCamera", None)
            continue
        props = camera.get("properties", {})
        expected_camera = expected_item["camera"]
        expected_type = 1 if expected_camera["type"] == "PERSP" else 0
        if props.get("_camera._type") != expected_type:
            fail(f"entities[{guid}].camera.type", expected_type, props.get("_camera._type"))
        if expected_camera["type"] == "ORTHO":
            numeric(f"entities[{guid}].camera.ortho_size", expected_camera["ortho_size"],
                    props.get("_camera._size"), 1e-4)
        numeric(f"entities[{guid}].camera.fov", expected_camera["fov_y_degrees"], props.get("_camera._fov_h"), 0.05)
        numeric(f"entities[{guid}].camera.near_clip", expected_camera["near_clip"], props.get("_camera._near_clip"), 1e-4)
        numeric(f"entities[{guid}].camera.far_clip", expected_camera["far_clip"], props.get("_camera._far_clip"), 1e-3)

actual_expected_ids = set(expected_by_guid)
actual_query_ids = set(queried_by_guid)
root_ids = {item["entity_guid"] for item in snapshot.get("link_roots", [])}
missing = sorted(actual_expected_ids - actual_query_ids)
extras = sorted(actual_query_ids - actual_expected_ids - root_ids)
if missing:
    fail("scene.query_entities.missing_source_ids", [], missing)
if extras:
    fail("scene.query_entities.unexpected_entities", sorted(actual_expected_ids), extras,
         "isolated fixture scene contains entities outside the Blender source/instance set")
if len(snapshot.get("link_roots", [])) != 1:
    fail("scene.query_entities.link_root_count", 1, len(snapshot.get("link_roots", [])))
if len(snapshot.get("inspections", [])) != len(expected):
    fail("scene.inspect_entity.count", len(expected), len(snapshot.get("inspections", [])))


def seeded_mutations():
    cases = []
    if expected:
        child = next((obj for obj in reference["objects"] if obj["parent_guid"]), expected[0])
        cases.append(("wrong_parent", child["guid"], "parent_guid", "wrong-parent-guid"))
        mesh_obj = next((obj for obj in expected if obj["object_type"] == "MESH"), None)
        transform_obj = mesh_obj or expected[0]
        cases.append(("swapped_axis", transform_obj["guid"], "position", None))
        cases.append(("world_transform_mismatch", transform_obj["guid"], "world_matrix", None))
        cases.append(("visibility_mismatch", transform_obj["guid"], "visible", None))
    instance = reference["instances"][0] if reference["instances"] else None
    if instance:
        cases.append(("absent_instance", instance["guid"], "missing", None))
    mesh_obj = next((obj for obj in expected if obj["object_type"] == "MESH" and obj["mesh"]["materials"]), None)
    if mesh_obj:
        cases.append(("material_mismatch", mesh_obj["guid"], "material", None))
        cases.append(("material_source_guid_mismatch", mesh_obj["guid"], "material_source_guid", None))
    camera_obj = next((obj for obj in expected if obj["object_type"] == "CAMERA"), None)
    if camera_obj:
        cases.append(("camera_type_mismatch", camera_obj["guid"], "camera_type", None))
    ortho_camera_obj = next((obj for obj in expected if obj["object_type"] == "CAMERA"
                             and obj["camera"]["type"] == "ORTHO"), None)
    if ortho_camera_obj:
        cases.append(("camera_ortho_size_mismatch", ortho_camera_obj["guid"], "camera_ortho_size", None))
    results = []
    for name, guid, kind, value in cases:
        mutated = copy.deepcopy(snapshot)
        if kind == "missing":
            mutated["inspections"] = [item for item in mutated["inspections"] if item["entity_guid"] != guid]
            mutated["queried_entities"] = [item for item in mutated["queried_entities"] if item["entity_guid"] != guid]
        else:
            inspected = next(item for item in mutated["inspections"] if item["entity_guid"] == guid)
            if kind == "parent_guid":
                inspected["parent_guid"] = value
            elif kind == "position":
                props = component(inspected, "Ailu.ECS.TransformComponent")["properties"]
                position = props["_local_transform._position"]
                props["_local_transform._position"] = [position[1], position[0], position[2]]
            elif kind == "world_matrix":
                inspected["world_matrix"][0][3] += 1.0
            elif kind == "visible":
                inspected["visible"] = not inspected["visible"]
            elif kind == "material":
                material = inspected["mesh_data"]["material_slots"][0]
                material["name"] = "InjectedWrongMaterial"
            elif kind == "material_source_guid":
                inspected["mesh_data"]["material_slots"][0]["source_guid"] = "wrong-source-guid"
            elif kind == "camera_type":
                camera = component(inspected, "Ailu.ECS.CCamera")["properties"]
                camera["_camera._type"] = 0 if camera["_camera._type"] == 1 else 1
            elif kind == "camera_ortho_size":
                camera = component(inspected, "Ailu.ECS.CCamera")["properties"]
                camera["_camera._size"] += 1.0
        mutated_failures = compare_without_mutations(reference, mutated)
        results.append({"name": name, "detected": bool(mutated_failures), "failure_count": len(mutated_failures),
                        "first_failure": mutated_failures[0] if mutated_failures else None})
    return results


def compare_without_mutations(source_reference, engine_snapshot):
    local_expected = source_reference["objects"] + source_reference["instances"]
    local_inspected = {item["entity_guid"]: item for item in engine_snapshot["inspections"]}
    local_failures = []
    for source in local_expected:
        actual = local_inspected.get(source["guid"])
        if actual is None:
            local_failures.append({"field": f"entities[{source['guid']}].present",
                                  "expected": "present", "actual": "absent"})
            continue
        expected_parent = source["parent_guid"] or snapshot.get("link_roots", [{}])[0].get("entity_guid", "")
        if actual.get("parent_guid", "") != expected_parent:
            local_failures.append({"field": f"entities[{source['guid']}].parent_guid",
                                  "expected": expected_parent, "actual": actual.get("parent_guid", "")})
        transform = component(actual, "Ailu.ECS.TransformComponent")
        actual_position = (transform or {}).get("properties", {}).get("_local_transform._position")
        wanted_position = source["transform"]["position"]
        if (not isinstance(actual_position, list) or len(actual_position) != 3
                or any(abs(float(a) - float(b)) > 1e-4 for a, b in zip(actual_position, wanted_position))):
            local_failures.append({"field": f"entities[{source['guid']}].position",
                                  "expected": wanted_position, "actual": actual_position})
        if actual.get("visible") != source["visible"]:
            local_failures.append({"field": f"entities[{source['guid']}].visible",
                                  "expected": source["visible"], "actual": actual.get("visible")})
        actual_world_matrix = actual.get("world_matrix")
        wanted_world_matrix = source["world_matrix"]
        if (not isinstance(actual_world_matrix, list) or len(actual_world_matrix) != 4
                or any(not isinstance(row, list) or len(row) != 4 for row in actual_world_matrix)
                or any(abs(float(actual_world_matrix[row][column]) - wanted_world_matrix[row][column]) > 1e-4
                       for row in range(4) for column in range(4))):
            local_failures.append({"field": f"entities[{source['guid']}].world_matrix",
                                  "expected": wanted_world_matrix, "actual": actual_world_matrix})
        if source["object_type"] == "MESH":
            slots = actual.get("mesh_data", {}).get("material_slots", [])
            for index, material in enumerate(source["mesh"]["materials"]):
                if index >= len(slots) or slots[index].get("name") != material["name"]:
                    local_failures.append({"field": f"entities[{source['guid']}].mesh.material_slots[{index}].name",
                                          "expected": material["name"],
                                          "actual": slots[index].get("name") if index < len(slots) else "absent"})
                    break
                if slots[index].get("source_guid") != material["guid"]:
                    local_failures.append({"field": f"entities[{source['guid']}].mesh.material_slots[{index}].source_guid",
                                          "expected": material["guid"],
                                          "actual": slots[index].get("source_guid") if index < len(slots) else "absent"})
                    break
        elif source["object_type"] == "CAMERA":
            camera = component(actual, "Ailu.ECS.CCamera")
            props = (camera or {}).get("properties", {})
            wanted_camera = source["camera"]
            wanted_type = 1 if wanted_camera["type"] == "PERSP" else 0
            if props.get("_camera._type") != wanted_type:
                local_failures.append({"field": f"entities[{source['guid']}].camera.type",
                                      "expected": wanted_type, "actual": props.get("_camera._type")})
            if (wanted_camera["type"] == "ORTHO"
                    and abs(float(props.get("_camera._size", float("nan"))) - wanted_camera["ortho_size"]) > 1e-4):
                local_failures.append({"field": f"entities[{source['guid']}].camera.ortho_size",
                                      "expected": wanted_camera["ortho_size"], "actual": props.get("_camera._size")})
    return local_failures


mutations = seeded_mutations()
for mutation in mutations:
    if not mutation["detected"]:
        fail(f"mutation_tests.{mutation['name']}", "detected", "not detected")

output_dir.mkdir(parents=True, exist_ok=True)
assertions = {"status": "PASS" if not failures else "FAIL", "source_blend_sha256": reference["source_blend_sha256"],
              "checked_source_entities": len(expected), "checked_engine_entities": len(snapshot["inspections"]),
              "check_count": len(checks), "checks": checks, "seeded_mutations": mutations,
              "unsupported_fields": unsupported, "failure_count": len(failures)}
(output_dir / "assertions.json").write_text(json.dumps(assertions, indent=2), encoding="utf-8")
(output_dir / "failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")
print(json.dumps(assertions, indent=2))
sys.exit(0 if not failures else 1)
