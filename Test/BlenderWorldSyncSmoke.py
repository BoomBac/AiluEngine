"""Native World split, cache and incremental-state acceptance (run with Blender)."""
import json
import sys
from pathlib import Path
import bpy
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Tools/Blender'))
from ailu_live_link import world_sync, dirty_tracker, protocol

output = Path(sys.argv[sys.argv.index('--') + 1])
output.mkdir(parents=True, exist_ok=True)
world = bpy.data.worlds.new('WorldSplitAcceptance')
world.use_nodes = True
bpy.context.scene.world = world
tree = world.node_tree
tree.nodes.clear()
background = tree.nodes.new('ShaderNodeBackground')
background.inputs['Strength'].default_value = 2
mix = tree.nodes.new('ShaderNodeMixRGB')
mix.inputs[1].default_value = (.2, .4, .8, 1)
mix.inputs[2].default_value = (.025, 0, 0, 1)
light_path = tree.nodes.new('ShaderNodeLightPath')
node_output = tree.nodes.new('ShaderNodeOutputWorld')
tree.links.new(light_path.outputs['Is Camera Ray'], mix.inputs[0])
tree.links.new(mix.outputs[0], background.inputs['Color'])
tree.links.new(background.outputs[0], node_output.inputs['Surface'])
class Link:
    connected = True
    def __init__(self): self.messages = []
    def send_json(self, kind, value): self.messages.append((kind, value))
link = Link()
counts = [len(bpy.data.scenes), len(bpy.data.objects), len(bpy.data.cameras), len(bpy.data.worlds)]
world_sync.export_world(bpy.context, link)
message = link.messages[-1][1]
assert message['enabled']
checks = []
for role, expected in [('lighting', [.4, .8, 1.6]), ('background', [.05, 0, 0])]:
    image = bpy.data.images.load(message[role + '_path'], check_existing=False)
    values = np.asarray(image.pixels[:], np.float32).reshape(-1, 4)[:, :3]
    error = float(np.max(abs(values - expected)))
    checks.append(dict(role=role, max_abs_error=error, passed=error < .002))
    bpy.data.images.remove(image)
assert all(check['passed'] for check in checks), checks
assert counts == [len(bpy.data.scenes), len(bpy.data.objects), len(bpy.data.cameras), len(bpy.data.worlds)]
dirty_tracker.mark_synced(bpy.context.scene)
background.inputs['Strength'].default_value = 3
dirty_tracker.flush(bpy.context, link)
world_messages = [value for kind, value in link.messages if kind == protocol.WORLD_UPSERT]
assert len(world_messages) == 2
assert world_messages[0]['revision'] != world_messages[1]['revision']
dirty_tracker.flush(bpy.context, link)
assert len([1 for kind, value in link.messages if kind == protocol.WORLD_UPSERT]) == 2
bpy.context.scene.view_settings.exposure += .25
dirty_tracker.flush(bpy.context, link)
assert len([1 for kind, value in link.messages if kind == protocol.WORLD_UPSERT]) == 2
(output / 'world_analysis.json').write_text(json.dumps(dict(status='PASS', checks=checks,
    incremental_world_messages=2, artist_exposure_does_not_rebake=True, cleanup=True), indent=2))
print('WORLD_SPLIT_ACCEPTANCE_PASS', flush=True)
