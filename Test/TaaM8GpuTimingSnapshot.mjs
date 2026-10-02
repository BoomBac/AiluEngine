import {Rpc, Write, Delay} from './TaaEditorAcceptanceClient.mjs';
const samples = [];
for (let i = 0; i < 3; ++i) {
  samples.push({timestamp: new Date().toISOString(), timings: await Rpc('render.gpu_timings'),
    features: await Rpc('render.list_features'), camera: await Rpc('editor.get_scene_camera')});
  Write('gpu_timing_samples.json', {samples,
    scope: 'Debug rolling averages with intermittent capture load; not dispatch microbenchmark.'});
  await Delay(3000);
}
console.log(JSON.stringify(samples, null, 2));

