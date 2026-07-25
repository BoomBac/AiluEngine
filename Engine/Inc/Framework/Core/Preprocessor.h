#pragma once

#define AL_CONCAT_INNER(lhs, rhs) lhs##rhs
#define AL_CONCAT(lhs, rhs) AL_CONCAT_INNER(lhs, rhs)

#define AL_STRINGIFY_INNER(value) #value
#define AL_STRINGIFY(value) AL_STRINGIFY_INNER(value)