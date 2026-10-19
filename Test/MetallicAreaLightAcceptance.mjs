import {Rpc,Write,Capture,Delay} from './TaaEditorAcceptanceClient.mjs';
const state=await Rpc('editor.get_state');
const objects=(await Rpc('scene.query_entities',{offset:0,limit:64})).items;
const probes=['Red','Green','Blue'].map(name=>objects.find(item=>item.name==='Probe'+name));
if(probes.some(item=>!item))throw Error('Missing metal probes');
const camera=await Rpc('editor.get_scene_camera');
async function Move(item,x){
  await Rpc('scene.set_property',{target:{kind:'component',scene_guid:state.scene_guid,
    entity_guid:item.entity_guid,component_type:'Ailu.ECS.TransformComponent'},
    property_path:'_local_transform._position',value:[x,-.1,0]});
}
try{
  await Rpc('editor.set_scene_camera',{position:[0,4,-7],pitch_degrees:25,yaw_degrees:0,
    fov_degrees:60,aa_mode:0,snap:true});
  await Delay(1200);
  for(let index=0;index<probes.length;++index){
    for(let other=0;other<probes.length;++other)await Move(probes[other],other===index?0:1000);
    await Delay(500);
    await Capture(probes[index].name,'DeferredLightingPass',3,
      {resource_name:'_CameraColorAttachmentA',camera_ordinal:0});
  }
  for(let other=0;other<probes.length;++other)await Move(probes[other],other===0?0:1000);
  for(const [name,power] of [['Area0',0],['ScaledArea',20]]){
    const item=objects.find(object=>object.name===name);
    await Rpc('scene.set_property',{target:{kind:'component',scene_guid:state.scene_guid,
      entity_guid:item.entity_guid,component_type:'Ailu.ECS.LightComponent'},
      property_path:'_light._intensity',value:power});
  }
  await Delay(500);
  await Capture('ScaledArea','DeferredLightingPass',3,
    {resource_name:'_CameraColorAttachmentA',camera_ordinal:0});
  Write('metal_acceptance.json',{status:'PASS'});
}finally{
  await Rpc('editor.set_scene_camera',{position:camera.target_position,pitch_degrees:camera.pitch_degrees,
    yaw_degrees:camera.yaw_degrees,fov_degrees:camera.fov_degrees,aa_mode:camera.aa_mode,snap:true});
}
