"""Run in qrenderdoc with --python; inputs come from AILU_TAA_RDC_DIR and AILU_TAA_RDC_REPORT."""
import json
import os
import traceback
from pathlib import Path

import renderdoc as rd

report = Path(os.environ['AILU_TAA_RDC_REPORT'])
report.write_text(json.dumps({'progress': 'script_started'}))
results = []

def Describe(value, depth=0):
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    if depth > 2:
        return str(value)
    if hasattr(value, '__iter__') or (hasattr(value, '__len__') and hasattr(value, '__getitem__')):
        return [Describe(item, depth + 1) for item in value]
    data = {}
    for name in dir(value):
        if name.startswith('_') or name in ['this', 'thisown']:
            continue
        field = getattr(value, name)
        if not callable(field):
            data[name] = Describe(field, depth + 1)
    return data or str(value)

def Walk(actions):
    for action in actions:
        yield action
        yield from Walk(action.children)

try:
    for capture_path in sorted(Path(os.environ['AILU_TAA_RDC_DIR']).glob('*.rdc')):
        capture = rd.OpenCaptureFile()
        report.write_text(json.dumps({'progress': 'open_file', 'capture': str(capture_path)}))
        status = capture.OpenFile(str(capture_path), '', None)
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError(str(status))
        report.write_text(json.dumps({'progress': 'open_capture', 'capture': str(capture_path)}))
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError(str(status))
        result = {'capture': str(capture_path), 'dispatches': [],
            'textures': [Describe(texture) for texture in controller.GetTextures()],
            'resources': [Describe(resource) for resource in controller.GetResources()]}
        for action in Walk(controller.GetRootActions()):
            if not action.flags & rd.ActionFlags.Dispatch:
                continue
            controller.SetFrameEvent(action.eventId, True)
            pipeline = controller.GetPipelineState()
            reflection = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
            result['dispatches'].append({'event': action.eventId, 'groups': Describe(action.dispatchDimension),
                'shader': str(pipeline.GetShader(rd.ShaderStage.Compute)),
                'reflection': {name: Describe(getattr(reflection, name)) for name in
                    ['readOnlyResources', 'readWriteResources', 'entryPoint', 'dispatchThreadsDimension']
                    if hasattr(reflection, name)},
                'read_only': Describe(pipeline.GetReadOnlyResources(rd.ShaderStage.Compute)),
                'read_write': Describe(pipeline.GetReadWriteResources(rd.ShaderStage.Compute))})
        result['debug_messages'] = [Describe(message) for message in controller.GetDebugMessages()]
        results.append(result)
        controller.Shutdown()
        capture.Shutdown()
    frames = []
    for result in results:
        names = {resource['resourceId']: resource['name'] for resource in result['resources']}
        textures = {texture['resourceId']: texture for texture in result['textures']}
        dispatch = next(action for action in result['dispatches'] if any(resource['name'] == '_CurrentColor'
            for resource in action['reflection']['readOnlyResources']))
        bindings = {}
        for binding_type, reflection_type in [('read_only', 'readOnlyResources'), ('read_write', 'readWriteResources')]:
            for descriptor in dispatch[binding_type]:
                name = dispatch['reflection'][reflection_type][descriptor['access']['index']]['name']
                resource_id = descriptor['descriptor']['resource']
                texture = textures[resource_id]
                bindings[name] = {'id': resource_id, 'name': names[resource_id],
                    'width': texture['width'], 'height': texture['height']}
        frames.append({'capture': result['capture'], 'event': dispatch['event'], 'groups': dispatch['groups'],
            'bindings': bindings, 'debug_messages': result['debug_messages']})
    gates = {'captures': len(frames) == 2,
        'groups': all(frame['groups'] == [40, 23, 1] for frame in frames),
        'binding_sizes': all((binding['width'], binding['height']) == (320, 180)
            for frame in frames for binding in frame['bindings'].values()),
        'no_read_write_alias': all(frame['bindings']['_CurrentTarget']['id'] not in
            [binding['id'] for name, binding in frame['bindings'].items() if name != '_CurrentTarget'] for frame in frames),
        'debug_messages_empty': all(not frame['debug_messages'] for frame in frames)}
    gates['ping_pong'] = len(frames) == 2 and (
        frames[0]['bindings']['_CurrentTarget']['id'] == frames[1]['bindings']['_HistoryColor']['id'] and
        frames[0]['bindings']['_HistoryColor']['id'] == frames[1]['bindings']['_CurrentTarget']['id'])
    report.write_text(json.dumps({'captures': results}, indent=2))
    (report.parent / 'binding_gates.json').write_text(json.dumps({'gates': gates, 'frames': frames}, indent=2))
except Exception:
    report.write_text(json.dumps({'captures': results, 'error': traceback.format_exc()}, indent=2))
finally:
    os._exit(0)
