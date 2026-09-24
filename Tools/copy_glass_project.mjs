import { cp, mkdir } from 'node:fs/promises';
import { existsSync } from 'node:fs';

const source = 'C:/Users/BoomBac/OneDrive/AiluTestProj';
const target = 'C:/Users/BoomBac/OneDrive/AiluEngine/Saved/GlassCaptureProject';
if (existsSync(target)) throw new Error(`Capture project already exists: ${target}`);
await mkdir(target, { recursive: true });
for (const name of ['AiluTestProj.ailuproject', 'Assets', 'Config', 'Library']) {
    await cp(`${source}/${name}`, `${target}/${name}`, { recursive: true });
}
console.log(target);
