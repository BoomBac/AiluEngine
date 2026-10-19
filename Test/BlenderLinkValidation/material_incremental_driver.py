"""Main-thread mutation phase for the controlled native validation fixture only."""
import copy
import json
import shutil
import time
import bpy
from ailu_live_link import material_bake,object_sync

def Run(link,output,wire):
    stage=output.parent
    reference=json.loads((stage/"material_reference.json").read_text())
    if not reference["synthetic"]:
        return {"status":"SKIPPED","reason":"Incremental mutation is restricted to the generated fixture"}
    wire.flush()
    shutil.copy2(output/"packets.bin",output/"packets_baseline.bin")
    deadline=time.monotonic()+90
    while not (stage/"material_baseline_ready").exists():
        if time.monotonic()>deadline:
            raise TimeoutError("Material baseline oracle did not finish")
        link.poll()
        time.sleep(.05)
    ordinary=bpy.data.objects["OrdinaryMulti"]
    sparse=bpy.data.objects["SparseSlots"]
    generated=bpy.data.objects["NodeReplacement"]
    originals=(ordinary.material_slots[0].material,sparse.material_slots[1].material,sparse.data.polygons[0].material_index)
    setter=next(node for node in generated.modifiers["EvaluatedMaterial"].node_group.nodes if node.type=="SET_MATERIAL")
    original_node_material=setter.inputs["Material"].default_value
    try:
        ordinary.material_slots[0].material=bpy.data.materials["MarkerBlue"]
        sparse.material_slots[1].material=bpy.data.materials["MarkerGreen"]
        sparse.data.polygons[0].material_index=1
        sparse.data.update()
        setter.inputs["Material"].default_value=bpy.data.materials["MarkerGreen"]
        bpy.context.view_layer.update()
        material_bake.invalidate()
        for obj,geometry in ((ordinary,False),(sparse,True),(generated,True)):
            object_sync.sync_object(bpy.context,obj,link,set(),geometry,True,False)
        graph=bpy.context.evaluated_depsgraph_get()
        updated=copy.deepcopy(reference)
        for item in updated["objects"]:
            if item["name"] not in {ordinary.name,sparse.name,generated.name}:
                continue
            obj=bpy.data.objects[item["name"]]
            evaluated=obj.evaluated_get(graph)
            mesh=evaluated.to_mesh(preserve_all_data_layers=True,depsgraph=graph)
            try:
                mesh.calc_loop_triangles()
                item["geometry"]={"loop_vertices":len(mesh.loops),"triangles":len(mesh.loop_triangles),
                    "materials":[{"name":material.original.name_full,"guid":material.original["ailu_live_link_guid"]}
                                 if material else None for material in mesh.materials],
                    "used_slots":sorted({triangle.material_index for triangle in mesh.loop_triangles})}
            finally:
                evaluated.to_mesh_clear()
        for marker in updated["markers"]:
            if marker["name"]=="OrdinaryMulti.left":
                marker["color"]=[0,0,1]
            elif marker["name"] in {"SparseSlots","NodeReplacement"}:
                marker["color"]=[0,1,0]
        (stage/"material_mutation_reference.json").write_text(json.dumps(updated,indent=2),encoding="utf-8")
        deadline=time.monotonic()+30
        while link.queued_bytes or link._wire_packet:
            if time.monotonic()>deadline:
                raise TimeoutError("Incremental material transport did not drain")
            link.poll()
            time.sleep(.001)
        wire.flush()
        (stage/"material_mutation_sent.json").write_text(json.dumps({"objects":[ordinary.name,sparse.name,generated.name]}),encoding="utf-8")
        deadline=time.monotonic()+30
        while not (stage/"material_mutation_checked").exists():
            if time.monotonic()>deadline:
                raise TimeoutError("Engine did not confirm material mutation assertions")
            link.poll()
            time.sleep(.05)
        return {"status":"PASS","mutations":["object_material_rebind","sparse_slot_reorder","geometry_nodes_material_replacement"]}
    finally:
        ordinary.material_slots[0].material=originals[0]
        sparse.material_slots[1].material=originals[1]
        sparse.data.polygons[0].material_index=originals[2]
        setter.inputs["Material"].default_value=original_node_material
