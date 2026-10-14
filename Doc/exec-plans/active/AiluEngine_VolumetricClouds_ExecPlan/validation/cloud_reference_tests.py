"""Pure Python/CPU reference tests for AiluEngine volumetric-cloud ExecPlan.

These tests validate analytic properties and serve as independent CPU double-precision
oracles. They DO NOT execute the AiluEngine shader. Each corresponding milestone
must compare captured GPU data against these reference values before acceptance.
"""

from __future__ import annotations

import math
import sys
from typing import Callable, Iterable

kPi = math.pi
kTolerance = 1.0e-9
kOffsetTable = [
    (0, 0), (2, 2), (2, 0), (0, 2),
    (1, 1), (3, 3), (3, 1), (1, 3),
    (1, 0), (3, 2), (3, 0), (1, 2),
    (0, 1), (2, 3), (2, 1), (0, 3),
]


def Dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return sum(x * y for x, y in zip(a, b))


def Subtract(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def SphereRayInterval(
    ray_origin: tuple[float, float, float], ray_direction: tuple[float, float, float], radius: float
) -> tuple[float, float] | None:
    assert radius > 0.0
    assert math.isclose(Dot(ray_direction, ray_direction), 1.0, rel_tol=1e-12)
    b = Dot(ray_origin, ray_direction)
    c = Dot(ray_origin, ray_origin) - radius * radius
    discriminant = b * b - c
    if discriminant < 0.0:
        return None
    root = math.sqrt(discriminant)
    return (-b - root, -b + root)


def ShellRayIntervals(
    ray_origin: tuple[float, float, float], ray_direction: tuple[float, float, float],
    inner_radius: float, outer_radius: float, max_distance: float = math.inf,
) -> list[tuple[float, float]]:
    assert 0.0 < inner_radius < outer_radius
    outer = SphereRayInterval(ray_origin, ray_direction, outer_radius)
    if outer is None:
        return []
    first = max(0.0, outer[0])
    last = min(max_distance, outer[1])
    if last <= first:
        return []
    inner = SphereRayInterval(ray_origin, ray_direction, inner_radius)
    if inner is None or inner[1] <= first or inner[0] >= last:
        return [(first, last)]
    sections = []
    if inner[0] > first:
        sections.append((first, min(last, inner[0])))
    if inner[1] < last:
        sections.append((max(first, inner[1]), last))
    return [(a, b) for a, b in sections if b > a]


def HeightFraction(radial_distance: float, earth_radius: float, bottom_height: float, thickness: float) -> float:
    assert thickness > 0.0
    return (radial_distance - earth_radius - bottom_height) / thickness


def SmoothStep(edge_a: float, edge_b: float, value: float) -> float:
    assert edge_b > edge_a
    t = min(1.0, max(0.0, (value - edge_a) / (edge_b - edge_a)))
    return t * t * (3.0 - 2.0 * t)


def Lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def CloudTypeProfile(stratus: float, cumulus: float, cumulonimbus: float, cloud_type: float) -> float:
    cloud_type = min(1.0, max(0.0, cloud_type))
    if cloud_type < 0.5:
        return Lerp(stratus, cumulus, SmoothStep(0.0, 0.5, cloud_type))
    return Lerp(cumulus, cumulonimbus, SmoothStep(0.5, 1.0, cloud_type))


def ApplyCoverage(shape: float, coverage: float) -> float:
    shape = min(1.0, max(0.0, shape))
    coverage = min(1.0, max(0.0, coverage))
    if coverage <= 0.0:
        return 0.0
    return min(1.0, max(0.0, (shape - (1.0 - coverage)) / max(coverage, 1.0e-4)))


def HgPhase(g: float, mu: float) -> float:
    assert -1.0 < g < 1.0
    assert -1.0 <= mu <= 1.0
    return (1.0 - g * g) / (4.0 * kPi * (1.0 + g * g - 2.0 * g * mu) ** 1.5)


def HomogeneousSlab(sigma_t: float, albedo: float, radiance_in: float, distance: float) -> tuple[float, float]:
    """radiance_in already contains phase * external incident radiance."""
    assert sigma_t >= 0.0 and 0.0 <= albedo <= 1.0 and distance >= 0.0 and radiance_in >= 0.0
    tau = sigma_t * distance
    attenuation = math.exp(-tau)
    source_factor = -math.expm1(-tau)
    return (attenuation, albedo * radiance_in * source_factor)


def DiscreteHomogeneousSlab(sigma_t: float, albedo: float, radiance_in: float, distance: float, steps: int) -> tuple[float, float]:
    assert steps > 0
    segment_length = distance / steps
    total_transmittance = 1.0
    accumulated = 0.0
    for _ in range(steps):
        transmission = math.exp(-sigma_t * segment_length)
        accumulated += total_transmittance * albedo * radiance_in * (1.0 - transmission)
        total_transmittance *= transmission
    return (total_transmittance, accumulated)


def CompositeCloud(scene_color: tuple[float, float, float], cloud_color: tuple[float, float, float], transmittance: float) -> tuple[float, float, float]:
    assert 0.0 <= transmittance <= 1.0
    return tuple(s + c * transmittance for s, c in zip(cloud_color, scene_color))


def NearlyEqual(actual: float, expected: float, tolerance: float = kTolerance) -> None:
    if not math.isclose(actual, expected, rel_tol=tolerance, abs_tol=tolerance):
        raise AssertionError(f"actual={actual!r} expected={expected!r} tol={tolerance}")


def TestShellIntervals() -> None:
    cases = [
        ((0.0, 100.0, 0.0), (0.0, 1.0, 0.0), [(10.0, 20.0)]),
        ((0.0, 115.0, 0.0), (0.0, 1.0, 0.0), [(0.0, 5.0)]),
        ((0.0, 115.0, 0.0), (0.0, -1.0, 0.0), [(0.0, 5.0), (225.0, 235.0)]),
        ((0.0, 130.0, 0.0), (0.0, -1.0, 0.0), [(10.0, 20.0), (240.0, 250.0)]),
        ((0.0, 130.0, 0.0), (0.0, 1.0, 0.0), []),
    ]
    for origin, direction, expected_intervals in cases:
        actual = ShellRayIntervals(origin, direction, 110.0, 120.0)
        assert len(actual) == len(expected_intervals), (origin, direction, actual, expected_intervals)
        for (actual_first, actual_last), (expected_first, expected_last) in zip(actual, expected_intervals):
            NearlyEqual(actual_first, expected_first)
            NearlyEqual(actual_last, expected_last)
    clipped = ShellRayIntervals((0.0, 100.0, 0.0), (0.0, 1.0, 0.0), 110.0, 120.0, 15.0)
    assert clipped == [(10.0, 15.0)]
    for i in range(256):
        angle = 2.0 * kPi * i / 256.0
        direction = (math.cos(angle), math.sin(angle), 0.0)
        for a, b in ShellRayIntervals((0.0, 100.0, 0.0), direction, 110.0, 120.0):
            assert a >= 0.0 and b > a and math.isfinite(b)



def TestShellRayClassification() -> None:
    # Independent point-in-shell predicate validates interval segmentation over many rays.
    for a in range(73):
        angle = 2.0 * kPi * a / 73.0
        direction = (math.cos(angle), math.sin(angle), 0.0)
        for radius in (85.0, 105.0, 115.0, 125.0, 150.0):
            origin = (0.0, radius, 0.0)
            sections = ShellRayIntervals(origin, direction, 110.0, 120.0, 300.0)
            for sample_index in range(600):
                t = (sample_index + 0.314159) * 0.5
                current_pos = tuple(o + v * t for o, v in zip(origin, direction))
                current_radius = math.sqrt(Dot(current_pos, current_pos))
                geometric_inside = 110.0 < current_radius < 120.0
                interval_inside = any(start < t < stop for start, stop in sections)
                assert geometric_inside == interval_inside, (origin, direction, t, current_radius, sections)


def TestRadialHeight() -> None:
    NearlyEqual(HeightFraction(110.0, 100.0, 10.0, 10.0), 0.0)
    NearlyEqual(HeightFraction(115.0, 100.0, 10.0, 10.0), 0.5)
    NearlyEqual(HeightFraction(120.0, 100.0, 10.0, 10.0), 1.0)
    NearlyEqual(HeightFraction(6371000.0 + 11000.0, 6371000.0, 5000.0, 6000.0), 1.0)


def TestCoverage() -> None:
    for shape in [0.0, 0.03, 0.25, 0.5, 0.75, 0.97, 1.0]:
        NearlyEqual(ApplyCoverage(shape, 0.0), 0.0)
        NearlyEqual(ApplyCoverage(shape, 1.0), shape)
        previous = -1.0
        for j in range(1001):
            value = ApplyCoverage(shape, j / 1000.0)
            assert 0.0 <= value <= 1.0
            assert value + 1.0e-12 >= previous, (shape, j, value, previous)
            previous = value


def TestCloudTypeInterpolation() -> None:
    stratus, cumulus, cumulonimbus = 0.1, 0.4, 0.8
    NearlyEqual(CloudTypeProfile(stratus, cumulus, cumulonimbus, 0.0), stratus)
    NearlyEqual(CloudTypeProfile(stratus, cumulus, cumulonimbus, 0.5), cumulus)
    NearlyEqual(CloudTypeProfile(stratus, cumulus, cumulonimbus, 1.0), cumulonimbus)
    NearlyEqual(CloudTypeProfile(stratus, cumulus, cumulonimbus, 0.25), 0.25)
    NearlyEqual(CloudTypeProfile(stratus, cumulus, cumulonimbus, 0.75), 0.6)
    assert abs(CloudTypeProfile(stratus, cumulus, cumulonimbus, 0.5 - 1e-6) - CloudTypeProfile(stratus, cumulus, cumulonimbus, 0.5 + 1e-6)) < 1e-8


def TestHgPhase() -> None:
    for mu_i in range(101):
        mu = mu_i / 50.0 - 1.0
        NearlyEqual(HgPhase(0.0, mu), 1.0 / (4.0 * kPi))
        for g in (-0.3, 0.7):
            assert math.isfinite(HgPhase(g, mu)) and HgPhase(g, mu) > 0.0
    assert HgPhase(0.7, 1.0) > HgPhase(0.7, -1.0)
    # Simpson quadrature over polar cosine; phi integration gives 2*pi.
    num_intervals = 20000
    for g in (-0.3, 0.0, 0.7, 0.85):
        integral = 0.0
        step = 2.0 / num_intervals
        for i in range(num_intervals + 1):
            mu = -1.0 + i * step
            weight = 1 if i in (0, num_intervals) else (4 if i % 2 else 2)
            integral += weight * HgPhase(g, mu)
        integral *= 2.0 * kPi * step / 3.0
        NearlyEqual(integral, 1.0, 1.0e-6)


def TestHomogeneousSlab() -> None:
    transmittance, radiance = HomogeneousSlab(0.002, 0.8, 1.0, 1000.0)
    NearlyEqual(transmittance, math.exp(-2.0))
    NearlyEqual(radiance, 0.8 * (1.0 - math.exp(-2.0)))
    for steps in (1, 8, 32, 128, 512):
        discrete_transmittance, discrete_radiance = DiscreteHomogeneousSlab(0.002, 0.8, 1.0, 1000.0, steps)
        NearlyEqual(discrete_transmittance, transmittance)
        NearlyEqual(discrete_radiance, radiance)
    for sigma_t in (0.0, 1.0e-12, 0.0001, 0.002, 0.1):
        for albedo in (0.0, 0.4, 1.0):
            t, l = HomogeneousSlab(sigma_t, albedo, 1.0, 1000.0)
            assert 0.0 <= t <= 1.0 and l >= 0.0 and math.isfinite(l)
    assert HomogeneousSlab(0.0, 0.8, 1.0, 1000.0) == (1.0, 0.0)
    assert HomogeneousSlab(0.002, 0.0, 1.0, 1000.0)[1] == 0.0


def TestComposite() -> None:
    actual = CompositeCloud((0.2, 0.4, 0.8), (0.3, 0.1, 0.0), 0.25)
    for a, e in zip(actual, (0.35, 0.20, 0.20)):
        NearlyEqual(a, e)
    assert CompositeCloud((0.2, 0.4, 0.8), (0.0, 0.0, 0.0), 1.0) == (0.2, 0.4, 0.8)


def TestTileCoverage() -> None:
    assert len(kOffsetTable) == 16 and len(set(kOffsetTable)) == 16
    assert set(kOffsetTable) == {(x, y) for x in range(4) for y in range(4)}
    for width, height in ((16, 16), (317, 179), (321, 181), (101, 37)):
        update_count = [[0] * width for _ in range(height)]
        groups_x = (width + 3) // 4
        groups_y = (height + 3) // 4
        for x_offset, y_offset in kOffsetTable:
            for thread_y in range(groups_y):
                pixel_y = thread_y * 4 + y_offset
                if pixel_y >= height:
                    continue
                for thread_x in range(groups_x):
                    pixel_x = thread_x * 4 + x_offset
                    if pixel_x < width:
                        update_count[pixel_y][pixel_x] += 1
        assert all(count == 1 for row in update_count for count in row), (width, height)


def TestWindAdvection() -> None:
    world_current = (150.0, 50.0, -20.0)
    velocity = (6.0, 0.0, -2.0)
    dt = 1.0 / 60.0
    time_current = 10.0
    time_previous = time_current - dt
    world_previous = tuple(position - speed * dt for position, speed in zip(world_current, velocity))
    noise_position_current = tuple(position - speed * time_current for position, speed in zip(world_current, velocity))
    noise_position_previous = tuple(position - speed * time_previous for position, speed in zip(world_previous, velocity))
    for a, b in zip(noise_position_current, noise_position_previous):
        NearlyEqual(a, b)


def Run() -> int:
    tests: list[Callable[[], None]] = [
        TestShellIntervals, TestShellRayClassification, TestRadialHeight, TestCoverage, TestCloudTypeInterpolation, TestHgPhase,
        TestHomogeneousSlab, TestComposite, TestTileCoverage, TestWindAdvection,
    ]
    failed = 0
    for test in tests:
        try:
            test()
            print(f"PASS  {test.__name__}")
        except Exception as error:
            failed += 1
            print(f"FAIL  {test.__name__}: {error}", file=sys.stderr)
    print(f"Result: {len(tests)-failed}/{len(tests)} passed")
    return 0 if not failed else 1


if __name__ == "__main__":
    raise SystemExit(Run())
