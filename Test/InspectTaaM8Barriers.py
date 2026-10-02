"""Run with qrenderdoc --python; read AILU_TAA_RDC_DIR and AILU_TAA_RDC_REPORT."""
import json
import os
import traceback
from pathlib import Path
import renderdoc as rd

report = Path(os.environ['AILU_TAA_RDC_REPORT'])
results = []

def Children(value):
    for name in ['NumChildren', 'GetNumChildren']:
        method = getattr(value, name, None)
        if method:
            return [value.GetChild(index) for index in range(method())]
    return list(getattr(value, 'children', []))

def Describe(value, depth=0):
    return {'name': value.name, 'type': str(value.type),
            'children': [Describe(child, depth + 1) for child in Children(value)] if depth < 3 else []}

try:
    for path in sorted(Path(os.environ['AILU_TAA_RDC_DIR']).glob('*.rdc')):
        capture = rd.OpenCaptureFile()
        status = capture.OpenFile(str(path), '', None)
        if status != rd.ResultCode.Succeeded: raise RuntimeError(str(status))
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if status != rd.ResultCode.Succeeded: raise RuntimeError(str(status))
        structured = controller.GetStructuredFile()
        barriers = [chunk for chunk in structured.chunks if 'ResourceBarrier' in chunk.name]
        count = 0
        for chunk in barriers:
            for child in Children(chunk):
                if 'barriers' in child.name.lower(): count += len(Children(child))
        results.append({'capture': str(path), 'barrier_calls': len(barriers), 'barrier_array_entries': count,
                        'example': Describe(barriers[0]) if barriers else None,
                        'debug_messages': [str(message.description) for message in controller.GetDebugMessages()]})
        report.write_text(json.dumps({'progress': 'replaying', 'results': results}, indent=2))
        controller.Shutdown(); capture.Shutdown()
    report.write_text(json.dumps({'complete': True, 'results': results}, indent=2))
except Exception:
    report.write_text(json.dumps({'complete': False, 'error': traceback.format_exc(), 'results': results}, indent=2))
finally:
    os._exit(0)
