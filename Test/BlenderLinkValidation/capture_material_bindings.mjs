import fs from 'node:fs';
import path from 'node:path';
import {Rpc, Write, Capture, Delay} from '../TaaEditorAcceptanceClient.mjs';

const [project, output, expectedPidText, sentinel] = process.argv.slice(2);
const expectedPid = Number(expectedPidText);
const reference = JSON.parse(fs.readFileSync(path.join(output, 'material_reference.json'), 'utf8'));
const deadline = Date.now() + 3600000;
while (!fs.existsSync(sentinel)) {
  if (Date.now() > deadline) throw Error('Timed out waiting for current applied End');
  await Delay(100);
}
const session = JSON.parse(fs.readFileSync(path.join(project, '.ailu/editor_session.json'), 'utf8'));
if (session.pid !== expectedPid) throw Error('Owned Editor session mismatch');
const queried = [];
for (let offset = 0;;) {
  const page = await Rpc('scene.query_entities', {offset, limit: 64});
  queried.push(...page.items);
  if (!page.has_more) break;
  if (!page.items.length || queried.length > 200000) throw Error('Invalid scene pagination');
  offset += page.items.length;
}
const expected = [
  ...reference.objects,
  ...(reference.static_merge ? [] : reference.instances),
  ...(reference.static_merge?.batches ?? []),
];
const present = new Set(queried.map(item => item.entity_guid));
const missing = expected.filter(item => !present.has(item.guid));
if (missing.length) throw Error('Expected renderable entities missing: ' + JSON.stringify(missing.map(item => item.name)));
const inspections = [];
for (let offset = 0; offset < expected.length; offset += 32) {
  const result = await Rpc('scene.inspect_entities', {entity_guids: expected.slice(offset, offset + 32).map(item => item.guid),
    include_components: false, include_mesh_summary: true});
  inspections.push(...result.items);
}
Write('material_engine_snapshot.json', {queried_entities: queried, inspections, editor_pid: expectedPid});
let original;
let test;
let restored;
let failure = null;
try {
  original = await Rpc('editor.get_scene_camera');
  const pose = reference.synthetic ? {position: [0, 8, -8], pitch_degrees: 45, yaw_degrees: 0,
    fov_degrees: 60, aa_mode: 0, snap: true} : JSON.parse(fs.readFileSync(path.join(output, 'fixture/camera.json'), 'utf8'));
  await Rpc('editor.set_scene_camera', pose);
  await Delay(1800);
  test = await Rpc('editor.get_scene_camera');
  await Capture('material_albedo', 'DeferedGeometryPass', 3,
    {resource_name: '_GBuffer1', access_index: 1, camera_ordinal: 0, output_format: 'png'});
  await Capture('material_final', 'DeferredLightingPass', 3, {resource_name: '_CameraColorAttachmentA', camera_ordinal: 0});
} catch (error) {
  failure = String(error);
} finally {
  if (original) {
    await Rpc('editor.set_scene_camera', {position: original.target_position, pitch_degrees: original.pitch_degrees,
      yaw_degrees: original.yaw_degrees, fov_degrees: original.fov_degrees, aa_mode: original.aa_mode, snap: true});
    restored = await Rpc('editor.get_scene_camera');
  }
  Write('material_capture.json', {status: failure ? 'FAIL' : 'PASS', failure, original_camera: original,
    test_camera: test, restored_camera: restored});
}
if (failure) throw Error(failure);
if (reference.synthetic) {
  fs.writeFileSync(path.join(output, 'material_baseline_ready'), 'ready');
  const mutationDeadline = Date.now() + 30000;
  const sent = path.join(output, 'material_mutation_sent.json');
  while (!fs.existsSync(sent)) {
    const failed = path.join(output, 'material_mutation_failed.json');
    if (fs.existsSync(failed)) throw Error(fs.readFileSync(failed, 'utf8'));
    if (Date.now() > mutationDeadline) throw Error('Timed out waiting for source material mutations');
    await Delay(50);
  }
  const changedNames = new Set(JSON.parse(fs.readFileSync(sent, 'utf8')).objects);
  const updated = JSON.parse(fs.readFileSync(path.join(output, 'material_mutation_reference.json'), 'utf8'));
  const changed = updated.objects.filter(item => changedNames.has(item.name));
  let mutated = [];
  let applied = false;
  while (Date.now() < mutationDeadline) {
    const response = await Rpc('scene.inspect_entities', {entity_guids: changed.map(item => item.guid),
      include_components: false, include_mesh_summary: true});
    mutated = response.items;
    applied = changed.every(item => {
      const actual = mutated.find(candidate => candidate.entity_guid === item.guid)?.mesh_data?.material_slots ?? [];
      return item.geometry.used_slots.every((slot, ordinal) => actual[ordinal]?.source_guid === item.geometry.materials[slot]?.guid &&
        actual[ordinal]?.is_fallback === false);
    });
    if (applied) break;
    await Delay(100);
  }
  if (!applied) throw Error('Native Engine material mutation did not converge');
  const replacements = new Map(mutated.map(item => [item.entity_guid, item]));
  Write('material_mutation_engine_snapshot.json', {queried_entities: queried,
    inspections: inspections.map(item => replacements.get(item.entity_guid) ?? item), editor_pid: expectedPid});
  try {
    await Rpc('editor.set_scene_camera', {position: test.target_position, pitch_degrees: test.pitch_degrees,
      yaw_degrees: test.yaw_degrees, fov_degrees: test.fov_degrees, aa_mode: test.aa_mode, snap: true});
    await Delay(600);
    await Capture('material_albedo_mutated', 'DeferedGeometryPass', 3,
      {resource_name: '_GBuffer1', access_index: 1, camera_ordinal: 0, output_format: 'png'});
    Write('material_mutation_capture.json', {status: 'PASS', test_camera: test});
  } finally {
    await Rpc('editor.set_scene_camera', {position: original.target_position, pitch_degrees: original.pitch_degrees,
      yaw_degrees: original.yaw_degrees, fov_degrees: original.fov_degrees, aa_mode: original.aa_mode, snap: true});
    fs.writeFileSync(path.join(output, 'material_mutation_checked'), 'checked');
  }
}
console.log(JSON.stringify({status: 'PASS', inspected: inspections.length, capture: true}));
