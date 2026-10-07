import uuid

OBJECT_GUID_KEY = "ailu_live_link_guid"
SESSION_GUID_KEY = "ailu_live_link_session_guid"


def get_guid(data_block):
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
            guid = str(uuid.uuid4())
            obj[OBJECT_GUID_KEY] = guid
        seen.add(guid)


def get_session_guid(scene):
    value = scene.get(SESSION_GUID_KEY)
    if not value:
        value = str(uuid.uuid4())
        scene[SESSION_GUID_KEY] = value
    return value
