import json
import sys
from pathlib import Path
import bpy
import numpy as np

root = Path(r'C:\Users\BoomBac\OneDrive\AiluEngine')
sys.path.insert(0, str(root / 'Tools/Blender'))
from ailu_live_link import material_sync, material_bake, mesh_sync

output = Path(sys.argv[sys.argv.index('--') + 1])
output.mkdir(parents=True, exist_ok=True)
mesh_sync._session_temp_dir = str(output)

def tree_info(tree, seen=None):
    seen = set() if seen is None else seen
    if not tree or tree.as_pointer() in seen:
        return []
    seen.add(tree.as_pointer())
    result = []
    for node in tree.nodes:
        value = {'name': node.name, 'type': node.type, 'inputs': []}
        for socket in node.inputs:
            item = {'name': socket.name, 'links': [(link.from_node.name, link.from_socket.name) for link in socket.links]}
            if hasattr(socket, 'default_value'):
                default = socket.default_value
                item['default'] = default if isinstance(default, (str, int, float, bool)) else list(default)
            value['inputs'].append(item)
        if getattr(node, 'image', None):
            value['image'] = [node.image.filepath, node.image.colorspace_settings.name]
        for name in ('operation', 'blend_type', 'distribution'):
            if hasattr(node, name):
                value[name] = getattr(node, name)
        if node.type == 'GROUP':
            value['group'] = tree_info(node.node_tree, seen)
        result.append(value)
    return result

class Recorder:
    def send_json(self, kind, value):
        self.value = value
        return True

report = []
for obj in bpy.context.scene.objects:
    if obj.name.lower() != 'sol':
        continue
    for slot in obj.material_slots:
        mat = slot.material
        if not mat:
            continue
        link = Recorder()
        record = {'object': obj.name, 'material': mat.name, 'bake': material_bake.object_needs_bake(obj),
                  'tree': tree_info(mat.node_tree)}
        (output / 'source.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
        material_sync.export_material(mat, link, obj)
        record['export'] = link.value
        stats = {}
        for channel in ('roughness', 'metallic', 'normal'):
            file = link.value[channel + '_path']
            if not file:
                continue
            image = bpy.data.images.load(file, check_existing=False)
            image.colorspace_settings.name = 'Non-Color'
            pixels = np.array(image.pixels[:]).reshape(-1, 4)
            stats[channel] = {'size': list(image.size), 'min': pixels.min(axis=0).tolist(),
                              'p10': np.quantile(pixels, .1, axis=0).tolist(),
                              'median': np.median(pixels, axis=0).tolist(), 'max': pixels.max(axis=0).tolist()}
            bpy.data.images.remove(image)
        record['stats'] = stats
        report.append(record)
        print('FLOOR', mat.name, link.value, stats, flush=True)
(output / 'floor-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
assert report, 'Floor object not found'
