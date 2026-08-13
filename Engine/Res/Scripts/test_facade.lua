-- T04 Regression: Script-facing Entity, Transform, Scene, Time, and Input APIs.

local TestFacade = {}

function TestFacade:on_create()
    reg_facade_started = true
    self.entity.name = "FacadeEntityRenamed"
    self.entity.transform.position = Vec3.new(4.0, 5.0, 6.0)
    local position = self.entity.transform.position
    reg_facade_position_ok = self.entity.name == "FacadeEntityRenamed" and position.x == 4.0 and
                             position.y == 5.0 and position.z == 6.0

    reg_facade_guid_ok = self.scene:find_guid(self.entity.guid):is_valid()
    reg_facade_find_name_ok = self.scene:find(self.entity.name):is_valid()
    reg_facade_spawn_safe = not self.scene:spawn(ScriptAssetValue.prefab()).valid
    local prefab_asset = ScriptAssetValue.prefab()
    reg_facade_asset_handle_ok = not prefab_asset.valid and prefab_asset.get_guid == nil and prefab_asset.get_path == nil
    reg_facade_main_camera_safe = self.scene.main_camera ~= nil
    reg_facade_time_ok = time.time >= 0.0 and time.delta_time >= 0.0 and time.fixed_delta_time >= 0.0 and
                         time.render_alpha >= 0.0
    reg_facade_rigidbody_safe = self.entity.rigidbody2d == nil
    reg_facade_raycast_safe = physics2d.raycast(Vec2(0.0, 0.0), Vec2(1.0, 0.0), 5.0, 0xffffffff) == nil
    reg_facade_audio_service_safe = not audio.play_one_shot(ScriptAssetValue.audio_clip(), Vec3(0.0, 0.0, 0.0))

    -- No Application/InputSystem exists in the standalone runner; queries must be safe.
    reg_facade_input_safe = input:pressed("Jump") == false and input:released("Jump") == false and
                            input:down("Jump") == false and input:axis("MoveX") == 0.0 and
                            input:axis2("Move").x == 0.0 and input:axis2("Move").y == 0.0

    local sprite = self.entity.sprite
    sprite.visible = false
    sprite.flip_x = true
    sprite.flip_y = true
    sprite.order = 7
    sprite.color = Color(0.25, 0.5, 0.75, 1.0)
    reg_facade_sprite_ok = sprite.valid and not sprite.visible and sprite.flip_x and sprite.flip_y and sprite.order == 7 and
                           sprite.color.r == 0.25 and sprite.color.g == 0.5 and sprite.color.b == 0.75

    local animator = self.entity.animator
    animator.speed = 2.0
    reg_facade_animator_ok = animator.valid and animator.speed == 2.0 and not animator:play("missing")

    local audio = self.entity.audio
    audio.volume = 0.25
    audio.pitch = 0.8
    audio.loop = true
    reg_facade_audio_ok = audio.valid and audio.volume == 0.25 and audio.pitch == 0.8 and audio.loop

    local camera = self.scene.main_camera
    camera.fov = 60.0
    camera.orthographic = false
    reg_facade_camera_ok = camera.valid and camera.fov == 60.0 and not camera.orthographic
end

return TestFacade
