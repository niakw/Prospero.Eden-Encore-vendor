#!/usr/bin/env python3
"""
extract_api.py — Parse opengnm C headers and generate Markdown API reference pages.

Usage:
    python3 docs/scripts/extract_api.py

Reads headers from include/ and generates reference markdown in docs/reference/.
This is a lightweight regex-based extractor, not a full C parser. It handles:
  - Function declarations (return_type name(params);)
  - Struct definitions (typedef struct { ... } Name;)
  - Enum definitions (typedef enum { ... } Name;)
  - Static inline functions
  - #define constants

The generated pages are meant to be hand-annotated after generation.
"""

import os
import re
import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
INCLUDE_DIR = REPO_ROOT / "include"
OUTPUT_DIR = REPO_ROOT / "docs" / "reference"


@dataclass
class EnumDef:
    name: str
    values: List[tuple] = field(default_factory=list)  # (name, value, comment)
    file: str = ""


@dataclass
class StructDef:
    name: str
    fields: List[tuple] = field(default_factory=list)  # (type, name, bits, comment)
    size_assert: Optional[str] = None
    file: str = ""


@dataclass
class FuncDef:
    name: str
    return_type: str
    params: List[tuple] = field(default_factory=list)  # (type, name)
    is_inline: bool = False
    is_static: bool = False
    file: str = ""
    line: int = 0


@dataclass
class DefineDef:
    name: str
    value: str
    file: str = ""


def strip_comments(text: str) -> str:
    """Remove C comments while preserving line structure."""
    # Remove block comments
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    # Remove line comments
    text = re.sub(r'//[^\n]*', '', text)
    return text


def clean_ws(text: str) -> str:
    """Normalize whitespace."""
    return re.sub(r'\s+', ' ', text).strip()


def parse_enums(text: str, filename: str) -> List[EnumDef]:
    """Extract typedef enum definitions."""
    enums = []
    # Match: typedef enum { ... } Name;
    pattern = re.compile(
        r'typedef\s+enum\s*\{([^}]*)\}\s*(\w+)\s*;',
        re.DOTALL
    )
    for m in pattern.finditer(text):
        body = m.group(1)
        name = m.group(2)
        values = []
        for line in body.split(','):
            line = line.strip()
            if not line or line.startswith('/*') or line.startswith('//'):
                continue
            # Match: NAME = value, or just NAME
            vm = re.match(r'(\w+)\s*(?:=\s*([^,/\s]+))?', line)
            if vm:
                vname = vm.group(1)
                vval = vm.group(2) or ""
                values.append((vname, vval))
        enums.append(EnumDef(name=name, values=values, file=filename))
    return enums


def parse_structs(text: str, filename: str) -> List[StructDef]:
    """Extract typedef struct definitions with fields."""
    structs = []
    # Match: typedef struct { ... } Name;  (non-greedy, handles nested braces)
    # We need to handle nested braces for unions
    pattern = re.compile(
        r'typedef\s+struct\s*\{',
        re.DOTALL
    )
    for m in pattern.finditer(text):
        # Find matching closing brace
        start = m.end()
        depth = 1
        i = start
        while i < len(text) and depth > 0:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        if depth != 0:
            continue
        body = text[start:i-1]
        # Get struct name: } Name;
        rest = text[i:]
        nm = re.match(r'\s*(\w+)\s*;', rest)
        if not nm:
            continue
        name = nm.group(1)

        # Parse fields — simplified: look for type name; patterns
        fields = []
        # Remove nested union/struct bodies for field parsing
        clean_body = re.sub(r'\{[^}]*\}', '', body)
        for line in clean_body.split(';'):
            line = line.strip()
            if not line:
                continue
            # Skip _Static_assert
            if line.startswith('_Static_assert'):
                continue
            # Parse: type name : bits  or  type name
            # Handle bitfields: uint32_t field : 3;
            fm = re.match(r'(.+?)\s+(\w+)\s*(?::\s*(\d+))?\s*$', line)
            if fm:
                ftype = clean_ws(fm.group(1))
                fname = fm.group(2)
                fbits = fm.group(3) or ""
                fields.append((ftype, fname, fbits))

        # Check for _Static_assert(sizeof...)
        sa = re.search(r'_Static_assert\(sizeof\((\w+)\)\s*==\s*(0x[0-9a-fA-F]+|\d+)', text[i:i+200])
        size_assert = sa.group(2) if sa and sa.group(1) == name else None

        structs.append(StructDef(name=name, fields=fields, size_assert=size_assert, file=filename))
    return structs


