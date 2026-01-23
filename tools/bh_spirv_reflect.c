/*
  bh_spirv_reflect.c

  Minimal SPIR-V reflection tool tailored for this minimal engine's needs.

  Purpose:
  - Run at build time on SPIR-V shader binaries.
  - Emit small JSON metadata consumed by runtime shader/material code.

  Supported (currently):
  - Uniform buffers (SDL GPU SPIR-V set conventions)
  - Struct member names + offsets
  - Basic types: float, float2/3/4, mat4

  Notes:
  - Probably switch to SPIRV-Cross at some point..
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SPIR-V headers */
#define SPIRV_MAGIC 0x07230203u

/* ExecutionModel */
#define SPIRV_EXECUTION_MODEL_VERTEX   0u
#define SPIRV_EXECUTION_MODEL_FRAGMENT 4u

/* StorageClass */
#define SPIRV_STORAGE_CLASS_UNIFORMCONSTANT 0u
#define SPIRV_STORAGE_CLASS_UNIFORM 2u

/* Decorations */
#define SPIRV_DECORATION_BINDING        33u
#define SPIRV_DECORATION_DESCRIPTORSET  34u
#define SPIRV_DECORATION_OFFSET         35u
#define SPIRV_DECORATION_MATRIXSTRIDE    7u

/* Opcodes (partial) */
#define SPIRV_OP_ENTRYPOINT      15u
#define SPIRV_OP_NAME             5u
#define SPIRV_OP_MEMBERNAME       6u
#define SPIRV_OP_DECORATE        71u
#define SPIRV_OP_MEMBERDECORATE  72u
#define SPIRV_OP_TYPEINT         21u
#define SPIRV_OP_TYPEFLOAT       22u
#define SPIRV_OP_TYPEVECTOR      23u
#define SPIRV_OP_TYPEMATRIX      24u
#define SPIRV_OP_TYPEIMAGE       25u
#define SPIRV_OP_TYPESAMPLER     26u
#define SPIRV_OP_TYPESAMPLEDIMAGE 27u
#define SPIRV_OP_TYPEARRAY       28u
#define SPIRV_OP_TYPESTRUCT      30u
#define SPIRV_OP_TYPEPOINTER     32u
#define SPIRV_OP_CONSTANT        43u
#define SPIRV_OP_VARIABLE        59u

typedef enum TypeKind
{
    TYPE_UNKNOWN = 0,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_VECTOR,
    TYPE_MATRIX,
    TYPE_IMAGE,
    TYPE_SAMPLER,
    TYPE_SAMPLED_IMAGE,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_POINTER,
} TypeKind;

typedef struct TypeInfo
{
    TypeKind kind;
    uint32_t elem_type;     /* vector component, matrix column type, array element, pointer pointee */
    uint32_t count;         /* vector components, matrix columns, array length, pointer storage class */
    uint32_t width_bits;    /* int/float width */
    uint32_t signedness;    /* int */

    /* struct */
    uint32_t member_count;
    uint32_t *member_types; /* length member_count */
} TypeInfo;

typedef struct StructExtra
{
    char **member_names;      /* length member_count */
    uint32_t *member_offsets; /* length member_count */
    uint32_t *member_mstride; /* length member_count (0 if not set) */
} StructExtra;

typedef struct VarInfo
{
    uint32_t pointer_type;  /* OpTypePointer id */
    uint32_t storage_class; /* OpVariable storage class */
    bool valid;
} VarInfo;

static uint32_t round_up_u32(uint32_t v, uint32_t alignment)
{
    if (alignment == 0u) {
        return v;
    }
    const uint32_t mask = alignment - 1u;
    return (v + mask) & ~mask;
}

