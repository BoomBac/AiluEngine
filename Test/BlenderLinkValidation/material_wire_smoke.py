"""Exercise production export and incremental rebinding against controlled source expectations, without an Engine peer."""
import copy
import json
import sys
from pathlib import Path
import bpy

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/"Tools/Blender"))
sys.path.insert(0,str(Path(__file__).parent))
from ailu_live_link import identity,object_sync,protocol
from validate_material_bindings import ReadPackets,Validate,SeededChecks

output=Path(sys.argv[sys.argv.index("--")+1])
reference=json.loads((output/"material_reference.json").read_text())
packets=output/"wire_smoke.bin"

class RecordingLink:
    def __init__(self):
        self.sequence=2
        self.messages=0
    def send(self,kind,payload=b""):
        with packets.open("ab") as stream:
            stream.write(protocol.packet(kind,self.sequence,payload))
        self.sequence+=1
        self.messages+=1
        return True
    def send_json(self,kind,value):
        return self.send(kind,json.dumps(value).encode())
    def begin_diagnostic_batch(self,*args):
        return 0
    def end_diagnostic_batch(self,*args):
        pass

packets.write_bytes(b"")
link=RecordingLink()
object_sync.full_sync(bpy.context,link)
baseline=Validate(reference,ReadPackets(packets))
corruptions=SeededChecks(reference,ReadPackets(packets),None)
assert baseline["status"]=="PASS",json.dumps(baseline["failures"][:5])
assert all(corruptions.values())

ordinary=bpy.data.objects["OrdinaryMulti"]
ordinary.material_slots[0].material=bpy.data.materials["MarkerBlue"]
bpy.context.view_layer.update()
object_sync.sync_object(bpy.context,ordinary,link,set(),False,True,False)
updated=copy.deepcopy(reference)
source=next(item for item in updated["objects"] if item["name"]==ordinary.name)
blue=bpy.data.materials["MarkerBlue"]
source["geometry"]["materials"][0]={"name":blue.name,"guid":identity.get_guid(blue)}
rebind=Validate(updated,ReadPackets(packets))
assert rebind["status"]=="PASS",json.dumps(rebind["failures"][:5])

sparse=bpy.data.objects["SparseSlots"]
green=bpy.data.materials["MarkerGreen"]
sparse.material_slots[1].material=green
sparse.data.polygons[0].material_index=1
sparse.data.update()
bpy.context.view_layer.update()
object_sync.sync_object(bpy.context,sparse,link,set(),True,True,False)
source=next(item for item in updated["objects"] if item["name"]==sparse.name)
source["geometry"]["used_slots"]=[1]
source["geometry"]["materials"][1]={"name":green.name,"guid":identity.get_guid(green)}
reorder=Validate(updated,ReadPackets(packets))
assert reorder["status"]=="PASS",json.dumps(reorder["failures"][:5])
before=link.messages
object_sync.sync_instances(bpy.context,link)
assert link.messages==before,"Idle material snapshots must not resend instance bindings"
result={"status":"PASS","scope":"production source-to-wire; no Engine peer", "baseline_checks":baseline["check_count"],
        "incremental_rebind":rebind["status"],"incremental_slot_reorder":reorder["status"],
        "seeded_corruptions":corruptions,"idle_no_resend":True}
(output/"material_wire_smoke.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
print("MATERIAL_WIRE_SMOKE",json.dumps(result),flush=True)
