from mathutils import Matrix, Vector
import numpy as np

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


def ObjectLocalMatrix(obj, parent):
    # Bone and vertex parenting use an evaluated frame that matrix_local does not express relative to the parent object.
    return parent.matrix_world.inverted_safe() @ obj.matrix_world if parent is not None else obj.matrix_world.copy()


def SplitAffineTransform(matrix):
    """Return child and optional helper-parent matrices whose product preserves the full affine transform."""
    position, rotation, scale = matrix.decompose()
    reconstructed = Matrix.LocRotScale(position, rotation, scale)
    magnitude = max(1.0, max(abs(matrix[row][column]) for row in range(3) for column in range(3)))
    if max(abs(matrix[row][column] - reconstructed[row][column]) for row in range(3) for column in range(3)) <= 1e-6 * magnitude:
        return matrix, None
    left, singular_values, right = np.linalg.svd(np.array(matrix.to_3x3(), dtype=np.float64))
    # Both factors must be proper rotations; retain reflections in the signed scale.
    if np.linalg.det(left) < 0.0:
        left[:, 2] *= -1.0
        singular_values[2] *= -1.0
    if np.linalg.det(right) < 0.0:
        right[2, :] *= -1.0
        singular_values[2] *= -1.0
    parent = Matrix.LocRotScale(position, Matrix(left.tolist()).to_quaternion(), Vector(singular_values.tolist()))
    return Matrix(right.tolist()).to_4x4(), parent
