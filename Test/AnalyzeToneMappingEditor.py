"""Validate Editor ToneMapping input/output captures, exposure, Bloom, and saved AgX scene reopen."""
import json
import sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from AnalyzeToneMappingSmoke import Decode, Reference

stage = Path(sys.argv[1])
checks = []
frames_by_name = {}

def Load(phase, name):
    manifest_path = next((stage/phase/name).glob('session_*/capture.json'))
    manifest = json.loads(manifest_path.read_text())
    frames = sorted(manifest['frames'], key=lambda item: item['capture_index'])
    indices = [frame['engine_frame'] for frame in frames]
    good = manifest['state'] == 'completed' and len(indices) == 3 and all(b == a+1 for a,b in zip(indices,indices[1:]))
    checks.append(dict(name=name+'.integrity', passed=good, frames=indices))
    data = [Decode(manifest_path.parent/frame['file'], manifest) for frame in frames]
    checks.append(dict(name=name+'.finite', passed=bool(all(np.isfinite(a).all() for a in data))))
    frames_by_name[name] = data[-1]
    return data[-1]

for name, mode, exposure in [('V_None_1',0,1),('V_None_05',0,.5),('V_Aces_1',1,1),('V_Agx_05',2,.5),
                             ('V_Agx_1',2,1),('V_Agx_2',2,2),('V_Agx_Bloom',2,1)]:
    source, output = Load('initial',name+'_input'), Load('initial',name+'_output')
    expected = Reference(source.astype(float)*exposure, mode)
    error = np.abs(expected-output)
    # R11G11B10 blue has a 1/64 step near unity; scene captures also occur on separate frames.
    checks.append(dict(name=name+'.numerical', passed=bool(np.quantile(error,.99)<=.016 and np.max(error)<=.03),
                       p99_abs_error=float(np.quantile(error,.99)), max_abs_error=float(error.max()),
                       input_max=float(source.max())))
none = Load('initial','no_volume_output')
checks.append(dict(name='no_volume.identity',
                   passed=bool(np.max(abs(none-frames_by_name['V_None_1_output']))<.02)))
reopened = Load('reopen','reopened_agx')
checks.append(dict(name='saved_scene.reopened_pixels',
                   passed=bool(np.max(abs(reopened-frames_by_name['V_Agx_1_output']))<.02),
                   max_abs_error=float(np.max(abs(reopened-frames_by_name['V_Agx_1_output'])))))
scene = json.loads((stage/'project/Assets/agx.almap').read_text())
volume = next(e['_postprocess_volume_component'] for e in scene['_entities']
              if e['_tag_component']['_name']=='V_Agx_1')
checks.append(dict(name='saved_scene.agx_enum',
                   passed=volume['_settings']['_tone_mapping']['_mode']=='kAgx' and volume['_is_global']))
checks.append(dict(name='bloom.changes_hdr_input', passed=bool(np.max(abs(frames_by_name['V_Agx_Bloom_input']-
                                                              frames_by_name['V_Agx_1_input']))>.01)))
checks.append(dict(name='mode.changes_output', passed=bool(np.max(abs(frames_by_name['V_Agx_1_output']-
                                                             frames_by_name['V_Aces_1_output']))>.01)))
summary=json.loads((stage/'summary.json').read_text())
checks.append(dict(name='lifecycle.restored',
                   passed=summary['status']=='PASS' and all(v['restored'] for v in summary['restoration'])))
result=dict(status='PASS' if all(v['passed'] for v in checks) else 'FAIL',checks=checks)
(stage/'analysis.json').write_text(json.dumps(result,indent=2))
canvas=Image.new('RGB',(1600,960),'#181818');draw=ImageDraw.Draw(canvas)
for index,name in enumerate(['V_None_1_output','V_Aces_1_output','V_Agx_1_output','V_Agx_Bloom_output']):
    rgb=np.clip(frames_by_name[name],0,1)
    srgb=np.where(rgb<=.0031308,12.92*rgb,1.055*rgb**(1/2.4)-.055)
    preview=Image.fromarray(np.uint8(srgb*255+.5));preview.save(stage/(name+'.png'))
    x=index%2*800;y=index//2*480
    canvas.paste(preview,(x,y+30))
    draw.text((x+8,y+8),name+' (sRGB preview, None clipped)',fill='white')
canvas.save(stage/'comparison.png')
print(json.dumps(dict(status=result['status'],failed=[v for v in checks if not v['passed']],
                      numeric=[v for v in checks if v['name'].endswith('.numerical')]),indent=2))
sys.exit(0 if result['status']=='PASS' else 1)
