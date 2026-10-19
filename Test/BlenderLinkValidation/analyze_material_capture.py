import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image
from material_capture_pixels import LoadFrame

stage=Path(sys.argv[1])
reference=json.loads((stage/"material_reference.json").read_text())
driver=json.loads((stage/"material_capture.json").read_text())
failures=[]
checks=[]
captures=[("material_albedo",reference,driver),("material_final",reference,driver)]
if (stage/"material_mutation_reference.json").exists():
    captures.append(("material_albedo_mutated",json.loads((stage/"material_mutation_reference.json").read_text()),
                     json.loads((stage/"material_mutation_capture.json").read_text())))
for name,reference,driver in captures:
    manifests=list((stage/name).glob("session_*/capture.json"))
    if len(manifests)!=1:
        failures.append({"field":name+".manifest","actual":len(manifests)})
        continue
    manifest=json.loads(manifests[0].read_text())
    frames=sorted(manifest["frames"],key=lambda item:item["capture_index"])
    indices=[item["engine_frame"] for item in frames]
    contiguous=len(indices)==3 and all(b==a+1 for a,b in zip(indices,indices[1:]))
    if manifest["state"]!="completed" or not contiguous:
        failures.append({"field":name+".contiguous","actual":indices})
    else:
        checks.append({"field":name+".contiguous","engine_frames":indices})
    for frame in frames:
        if not (manifests[0].parent/frame["file"]).is_file():
            failures.append({"field":name+".file_missing","actual":frame["file"]})
            continue
        try:
            pixels=LoadFrame(manifests[0].parent/frame["file"],manifest)
            if not np.isfinite(pixels).all():
                raise ValueError("Nonfinite capture pixels")
            checks.append({"field":name+".finite","engine_frame":frame["engine_frame"]})
        except Exception as error:
            failures.append({"field":name+".pixels","actual":str(error)})
    if name.startswith("material_albedo") and frames:
        image=np.asarray(Image.open(manifests[0].parent/frames[-1]["file"]).convert("RGB"),dtype=np.float64)/255
        for marker in reference["markers"]:
            vp=np.asarray(driver["test_camera"]["vp_stable"]).reshape(4,4)
            clip=np.asarray([*marker["position"],1])@vp
            ndc=clip[:3]/clip[3]
            x=int(round((ndc[0]*.5+.5)*image.shape[1]))
            y=int(round((1-(ndc[1]*.5+.5))*image.shape[0]))
            if clip[3]<=0 or x<3 or y<3 or x+3>=image.shape[1] or y+3>=image.shape[0]:
                failures.append({"field":marker["name"]+".projection","actual":[x,y]})
                continue
            value=np.median(image[y-3:y+4,x-3:x+4],axis=(0,1))
            # Assert marker identity rather than exact RGB: this capture path stores the sampled material tint.
            good=all(value[index]>.65 if expected else value[index]<.2 for index,expected in enumerate(marker["color"]))
            record={"field":marker["name"]+".albedo_marker","expected":marker["color"],"actual":value.tolist(),"pixel":[x,y]}
            (checks if good else failures).append(record)
            if good and marker["color"][0]!=marker["color"][2]:
                corrupted=value[[2,1,0]]
                detected=not all(corrupted[index]>.65 if expected else corrupted[index]<.2
                                 for index,expected in enumerate(marker["color"]))
                (checks if detected else failures).append({"field":marker["name"]+".seeded_red_blue_swap_detected","actual":detected})
if driver["status"]!="PASS":
    failures.append({"field":"capture_driver","actual":driver})
report={"status":"FAIL" if failures else "PASS","checks":checks,"failures":failures,
        "scope":"Material binding/color ROI for controlled markers; scene capture integrity otherwise. Lighting parity is not asserted."}
(stage/"material_image_assertions.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print(json.dumps(report),flush=True)
sys.exit(1 if failures else 0)
