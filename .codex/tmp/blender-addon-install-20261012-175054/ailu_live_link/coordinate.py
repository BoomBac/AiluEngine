from mathutils import Matrix, Vector

# Blender Z-up to Ailu Y-up, with a right-handed basis.
_basis = Matrix(((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0)))
_basis4 = _basis.to_4x4()
_basis4_inverse = _basis4.inverted()


def blender_to_ailu_matrix(matrix):
    return _basis4 @ matrix @ _basis4_inverse


def blender_to_ailu_position(value):
    return _basis @ Vector(value)


def blender_to_ailu_direction(value):
    return (_basis @ Vector(value)).normalized()


def blender_to_ailu_transform(matrix):
    location, rotation, scale = blender_to_ailu_matrix(matrix).decompose()
    return location, rotation, scale
