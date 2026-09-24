// 通过 editor 的自动化命名管道调用 render.capture_pass / render.capture_status。
//
// 用法：
//   node Tools/render_capture_client.mjs status
//   node Tools/render_capture_client.mjs capture --pass "RayTrace GI DXR" --frames 8
//   node Tools/render_capture_client.mjs capture --pass "RayTrace GI DXR" --resource RayTraceGI_A --frames 8 --wait
//
// 会话来自 <project>/.ailu/editor_session.json（Editor 打开工程时写入）。
import { readFileSync, existsSync } from "node:fs";
import { open } from "node:fs/promises";
import path from "node:path";

const PROJECT_ROOT = process.argv.includes("--project")
  ? process.argv[process.argv.indexOf("--project") + 1]
  : "C:/Users/BoomBac/OneDrive/AiluTestProj";

function arg(name, fallback = undefined) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 && i + 1 < process.argv.length ? process.argv[i + 1] : fallback;
}
const hasFlag = name => process.argv.includes(`--${name}`);

function loadSession() {
  const file = path.join(PROJECT_ROOT, ".ailu", "editor_session.json");
  if (!existsSync(file)) throw new Error(`找不到会话文件 ${file}（editor 是否已打开该工程？）`);
  const info = JSON.parse(readFileSync(file, "utf8"));
  if (!info.pipe_name) throw new Error("会话文件里没有 pipe_name");
  return info;
}

// 帧格式：u32 小端长度前缀 + UTF-8 JSON 负载
async function readExact(handle, size) {
  const buffer = Buffer.alloc(size);
  let offset = 0;
  while (offset < size) {
    const { bytesRead } = await handle.read(buffer, offset, size - offset, null);
    if (bytesRead === 0) throw new Error("管道提前关闭");
    offset += bytesRead;
  }
  return buffer;
}

async function callPipe(pipeName, request) {
  const handle = await open(pipeName, "r+");
  try {
    const payload = Buffer.from(JSON.stringify(request), "utf8");
    const header = Buffer.alloc(4);
    header.writeUInt32LE(payload.length, 0);
    await handle.write(Buffer.concat([header, payload]));

    const lengthBuffer = await readExact(handle, 4);
    const length = lengthBuffer.readUInt32LE(0);
    const body = length > 0 ? await readExact(handle, length) : Buffer.alloc(0);
    return JSON.parse(body.toString("utf8"));
  } finally {
    await handle.close();
  }
}

const session = loadSession();
const command = process.argv[2] ?? "status";

async function invoke(method, args) {
  const response = await callPipe(session.pipe_name, {
    request_id: Date.now() % 1000000,
    method,
    arguments: args,
    context: { caller: "render_capture_client", allow_write: true, allow_destructive: command === "capture", interactive: false },
  });
  return response;
}

function printResult(tag, response) {
  console.log(`--- ${tag} ---`);
  console.log(JSON.stringify(response, null, 2));
}

if (command === "status") {
  printResult("render.capture_status", await invoke("render.capture_status", {}));
} else if (command === "capture") {
  const passName = arg("pass", "RayTrace GI DXR");
  const args = {
    pass_name: passName,
    frame_count: Number(arg("frames", "8")),
    capture_output: true,
  };
  const resource = arg("resource");
  if (resource) args.resource_name = resource;
  if (hasFlag("input")) args.capture_output = false;
  if (arg("access_index")) args.access_index = Number(arg("access_index"));
  if (arg("camera_ordinal")) args.camera_ordinal = Number(arg("camera_ordinal"));
  if (arg("prefix")) args.file_prefix = arg("prefix");

  const response = await invoke("render.capture_pass", args);
  printResult(`render.capture_pass ${passName}`, response);
  if (!response?.success) process.exit(2);

  if (!hasFlag("wait")) process.exit(0);

  // 轮询直到 completed / failed，或超时
  const deadline = Date.now() + Number(arg("timeout_ms", "60000"));
  let last = "";
  while (Date.now() < deadline) {
    await new Promise(r => setTimeout(r, 750));
    const status = await invoke("render.capture_status", {});
    const state = status?.data?.state ?? "?";
    const done = status?.data?.completed_frames ?? 0;
    const line = `state=${state} completed=${done}`;
    if (line !== last) { console.log(line); last = line; }
    if (state === "completed" || state === "failed" || state === "cancelled") {
      printResult("final", status);
      process.exit(state === "completed" ? 0 : 3);
    }
  }
  console.log("等待超时");
  process.exit(4);
} else {
  console.log("用法: node Tools/render_capture_client.mjs [status|capture] [--pass NAME] [--resource NAME] [--frames N] [--wait]");
  process.exit(1);
}
