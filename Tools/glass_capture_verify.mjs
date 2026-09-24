import { spawn } from 'node:child_process';

const child = spawn('F:/AiluEngine/artifacts/out/editor/bin/x64/debug/Editor.exe',
    ['C:/Users/BoomBac/OneDrive/AiluEngine/Saved/GlassCaptureProject/AiluTestProj.ailuproject'], {
        cwd: 'C:/Users/BoomBac/OneDrive/AiluEngine',
        env: {
            ...process.env,
            AILU_SCENE: 'project://scene/NewScene.almap',
            AILU_CAM_POS: '0,1.69,11',
            AILU_CAM_YAW: '180',
            AILU_CAM_PITCH: '0',
            AILU_FOV: '40',
            AILU_AUTOCAP_FRAME: '120',
            AILU_AUTOCAP_PASS: 'RayTrace GI DXR',
            AILU_AUTOCAP_ACCESS_INDEX: '4',
            AILU_AUTOCAP_PREFIX: process.env.GLASS_CAPTURE_PREFIX ?? 'glass_final',
        },
        windowsHide: true,
        stdio: 'ignore',
    });
console.log(`Editor pid=${child.pid}`);
child.on('error', error => console.error(error));
child.on('exit', (code, signal) => console.log(`Editor exit=${code} signal=${signal}`));