def parse_functions(text: str, filename: str) -> List[FuncDef]:
    """Extract function declarations from header text."""
    funcs = []
    # Remove static inline functions first (parse separately)
    # Match: [static] [inline] return_type name(params);
    # Also match: return_type PS4_SYSV_ABI name(params);
    # And: static inline return_type name(params) { ... }

    # Pattern for regular function declarations
    # We look for: type [*] name ( params ) ;
    pattern = re.compile(
        r'(?:^|\n)\s*'
        r'((?:static\s+)?(?:inline\s+)?)?'
        r'([\w\s\*]+?)\s+'  # return type
        r'(PS4_SYSV_ABI\s+)?'
        r'(\w+)\s*'  # function name
        r'\(([^;{}]*)\)\s*'
        r'(?:PS4_SYSV_ABI)?\s*'
        r'(?:(\{)|;)',  # either { for inline body or ; for declaration
        re.DOTALL
    )

    lines = text.split('\n')
    for m in pattern.finditer(text):
        prefix = m.group(1) or ""
        ret_type = clean_ws(m.group(2))
        name = m.group(4)
        params_str = m.group(5).strip()
        has_body = m.group(6) is not None

        # Skip if return type looks like a control statement
        if ret_type in ('if', 'for', 'while', 'switch', 'return', 'else', 'do'):
            continue
        # Skip macro-like names
        if name.startswith('_') and name not in ('_Static_assert',):
            continue
        # Skip if it's actually a struct/enum field
        if ret_type in ('typedef', 'struct', 'enum', 'union'):
            continue

        is_static = 'static' in prefix
        is_inline = 'inline' in prefix or has_body

        # Parse parameters
        params = []
        if params_str and params_str != 'void':
            for p in split_params(params_str):
                p = clean_ws(p)
                if not p:
                    continue
                # Split into type and name
                pm = re.match(r'(.+?)[\s\*]+(\w+)\s*$', p)
                if pm:
                    params.append((clean_ws(pm.group(1)), pm.group(2)))
                else:
                    params.append((p, ''))

        # Find line number
        pos = m.start()
        line_num = text[:pos].count('\n') + 1

        funcs.append(FuncDef(
            name=name,
            return_type=ret_type,
            params=params,
            is_inline=is_inline,
            is_static=is_static,
            file=filename,
            line=line_num,
        ))

    return funcs


def split_params(params_str: str) -> List[str]:
    """Split function parameters, respecting nested parens."""
    parts = []
    depth = 0
    current = ""
    for ch in params_str:
        if ch == '(' or ch == '[':
            depth += 1
            current += ch
        elif ch == ')' or ch == ']':
            depth -= 1
            current += ch
        elif ch == ',' and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += ch
    if current.strip():
        parts.append(current)
    return parts


def parse_defines(text: str, filename: str) -> List[DefineDef]:
    """Extract #define constants."""
    defines = []
    pattern = re.compile(r'#define\s+(\w+)\s+([^\n]+)')
    for m in pattern.finditer(text):
        name = m.group(1)
        value = clean_ws(m.group(2))
        # Skip include guards and function-like macros
        if name.endswith('_H_') or name.endswith('_H') or '(' in name:
            continue
        defines.append(DefineDef(name=name, value=value, file=filename))
    return defines


