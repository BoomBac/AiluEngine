import { readFile, writeFile } from 'node:fs/promises';

const scene_path = 'C:/Users/BoomBac/OneDrive/AiluEngine/Saved/GlassCaptureProject/Assets/scene/NewScene.almap';
const source_path = 'C:/Users/BoomBac/OneDrive/AiluTestProj/Assets/scene/NewScene.almap';
const scene = JSON.parse(await readFile(source_path, 'utf8'));
scene._entities = scene._entities.filter(entity =>
    ['new_object_3', 'env', 'main_light'].includes(entity._tag_component?._name));
console.log(scene._entities.map(entity => entity._tag_component._name).join(', '));
await writeFile(scene_path, JSON.stringify(scene, null, 4));
