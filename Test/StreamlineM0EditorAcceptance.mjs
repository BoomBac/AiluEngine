import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';

const [project] = process.argv.slice(2);
const session = JSON.parse(fs.readFileSync(path.join(project, '.ailu/editor_session.json')));
const camera = await Rpc('editor.get_scene_camera');
Write('original_camera.json', camera);
Write('original_state.json', await Rpc('editor.get_state'));
const actions = [];
try {
  await Rpc('editor.set_scene_camera', {aa_mode: 2});
  await Delay(500);
  Write('probe_phase.json', {phase: 'baseline'});
  await Capture('taa_before', 'TemporalAA', 4, {output_format: 'png'});
  for (const [index, size] of [[1280, 720], [1600, 900]].entries()) {
    Write('probe_phase.json', {phase: 'resize_' + index});
    actions.push(JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-File',
      path.join(process.cwd(), 'Test/TaaM8WindowControl.ps1'), '-TargetPid', String(session.pid),
      '-Action', 'resize', '-X', String(size[0]), '-Y', String(size[1])],
      {encoding: 'utf8', windowsHide: true, timeout: 20000})));
    Write('window_actions.json', actions);
    await Delay(500);
    await Capture('taa_resize_' + index, 'TemporalAA', 4, {output_format: 'png'});
  }
  for (const action of ['minimize', 'restore']) {
    Write('probe_phase.json', {phase: action});
    actions.push(JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-File',
      path.join(process.cwd(), 'Test/StreamlineM0WindowControl.ps1'), '-TargetPid', String(session.pid),
      '-Action', action], {encoding: 'utf8', windowsHide: true, timeout: 20000})));
    Write('window_actions.json', actions);
    await Delay(1000);
  }
  await Capture('taa_restored', 'TemporalAA', 4, {output_format: 'png'});
  Write('completion.json', {resize_count: 2, scope: 'TAA, Win32 resize and minimize/restore; hook uses the probe.'});
} finally {
  await Rpc('editor.set_scene_camera', {aa_mode: camera.aa_mode, position: camera.target_position,
    pitch_degrees: camera.pitch_degrees, yaw_degrees: camera.yaw_degrees, snap: true});
  Write('restored_camera.json', await Rpc('editor.get_scene_camera'));
}
