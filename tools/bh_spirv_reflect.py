"""bh_spirv_reflect.py

Minimal SPIR-V reflection tool (Python) tailored for OpenBHOP.

Why this exists:
  - When cross-compiling (e.g. to Emscripten/WebAssembly) CMake cannot build a
    native host tool and then run it during the build.
  - This script mirrors the JSON output format of tools/bh_spirv_reflect.c so
    the runtime shader/material code can stay unchanged.

Usage:
  python3 bh_spirv_reflect.py <input.spv> <output.json>
"""

from __future__ import annotations

import json
import struct
import sys
from dataclasses import dataclass, field
from enum import IntEnum
from pathlib import Path
from typing import Dict, List, Optional, Tuple


SPIRV_MAGIC = 0x07230203


class Op(IntEnum):
    Name = 5
    MemberName = 6
    EntryPoint = 15
    Decorate = 71
    MemberDecorate = 72
    TypeInt = 21
    TypeFloat = 22
    TypeVector = 23
    TypeMatrix = 24
    TypeImage = 25
    TypeSampler = 26
    TypeSampledImage = 27
    TypeArray = 28
    TypeStruct = 30
    TypePointer = 32
    Constant = 43
    Variable = 59


class ExecutionModel(IntEnum):
    Vertex = 0
    Fragment = 4


class StorageClass(IntEnum):
    UniformConstant = 0
    Uniform = 2


class Decoration(IntEnum):
    Binding = 33
    DescriptorSet = 34
    Offset = 35
    MatrixStride = 7


class TypeKind(IntEnum):
    Unknown = 0
    Int = 1
    Float = 2
    Vector = 3
    Matrix = 4
    Image = 5
    Sampler = 6
    SampledImage = 7
    Array = 8
    Struct = 9
    Pointer = 10


@dataclass
class TypeInfo:
    kind: TypeKind = TypeKind.Unknown
    elem_type: int = 0  # component, array element, pointer pointee
    count: int = 0      # vector components, matrix columns, array length, pointer storage class
    width_bits: int = 0
    signedness: int = 0
    member_types: List[int] = field(default_factory=list)
    # For arrays we keep the length id so we can resolve it after parsing constants.
    array_len_id: int = 0


@dataclass
class StructExtra:
    member_names: List[Optional[str]] = field(default_factory=list)
    member_offsets: List[int] = field(default_factory=list)
    member_mstride: List[int] = field(default_factory=list)


@dataclass
class VarInfo:
    pointer_type: int = 0
    storage_class: int = 0
    valid: bool = False


def _read_spirv_words(path: Path) -> Tuple[List[int], int]:
    data = path.read_bytes()
    if len(data) < 20 or (len(data) % 4) != 0:
        raise ValueError("Invalid SPIR-V file size")
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    if words[0] != SPIRV_MAGIC:
        raise ValueError("Not a SPIR-V binary (bad magic)")
    bound = words[3]
    if bound <= 0:
        raise ValueError("Bad SPIR-V id bound")
    return words, bound


def _decode_cstr_from_words(words: List[int], start_word: int, total_words: int) -> str:
    # SPIR-V strings are packed into 32-bit words, nul-terminated.
    raw = b"".join(struct.pack("<I", w) for w in words[start_word:total_words])
    nul = raw.find(b"\x00")
    if nul == -1:
        return raw.decode("utf-8", errors="replace")
    return raw[:nul].decode("utf-8", errors="replace")


def _round_up_u32(v: int, alignment: int) -> int:
    if alignment <= 0:
        return v
    mask = alignment - 1
    return (v + mask) & ~mask


def _is_float32(types: List[TypeInfo], type_id: int) -> bool:
    t = types[type_id]
    return t.kind == TypeKind.Float and t.width_bits == 32


def _std140_size_of_type(
    types: List[TypeInfo],
    const_u32: List[int],
    has_const: List[bool],
    type_id: int,
    matrix_stride_opt: int,
) -> int:
    t = types[type_id]
    if t.kind in (TypeKind.Float, TypeKind.Int):
        return 4

    if t.kind == TypeKind.Vector:
        if not _is_float32(types, t.elem_type):
            return 0
        if t.count == 2:
            return 8
        if t.count in (3, 4):
            return 16
        return 0

    if t.kind == TypeKind.Matrix:
        stride = matrix_stride_opt if matrix_stride_opt != 0 else 16
        return stride * t.count

    if t.kind == TypeKind.Array:
        elem_size = _std140_size_of_type(types, const_u32, has_const, t.elem_type, 0)
        if elem_size == 0:
            return 0
        # Minimal handling (matches C tool): element size times count, no alignment tweaks.
        return elem_size * t.count

    if t.kind == TypeKind.Struct:
        # Size determined by member offsets externally.
        return 0

    return 0


