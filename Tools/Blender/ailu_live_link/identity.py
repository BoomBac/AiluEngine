import os
import uuid

OBJECT_GUID_KEY = "ailu_live_link_guid"
SESSION_GUID_KEY = "ailu_live_link_session_guid"


def library_guid(data_block):
    library = getattr(data_block, "library", None)
    if library is None:
        return ""
    import bpy
    path = os.path.normcase(os.path.normpath(bpy.path.abspath(library.filepath)))
    node = "lib:" + path + ":" + data_block.bl_rna.identifier + ":" + data_block.name
    return str(uuid.uuid5(uuid.NAMESPACE_URL, node))


def get_guid(data_block):
    linked_guid = library_guid(data_block)
    if linked_guid:
        return linked_guid
    value = data_block.get(OBJECT_GUID_KEY)
    if not value:
        value = str(uuid.uuid4())
        data_block[OBJECT_GUID_KEY] = value
    return value


def repair_duplicate_guids(objects):
    seen = set()
    for obj in objects:
        guid = get_guid(obj)
        if guid in seen:
            if getattr(obj, "library", None) is not None:
                continue
            guid = str(uuid.uuid4())
            obj[OBJECT_GUID_KEY] = guid
        seen.add(guid)


def get_session_guid(scene):
    value = scene.get(SESSION_GUID_KEY)
    if not value:
        value = str(uuid.uuid4())
        scene[SESSION_GUID_KEY] = value
    return value
