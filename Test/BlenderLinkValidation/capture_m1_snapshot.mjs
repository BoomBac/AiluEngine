import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';

const [projectPath, outputPath, referencePath, expectedPidText, appliedSentinel] = process.argv.slice(2);
if (!projectPath || !outputPath || !referencePath || !expectedPidText || !appliedSentinel)
  throw Error('Usage: capture_m1_snapshot.mjs PROJECT OUTPUT REFERENCE EDITOR_PID APPLIED_SENTINEL');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const project = path.resolve(projectPath);
const output = path.resolve(outputPath);
const expectedPid = Number(expectedPidText);
const reference = JSON.parse(fs.readFileSync(referencePath, 'utf8'));
const sessionPath = path.join(project, '.ailu/editor_session.json');
const sentinelPath = path.resolve(appliedSentinel);
const deadline = Date.now() + 30000;
let lastSession = null;

while (Date.now() < deadline) {
  try {
    lastSession = JSON.parse(fs.readFileSync(sessionPath, 'utf8'));
    if (lastSession.pid === expectedPid) break;
  } catch {}
  await delay(100);
}
if (Date.now() >= deadline) throw Error(`Editor automation session PID mismatch: expected=${expectedPid}, ` +
  `actual=${lastSession?.pid ?? 'missing'}, path=${sessionPath}`);
console.log(JSON.stringify({status: 'session_ready', editor_pid: expectedPid, session_id: lastSession.session_id}));

let requestId = 0;
async function attemptRpc(method, arguments_ = {}) {
  const session = JSON.parse(fs.readFileSync(sessionPath, 'utf8'));
  return await new Promise((resolve, reject) => {
    const pipe = net.createConnection('\\\\.\\pipe\\' + session.session_id);
    let buffer = Buffer.alloc(0);
    let settled = false;
    const fail = error => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      reject(error);
    };
    const timer = setTimeout(() => pipe.destroy(Error('RPC timeout: ' + method)), 20000);
    pipe.once('error', fail);
    pipe.once('close', () => fail(Error('RPC pipe closed before response: ' + method)));
    pipe.once('connect', () => {
      const body = Buffer.from(JSON.stringify({request_id: ++requestId, method, arguments: arguments_,
        context: {caller: 'Blender Link M1 oracle', allow_write: false, allow_destructive: false, interactive: false}}));
      const header = Buffer.alloc(4);
      header.writeUInt32LE(body.length);
      pipe.write(Buffer.concat([header, body]));
    });
    pipe.on('data', bytes => {
      buffer = Buffer.concat([buffer, bytes]);
      if (buffer.length < 4) return;
      const bodyLength = buffer.readUInt32LE(0);
      if (buffer.length < bodyLength + 4) return;
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      pipe.end();
      const response = JSON.parse(buffer.subarray(4, 4 + bodyLength).toString('utf8'));
      if (!response.success) reject(Error(JSON.stringify(response)));
      else resolve(response.data);
    });
  });
}
async function rpc(method, arguments_ = {}) {
  for (let retry = 0; retry < 20; ++retry) {
    try {return await attemptRpc(method, arguments_);}
    catch (error) {
      if (error.message.startsWith('{') || retry === 19) throw error;
      await delay(100);
    }
  }
}

const queried = [];
let offset = 0;
let sceneGuid = '';
let sceneRevision = 0;
const expected = [...reference.objects, ...reference.instances];
const expectedIds = new Set(expected.map(source => source.guid));
const snapshotDeadline = Date.now() + 180000;
let complete = false;
while (Date.now() < snapshotDeadline && !complete) {
  if (!fs.existsSync(sentinelPath)) {await delay(50); continue;}
  queried.length = 0;
  offset = 0;
  for (;;) {
    const page = await rpc('scene.query_entities', {offset, limit: 64});
    sceneGuid = page.scene_guid;
    sceneRevision = page.scene_revision;
    queried.push(...page.items);
    if (!page.has_more) break;
    offset += page.items.length;
    if (queried.length > 10000) throw Error('Scene query exceeded 10000 entities');
  }
  complete = [...expectedIds].every(guid => queried.some(item => item.entity_guid === guid));
  if (!complete) await delay(50);
}
if (!complete) throw Error('Timed out waiting for all M1 source entities after applied End');

const inspections = [];
const inspected = await rpc('scene.inspect_entities', {entity_guids: expected.map(source => source.guid),
  include_components: true, include_properties: true,
  include_mesh_data_guids: expected.filter(source => source.object_type === 'MESH').map(source => source.guid)});
inspections.push(...inspected.items);

const linkRoots = queried.filter(item => item.name.startsWith('__BlenderLiveLink_'));
fs.mkdirSync(output, {recursive: true});
const snapshot = {scene_guid: sceneGuid, scene_revision: sceneRevision, queried_entities: queried,
  link_roots: linkRoots, inspections, captured_pid: expectedPid};
fs.writeFileSync(path.join(output, 'engine_snapshot.json'), JSON.stringify(snapshot, null, 2));
console.log(JSON.stringify({status: 'captured', queried: queried.length, inspected: inspections.length,
  link_roots: linkRoots.length}));
