import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';

const [project, output] = process.argv.slice(2);
if (!project || !output) throw Error('Usage: node TaaRenderGraphEditorAcceptance.mjs PROJECT OUTPUT');
fs.mkdirSync(output, {recursive: true});
const Write = (name, value) => fs.writeFileSync(path.join(output, name), JSON.stringify(value, null, 2));
const Delay = ms => new Promise(resolve => setTimeout(resolve, ms));
let request_id = 0;
async function Attempt(method, arguments_ = {}) {
  const session = JSON.parse(fs.readFileSync(path.join(project, '.ailu/editor_session.json')));
  return await new Promise((resolve, reject) => {
    const socket = net.createConnection('\\\\.\\pipe\\' + session.session_id);
    let received = Buffer.alloc(0);
    socket.setTimeout(15000, () => socket.destroy(Error('RPC timeout: ' + method)));
    socket.on('error', reject);
    socket.on('connect', () => {
      const body = Buffer.from(JSON.stringify({request_id: ++request_id, method, arguments: arguments_,
        context: {caller: 'M8 acceptance', allow_write: true, allow_destructive: true, interactive: false}}));
      const header = Buffer.alloc(4);
      header.writeUInt32LE(body.length);
      socket.write(Buffer.concat([header, body]));
    });
    socket.on('data', data => {
      received = Buffer.concat([received, data]);
      if (received.length < 4 || received.length < 4 + received.readUInt32LE(0)) return;
      const result = JSON.parse(received.subarray(4, 4 + received.readUInt32LE(0)));
      socket.end();
      if (!result.success) reject(Error(JSON.stringify(result))); else resolve(result.data);
    });
  });
}
async function Rpc(method, arguments_ = {}) {
  for (let retry = 0; retry < 8; ++retry) {
    try { return await Attempt(method, arguments_); }
    catch (error) {
      if (retry === 7 || error.message.startsWith('{')) throw error;
      await Delay(100);
    }
  }
}
async function Capture(name, pass, count = 8, extra = {}) {
  const start = Date.now();
  await Rpc('render.capture_pass', {pass_name: pass, frame_count: count,
    output_directory: path.join(output, name), file_prefix: name, output_format: 'dds_native', ...extra});
  for (let attempt = 0; attempt < 120; ++attempt) {
    const status = await Rpc('render.capture_status');
    if (status.state === 'failed') throw Error(JSON.stringify(status));
    if (status.state === 'completed') {
      status.elapsed_ms = Date.now() - start;
      Write(name + '_status.json', status);
      console.log(name, status.completed_frames, status.elapsed_ms);
      return;
    }
    await Delay(200);
  }
  throw Error('Capture timeout: ' + name);
}

export {Rpc, Write, Capture, Delay};
