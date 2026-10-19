"""Generate focused fixtures or copy a source scene; derive material expectations directly from bpy.to_mesh."""
import bpy
import json
import math
import os
import sys
import uuid
from pathlib import Path
from mathutils import Matrix

output = Path(sys.argv[sys.argv.index("--")+1])
existing = "--existing" in sys.argv
namespace = uuid.UUID("cab5c51a-0871-4519-a75a-a542f720b77b")
markers = []

if existing and bpy.context.mode != "OBJECT":
    active = bpy.context.view_layer.objects.active or bpy.context.active_object
    if active is not None:
        with bpy.context.temp_override(active_object=active, selected_objects=[active],
                                       selected_editable_objects=[active]):
            bpy.ops.object.mode_set(mode="OBJECT")

def Material(name,color,bake=False):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    node = next(node for node in material.node_tree.nodes if node.type=="BSDF_PRINCIPLED")
    node.inputs["Base Color"].default_value = (*color,1)
    node.inputs["Roughness"].default_value = 0.9
    if bake:
        material["ailu_export_mode"] = "BAKE"
        material["ailu_bake_resolution"] = 32
    return material

def Plane(name,location,materials,slot=0):
    mesh = bpy.data.meshes.new(name+"Mesh")
    mesh.from_pydata([(-0.65,-0.65,0),(0.65,-0.65,0),(0.65,0.65,0),(-0.65,0.65,0)],[],[(0,1,2,3)])
    for material in materials:
        mesh.materials.append(material or red)
        if material is None:
            mesh.materials[len(mesh.materials)-1] = None
    mesh.polygons[0].material_index = slot
    uv = mesh.uv_layers.new(name="UV0")
    for item,co in zip(uv.data,((0,0),(1,0),(1,1),(0,1))):
        item.uv=co
    obj = bpy.data.objects.new(name,mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.location=location
    return obj

def SetMaterialNodes(obj,material):
    tree = bpy.data.node_groups.new(obj.name+"Nodes","GeometryNodeTree")
    tree.interface.new_socket(name="Geometry",in_out="INPUT",socket_type="NodeSocketGeometry")
    tree.interface.new_socket(name="Geometry",in_out="OUTPUT",socket_type="NodeSocketGeometry")
    source = tree.nodes.new("GeometryNodeMeshGrid") if obj.type=="CURVE" else tree.nodes.new("NodeGroupInput")
    if obj.type=="CURVE":
        source.inputs["Size X"].default_value=1.3
        source.inputs["Size Y"].default_value=1.3
        source.inputs["Vertices X"].default_value=2
        source.inputs["Vertices Y"].default_value=2
    assign=tree.nodes.new("GeometryNodeSetMaterial")
    assign.inputs["Material"].default_value=material
    destination=tree.nodes.new("NodeGroupOutput")
    tree.links.new(source.outputs["Mesh" if obj.type=="CURVE" else "Geometry"],assign.inputs["Geometry"])
    tree.links.new(assign.outputs["Geometry"],destination.inputs["Geometry"])
    obj.modifiers.new("EvaluatedMaterial","NODES").node_group=tree
    return assign

if not existing:
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj,do_unlink=True)
    red=Material("MarkerRed",(1,0,0))
    green=Material("MarkerGreen",(0,1,0))
    blue=Material("MarkerBlue",(0,0,1))
    yellow=Material("MarkerBakedYellow",(1,1,0),True)
    ordinary=Plane("OrdinaryMulti",(-3,-1.5,0),[red,green])
    # A second face makes both slots used rather than merely present in the table.
    ordinary.data.clear_geometry()
    ordinary.data.from_pydata([(-.65,-.65,0),(0,-.65,0),(.65,-.65,0),(-.65,.65,0),(0,.65,0),(.65,.65,0)],[],[(0,1,4,3),(1,2,5,4)])
    ordinary.data.polygons[1].material_index=1
    markers.extend([{"name":"OrdinaryMulti.left","position":[-3.325,0,1.5],"color":[1,0,0]},
                    {"name":"OrdinaryMulti.right","position":[-2.675,0,1.5],"color":[0,1,0]}])
    Plane("SparseSlots",(0,-1.5,0),[None,red,None,blue],3)
    override=Plane("ObjectOverride",(3,-1.5,0),[red])
    override.material_slots[0].link="OBJECT"
    override.material_slots[0].material=green
    replaced=Plane("NodeReplacement",(-3,1.5,0),[red])
    SetMaterialNodes(replaced,blue)
    curve=bpy.data.objects.new("CurveGenerated",bpy.data.curves.new("CurveGeneratedData","CURVE"))
    bpy.context.scene.collection.objects.link(curve)
    curve.location=(0,1.5,0)
    SetMaterialNodes(curve,green)
    plain_data=bpy.data.curves.new("PlainCurveData","CURVE")
    plain_data.dimensions="3D"
    plain_data.bevel_depth=.08
    spline=plain_data.splines.new("POLY")
    spline.points.add(1)
    spline.points[0].co=(-.5,0,0,1)
    spline.points[1].co=(.5,0,0,1)
    plain_data.materials.append(red)
    plain=bpy.data.objects.new("PlainCurve",plain_data)
    plain.location=(-3,4,0)
    bpy.context.scene.collection.objects.link(plain)
    Plane("BakedSparse",(3,1.5,0),[None,yellow],1)
    texture_material=Material("MarkerTexture",(1,1,1))
    image=bpy.data.images.new("MarkerQuadrants",width=16,height=16,alpha=True)
    quadrant_colors=((1,0,0,1),(0,1,0,1),(0,0,1,1),(1,1,0,1))
    image.pixels=[channel for y in range(16) for x in range(16) for channel in quadrant_colors[(y//8)*2+x//8]]
    image.pack()
    texture=texture_material.node_tree.nodes.new("ShaderNodeTexImage")
    texture.image=image
    principled=next(node for node in texture_material.node_tree.nodes if node.type=="BSDF_PRINCIPLED")
    texture_material.node_tree.links.new(texture.outputs["Color"],principled.inputs["Base Color"])
    Plane("TextureQuadrants",(3,4,0),[texture_material])
    for dx,dy,color in ((-.325,-.325,[1,0,0]),(.325,-.325,[0,1,0]),(-.325,.325,[0,0,1]),(.325,.325,[1,1,0])):
        markers.append({"name":"TextureQuadrants"+str((dx,dy)),"position":[3+dx,0,-4-dy],"color":color})
    source=Plane("InstanceSource",(0,0,0),[None,blue],1)
    collection=bpy.data.collections.new("ExternalMaterialSource")
    for owner in list(source.users_collection):
        owner.objects.unlink(source)
    collection.objects.link(source)
    instancer=bpy.data.objects.new("MaterialInstancer",None)
    instancer.instance_type="COLLECTION"
    instancer.instance_collection=collection
    instancer.location=(0,4,0)
    bpy.context.scene.collection.objects.link(instancer)
    for name,color in (("SparseSlots",[0,0,1]),("ObjectOverride",[0,1,0]),("NodeReplacement",[0,0,1]),
                       ("CurveGenerated",[0,1,0]),("BakedSparse",[1,1,0])):
        obj=bpy.data.objects[name]
        markers.append({"name":name,"position":[obj.location.x,0,-obj.location.y],"color":color})

def ExpectedGuid(data_block,kind):
    library=getattr(data_block,"library",None)
    if library is None:
        return str(uuid.uuid5(namespace,kind+":"+data_block.name_full))
    path=os.path.normcase(os.path.normpath(bpy.path.abspath(library.filepath)))
    node="lib:"+path+":"+data_block.bl_rna.identifier+":"+data_block.name
    return str(uuid.uuid5(uuid.NAMESPACE_URL,node))

for obj in bpy.data.objects:
    obj["ailu_live_link_guid"]=ExpectedGuid(obj,"object")
for material in bpy.data.materials:
    material["ailu_live_link_guid"]=ExpectedGuid(material,"material")
for obj in bpy.data.objects:
    if getattr(obj,"data",None) is not None:
        obj.data["ailu_live_link_guid"]=ExpectedGuid(obj.data,"data")
scene=bpy.context.scene
session=str(uuid.uuid5(namespace,"session:"+scene.name_full))
scene["ailu_live_link_session_guid"]=session
bpy.context.view_layer.update()
graph=bpy.context.evaluated_depsgraph_get()
supported={"MESH","CURVE","SURFACE","FONT","META"}
geometry={}

def BaseGuid(data_block):
    return data_block["ailu_live_link_guid"] if data_block else ""

def TreeUsesSpatialCoordinates(tree, visited=None):
    visited=set() if visited is None else visited
    if not tree or tree.as_pointer() in visited:
        return False
    visited.add(tree.as_pointer())
    for node in tree.nodes:
        if node.type=="NEW_GEOMETRY":
            return True
        if node.type=="TEX_COORD" and any(socket.is_linked and socket.name!="UV" for socket in node.outputs):
            return True
        if node.type=="GROUP" and TreeUsesSpatialCoordinates(node.node_tree,visited):
            return True
    return False

def UsesSpatialCoordinates(material):
    return bool(material and material.use_nodes and TreeUsesSpatialCoordinates(material.node_tree))

def HasImage(tree, visited=None):
    visited=set() if visited is None else visited
    if not tree or tree.as_pointer() in visited:
        return False
    visited.add(tree.as_pointer())
    return any(node.type=="TEX_IMAGE" or (node.type=="GROUP" and HasImage(node.node_tree,visited))
               for node in tree.nodes)

def CanExportDirect(material):
    if not material or not material.use_nodes or not material.node_tree:
        return False
    output=next((node for node in material.node_tree.nodes
                 if node.type=="OUTPUT_MATERIAL" and node.is_active_output),None)
    if not output or not output.inputs["Surface"].is_linked:
        return True
    source=output.inputs["Surface"].links[0].from_node
    if source.type=="BSDF_TRANSPARENT":
        return True
    if source.type!="BSDF_PRINCIPLED":
        return False
    for name in ("Base Color","Color","Roughness","Metallic","Emission Color","Emission Strength",
                 "Strength","Alpha","Normal"):
        socket=source.inputs.get(name)
        if not socket or not socket.is_linked:
            continue
        node=socket.links[0].from_node
        while node.type=="REROUTE" and node.inputs[0].is_linked:
            node=node.inputs[0].links[0].from_node
        if node.type not in {"TEX_IMAGE","RGB","VALUE"}:
            return False
    return True

def RequiresBake(material):
    if not material or not material.use_nodes:
        return False
    mode=str(material.get("ailu_export_mode","AUTO")).upper()
    if mode=="BAKE":
        return True
    if mode=="DIRECT" or CanExportDirect(material):
        return False
    output=next((node for node in material.node_tree.nodes
                 if node.type=="OUTPUT_MATERIAL" and node.is_active_output),None)
    return bool(output and output.inputs["Surface"].is_linked)

def MaterialTable(mesh, material_slots):
    materials=[material.original if material else None for material in mesh.materials]
    for index,slot in enumerate(material_slots):
        if slot.link=="OBJECT" and index<len(materials):
            materials[index]=slot.material.original if slot.material else None
    return materials

def MaterialGuid(material, obj, evaluated_materials, has_source_uv):
    if material is None:
        return ""
    guid=BaseGuid(material)
    if obj is None:
        return guid
    mode=str(material.get("ailu_export_mode","AUTO")).upper()
    object_needs_bake=any(RequiresBake(item) for item in evaluated_materials if item)
    if not RequiresBake(material):
        if mode=="DIRECT" or not object_needs_bake:
            return guid
        has_explicit_atlas=any(item and str(item.get("ailu_export_mode","AUTO")).upper()=="BAKE"
                               for item in evaluated_materials)
        if not has_explicit_atlas and has_source_uv:
            return guid
        if not HasImage(material.node_tree):
            return guid
    if obj.modifiers or any(UsesSpatialCoordinates(item) for item in evaluated_materials if item):
        bake_identity=BaseGuid(obj)
    else:
        slots=":".join(BaseGuid(item) if item else "" for item in evaluated_materials)
        bake_identity=(BaseGuid(obj.data)+":"+obj.data.name_full+":"+slots+":"+
                       str(tuple(obj.matrix_world.to_scale())))
    return str(uuid.uuid5(uuid.NAMESPACE_URL,bake_identity+":"+guid))

def Geometry(obj, mesh_override=None, evaluated_object=None, bake_materials=True):
    key=obj.name_full if mesh_override is None else None
    if key is not None and key in geometry:
        return geometry[key]
    evaluated=None
    if mesh_override is None:
        evaluated=obj.evaluated_get(graph)
        mesh=evaluated.to_mesh(preserve_all_data_layers=True,depsgraph=graph)
        material_slots=obj.material_slots
    else:
        mesh=mesh_override
        material_slots=evaluated_object.material_slots if evaluated_object else obj.material_slots
    try:
        if mesh is None:
            return None
        mesh.calc_loop_triangles()
        evaluated_materials=MaterialTable(mesh,material_slots)
        has_source_uv=any(layer.active_render for layer in mesh.uv_layers)
        slots=[{"name":material.name_full,"guid":MaterialGuid(material,obj if bake_materials else None,
                evaluated_materials,has_source_uv),"expects_texture":not existing and
                material.name_full=="MarkerTexture"} if material else None for material in evaluated_materials]
        for index,slot in enumerate(material_slots):
            if slot.link=="OBJECT" and index<len(slots):
                material=slot.material
                slots[index]={"name":material.name_full,"guid":MaterialGuid(material,obj if bake_materials else None,
                    evaluated_materials,has_source_uv)} if material else None
        if not existing and obj.name=="ObjectOverride":
            assert slots[0]["name"]=="MarkerGreen", "The controlled object override must replace the red data material"
        used=sorted({triangle.material_index for triangle in mesh.loop_triangles})
        slot_triangles={index:sum(triangle.material_index==index for triangle in mesh.loop_triangles) for index in used}
        result={"loop_vertices":len(mesh.loops),"triangles":len(mesh.loop_triangles),"materials":slots,
                "used_slots":used,"slot_triangles":slot_triangles}
        if key is not None:
            geometry[key]=result
        return result
    finally:
        if evaluated:
            evaluated.to_mesh_clear()

objects=[]
converted_self_sources={instance.object.original for instance in graph.object_instances
    if instance.is_instance and instance.object.type=="MESH" and instance.object.original.type!="MESH"
    and instance.parent and instance.parent.original==instance.object.original}
for obj in scene.objects:
    shape=Geometry(obj) if obj.type in supported and obj not in converted_self_sources else None
    if shape and shape["triangles"]:
        objects.append({"guid":obj["ailu_live_link_guid"],"name":obj.name_full,"source_type":obj.type,"geometry":shape})
instances=[]
for instance in graph.object_instances:
    if not instance.is_instance or instance.object.type!="MESH":
        continue
    obj=instance.object.original
    generated_component=bool(instance.parent and instance.parent.original==obj)
    shape=(Geometry(obj,instance.object.data,instance.object,False) if generated_component else Geometry(obj))
    if not shape or not shape["triangles"]:
        continue
    parent=instance.parent.original["ailu_live_link_guid"] if instance.parent else ""
    guid=str(uuid.uuid5(uuid.NAMESPACE_URL,session+":"+parent+":"+obj["ailu_live_link_guid"]+":"+str(tuple(instance.persistent_id))))
    prototype_guid=(str(uuid.uuid5(uuid.NAMESPACE_URL,obj["ailu_live_link_guid"]+":instance-geometry"))
                    if generated_component else obj["ailu_live_link_guid"])
    instances.append({"guid":guid,"prototype_guid":prototype_guid,"name":obj.name_full+" [Instance]",
                      "generated_component":generated_component,
                      "visible":instance.show_self and obj.visible_camera,"geometry":shape})

static_merge=None
if len(instances)>=4096:
    groups={}
    for item in instances:
        material_guids=tuple(slot["guid"] if slot else "" for slot in item["geometry"]["materials"])
        key=(material_guids,item["visible"])
        groups.setdefault(key,[]).append(item)
    merged_batches=[]
    for (material_guids,visible),group in groups.items():
        group.sort(key=lambda item:item["guid"])
        group_guid=str(uuid.uuid5(uuid.NAMESPACE_URL,session+":static-merge:"+",".join(material_guids)+":"+str(int(visible))))
        chunks=[]
        chunk=[]
        loop_vertices=0
        triangles=0
        slot_triangles={}
        for item in group:
            geometry_shape=item["geometry"]
            vertices=geometry_shape["loop_vertices"]
            item_triangles=geometry_shape["triangles"]
            if chunk and (loop_vertices+vertices>200000 or triangles+item_triangles>400000 or len(chunk)>=1024):
                chunks.append((chunk,loop_vertices,triangles,slot_triangles))
                chunk=[]
                loop_vertices=0
                triangles=0
                slot_triangles={}
            chunk.append(item)
            loop_vertices+=vertices
            triangles+=item_triangles
            for slot,count in geometry_shape["slot_triangles"].items():
                slot_triangles[slot]=slot_triangles.get(slot,0)+count
        if chunk:
            chunks.append((chunk,loop_vertices,triangles,slot_triangles))
        for chunk_index,(chunk,loop_vertices,triangles,slot_triangles) in enumerate(chunks):
            batch_guid=str(uuid.uuid5(uuid.NAMESPACE_URL,group_guid+":object:"+str(chunk_index)))
            prototype_guid=str(uuid.uuid5(uuid.NAMESPACE_URL,group_guid+":mesh:"+str(chunk_index)))
            material_table=chunk[0]["geometry"]["materials"]
            merged_geometry={"loop_vertices":loop_vertices,"triangles":triangles,
                             "used_slots":sorted(slot_triangles),"slot_triangles":slot_triangles,
                             "materials":material_table}
            merged_batches.append({"guid":batch_guid,"prototype_guid":prototype_guid,
                "name":chunk[0]["name"].removesuffix(" [Instance]")+" [Merged Batch "+str(chunk_index)+"]",
                "geometry":merged_geometry})
    static_merge={"source_instances":len(instances),"batches":merged_batches}

reference={"session_guid":session,"objects":objects,"instances":instances,"static_merge":static_merge,
           "markers":markers,"synthetic":not existing}
output.mkdir(parents=True,exist_ok=True)
(output/"material_reference.json").write_text(json.dumps(reference,indent=2),encoding="utf-8")
bpy.ops.wm.save_as_mainfile(filepath=str(output/"materials_fixture.blend"),relative_remap=True)
print("MATERIAL_REFERENCE_READY",len(objects),len(instances),flush=True)
