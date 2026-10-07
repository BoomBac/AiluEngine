import bpy

from . import connection, dirty_tracker, object_sync


class AILU_OT_connect(bpy.types.Operator):
    bl_idname = "ailu_live_link.connect"
    bl_label = "Connect to Ailu"

    def execute(self, context):
        connection.connection.connect(context.scene.ailu_live_link_host,
            context.scene.ailu_live_link_port, context.scene.ailu_live_link_auto_reconnect)
        return {"FINISHED"}


class AILU_OT_disconnect(bpy.types.Operator):
    bl_idname = "ailu_live_link.disconnect"
    bl_label = "Disconnect"

    def execute(self, context):
        connection.connection.disconnect()
        return {"FINISHED"}


class AILU_OT_full_sync(bpy.types.Operator):
    bl_idname = "ailu_live_link.full_sync"
    bl_label = "Full Sync"

    def execute(self, context):
        if not connection.connection.connected:
            self.report({"ERROR"}, "Connect to Ailu first")
            return {"CANCELLED"}
        object_sync.full_sync(context, connection.connection)
        dirty_tracker.mark_synced(context.scene)
        return {"FINISHED"}


class AILU_PT_panel(bpy.types.Panel):
    bl_label = "Ailu Live Link"
    bl_idname = "AILU_PT_live_link"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Ailu"

    def draw(self, context):
        layout = self.layout
        scene = context.scene
        link = connection.connection
        link.poll()
        layout.prop(scene, "ailu_live_link_host")
        layout.prop(scene, "ailu_live_link_port")
        layout.prop(scene, "ailu_live_link_auto_reconnect")
        row = layout.row(align=True)
        row.operator("ailu_live_link.disconnect" if link.connected else "ailu_live_link.connect")
        layout.operator("ailu_live_link.full_sync")
        layout.label(text=link.status)
        if link.last_error:
            layout.label(text=link.last_error, icon="ERROR")
        layout.label(text=f"Queued: {link.queued_bytes} bytes")


_classes = (AILU_OT_connect, AILU_OT_disconnect, AILU_OT_full_sync, AILU_PT_panel)
_timer_running = False
_was_connected = False


def _timer():
    global _was_connected
    link = connection.connection
    link.poll()
    context = bpy.context
    if link.connected and not _was_connected and context.scene:
        object_sync.full_sync(context, link)
        dirty_tracker.mark_synced(context.scene)
    _was_connected = link.connected
    if link.connected and context.scene:
        dirty_tracker.flush(context, link)
    return 0.05


def register():
    global _timer_running
    for cls in _classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.ailu_live_link_host = bpy.props.StringProperty(name="Host", default="127.0.0.1")
    bpy.types.Scene.ailu_live_link_port = bpy.props.IntProperty(name="Port", default=17777, min=1, max=65535)
    bpy.types.Scene.ailu_live_link_auto_reconnect = bpy.props.BoolProperty(name="Auto Reconnect", default=True)
    dirty_tracker.install_handler()
    if not bpy.app.timers.is_registered(_timer):
        bpy.app.timers.register(_timer, first_interval=0.5, persistent=True)
        _timer_running = True


def unregister():
    global _timer_running
    dirty_tracker.remove_handler()
    connection.connection.disconnect()
    if bpy.app.timers.is_registered(_timer):
        bpy.app.timers.unregister(_timer)
    for name in ("ailu_live_link_host", "ailu_live_link_port", "ailu_live_link_auto_reconnect"):
        if hasattr(bpy.types.Scene, name):
            delattr(bpy.types.Scene, name)
    for cls in reversed(_classes):
        bpy.utils.unregister_class(cls)