def format_func_signature(f: FuncDef) -> str:
    """Format a function signature for markdown."""
    params = []
    for ptype, pname in f.params:
        if pname:
            params.append(f"{ptype} {pname}")
        else:
            params.append(ptype)
    param_str = ", ".join(params) if params else "void"
    prefix = ""
    if f.is_static:
        prefix += "static "
    if f.is_inline:
        prefix += "inline "
    return f"{prefix}{f.return_type} {f.name}({param_str})"


def generate_enum_table(enum: EnumDef) -> str:
    """Generate a markdown table for an enum."""
    if not enum.values:
        return ""
    lines = ["| Constant | Value |", "|----------|-------|"]
    for vname, vval in enum.values:
        lines.append(f"| `{vname}` | `{vval}` |" if vval else f"| `{vname}` | |")
    return "\n".join(lines)


def generate_struct_table(struct: StructDef) -> str:
    """Generate a markdown table for a struct."""
    if not struct.fields:
        return ""
    lines = ["| Type | Field | Bits |", "|------|-------|------|"]
    for ftype, fname, fbits in struct.fields:
        bits_col = f"`{fbits}`" if fbits else ""
        lines.append(f"| `{ftype}` | `{fname}` | {bits_col} |")
    return "\n".join(lines)


def main():
    if not INCLUDE_DIR.exists():
        print(f"Error: include directory not found at {INCLUDE_DIR}", file=sys.stderr)
        sys.exit(1)

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Headers to parse (in dependency order)
    headers = [
        "gnm_types.h",
        "gnm_error.h",
        "gnm_dataformat.h",
        "gnm_buffer.h",
        "gnm_texture.h",
        "gnm_sampler.h",
        "gnm_rendertarget.h",
        "gnm_depthrendertarget.h",
        "gnm_controls.h",
        "gnm_shader.h",
        "gnm_shaderbinary.h",
        "gnm_commandbuffer.h",
        "gnm_drawcommandbuffer.h",
        "gnmdriver.h",
        "gpuaddr.h",
        "platform.h",
        "gnm_helpers.h",
        "gnm_strings.h",
    ]

    all_enums = []
    all_structs = []
    all_funcs = []
    all_defines = []

    for header in headers:
        path = INCLUDE_DIR / header
        if not path.exists():
            print(f"  skip: {header} (not found)")
            continue
        raw = path.read_text()
        text = strip_comments(raw)
        enums = parse_enums(text, header)
        structs = parse_structs(text, header)
        funcs = parse_functions(text, header)
        defines = parse_defines(raw, header)  # use raw for defines to keep comments

        all_enums.extend(enums)
        all_structs.extend(structs)
        all_funcs.extend(funcs)
        all_defines.extend(defines)
        print(f"  {header}: {len(enums)} enums, {len(structs)} structs, {len(funcs)} funcs, {len(defines)} defines")

    # Generate summary
    print(f"\nTotal: {len(all_enums)} enums, {len(all_structs)} structs, {len(all_funcs)} functions, {len(all_defines)} defines")

    # Write a summary file that can be used for reference pages
    summary_path = OUTPUT_DIR / "_api_summary.txt"
    with open(summary_path, 'w') as f:
        f.write(f"opengnm API Summary\n{'=' * 60}\n\n")
        f.write(f"Enums: {len(all_enums)}\n")
        for e in all_enums:
            f.write(f"  {e.name} ({e.file}): {len(e.values)} values\n")
        f.write(f"\nStructs: {len(all_structs)}\n")
        for s in all_structs:
            sa = f" [size={s.size_assert}]" if s.size_assert else ""
            f.write(f"  {s.name} ({s.file}): {len(s.fields)} fields{sa}\n")
        f.write(f"\nFunctions: {len(all_funcs)}\n")
        for fn in all_funcs:
            tag = " (inline)" if fn.is_inline else ""
            f.write(f"  {fn.name} ({fn.file}:{fn.line}){tag}\n")
        f.write(f"\nDefines: {len(all_defines)}\n")
        for d in all_defines:
            f.write(f"  {d.name} = {d.value} ({d.file})\n")

    print(f"\nSummary written to {summary_path}")
    print("Reference pages should be hand-written using this data as a base.")


if __name__ == "__main__":
    main()
