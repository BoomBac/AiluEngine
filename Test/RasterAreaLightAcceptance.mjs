import {Rpc, Write, Capture, Delay} from './TaaEditorAcceptanceClient.mjs';
const state = await Rpc('editor.get_state');
const entities = (await Rpc('scene.query_entities', {offset:0,limit:64})).items;
const lights = entities.filter(e => /^Area[0-8]$/.test(e.name)).sort((a,b)=>a.name.localeCompare(b.name));
if (lights.length !== 9) throw Error('Expected nine independent area lights');
const original = await Rpc('editor.get_scene_camera');
async function Set(light, intensity) {
  await Rpc('scene.set_property',{target:{kind:'component',scene_guid:state.scene_guid,
    entity_guid:light.entity_guid,component_type:'Ailu.ECS.LightComponent'},
    property_path:'_light._intensity',value:intensity});
}
async function Frame(name) {
  await Delay(350);
  await Capture(name,'DeferredLightingPass',3,{resource_name:'_CameraColorAttachmentA',camera_ordinal:0});
}
try {
  await Rpc('editor.set_scene_camera',{position:[0,4,-7],pitch_degrees:25,yaw_degrees:0,
    fov_degrees:60,aa_mode:0,snap:true});
  await Delay(1200);
  Write('lights.json',await Rpc('scene.inspect_entities',{entity_guids:lights.map(e=>e.entity_guid),
    include_components:true,include_properties:true}));
  await Frame('zero');
  for (let i=0;i<9;++i) {
    await Set(lights[i],20); await Frame('single'+i); await Set(lights[i],0);
  }
  for (const light of lights) await Set(light,20);
  await Frame('all9');
  for (const light of lights.slice(4)) await Rpc('scene.delete_entity',{entity_guid:light.entity_guid});
  await Frame('shrink4');
  for (let i=0;i<5;++i) {
    await Rpc('scene.create_entity',{name:'Regrown'+i});
    const item=(await Rpc('scene.query_entities',{offset:0,limit:64})).items.find(e=>e.name==='Regrown'+i);
    await Rpc('scene.add_component',{entity_guid:item.entity_guid,component_type:'Ailu.ECS.LightComponent'});
    for (const [component_type,property_path,value] of [
      ['Ailu.ECS.TransformComponent','_local_transform._position',[0,3,0]],
      ['Ailu.ECS.LightComponent','_type','kArea'],
      ['Ailu.ECS.LightComponent','_light._range',0],
      ['Ailu.ECS.LightComponent','_light._intensity_unit','kWatt'],
      ['Ailu.ECS.LightComponent','_shadow._is_cast_shadow',false]]) {
      await Rpc('scene.set_property',{target:{kind:'component',scene_guid:state.scene_guid,
        entity_guid:item.entity_guid,component_type},property_path,value});
    }
    await Set(item,20);
  }
  await Frame('regrow9');
  const remaining=(await Rpc('scene.query_entities',{offset:0,limit:64})).items
    .filter(e=>e.name.startsWith('Area')||e.name.startsWith('Regrown'));
  for (const light of remaining) await Rpc('scene.delete_entity',{entity_guid:light.entity_guid});
  await Frame('empty');
  Write('acceptance.json',{status:'PASS',light_count:9});
} finally {
  await Rpc('editor.set_scene_camera',{position:original.target_position,pitch_degrees:original.pitch_degrees,
    yaw_degrees:original.yaw_degrees,fov_degrees:original.fov_degrees,aa_mode:original.aa_mode,snap:true});
}
