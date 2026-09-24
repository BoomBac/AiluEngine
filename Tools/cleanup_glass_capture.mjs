import { realpath, rm } from 'node:fs/promises';
import path from 'node:path';

const workspace = await realpath('C:/Users/BoomBac/OneDrive/AiluEngine');
const target = await realpath('C:/Users/BoomBac/OneDrive/AiluEngine/Saved/GlassCaptureProject');
const relative = path.relative(workspace, target);
if (relative !== path.join('Saved', 'GlassCaptureProject')) {
    throw new Error(`Refusing to remove unexpected path: ${target}`);
}
await rm(target, { recursive: true });
console.log(`Removed ${target}`);
