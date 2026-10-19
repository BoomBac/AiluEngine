"""Independent evaluated-source -> wire -> Engine material binding oracle; importable by focused regression tests."""
import copy
import json
import struct
import sys
from pathlib import Path


def ReadPackets(path):
    data=Path(path).read_bytes()
    offset=0
    objects,meshes,materials={},{},{}
    while offset<len(data):
        if len(data)-offset<20:
            raise ValueError("Truncated protocol header")
        magic,version,kind,size,sequence=struct.unpack_from("<IHHIQ",data,offset)
        if magic!=0x4B4C4C41 or version!=1 or offset+20+size>len(data):
            raise ValueError("Invalid protocol frame")
        payload=data[offset+20:offset+20+size]
        offset+=20+size
        if kind==12:
            value=json.loads(payload)
            objects[value["guid"]]=value
        elif kind==21:
            value=json.loads(payload)
            materials[value["material_guid"]]=value
        elif kind==31:
            batch=json.loads(payload)
            for item in batch["instances"]:
                objects[item["guid"]]=dict(item,prototype_guid=batch["prototype_guid"])
        elif kind in (20,24,32):
            guid=payload[:36].decode("ascii")
            vertices,submeshes=struct.unpack_from("<II",payload,44)
            position=76+vertices*48
            slots=[]
            slot_triangles={}
            triangles=0
            for index in range(submeshes):
                slot,count=struct.unpack_from("<II",payload,position)
                if count%3 or position+8+count*4>len(payload):
                    raise ValueError("Invalid submesh")
                slots.append(slot)
                triangles+=count//3
                slot_triangles[str(slot)]=count//3
                position+=8+count*4
            meshes[guid]={"used_slots":slots,"loop_vertices":vertices,"triangles":triangles,
                          "slot_triangles":slot_triangles}
    return {"objects":objects,"meshes":meshes,"materials":materials}


def ExpectedItems(reference):
    batches=reference.get("static_merge")
    instances=[] if batches else reference["instances"]
    return reference["objects"]+instances+(batches["batches"] if batches else [])


def Validate(reference,wire,snapshot=None):
    checks,failures,unsupported=[],[],[]
    actual={item["entity_guid"]:item for item in snapshot.get("inspections",[])} if snapshot else {}
    def Check(subject,field,expected,value):
        record={"subject":subject,"field":field,"expected":expected,"actual":value}
        (checks if expected==value else failures).append(record)
    for source in ExpectedItems(reference):
        guid,name=source["guid"],source["name"]
        obj=wire["objects"].get(guid)
        Check(name,"wire.object_present",True,obj is not None)
        if obj is None:
            continue
        mesh=wire["meshes"].get(obj.get("prototype_guid",guid))
        Check(name,"wire.mesh_present",True,mesh is not None)
        if mesh is None:
            continue
        geometry=source["geometry"]
        for field in ("used_slots","triangles","loop_vertices"):
            Check(name,"wire.mesh."+field,geometry[field],mesh[field])
        if "slot_triangles" in geometry:
            expected_slot_triangles={str(index):count for index,count in geometry["slot_triangles"].items()}
            Check(name,"wire.mesh.slot_triangles",expected_slot_triangles,mesh["slot_triangles"])
        slots=obj.get("material_guids",[])
        Check(name,"wire.material_table_size",len(geometry["materials"]),len(slots))
        for index,material in enumerate(geometry["materials"]):
            if material is None:
                Check(name,f"wire.material[{index}].empty_preserved","",slots[index] if index<len(slots) else None)
        if snapshot is not None:
            current=actual.get(guid,{})
            engine=current.get("mesh_data")
            Check(name,"engine.mesh_present",True,isinstance(engine,dict))
            if engine:
                Check(name,"engine.triangles",geometry["triangles"],engine.get("triangle_count"))
                Check(name,"engine.submesh_material_count",len(geometry["used_slots"]),len(engine.get("material_slots",[])))
        for ordinal,index in enumerate(geometry["used_slots"]):
            material=geometry["materials"][index] if index<len(geometry["materials"]) else None
            if material is None:
                continue
            valid=index<len(slots) and bool(slots[index])
            Check(name,f"wire.material[{index}].resolvable",True,valid)
            if not valid:
                continue
            packet=wire["materials"].get(slots[index])
            Check(name,f"wire.material[{index}].published",True,packet is not None)
            if packet is None:
                continue
            actual_name=packet.get("name","")
            Check(name,f"wire.material[{index}].source_name",material["name"],actual_name.split("@",1)[0])
            if "@" not in actual_name:
                Check(name,f"wire.material[{index}].source_guid",material["guid"],slots[index])
            if material.get("expects_texture"):
                Check(name,f"wire.material[{index}].texture_published",True,bool(packet.get("base_color_path")))
            if snapshot is not None:
                bindings=actual.get(guid,{}).get("mesh_data",{}).get("material_slots",[])
                binding=bindings[ordinal] if ordinal<len(bindings) else {}
                Check(name,f"engine.material[{ordinal}].source_guid",slots[index],binding.get("source_guid"))
                Check(name,f"engine.material[{ordinal}].unexpected_fallback",False,binding.get("is_fallback"))
    return {"status":"FAIL" if failures else "INCONCLUSIVE" if unsupported else "PASS",
            "check_count":len(checks),"failure_count":len(failures),"checks":checks,"failures":failures,"unsupported":unsupported}