def _param_type_string(types: List[TypeInfo], type_id: int) -> str:
    t = types[type_id]
    if t.kind == TypeKind.Float:
        return "float"
    if t.kind == TypeKind.Vector and _is_float32(types, t.elem_type):
        if t.count == 2:
            return "float2"
        if t.count == 3:
            return "float3"
        if t.count == 4:
            return "float4"
    if t.kind == TypeKind.Matrix:
        col = types[t.elem_type]
        if t.count == 4 and col.kind == TypeKind.Vector and col.count == 4 and _is_float32(types, col.elem_type):
            return "mat4"
    return "unknown"


def reflect_spirv_to_json(in_path: Path, out_path: Path) -> None:
    words, bound = _read_spirv_words(in_path)
    total_words = len(words)

    types: List[TypeInfo] = [TypeInfo() for _ in range(bound)]
    structs: List[StructExtra] = [StructExtra() for _ in range(bound)]
    vars_: List[VarInfo] = [VarInfo() for _ in range(bound)]
    id_names: List[Optional[str]] = [None for _ in range(bound)]

    binding: List[int] = [0 for _ in range(bound)]
    dset: List[int] = [0 for _ in range(bound)]
    has_binding: List[bool] = [False for _ in range(bound)]
    has_dset: List[bool] = [False for _ in range(bound)]

    const_u32: List[int] = [0 for _ in range(bound)]
    has_const: List[bool] = [False for _ in range(bound)]

    exec_model: Optional[int] = None
    entry_name: Optional[str] = None

    # ------------------------------------------------------------------
    # Pass 1: types, variables, decorations, constants, entry point
    # ------------------------------------------------------------------
    i = 5
    while i < total_words:
        word0 = words[i]
        op = word0 & 0xFFFF
        wc = word0 >> 16
        if wc == 0:
            raise ValueError("Invalid instruction word count")

        if op == Op.EntryPoint:
            # operands: ExecutionModel, entrypoint id, name, interface ids...
            if wc >= 4:
                exec_model = words[i + 1]
                # entry_id = words[i + 2]
                entry_name = _decode_cstr_from_words(words, i + 3, i + wc)

        elif op == Op.TypeInt:
            if wc >= 4:
                rid = words[i + 1]
                types[rid] = TypeInfo(kind=TypeKind.Int, width_bits=words[i + 2], signedness=words[i + 3])

        elif op == Op.TypeFloat:
            if wc >= 3:
                rid = words[i + 1]
                types[rid] = TypeInfo(kind=TypeKind.Float, width_bits=words[i + 2])

        elif op == Op.TypeVector:
            if wc >= 4:
                rid = words[i + 1]
                comp = words[i + 2]
                cnt = words[i + 3]
                types[rid] = TypeInfo(kind=TypeKind.Vector, elem_type=comp, count=cnt)

        elif op == Op.TypeMatrix:
            if wc >= 4:
                rid = words[i + 1]
                col_type = words[i + 2]
                cols = words[i + 3]
                types[rid] = TypeInfo(kind=TypeKind.Matrix, elem_type=col_type, count=cols)

        elif op == Op.TypeImage:
            if wc >= 2:
                rid = words[i + 1]
                types[rid] = TypeInfo(kind=TypeKind.Image)

        elif op == Op.TypeSampler:
            if wc >= 2:
                rid = words[i + 1]
                types[rid] = TypeInfo(kind=TypeKind.Sampler)

        elif op == Op.TypeSampledImage:
            if wc >= 3:
                rid = words[i + 1]
                img = words[i + 2]
                types[rid] = TypeInfo(kind=TypeKind.SampledImage, elem_type=img)

        elif op == Op.TypeArray:
            if wc >= 4:
                rid = words[i + 1]
                elem = words[i + 2]
                length_id = words[i + 3]
                cnt = const_u32[length_id] if (length_id < bound and has_const[length_id]) else 0
                types[rid] = TypeInfo(kind=TypeKind.Array, elem_type=elem, count=cnt, array_len_id=length_id)

        elif op == Op.TypeStruct:
            if wc >= 2:
                rid = words[i + 1]
                members = [words[i + 2 + k] for k in range(max(0, wc - 2))]
                types[rid] = TypeInfo(kind=TypeKind.Struct, member_types=members)
                # Prepare parallel arrays in StructExtra.
                structs[rid].member_names = [None for _ in range(len(members))]
                structs[rid].member_offsets = [0 for _ in range(len(members))]
                structs[rid].member_mstride = [0 for _ in range(len(members))]

        elif op == Op.TypePointer:
            if wc >= 4:
                rid = words[i + 1]
                storage = words[i + 2]
                pointee = words[i + 3]
                types[rid] = TypeInfo(kind=TypeKind.Pointer, elem_type=pointee, count=storage)

        elif op == Op.Constant:
            # operands: result_type, result_id, value...
            if wc >= 4:
                result_id = words[i + 2]
                value = words[i + 3]
                if result_id < bound:
                    const_u32[result_id] = value
                    has_const[result_id] = True

        elif op == Op.Variable:
            # operands: result_type (pointer type), result_id, storage_class, initializer...
            if wc >= 4:
                ptr_type = words[i + 1]
                result_id = words[i + 2]
                storage = words[i + 3]
                if result_id < bound:
                    vars_[result_id] = VarInfo(pointer_type=ptr_type, storage_class=storage, valid=True)

        elif op == Op.Decorate:
            # operands: target, decoration, literals...
            if wc >= 4:
                target = words[i + 1]
                deco = words[i + 2]
                lit = words[i + 3]
                if target < bound:
                    if deco == Decoration.Binding:
                        binding[target] = lit
                        has_binding[target] = True
                    elif deco == Decoration.DescriptorSet:
                        dset[target] = lit
                        has_dset[target] = True

        i += wc

    # Resolve any array counts that depended on constants defined later.
    for tid in range(bound):
        t = types[tid]
        if t.kind == TypeKind.Array and t.count == 0 and t.array_len_id and t.array_len_id < bound:
            if has_const[t.array_len_id]:
                t.count = const_u32[t.array_len_id]

    # ------------------------------------------------------------------
    # Pass 2: names and member decorations
    # ------------------------------------------------------------------
    i = 5
    while i < total_words:
        word0 = words[i]
        op = word0 & 0xFFFF
        wc = word0 >> 16
        if wc == 0:
            raise ValueError("Invalid instruction word count")

        if op == Op.Name:
            if wc >= 3:
                target = words[i + 1]
                if target < bound:
                    id_names[target] = _decode_cstr_from_words(words, i + 2, i + wc)

        elif op == Op.MemberName:
            if wc >= 4:
                type_id = words[i + 1]
                member = words[i + 2]
                if type_id < bound:
                    name = _decode_cstr_from_words(words, i + 3, i + wc)
                    sx = structs[type_id]
                    if member < len(sx.member_names):
                        sx.member_names[member] = name

        elif op == Op.MemberDecorate:
            if wc >= 5:
                type_id = words[i + 1]
                member = words[i + 2]
                deco = words[i + 3]
                lit = words[i + 4]
                if type_id < bound:
                    sx = structs[type_id]
                    if member < len(sx.member_offsets):
                        if deco == Decoration.Offset:
                            sx.member_offsets[member] = lit
                        elif deco == Decoration.MatrixStride:
                            sx.member_mstride[member] = lit

        i += wc

    # ------------------------------------------------------------------
    # Output
    # ------------------------------------------------------------------
    stage_str = "unknown"
    expected_set = None
    expected_sampled_set = None

    if exec_model == ExecutionModel.Vertex:
        stage_str = "vertex"
        expected_set = 1
        expected_sampled_set = 0
    elif exec_model == ExecutionModel.Fragment:
        stage_str = "fragment"
        expected_set = 3
        expected_sampled_set = 2

    if not entry_name:
        entry_name = "main"

    # Sampler count (texture/sampler pairs) in the expected sampled set.
    num_samplers = 0
    any_sampler = False
    max_binding = 0
    for vid in range(bound):
        v = vars_[vid]
        if not v.valid:
            continue
        if v.storage_class != StorageClass.UniformConstant:
            continue
        if not (has_dset[vid] and has_binding[vid]):
            continue
        if expected_sampled_set is not None and dset[vid] != expected_sampled_set:
            continue

        ptr_type = v.pointer_type
        if ptr_type >= bound or types[ptr_type].kind != TypeKind.Pointer:
            continue
        pointee = types[ptr_type].elem_type
        if pointee >= bound:
            continue
        k = types[pointee].kind
        if k not in (TypeKind.Image, TypeKind.Sampler, TypeKind.SampledImage):
            continue

        any_sampler = True
        max_binding = max(max_binding, binding[vid])

    if any_sampler:
        num_samplers = max_binding + 1

    # Uniform buffers
    ubs: List[Dict[str, int]] = []
    for vid in range(bound):
        v = vars_[vid]
        if not v.valid:
            continue
        if v.storage_class != StorageClass.Uniform:
            continue
        if not (has_dset[vid] and has_binding[vid]):
            continue
        if expected_set is not None and dset[vid] != expected_set:
            continue

        ptr_type = v.pointer_type
        if ptr_type >= bound or types[ptr_type].kind != TypeKind.Pointer:
            continue
        struct_id = types[ptr_type].elem_type
        if struct_id >= bound or types[struct_id].kind != TypeKind.Struct:
            continue

        slot = binding[vid]
        sx = structs[struct_id]

        max_end = 0
        for m, mtype in enumerate(types[struct_id].member_types):
            moffset = sx.member_offsets[m] if m < len(sx.member_offsets) else 0
            mstride = sx.member_mstride[m] if m < len(sx.member_mstride) else 0
            msize = _std140_size_of_type(types, const_u32, has_const, mtype, mstride)
            if msize == 0:
                continue
            max_end = max(max_end, moffset + msize)

        size_bytes = _round_up_u32(max_end, 16)
        ubs.append({"var_id": vid, "struct_id": struct_id, "slot": slot, "size": size_bytes})

    num_uniform_buffers = 0
    for ub in ubs:
        num_uniform_buffers = max(num_uniform_buffers, int(ub["slot"]) + 1)

    uniform_buffers_json: List[Dict[str, object]] = []
    params_json: List[Dict[str, object]] = []

    for ub in ubs:
        vid = int(ub["var_id"])
        sid = int(ub["struct_id"])
        name = id_names[vid] or id_names[sid] or "UniformBuffer"

        uniform_buffers_json.append({"name": name, "slot": int(ub["slot"]), "size": int(ub["size"])})

        sx = structs[sid]
        member_types = types[sid].member_types
        for m, mtype in enumerate(member_types):
            ptype = _param_type_string(types, mtype)
            if ptype == "unknown":
                continue
            pname = None
            if m < len(sx.member_names):
                pname = sx.member_names[m]
            if not pname:
                pname = f"param_{m}"

            poffset = sx.member_offsets[m] if m < len(sx.member_offsets) else 0
            pstride = sx.member_mstride[m] if m < len(sx.member_mstride) else 0
            psize = _std140_size_of_type(types, const_u32, has_const, mtype, pstride)

            params_json.append(
                {
                    "name": pname,
                    "type": ptype,
                    "slot": int(ub["slot"]),
                    "offset": int(poffset),
                    "size": int(psize),
                }
            )

    out_obj = {
        "stage": stage_str,
        "entry": entry_name,
        "resources": {
            "num_samplers": int(num_samplers),
            "num_storage_textures": 0,
            "num_storage_buffers": 0,
            "num_uniform_buffers": int(num_uniform_buffers),
            "uniform_buffers": uniform_buffers_json,
        },
        "params": params_json,
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(out_obj, indent=2) + "\n", encoding="utf-8")


def main(argv: List[str]) -> int:
    if len(argv) != 3:
        print(f"Usage: {argv[0]} <input.spv> <output.json>", file=sys.stderr)
        return 1

    in_path = Path(argv[1])
    out_path = Path(argv[2])

    try:
        reflect_spirv_to_json(in_path, out_path)
    except Exception as e:
        print(f"bh_spirv_reflect.py: error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
