import { cp, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const workspace = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = 'C:/Users/BoomBac/OneDrive/AiluTestProj';
const target = path.join(workspace, 'Saved', 'GlassCaptureProject');
if (existsSync(target)) throw new Error(`Capture project already exists: ${target}`);
await mkdir(target, { recursive: true });
for (const name of ['AiluTestProj.ailuproject', 'Assets', 'Config', 'Library']) {
    await cp(`${source}/${name}`, `${target}/${name}`, { recursive: true });
}
console.log(target);