def SeededChecks(reference,wire,snapshot):
    expected=ExpectedItems(reference)
    subject=next(item for item in expected if any(slot is not None for slot in item["geometry"]["materials"]))
    guid=subject["guid"]
    bad=copy.deepcopy(wire)
    bad["objects"][guid]["material_guids"]=[]
    results={"missing_material_table":Validate(reference,bad,snapshot)["status"]=="FAIL"}
    sparse=next((item for item in expected if any(material is None for material in item["geometry"]["materials"])),None)
    if sparse:
        bad=copy.deepcopy(wire)
        bad["objects"][sparse["guid"]]["material_guids"]=[guid for guid in bad["objects"][sparse["guid"]]["material_guids"] if guid]
        results["compressed_sparse_slots"]=Validate(reference,bad,snapshot)["status"]=="FAIL"
    bad=copy.deepcopy(wire)
    index=subject["geometry"]["used_slots"][0]
    material_guid=bad["objects"][guid]["material_guids"][index]
    bad["materials"][material_guid]["name"]="InjectedWrongMaterial"
    results["wrong_source_material"]=Validate(reference,bad,snapshot)["status"]=="FAIL"
    if snapshot:
        bad=copy.deepcopy(snapshot)
        item=next(item for item in bad["inspections"] if item["entity_guid"]==guid)
        item["mesh_data"]["material_slots"][0]["is_fallback"]=True
        results["unexpected_engine_checker"]=Validate(reference,wire,bad)["status"]=="FAIL"
    return results


if __name__=="__main__":
    reference_path,packet_path,snapshot_path,output=map(Path,sys.argv[1:5])
    reference=json.loads(reference_path.read_text(encoding="utf-8"))
    wire=ReadPackets(packet_path)
    snapshot=json.loads(snapshot_path.read_text(encoding="utf-8")) if snapshot_path.is_file() else None
    result=Validate(reference,wire,snapshot)
    result["seeded_corruptions"]=SeededChecks(reference,wire,snapshot)
    if not all(result["seeded_corruptions"].values()):
        result["status"]="FAIL"
    output.mkdir(parents=True,exist_ok=True)
    (output/"material_assertions.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
    print(json.dumps({key:value for key,value in result.items() if key not in ("checks","failures")}),flush=True)
    sys.exit(0 if result["status"]=="PASS" else 1)
