#pragma

#define ACLASS()
#define ASTRUCT()
#define AENUM()
#define APROPERTY(...)
#define AFIELD(...)
#define AFUNCTION(...)

#define BODY_MACRO_COMBINE_INNER(A, B, C, D) A##B##C##D
#define BODY_MACRO_COMBINE(A, B, C, D) BODY_MACRO_COMBINE_INNER(A, B, C, D)
#define GENERATED_BODY(...) BODY_MACRO_COMBINE(CURRENT_FILE_ID, _, __LINE__, _GENERATED_BODY)