static char *dup_cstr(const char *s)
{
    if (!s) {
        return NULL;
    }
    const size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static char *read_string_operand(const uint32_t *words, uint32_t total_words, uint32_t start_word)
{
    /* SPIR-V strings are nul-terminated and packed into 32-bit words. */
    if (!words || start_word >= total_words) {
        return NULL;
    }

    const uint32_t max_bytes = (total_words - start_word) * 4u;
    const char *raw = (const char *)&words[start_word];

    /* Find first nul, cap at max_bytes to avoid OOB. */
    uint32_t n = 0;
    while (n < max_bytes && raw[n] != '\0') {
        ++n;
    }

    char *s = (char *)malloc((size_t)n + 1u);
    if (!s) {
        return NULL;
    }
    memcpy(s, raw, n);
    s[n] = '\0';
    return s;
}

static bool is_float_type(const TypeInfo *types, uint32_t type_id)
{
    return types[type_id].kind == TYPE_FLOAT && types[type_id].width_bits == 32;
}

static uint32_t std140_size_of_type(
    const TypeInfo *types,
    const uint32_t *const_u32,
    const bool *has_const,
    uint32_t type_id,
    uint32_t matrix_stride_opt)
{
    (void)const_u32;
    (void)has_const;

    const TypeInfo *t = &types[type_id];
    switch (t->kind) {
        case TYPE_FLOAT:
            return 4u;
        case TYPE_INT:
            return 4u;
        case TYPE_VECTOR:
        {
            if (!is_float_type(types, t->elem_type)) {
                /* Only float vectors supported in this skeleton. */
                return 0u;
            }
            if (t->count == 2u) {
                return 8u;
            }
            /* vec3/vec4 take 16 bytes in std140 (and in D3D cbuffer packing). */
            if (t->count == 3u || t->count == 4u) {
                return 16u;
            }
            return 0u;
        }
        case TYPE_MATRIX:
        {
            /* Assume float matrices. Column type should be a float vector. */
            const uint32_t stride = (matrix_stride_opt != 0u) ? matrix_stride_opt : 16u;
            return stride * t->count;
        }
        case TYPE_ARRAY:
        {
            /* Minimal support: element size times count (no alignment handling). */
            const uint32_t elem_size = std140_size_of_type(types, const_u32, has_const, t->elem_type, 0u);
            return elem_size * t->count;
        }
        case TYPE_STRUCT:
        {
            /* Size determined by member offsets externally. */
            return 0u;
        }
        default:
            return 0u;
    }
}

static const char *param_type_string(const TypeInfo *types, uint32_t type_id)
{
    const TypeInfo *t = &types[type_id];
    if (t->kind == TYPE_FLOAT) {
        return "float";
    }
    if (t->kind == TYPE_VECTOR && is_float_type(types, t->elem_type)) {
        if (t->count == 2u) return "float2";
        if (t->count == 3u) return "float3";
        if (t->count == 4u) return "float4";
    }
    if (t->kind == TYPE_MATRIX) {
        /* Detect mat4: columns=4, column type=vec4 */
        const TypeInfo *col = &types[t->elem_type];
        if (t->count == 4u && col->kind == TYPE_VECTOR && col->count == 4u && is_float_type(types, col->elem_type)) {
            return "mat4";
        }
    }
    return "unknown";
}

static void free_struct(TypeInfo *t, StructExtra *sx)
{
    if (!t) {
        return;
    }
    if (t->kind == TYPE_STRUCT) {
        free(t->member_types);
        t->member_types = NULL;
    }
    if (sx) {
        if (sx->member_names) {
            for (uint32_t i = 0; i < t->member_count; ++i) {
                free(sx->member_names[i]);
            }
        }
        free(sx->member_names);
        free(sx->member_offsets);
        free(sx->member_mstride);
        sx->member_names = NULL;
        sx->member_offsets = NULL;
        sx->member_mstride = NULL;
    }
}

static void parse_pass1(
    const uint32_t *words,
    uint32_t total_words,
    uint32_t bound,
    TypeInfo *types,
    StructExtra *structs,
    VarInfo *vars,
    uint32_t *binding,
    uint32_t *dset,
    bool *has_binding,
    bool *has_dset,
    uint32_t *const_u32,
    bool *has_const,
    uint32_t *out_exec_model,
    char **out_entry_name)
{
    uint32_t exec_model = 0xFFFFFFFFu;
    char *entry_name = NULL;

    uint32_t idx = 5;
    while (idx < total_words) {
        const uint32_t w0 = words[idx];
        const uint16_t opcode = (uint16_t)(w0 & 0xFFFFu);
        const uint16_t wcount = (uint16_t)(w0 >> 16);

        if (wcount == 0) {
            break;
        }
        if (idx + (uint32_t)wcount > total_words) {
            break;
        }

        switch (opcode) {
            case SPIRV_OP_ENTRYPOINT:
            {
                /* ExecutionModel, EntryPoint, Name, Interface... */
                exec_model = words[idx + 1];
                free(entry_name);
                entry_name = read_string_operand(words, total_words, idx + 3);
            } break;

            case SPIRV_OP_DECORATE:
            {
                const uint32_t target_id = words[idx + 1];
                const uint32_t deco = words[idx + 2];
                if (target_id < bound) {
                    if (deco == SPIRV_DECORATION_BINDING && wcount >= 4) {
                        has_binding[target_id] = true;
                        binding[target_id] = words[idx + 3];
                    } else if (deco == SPIRV_DECORATION_DESCRIPTORSET && wcount >= 4) {
                        has_dset[target_id] = true;
                        dset[target_id] = words[idx + 3];
                    }
                }
            } break;

            case SPIRV_OP_TYPEFLOAT:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t width = words[idx + 2];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_FLOAT;
                    types[result_id].width_bits = width;
                }
            } break;

            case SPIRV_OP_TYPEINT:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t width = words[idx + 2];
                const uint32_t signedness = words[idx + 3];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_INT;
                    types[result_id].width_bits = width;
                    types[result_id].signedness = signedness;
                }
            } break;

            case SPIRV_OP_TYPEVECTOR:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t comp_type = words[idx + 2];
                const uint32_t count = words[idx + 3];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_VECTOR;
                    types[result_id].elem_type = comp_type;
                    types[result_id].count = count;
                }
            } break;

            case SPIRV_OP_TYPEMATRIX:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t col_type = words[idx + 2];
                const uint32_t col_count = words[idx + 3];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_MATRIX;
                    types[result_id].elem_type = col_type;
                    types[result_id].count = col_count;
                }
            } break;

            case SPIRV_OP_TYPEIMAGE:
            {
                /* ResultId = words[idx + 1] */
                const uint32_t result_id = words[idx + 1];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_IMAGE;
                }
            } break;

            case SPIRV_OP_TYPESAMPLER:
            {
                const uint32_t result_id = words[idx + 1];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_SAMPLER;
                }
            } break;

            case SPIRV_OP_TYPESAMPLEDIMAGE:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t image_type = words[idx + 2];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_SAMPLED_IMAGE;
                    types[result_id].elem_type = image_type;
                }
            } break;

            case SPIRV_OP_CONSTANT:
            {
                const uint32_t result_type = words[idx + 1];
                const uint32_t result_id = words[idx + 2];
                if (result_id < bound && result_type < bound && types[result_type].kind == TYPE_INT && wcount >= 4) {
                    has_const[result_id] = true;
                    const_u32[result_id] = words[idx + 3];
                }
            } break;

            case SPIRV_OP_TYPEARRAY:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t elem_type = words[idx + 2];
                const uint32_t len_id = words[idx + 3];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_ARRAY;
                    types[result_id].elem_type = elem_type;
                    types[result_id].count = (has_const[len_id]) ? const_u32[len_id] : 0u;
                }
            } break;

            case SPIRV_OP_TYPESTRUCT:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t member_count = (uint32_t)wcount - 2u;
                if (result_id < bound) {
                    /* Clean any previous struct data */
                    free_struct(&types[result_id], &structs[result_id]);

                    types[result_id].kind = TYPE_STRUCT;
                    types[result_id].member_count = member_count;
                    types[result_id].member_types = (uint32_t *)calloc(member_count, sizeof(uint32_t));

                    structs[result_id].member_names = (char **)calloc(member_count, sizeof(char *));
                    structs[result_id].member_offsets = (uint32_t *)calloc(member_count, sizeof(uint32_t));
                    structs[result_id].member_mstride = (uint32_t *)calloc(member_count, sizeof(uint32_t));

                    if (types[result_id].member_types && structs[result_id].member_names && structs[result_id].member_offsets && structs[result_id].member_mstride) {
                        for (uint32_t i = 0; i < member_count; ++i) {
                            types[result_id].member_types[i] = words[idx + 2u + i];
                            structs[result_id].member_offsets[i] = 0u;
                            structs[result_id].member_mstride[i] = 0u;
                        }
                    }
                }
            } break;

            case SPIRV_OP_TYPEPOINTER:
            {
                const uint32_t result_id = words[idx + 1];
                const uint32_t storage_class = words[idx + 2];
                const uint32_t pointee = words[idx + 3];
                if (result_id < bound) {
                    types[result_id].kind = TYPE_POINTER;
                    types[result_id].elem_type = pointee;
                    types[result_id].count = storage_class;
                }
            } break;

            case SPIRV_OP_VARIABLE:
            {
                const uint32_t result_type = words[idx + 1];
                const uint32_t result_id = words[idx + 2];
                const uint32_t storage_class = words[idx + 3];
                if (result_id < bound) {
                    vars[result_id].pointer_type = result_type;
                    vars[result_id].storage_class = storage_class;
                    vars[result_id].valid = true;
                }
            } break;

            default:
                break;
        }

        idx += (uint32_t)wcount;
    }

    if (!entry_name) {
        entry_name = dup_cstr("main");
    }

    if (out_exec_model) {
        *out_exec_model = exec_model;
    }
    if (out_entry_name) {
        *out_entry_name = entry_name;
    } else {
        free(entry_name);
    }
}

