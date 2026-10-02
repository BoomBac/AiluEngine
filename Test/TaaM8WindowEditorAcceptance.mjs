import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';
const [project] = process.argv.slice(2);
const session = JSON.parse(fs.readFileSync(path.join(project, '.ailu/editor_session.json')));
const repo = process.cwd();
const camera = await Rpc('editor.get_scene_camera');
const original = await Rpc('editor.get_state');
const actions = [];
function Window(action, x = 0, y = 0) {
  const result = JSON.parse(execFileSync('powershell.exe', ['-NoProfile', '-File',
    path.join(repo, 'Test/TaaM8WindowControl.ps1'), '-TargetPid', String(session.pid), '-Action', action,
    '-X', String(x), '-Y', String(y)], {encoding: 'utf8', windowsHide: true, timeout: 20000}));
  actions.push(result); Write('window_actions.json', actions); return result;
}
const window = Window('inspect');
Write('original_state.json', original);
Write('camera_entities.json', await Rpc('scene.query_entities', {limit: 100}));
try {
  await Rpc('editor.set_scene_camera', {aa_mode: 2});
  await Capture('before', 'TemporalAA', 16);
  for (const [index, size] of [[1280, 720], [1600, 900], [window.width, window.height]].entries()) {
    Window('resize', ...size); await Delay(1000);
    Write('resize_' + index + '_camera.json', await Rpc('editor.get_scene_camera'));
    await Capture('resize_' + index, 'TemporalAA', 16);
  }
  const selections = [];
  for (let index = 0; index < 12; ++index) {
    Window('click', Math.round(window.width * (.42 + (index % 3) * .06)), Math.round(window.height * .35));
    Window('key', [87, 69, 82, 81][index % 4]);
    selections.push(await Rpc('editor.get_state'));
    await Capture('selection_' + index, 'TemporalAA', 4);
  }
  Write('selections.json', selections);
  Window('click', 291, 17); await Delay(1000);
  const playing = await Rpc('editor.get_state'); Write('play_state.json', playing);
  if (playing.editor_mode === 'play') {
    try { await Capture('main_play', 'TemporalAA', 16); }
    finally { Window('click', 427, 17); await Delay(1000); }
  }
  Write('after_play_state.json', await Rpc('editor.get_state'));
  await Capture('after_play', 'TemporalAA', 16);
  Write('completion.json', {play_entered: playing.editor_mode === 'play',
    selection_observed: selections.some(value => value.selection.length > 0),
    scope: 'Actual Win32 window resize and routed mouse/key input; gizmo transform movement is not asserted.'});
} finally {
  const state = await Rpc('editor.get_state');
  if (state.editor_mode === 'play') { Window('click', 427, 17); await Delay(1000); }
  Window('resize', window.width, window.height);
  await Rpc('editor.set_scene_camera', {aa_mode: camera.aa_mode, position: camera.target_position,
    pitch_degrees: camera.pitch_degrees, yaw_degrees: camera.yaw_degrees, snap: true});
  Write('restored_state.json', await Rpc('editor.get_state'));
}
