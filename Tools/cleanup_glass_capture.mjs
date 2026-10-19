import { realpath, rm } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const workspace = await realpath(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..'));
const target = await realpath(path.join(workspace, 'Saved', 'GlassCaptureProject'));
const relative = path.relative(workspace, target);
if (relative !== path.join('Saved', 'GlassCaptureProject')) {
    throw new Error(`Refusing to remove unexpected path: ${target}`);
}
await rm(target, { recursive: true });
console.log(`Removed ${target}`);
