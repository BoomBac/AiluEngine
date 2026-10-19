import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';
const state = await Rpc('editor.get_state');
const items = (await Rpc('scene.query_entities', {offset:0,limit:64})).items;
const lights = items.filter(e=>/^PointLight[0-9]{2}$/.test(e.name)).sort((a,b)=>a.name.localeCompare(b.name));
if (lights.length!==33) throw Error('Expected 33 ordered point lights');
const original = await Rpc('editor.get_scene_camera');
async function Set(index,value) {
  await Rpc('scene.set_property',{target:{kind:'component',scene_guid:state.scene_guid,
    entity_guid:lights[index].entity_guid,component_type:'Ailu.ECS.LightComponent'},
    property_path:'_light._intensity',value});
}
async function Frame(name) {
  await Delay(350);
  await Capture(name,'DeferredLightingPass',3,{resource_name:'_CameraColorAttachmentA',camera_ordinal:0});
}
try {
  await Rpc('editor.set_scene_camera',{position:[0,4,-7],pitch_degrees:25,yaw_degrees:0,
    fov_degrees:60,aa_mode:0,snap:true});
  await Delay(1200);
  Write('point_lights.json',await Rpc('scene.inspect_entities',{entity_guids:lights.map(e=>e.entity_guid),
    include_components:true,include_properties:true}));
  await Frame('zero');
  for (const index of [0,4,15,31,32]) {
    await Set(index,20); await Frame('single'+index); await Set(index,0);
  }
  await Set(0,10); await Frame('half_power'); await Set(0,0);
  async function Property(path,value) {
    await Rpc('scene.set_property',{target:{kind:'component',scene_guid:state.scene_guid,
      entity_guid:lights[0].entity_guid,component_type:'Ailu.ECS.LightComponent'},property_path:path,value});
  }
  await Property('_light._light_color',[.25,.5,1,1]); await Set(0,20); await Frame('colored_point');
  await Property('_light._light_color',[1,1,1,1]); await Property('_type','kSpot');
  await Frame('spot_full'); await Set(0,10); await Frame('spot_half');
  await Property('_light._light_color',[.25,.5,1,1]); await Set(0,20); await Frame('colored_spot');
  await Property('_light._light_color',[1,1,1,1]); await Set(0,0); await Property('_type','kPoint');
  for (let index=0;index<32;++index) await Set(index,20);
  await Frame('all32');
  await Set(32,20); await Frame('overflow33');
  for (let index=4;index<33;++index) await Set(index,0);
  await Frame('first4');
  for (const light of lights) await Rpc('scene.delete_entity',{entity_guid:light.entity_guid});
  await Frame('empty');
  Write('point_acceptance.json',{status:'PASS',tested_lights:33,accepted_capacity:32});
} finally {
  await Rpc('editor.set_scene_camera',{position:original.target_position,pitch_degrees:original.pitch_degrees,
    yaw_degrees:original.yaw_degrees,fov_degrees:original.fov_degrees,aa_mode:original.aa_mode,snap:true});
}