static void parse_pass2(
    const uint32_t *words,
    uint32_t total_words,
    uint32_t bound,
    TypeInfo *types,
    StructExtra *structs,
    char **id_names)
{
    uint32_t idx = 5;
    while (idx < total_words) {
        const uint32_t w0 = words[idx];
        const uint16_t opcode = (uint16_t)(w0 & 0xFFFFu);
        const uint16_t wcount = (uint16_t)(w0 >> 16);

        if (wcount == 0) {
            break;
        }
        if (idx + (uint32_t)wcount > total_words) {
            break;
        }

        switch (opcode) {
            case SPIRV_OP_NAME:
            {
                const uint32_t target_id = words[idx + 1];
                char *name = read_string_operand(words, total_words, idx + 2);
                if (target_id < bound) {
                    free(id_names[target_id]);
                    id_names[target_id] = name;
                } else {
                    free(name);
                }
            } break;

            case SPIRV_OP_MEMBERNAME:
            {
                const uint32_t struct_id = words[idx + 1];
                const uint32_t member_idx = words[idx + 2];
                char *mname = read_string_operand(words, total_words, idx + 3);
                if (struct_id < bound && types[struct_id].kind == TYPE_STRUCT && member_idx < types[struct_id].member_count) {
                    free(structs[struct_id].member_names[member_idx]);
                    structs[struct_id].member_names[member_idx] = mname;
                } else {
                    free(mname);
                }
            } break;

            case SPIRV_OP_MEMBERDECORATE:
            {
                const uint32_t struct_id = words[idx + 1];
                const uint32_t member_idx = words[idx + 2];
                const uint32_t deco = words[idx + 3];
                if (struct_id < bound && types[struct_id].kind == TYPE_STRUCT && member_idx < types[struct_id].member_count && wcount >= 5) {
                    if (deco == SPIRV_DECORATION_OFFSET) {
                        structs[struct_id].member_offsets[member_idx] = words[idx + 4];
                    } else if (deco == SPIRV_DECORATION_MATRIXSTRIDE) {
                        structs[struct_id].member_mstride[member_idx] = words[idx + 4];
                    }
                }
            } break;

            default:
                break;
        }

        idx += (uint32_t)wcount;
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.spv> <output.json>\n", argv[0]);
        return 2;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];

    FILE *f = fopen(in_path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open: %s\n", in_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || (file_size % 4) != 0) {
        fprintf(stderr, "Invalid SPIR-V file size: %ld\n", file_size);
        fclose(f);
        return 1;
    }

    uint32_t *words = (uint32_t *)malloc((size_t)file_size);
    if (!words) {
        fclose(f);
        return 1;
    }

    if (fread(words, 1, (size_t)file_size, f) != (size_t)file_size) {
        fprintf(stderr, "Failed to read: %s\n", in_path);
        free(words);
        fclose(f);
        return 1;
    }
    fclose(f);

    const uint32_t total_words = (uint32_t)file_size / 4u;
    if (total_words < 5) {
        fprintf(stderr, "SPIR-V too small.\n");
        free(words);
        return 1;
    }

    if (words[0] != SPIRV_MAGIC) {
        fprintf(stderr, "Not SPIR-V (bad magic).\n");
        free(words);
        return 1;
    }

    const uint32_t bound = words[3];
    if (bound == 0) {
        fprintf(stderr, "Bad SPIR-V bound.\n");
        free(words);
        return 1;
    }

    char **id_names = (char **)calloc(bound, sizeof(char *));
    TypeInfo *types = (TypeInfo *)calloc(bound, sizeof(TypeInfo));
    StructExtra *structs = (StructExtra *)calloc(bound, sizeof(StructExtra));
    VarInfo *vars = (VarInfo *)calloc(bound, sizeof(VarInfo));

    uint32_t *binding = (uint32_t *)calloc(bound, sizeof(uint32_t));
    uint32_t *dset = (uint32_t *)calloc(bound, sizeof(uint32_t));
    bool *has_binding = (bool *)calloc(bound, sizeof(bool));
    bool *has_dset = (bool *)calloc(bound, sizeof(bool));

    uint32_t *const_u32 = (uint32_t *)calloc(bound, sizeof(uint32_t));
    bool *has_const = (bool *)calloc(bound, sizeof(bool));

    if (!id_names || !types || !structs || !vars || !binding || !dset || !has_binding || !has_dset || !const_u32 || !has_const) {
        fprintf(stderr, "Out of memory.\n");
        free(words);
        free(id_names);
        free(types);
        free(structs);
        free(vars);
        free(binding);
        free(dset);
        free(has_binding);
        free(has_dset);
        free(const_u32);
        free(has_const);
        return 1;
    }

    uint32_t exec_model = 0xFFFFFFFFu;
    char *entry_name = NULL;

    /* Pass 1: build types/vars + collect resource set/binding decorations */
    parse_pass1(
        words,
        total_words,
        bound,
        types,
        structs,
        vars,
        binding,
        dset,
        has_binding,
        has_dset,
        const_u32,
        has_const,
        &exec_model,
        &entry_name);

    /* Pass 2: apply names + member decorations (order-independent) */
    parse_pass2(words, total_words, bound, types, structs, id_names);

    const char *stage_str = "unknown";
    uint32_t expected_set = 0xFFFFFFFFu;
    uint32_t expected_sampled_set = 0xFFFFFFFFu;
    if (exec_model == SPIRV_EXECUTION_MODEL_VERTEX) {
        stage_str = "vertex";
        expected_set = 1u; /* SDL GPU convention for SPIR-V vertex uniform buffers */
        expected_sampled_set = 0u; /* SDL GPU convention for SPIR-V vertex sampled textures */
    } else if (exec_model == SPIRV_EXECUTION_MODEL_FRAGMENT) {
        stage_str = "fragment";
        expected_set = 3u; /* SDL GPU convention for SPIR-V fragment uniform buffers */
        expected_sampled_set = 2u; /* SDL GPU convention for SPIR-V fragment sampled textures */
    }

    /* Collect sampled texture/sampler bindings in expected_sampled_set. SDL expects a
       single "sampler count" describing the texture-sampler pair slots. */
    uint32_t num_samplers = 0;
    {
        bool any = false;
        uint32_t max_binding = 0u;
        for (uint32_t id = 0; id < bound; ++id) {
            if (!vars[id].valid) {
                continue;
            }
            if (vars[id].storage_class != SPIRV_STORAGE_CLASS_UNIFORMCONSTANT) {
                continue;
            }
            if (!has_dset[id] || !has_binding[id]) {
                continue;
            }
            if (expected_sampled_set != 0xFFFFFFFFu && dset[id] != expected_sampled_set) {
                continue;
            }

            const uint32_t ptr_type = vars[id].pointer_type;
            if (ptr_type >= bound || types[ptr_type].kind != TYPE_POINTER) {
                continue;
            }

            const uint32_t pointee = types[ptr_type].elem_type;
            if (pointee >= bound) {
                continue;
            }

            const TypeKind k = types[pointee].kind;
            if (k != TYPE_IMAGE && k != TYPE_SAMPLER && k != TYPE_SAMPLED_IMAGE) {
                continue;
            }

            any = true;
            if (binding[id] > max_binding) {
                max_binding = binding[id];
            }
        }

        if (any) {
            num_samplers = max_binding + 1u;
        }
    }

    if (!entry_name) {
        entry_name = dup_cstr("main");
    }

    /* Collect uniform buffers in expected_set */
    typedef struct UBOut
    {
        uint32_t var_id;
        uint32_t slot;      /* binding number -> SDL uniform slot */
        uint32_t size_bytes;
        uint32_t struct_id;
    } UBOut;

    UBOut *ubs = (UBOut *)calloc(bound, sizeof(UBOut));
    uint32_t ub_count = 0;

    for (uint32_t id = 0; id < bound; ++id) {
        if (!vars[id].valid) {
            continue;
        }
        if (vars[id].storage_class != SPIRV_STORAGE_CLASS_UNIFORM) {
            continue;
        }
        if (!has_dset[id] || !has_binding[id]) {
            continue;
        }
        if (expected_set != 0xFFFFFFFFu && dset[id] != expected_set) {
            continue;
        }

        const uint32_t ptr_type = vars[id].pointer_type;
        if (ptr_type >= bound || types[ptr_type].kind != TYPE_POINTER) {
            continue;
        }

        const uint32_t struct_id = types[ptr_type].elem_type;
        if (struct_id >= bound || types[struct_id].kind != TYPE_STRUCT) {
            continue;
        }

        const uint32_t slot = binding[id];

        /* compute size from member offsets */
        uint32_t max_end = 0u;
        for (uint32_t m = 0; m < types[struct_id].member_count; ++m) {
            const uint32_t mtype = types[struct_id].member_types[m];
            const uint32_t moffset = structs[struct_id].member_offsets[m];
            const uint32_t mstride = structs[struct_id].member_mstride[m];
            const uint32_t msize = std140_size_of_type(types, const_u32, has_const, mtype, mstride);
            if (msize == 0u) {
                continue;
            }
            const uint32_t mend = moffset + msize;
            if (mend > max_end) {
                max_end = mend;
            }
        }

        /* std140 and D3D cbuffers round struct sizes up to 16 bytes */
        const uint32_t size_bytes = round_up_u32(max_end, 16u);

        ubs[ub_count++] = (UBOut){ id, slot, size_bytes, struct_id };
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "Failed to open output: %s\n", out_path);
        free(ubs);
        free(entry_name);
        free(words);
        /* free allocations */
        for (uint32_t i = 0; i < bound; ++i) {
            free(id_names[i]);
            free_struct(&types[i], &structs[i]);
        }
        free(id_names);
        free(types);
        free(structs);
        free(vars);
        free(binding);
        free(dset);
        free(has_binding);
        free(has_dset);
        free(const_u32);
        free(has_const);
        return 1;
    }

    fprintf(out, "{\n");
    fprintf(out, "  \"stage\": \"%s\",\n", stage_str);
    fprintf(out, "  \"entry\": \"%s\",\n", entry_name);
    fprintf(out, "  \"resources\": {\n");
    fprintf(out, "    \"num_samplers\": %u,\n", num_samplers);
    fprintf(out, "    \"num_storage_textures\": 0,\n");
    fprintf(out, "    \"num_storage_buffers\": 0,\n");

    uint32_t num_uniform_buffers = 0;
    for (uint32_t i = 0; i < ub_count; ++i) {
        const uint32_t slot = ubs[i].slot;
        if (slot + 1u > num_uniform_buffers) {
            num_uniform_buffers = slot + 1u;
        }
    }

    fprintf(out, "    \"num_uniform_buffers\": %u,\n", num_uniform_buffers);
    fprintf(out, "    \"uniform_buffers\": [\n");

    for (uint32_t i = 0; i < ub_count; ++i) {
        const uint32_t var_id = ubs[i].var_id;
        const uint32_t struct_id = ubs[i].struct_id;
        const char *name = NULL;
        if (id_names[var_id]) {
            name = id_names[var_id];
        } else if (id_names[struct_id]) {
            name = id_names[struct_id];
        } else {
            name = "UniformBuffer";
        }

        fprintf(out, "      { \"name\": \"%s\", \"slot\": %u, \"size\": %u }%s\n",
            name,
            ubs[i].slot,
            ubs[i].size_bytes,
            (i + 1u < ub_count) ? "," : "");
    }

    fprintf(out, "    ]\n");
    fprintf(out, "  },\n");

    fprintf(out, "  \"params\": [\n");

    bool first_param = true;
    for (uint32_t u = 0; u < ub_count; ++u) {
        const uint32_t struct_id = ubs[u].struct_id;
        for (uint32_t m = 0; m < types[struct_id].member_count; ++m) {
            const uint32_t mtype = types[struct_id].member_types[m];
            const char *ptype = param_type_string(types, mtype);
            if (strcmp(ptype, "unknown") == 0) {
                continue;
            }

            const char *pname = structs[struct_id].member_names[m];
            char fallback_name[64];
            if (!pname || pname[0] == '\0') {
                snprintf(fallback_name, sizeof(fallback_name), "param_%u", m);
                pname = fallback_name;
            }

            const uint32_t poffset = structs[struct_id].member_offsets[m];
            const uint32_t pstride = structs[struct_id].member_mstride[m];
            const uint32_t psize = std140_size_of_type(types, const_u32, has_const, mtype, pstride);

            if (!first_param) {
                fprintf(out, ",\n");
            }
            first_param = false;

            fprintf(out, "    { \"name\": \"%s\", \"type\": \"%s\", \"slot\": %u, \"offset\": %u, \"size\": %u }",
                pname,
                ptype,
                ubs[u].slot,
                poffset,
                psize);
        }
    }

    fprintf(out, "\n  ]\n");
    fprintf(out, "}\n");

    fclose(out);

    free(ubs);
    free(entry_name);

    free(words);

    for (uint32_t i = 0; i < bound; ++i) {
        free(id_names[i]);
        free_struct(&types[i], &structs[i]);
    }

    free(id_names);
    free(types);
    free(structs);
    free(vars);
    free(binding);
    free(dset);
    free(has_binding);
    free(has_dset);
    free(const_u32);
    free(has_const);

    return 0;
}
