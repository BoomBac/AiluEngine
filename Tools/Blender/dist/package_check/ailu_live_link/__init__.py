bl_info = {
    "name": "AiluEngine Blender Live Link",
    "author": "AiluEngine",
    "version": (0, 1, 0),
    "blender": (3, 6, 0),
    "location": "View3D > Sidebar > Ailu",
    "category": "Import-Export",
}

from .addon import register, unregister
