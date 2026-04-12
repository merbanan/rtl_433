"""
Code generator for the proto-compiler DSL.

Entry point: compile_decoder(instance) -> str

Reads a Decoder instance and emits a complete C source file.
C standard: C23 for generated files — uses `static constexpr`.
"""

from __future__ import annotations

from contextlib import contextmanager
from typing import Any

from proto_compiler.dsl import (
    BinaryExpr,
    BitbufferPipeline,
    BitsSpec,
    CallableEntry,
    DecodeFail,
    Decoder,
    Dispatcher,
    ModulationConfig,
    Expr,
    ExprBoolLiteral,
    ExprFloatLiteral,
    ExprIntLiteral,
    FieldRef,
    FieldRefSubscript,
    FindRepeatedRow,
    FirstValid,
    Invert,
    JsonRecord,
    LfsrDigest8Expr,
    LiteralSpec,
    ManchesterDecode,
    Modulation,
    Protocol,
    Reflect,
    RepeatSpec,
    Rev8Expr,
    RowFieldAccess,
    RowsParamType,
    RowsSpec,
    SearchPreamble,
    SkipBits,
    UnaryExpr,
    _call_inline,
)


# ---------------------------------------------------------------------------
# CodeSnippet
# ---------------------------------------------------------------------------


class CodeSnippet:
    """Mutable list of indented lines. Combine snippets by adding them."""

    def __init__(self, indent: int = 0) -> None:
        self._lines: list[str] = []
        self._indent: int = indent

    # --- building ---

    def append_line(self, text: str = "") -> None:
        """Append one line at the current indentation level."""
        if text:
            self._lines.append(" " * self._indent + text)
        else:
            self._lines.append("")

    def extend(self, other: "CodeSnippet") -> None:
        """Append all lines from *other*, prefixing each non-empty line with self's current indent."""
        prefix = " " * self._indent
        for line in other._lines:
            self._lines.append(prefix + line if line else "")

    def __iadd__(self, other: "CodeSnippet") -> "CodeSnippet":
        self.extend(other)
        return self

    # --- indentation ---

    @contextmanager
    def with_indent(self, amount: int = 2):
        """Temporarily increase indentation level."""
        self._indent += amount
        try:
            yield self
        finally:
            self._indent -= amount

    @contextmanager
    def block(self, lead: str):
        """Emit `lead {`, indent, yield, dedent, emit `}`."""
        self.append_line(lead + " {")
        with self.with_indent():
            yield self
        self.append_line("}")

    # --- output ---

    def lines(self) -> list[str]:
        return list(self._lines)

    def render(self) -> str:
        return "\n".join(self._lines)


class ElifLadder:
    """Builds an `if (...) { } else if (...) { } ...` chain into its own snippet.

    Use as a context manager. Each call to ``branch(condition)`` opens a new
    branch and is itself a context manager that yields the inner snippet at
    the indented body level. Exiting the outer ``with`` emits the closing ``}``.
    """

    def __init__(self) -> None:
        self.snippet = CodeSnippet()
        self._count = 0

    def __enter__(self) -> "ElifLadder":
        return self

    def __exit__(self, *_: object) -> None:
        if self._count > 0:
            self.snippet.append_line("}")

    @contextmanager
    def branch(self, condition: str):
        keyword = "if" if self._count == 0 else "} else if"
        self.snippet.append_line(f"{keyword} ({condition}) {{")
        self._count += 1
        with self.snippet.with_indent():
            yield self.snippet


# ---------------------------------------------------------------------------
# Expression rendering
# ---------------------------------------------------------------------------


def emit_expr(node: Any) -> str:
    """Render a DSL expression tree to a C expression string."""
    if isinstance(node, ExprBoolLiteral):
        return "1" if node.value else "0"
    if isinstance(node, ExprIntLiteral):
        v = node.value
        if v < 0:
            return str(v)
        if v > 9:
            return hex(v)
        return str(v)
    if isinstance(node, ExprFloatLiteral):
        return repr(node.value)
    if isinstance(node, RowFieldAccess):
        return f"({node.rows_name}_{node.field_name}[{node.row_index}])"
    if isinstance(node, FieldRefSubscript):
        return f"({node.base.name}[{node.index}])"
    if isinstance(node, FieldRef):
        return node.name
    if isinstance(node, BinaryExpr):
        left = emit_expr(node.left)
        right = emit_expr(node.right)
        return f"({left} {node.op} {right})"
    if isinstance(node, UnaryExpr):
        operand = emit_expr(node.operand)
        return f"({node.op}{operand})"
    if isinstance(node, Rev8Expr):
        operand = emit_expr(node.operand)
        return f"(reverse8({operand}))"
    if isinstance(node, LfsrDigest8Expr):
        byte_strs = ", ".join(emit_expr(b) for b in node.bytes_args)
        n = len(node.bytes_args)
        gen = hex(node.gen)
        key = hex(node.key)
        return f"(lfsr_digest8((uint8_t const[]){{{byte_strs}}}, {n}, {gen}, {key}))"
    raise TypeError(f"emit_expr: unknown node type {type(node).__name__}: {node!r}")


# ---------------------------------------------------------------------------
# C type helpers
# ---------------------------------------------------------------------------


def _c_type(py_type: Any) -> str:
    if py_type is float:
        return "float"
    if py_type is str:
        return "const char *"
    if py_type is bool:
        return "bool"
    if py_type is BitbufferPipeline:
        return "bitbuffer_t *"
    return "int"


def _data_type(py_type: Any) -> str:
    if py_type is float:
        return "DATA_DOUBLE"
    if py_type is str:
        return "DATA_STRING"
    return "DATA_INT"


# ---------------------------------------------------------------------------
# Modulation enum → C name
# ---------------------------------------------------------------------------


def _modulation_c_name(mod: Modulation) -> str:
    return mod.name  # e.g. "FSK_PULSE_PCM"


# ---------------------------------------------------------------------------
# Decoder name (class name of the instance)
# ---------------------------------------------------------------------------


def _decoder_name(instance: Any) -> str:
    return type(instance).__name__


# ---------------------------------------------------------------------------
# Pipeline code generation
# ---------------------------------------------------------------------------


def _preamble_bytes(pattern: int, bit_length: int) -> list[int]:
    """Convert an integer pattern to big-endian bytes covering bit_length bits."""
    n_bytes = (bit_length + 7) // 8
    result = []
    for i in range(n_bytes - 1, -1, -1):
        result.append((pattern >> (i * 8)) & 0xFF)
    return result


def emit_pipeline(steps: list) -> tuple[CodeSnippet, str, str]:
    """
    Emit pipeline C code into a CodeSnippet.

    Returns (snippet, row_var, offset_var) where:
    - snippet: CodeSnippet containing the emitted C lines
    - row_var: C variable holding the current row (may be "0" literal or "row")
    - offset_var: C variable/expression holding the starting bit offset
    """
    snippet = CodeSnippet()
    has_row_var = False
    row_var = "0"
    offset_var = "0"
    after_manchester = False

    for step in steps:
        if isinstance(step, Invert):
            snippet.append_line("bitbuffer_invert(bitbuffer);")

        elif isinstance(step, Reflect):
            if after_manchester:
                snippet.append_line(
                    "reflect_bytes(bitbuffer->bb[0], (bitbuffer->bits_per_row[0] + 7) / 8);"
                )
            else:
                snippet.append_line("for (int i = 0; i < bitbuffer->num_rows; ++i)")
                with snippet.with_indent(4):
                    snippet.append_line(
                        "reflect_bytes(bitbuffer->bb[i], (bitbuffer->bits_per_row[i] + 7) / 8);"
                    )

        elif isinstance(step, FindRepeatedRow):
            snippet.append_line(
                f"int row = bitbuffer_find_repeated_row(bitbuffer, {step.min_repeats}, {step.min_bits});"
            )
            snippet.append_line("if (row < 0)")
            with snippet.with_indent(4):
                snippet.append_line("return DECODE_ABORT_EARLY;")
            if step.max_bits is None:
                snippet.append_line(
                    f"if (bitbuffer->bits_per_row[row] != {step.min_bits})"
                )
                with snippet.with_indent(4):
                    snippet.append_line("return DECODE_ABORT_LENGTH;")
            else:
                snippet.append_line(
                    f"if (bitbuffer->bits_per_row[row] > {step.max_bits})"
                )
                with snippet.with_indent(4):
                    snippet.append_line("return DECODE_ABORT_LENGTH;")
            has_row_var = True
            row_var = "row"
            offset_var = "0"

        elif isinstance(step, SearchPreamble):
            bit_length = step.resolved_bit_length
            byte_vals = _preamble_bytes(step.pattern, bit_length)
            byte_str = ", ".join(f"0x{b:02x}" for b in byte_vals)
            snippet.append_line(f"uint8_t const preamble[] = {{ {byte_str} }};")
            snippet.append_line("if (bitbuffer->num_rows < 1)")
            with snippet.with_indent(4):
                snippet.append_line("return DECODE_ABORT_LENGTH;")

            if step.scan_all_rows:
                if not has_row_var:
                    snippet.append_line("unsigned tip_row = 0;")
                    row_var = "tip_row"
                    has_row_var = True
                snippet.append_line("int preamble_found = 0;")
                with snippet.block(
                    "for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows && !preamble_found; ++row)"
                ):
                    snippet.append_line(
                        f"unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, {bit_length});"
                    )
                    with snippet.block("if (pos < bitbuffer->bits_per_row[row])"):
                        snippet.append_line("tip_row = row;")
                        snippet.append_line("offset = pos;")
                        snippet.append_line("preamble_found = 1;")
                snippet.append_line("if (!preamble_found)")
                with snippet.with_indent(4):
                    snippet.append_line("return DECODE_ABORT_EARLY;")
                offset_var = "offset"
            else:
                row_expr = row_var if has_row_var else "0"
                snippet.append_line(
                    f"unsigned offset = bitbuffer_search(bitbuffer, {row_expr}, 0, preamble, {bit_length});"
                )
                snippet.append_line(
                    f"if (offset >= bitbuffer->bits_per_row[{row_expr}])"
                )
                with snippet.with_indent(4):
                    snippet.append_line("return DECODE_ABORT_EARLY;")
                offset_var = "offset"

        elif isinstance(step, SkipBits):
            n = step.count
            if n > 0:
                snippet.append_line(f"offset += {n};")
            else:
                abs_n = abs(n)
                snippet.append_line(f"if (offset < {abs_n})")
                with snippet.with_indent(4):
                    snippet.append_line("return DECODE_FAIL_SANITY;")
                snippet.append_line(f"offset -= {abs_n};")

        elif isinstance(step, ManchesterDecode):
            row_expr = row_var if has_row_var else "0"
            off_expr = offset_var if offset_var != "0" else "0"
            snippet.append_line(f"bitbuffer_t packet_bits = {{0}};")
            snippet.append_line(
                f"bitbuffer_manchester_decode(bitbuffer, {row_expr}, {off_expr}, &packet_bits, {step.max_bits});"
            )
            snippet.append_line("if (packet_bits.bits_per_row[0] < 8)")
            with snippet.with_indent(4):
                snippet.append_line("return DECODE_ABORT_LENGTH;")
            snippet.append_line("bitbuffer = &packet_bits;")
            after_manchester = True
            row_var = "0"
            offset_var = "0"
            has_row_var = False

    return snippet, row_var, offset_var


# ---------------------------------------------------------------------------
# Callable (method/validate) C function emission
# ---------------------------------------------------------------------------


def _callable_c_name(
    decoder_name: str, callable_name: str, variant_name: str | None = None
) -> str:
    """Generate the C function name for a callable."""
    if variant_name:
        return f"{decoder_name}_{variant_name}_{callable_name}"
    return f"{decoder_name}_{callable_name}"


def _walk_expr(node: Any):
    """Yield all nodes in an expression tree (depth-first)."""
    stack: list[Any] = [node]
    while stack:
        cur = stack.pop()
        if cur is None or isinstance(cur, (int, float, bool)):
            continue
        if isinstance(cur, (ExprIntLiteral, ExprFloatLiteral, ExprBoolLiteral)):
            continue
        yield cur
        if isinstance(cur, BinaryExpr):
            stack.append(cur.left)
            stack.append(cur.right)
        elif isinstance(cur, UnaryExpr):
            stack.append(cur.operand)
        elif isinstance(cur, Rev8Expr):
            stack.append(cur.operand)
        elif isinstance(cur, LfsrDigest8Expr):
            stack.extend(cur.bytes_args)


def _collect_row_field_accesses(node: Any) -> dict[str, set[str]]:
    """Walk an expression tree and return {rows_name: {sub_field, ...}} for all RowFieldAccess nodes."""
    result: dict[str, set[str]] = {}
    for cur in _walk_expr(node):
        if isinstance(cur, RowFieldAccess):
            result.setdefault(cur.rows_name, set()).add(cur.field_name)
    return result


def _resolve_callable_deps(callables: dict[str, CallableEntry], parsed_fields: set[str]) -> None:
    """Add callable names to parsed_fields when their own deps are satisfied.

    Runs a fixpoint loop: a callable whose deps are all in parsed_fields
    becomes available, which may unblock further callables.
    """
    non_validate = {n: e for n, e in callables.items() if e.kind != "validate"}
    changed = True
    while changed:
        changed = False
        for name, entry in non_validate.items():
            if name not in parsed_fields and _field_deps(entry).issubset(parsed_fields):
                parsed_fields.add(name)
                changed = True


def _field_deps(entry: CallableEntry) -> set[str]:
    """Return the set of parsed-field names this callable depends on.

    Excludes BitbufferPipeline params (always in scope, not field deps).
    """
    return {p for p in entry.params if entry.param_types.get(p) is not BitbufferPipeline}


def _rows_sub_fields_for_param(entry: CallableEntry, param_name: str) -> list[str] | None:
    """Return the sorted sub-field names for a Rows param, or None if not a Rows param.

    For inline callables, discovers sub-fields from the expression tree.
    For external callables (expr_tree is None), uses the RowsParamType from param_types.
    """
    # Check expr tree first (inline callables)
    rows_accesses = _collect_row_field_accesses(entry.expr_tree)
    if param_name in rows_accesses:
        return sorted(rows_accesses[param_name])
    # Fall back to RowsParamType (external callables)
    pt = entry.param_types.get(param_name)
    if isinstance(pt, RowsParamType):
        return sorted(pt.sub_fields)
    return None


def _expand_callable_params(entry: CallableEntry) -> list[str]:
    """Build the C parameter list for a callable, expanding Rows params to ``int *`` pointers."""
    c_params: list[str] = []
    for p in entry.params:
        pt = entry.param_types.get(p)
        if pt is BitbufferPipeline:
            c_params.append("bitbuffer_t *bitbuffer")
        else:
            sub_fields = _rows_sub_fields_for_param(entry, p)
            if sub_fields is not None:
                for sf in sub_fields:
                    c_params.append(f"int *{p}_{sf}")
            else:
                c_params.append(f"{_c_type(pt)} {p}")
    return c_params


def _expand_callable_args(entry: CallableEntry) -> list[str]:
    """Build the C argument list for a call to a callable, expanding Rows params."""
    c_args: list[str] = []
    for p in entry.params:
        pt = entry.param_types.get(p)
        if pt is BitbufferPipeline:
            c_args.append("bitbuffer")
        else:
            sub_fields = _rows_sub_fields_for_param(entry, p)
            if sub_fields is not None:
                for sf in sub_fields:
                    c_args.append(f"{p}_{sf}")
            else:
                c_args.append(p)
    return c_args


def emit_callable(
    decoder_name: str,
    callable_name: str,
    entry: CallableEntry,
    variant_name: str | None = None,
) -> CodeSnippet:
    """Emit a static constexpr C function for a callable."""
    if entry.is_external:
        return CodeSnippet()

    c_name = _callable_c_name(decoder_name, callable_name, variant_name)
    c_ret = _c_type(entry.return_type) if entry.return_type is not None else "int"
    c_params = _expand_callable_params(entry)
    param_str = ", ".join(c_params) if c_params else "void"
    body = emit_expr(entry.expr_tree)

    snippet = CodeSnippet()
    with snippet.block(f"static constexpr {c_ret} {c_name}({param_str})"):
        snippet.append_line(f"return {body};")
    return snippet


# ---------------------------------------------------------------------------
# data_make emission
# ---------------------------------------------------------------------------


def emit_data_make(
    records: list[JsonRecord],
    decoder_name: str,
    variant_name: str | None,
    callables: dict[str, CallableEntry],
) -> CodeSnippet:
    """Emit data_make(...) and decoder_output_data(...). Returns a CodeSnippet."""
    snippet = CodeSnippet()

    buf_counter = [0]
    data_args = []

    for rec in records:
        key = rec.key
        label = rec.label
        value = rec.value
        data_type = rec.data_type
        fmt = rec.fmt
        when = rec.when

        val_expr = _value_to_c(value, decoder_name, variant_name, callables)

        # DATA_STRING + fmt + non-literal value → snprintf
        if (
            data_type == "DATA_STRING"
            and fmt is not None
            and not isinstance(value, str)
        ):
            buf_name = f"buf_{key}_{buf_counter[0]}"
            buf_counter[0] += 1
            snippet.append_line(f"char {buf_name}[64];")
            snippet.append_line(
                f'snprintf({buf_name}, sizeof({buf_name}), "{fmt}", {val_expr});'
            )
            val_expr = buf_name
            fmt = None

        entry_parts = [f'"{key}"', f'"{label}"']
        if when is not None:
            when_c = emit_expr(when) if isinstance(when, Expr) else str(when)
            entry_parts.append(f"DATA_COND, {when_c}")
        if fmt is not None:
            entry_parts.append(f'DATA_FORMAT, "{fmt}"')
        entry_parts.append(data_type)
        entry_parts.append(val_expr)
        data_args.append(", ".join(entry_parts))

    snippet.append_line("data_t *data = data_make(")
    with snippet.with_indent(8):
        for arg_str in data_args:
            snippet.append_line(f"{arg_str},")
        snippet.append_line("NULL);")
    snippet.append_line("decoder_output_data(decoder, data);")
    return snippet


def _value_to_c(
    value: Any,
    decoder_name: str,
    variant_name: str | None,
    callables: dict[str, CallableEntry],
) -> str:
    """Convert a JsonRecord value to a C expression string."""
    if isinstance(value, str):
        return f'"{value}"'
    if isinstance(value, FieldRef):
        # Check if it's a method reference
        callable_entry = callables.get(value.name)
        if callable_entry is not None and callable_entry.kind in ("prop", "method"):
            # Emit as function call — expand Rows params to flat arrays
            param_str = ", ".join(_expand_callable_args(callable_entry))
            c_name = _callable_c_name(decoder_name, value.name, variant_name)
            call_expr = f"{c_name}({param_str})"
            if callable_entry.return_type is float:
                call_expr = f"(double){call_expr}"
            return call_expr
        # Plain field reference
        return value.name
    if isinstance(value, Expr):
        return emit_expr(value)
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return repr(value)
    return str(value)


# ---------------------------------------------------------------------------
# Validation hook call emission
# ---------------------------------------------------------------------------


def _emit_decode_failure(snippet: CodeSnippet, fail_token: str, fail_hard: bool) -> None:
    """Emit a failure path: ``return`` if fail_hard, else ``result = ...; continue;``."""
    if fail_hard:
        snippet.append_line(f"return {fail_token};")
    else:
        snippet.append_line(f"result = {fail_token};")
        snippet.append_line("continue;")


def emit_validate_call(
    decoder_name: str,
    hook_name: str,
    entry: CallableEntry,
    fail_hard: bool = True,
    variant_name: str | None = None,
) -> CodeSnippet:
    """Emit the if(!hook(...)) guard. Returns a CodeSnippet."""
    snippet = CodeSnippet()
    c_name = _callable_c_name(decoder_name, hook_name, variant_name)
    param_str = ", ".join(_expand_callable_args(entry))

    if fail_hard:
        snippet.append_line(f"if (!{c_name}({param_str}))")
        with snippet.with_indent(4):
            snippet.append_line(f"return {entry.fail_c};")
    else:
        with snippet.block(f"if (!{c_name}({param_str}))"):
            snippet.append_line(f"result = {entry.fail_c};")
            snippet.append_line("continue;")
    return snippet


def _emit_pending_validates(
    snippet: CodeSnippet,
    callables: dict[str, CallableEntry],
    decoder_name: str,
    fail_hard: bool,
    parsed_fields: set[str],
    emitted_hooks: set[str],
    variant_name: str | None = None,
) -> None:
    """Emit all validate_* hooks whose deps are now satisfied."""
    for hname in sorted(callables):
        if hname in emitted_hooks:
            continue
        entry = callables[hname]
        if entry.kind != "validate":
            continue
        if _field_deps(entry).issubset(parsed_fields):
            snippet += emit_validate_call(
                decoder_name, hname, entry, fail_hard, variant_name
            )
            emitted_hooks.add(hname)


def _emit_literal_check(
    snippet: CodeSnippet, width: int, value: int, fail_hard: bool,
) -> None:
    """Emit a bitrow_get_bits literal comparison and advance bit_pos."""
    val_c = hex(value)
    if fail_hard:
        snippet.append_line(
            f"if (bitrow_get_bits(b, bit_pos, {width}) != {val_c})"
        )
        with snippet.with_indent(4):
            _emit_decode_failure(snippet, "DECODE_FAIL_SANITY", fail_hard)
    else:
        with snippet.block(
            f"if (bitrow_get_bits(b, bit_pos, {width}) != {val_c})"
        ):
            _emit_decode_failure(snippet, "DECODE_FAIL_SANITY", fail_hard)
    snippet.append_line(f"bit_pos += {width};")


def _emit_sub_fields(
    snippet: CodeSnippet,
    sub_fields: tuple,
    array_prefix: str,
    index_var: str,
    fail_hard: bool,
) -> None:
    """Emit bitrow_get_bits extraction for each field in a Repeatable's _fields.

    For BitsSpec: emits ``array_prefix_name[index_var] = bitrow_get_bits(...)``.
    For LiteralSpec: emits a sanity check with the appropriate failure mode.
    Skip fields (``_``-prefixed) just advance ``bit_pos``.
    """
    for sf_name, sf_spec in sub_fields:
        if isinstance(sf_spec, BitsSpec):
            if sf_name.startswith("_"):
                snippet.append_line(f"bit_pos += {sf_spec.width};")
            else:
                snippet.append_line(
                    f"{array_prefix}_{sf_name}[{index_var}] = bitrow_get_bits(b, bit_pos, {sf_spec.width});"
                )
                snippet.append_line(f"bit_pos += {sf_spec.width};")
        elif isinstance(sf_spec, LiteralSpec):
            _emit_literal_check(snippet, sf_spec.width, sf_spec.value, fail_hard)


# ---------------------------------------------------------------------------
# Field extraction code generation
# ---------------------------------------------------------------------------


def emit_fields(
    fields: list[tuple[str, Any]],
    callables: dict[str, CallableEntry],
    decoder_name: str,
    fail_hard: bool,
    parsed_fields: set[str],
    emitted_hooks: set[str],
    variant_name: str | None = None,
) -> CodeSnippet:
    """Emit field extraction code and auto-insert validate_* calls. Returns a CodeSnippet."""
    snippet = CodeSnippet()

    for field_name, spec in fields:
        if isinstance(spec, BitsSpec):
            n = spec.width
            if field_name.startswith("_"):
                snippet.append_line(f"bit_pos += {n};")
            else:
                snippet.append_line(
                    f"int {field_name} = bitrow_get_bits(b, bit_pos, {n});"
                )
                snippet.append_line(f"bit_pos += {n};")
                parsed_fields.add(field_name)

        elif isinstance(spec, LiteralSpec):
            _emit_literal_check(snippet, spec.width, spec.value, fail_hard)

        elif isinstance(spec, RepeatSpec):
            sub_fields = [
                (n, s)
                for n, s in spec.sub_protocol._fields
                if isinstance(s, BitsSpec) and not n.startswith("_")
            ]
            count_expr = emit_expr(spec.count_expr)
            for sf_name, _ in sub_fields:
                snippet.append_line(f"int {field_name}_{sf_name}[{count_expr}];")
            with snippet.block(f"for (int _i = 0; _i < {count_expr}; _i++)"):
                _emit_sub_fields(snippet, spec.sub_protocol._fields, field_name, "_i", fail_hard)
            for sf_name, _ in sub_fields:
                parsed_fields.add(f"{field_name}_{sf_name}")
            parsed_fields.add(field_name)

        elif isinstance(spec, RowsSpec):
            for row_idx, exact_bits in spec.required_bits:
                snippet.append_line(
                    f"if (bitbuffer->bits_per_row[{row_idx}] != {exact_bits})"
                )
                with snippet.with_indent(4):
                    snippet.append_line("return DECODE_ABORT_LENGTH;")

            # FirstValid RowsSpec is handled by emit_decode_fn directly;
            # emit_fields only sees static-tuple RowsSpec.
            assert not isinstance(spec.row_spec, FirstValid)

            row_indices = spec.row_spec
            n_rows = len(row_indices)
            sub_fields = [
                (n, s)
                for n, s in spec.sub_protocol._fields
                if isinstance(s, BitsSpec) and not n.startswith("_")
            ]
            for sf_name, _ in sub_fields:
                snippet.append_line(f"int {field_name}_{sf_name}[BITBUF_ROWS];")
                parsed_fields.add(f"{field_name}_{sf_name}")
            parsed_fields.add(field_name)
            rows_c = ", ".join(str(r) for r in row_indices)
            snippet.append_line(
                f"static uint16_t const _rows_{field_name}[] = {{{rows_c}}};"
            )
            with snippet.block(f"for (size_t _k = 0; _k < {n_rows}; ++_k)"):
                snippet.append_line(f"unsigned _r = _rows_{field_name}[_k];")
                snippet.append_line("uint8_t *b = bitbuffer->bb[_r];")
                snippet.append_line("unsigned bit_pos = 0;")
                _emit_sub_fields(snippet, spec.sub_protocol._fields, field_name, "_r", fail_hard)

        _emit_pending_validates(
            snippet, callables, decoder_name, fail_hard,
            parsed_fields, emitted_hooks, variant_name,
        )

    return snippet


# ---------------------------------------------------------------------------
# Decode function generation
# ---------------------------------------------------------------------------


def _emit_decode_tail(
    snippet: CodeSnippet,
    cls: type,
    callables: dict[str, CallableEntry],
    decoder_name: str,
    fail_hard: bool,
    parsed_fields: set[str],
    emitted_hooks: set[str],
) -> None:
    """Emit the resolve-deps → pending-validates → variants-or-data_make tail."""
    _resolve_callable_deps(callables, parsed_fields)
    _emit_pending_validates(
        snippet, callables, decoder_name, fail_hard,
        parsed_fields, emitted_hooks,
    )

    variants = list(cls._variants)
    if variants:
        snippet += _emit_variants(
            cls, variants, decoder_name, callables,
            fail_hard, parsed_fields, emitted_hooks,
        )
    else:
        records = list(cls.json_records())
        snippet += emit_data_make(records, decoder_name, None, callables)
        snippet.append_line("return 1;")


def emit_decode_fn(decoder_name: str, instance: Decoder) -> CodeSnippet:
    """Emit the static int foo_decode(...) function. Returns a CodeSnippet."""
    snippet = CodeSnippet()
    fn_name = f"{decoder_name}_decode"
    cls = type(instance)

    pipeline = list(instance.prepare(BitbufferPipeline()).steps)

    pipeline_snippet, row_var, offset_var = emit_pipeline(pipeline)

    callables = dict(cls._callables)
    parsed_fields: set[str] = set()
    emitted_hooks: set[str] = set()
    fields = list(cls._fields)

    has_rows_first_valid = any(
        isinstance(spec, RowsSpec) and isinstance(spec.row_spec, FirstValid)
        for _, spec in fields
    )

    with snippet.block(
        f"static int {fn_name}(r_device *decoder, bitbuffer_t *bitbuffer)"
    ):
        snippet += pipeline_snippet

        row_expr = row_var if row_var != "0" else "0"
        snippet.append_line(f"uint8_t *b = bitbuffer->bb[{row_expr}];")
        snippet.append_line(
            f"unsigned bit_pos = {offset_var if offset_var != '0' else '0'};"
        )

        if has_rows_first_valid:
            fv_idx = next(
                i
                for i, (_, spec) in enumerate(fields)
                if isinstance(spec, RowsSpec) and isinstance(spec.row_spec, FirstValid)
            )
            pre_fields = fields[:fv_idx]
            fv_name, fv_spec = fields[fv_idx]
            post_fields = fields[fv_idx + 1 :]

            snippet += emit_fields(
                pre_fields, callables, decoder_name, True, parsed_fields, emitted_hooks
            )

            for row_idx, exact_bits in fv_spec.required_bits:
                snippet.append_line(
                    f"if (bitbuffer->bits_per_row[{row_idx}] != {exact_bits})"
                )
                with snippet.with_indent(4):
                    snippet.append_line("return DECODE_ABORT_LENGTH;")

            bits = fv_spec.row_spec.bits
            sub_fields = [
                (n, s)
                for n, s in fv_spec.sub_protocol._fields
                if isinstance(s, BitsSpec) and not n.startswith("_")
            ]

            for sf_name, _ in sub_fields:
                snippet.append_line(f"int {fv_name}_{sf_name}[BITBUF_ROWS];")
                parsed_fields.add(f"{fv_name}_{sf_name}")

            snippet.append_line("int result = 0;")
            with snippet.block("for (int _r = 0; _r < bitbuffer->num_rows; ++_r)"):
                with snippet.block(f"if (bitbuffer->bits_per_row[_r] != {bits})"):
                    snippet.append_line("result = DECODE_ABORT_LENGTH;")
                    snippet.append_line("continue;")
                snippet.append_line("uint8_t *b = bitbuffer->bb[_r];")
                snippet.append_line("unsigned bit_pos = 0;")

                _emit_sub_fields(snippet, fv_spec.sub_protocol._fields, fv_name, "_r", False)

                snippet += emit_fields(
                    post_fields,
                    callables,
                    decoder_name,
                    False,
                    parsed_fields,
                    emitted_hooks,
                )

                _emit_decode_tail(
                    snippet, cls, callables, decoder_name, False,
                    parsed_fields, emitted_hooks,
                )

            snippet.append_line("return result;")

        else:
            # Normal path
            snippet += emit_fields(
                fields, callables, decoder_name, True, parsed_fields, emitted_hooks
            )

            _emit_decode_tail(
                snippet, cls, callables, decoder_name, True,
                parsed_fields, emitted_hooks,
            )

    # Check for validate hooks that never fired.
    all_validate = {n for n, e in callables.items() if e.kind == "validate"}
    never_emitted = {
        n for n in all_validate - emitted_hooks
        if not _field_deps(callables[n]).issubset(parsed_fields)
    }
    if never_emitted:
        raise TypeError(
            f"{decoder_name}: validate hook(s) {', '.join(sorted(never_emitted))} "
            "have unsatisfiable dependencies. "
            "Check that all parameter names match public fields or callables."
        )

    return snippet


def _emit_variants(
    cls: type,
    variants: list,
    decoder_name: str,
    parent_callables: dict[str, CallableEntry],
    fail_hard: bool,
    parsed_fields: set,
    emitted_hooks: set,
) -> CodeSnippet:
    """Emit the if/else if chain for variants. Returns a CodeSnippet."""
    with ElifLadder() as ladder:
        for vcls in variants:
            when_c = emit_expr(_call_inline(vcls.when))
            with ladder.branch(when_c) as body:
                v_parsed = set(parsed_fields)
                v_emitted_hooks = set(emitted_hooks)
                all_callables = dict(parent_callables)
                all_callables.update(vcls._callables)

                body += emit_fields(
                    list(vcls._fields),
                    all_callables,
                    decoder_name,
                    fail_hard,
                    v_parsed,
                    v_emitted_hooks,
                    variant_name=vcls.__name__,
                )

                _emit_pending_validates(
                    body, all_callables, decoder_name, fail_hard,
                    v_parsed, v_emitted_hooks, variant_name=vcls.__name__,
                )

                records = list(vcls.json_records()) or _default_variant_records(cls, vcls)
                body += emit_data_make(
                    records, decoder_name, vcls.__name__, all_callables
                )
                body.append_line("return 1;")

    ladder.snippet.append_line("return DECODE_FAIL_SANITY;")
    return ladder.snippet


def _default_variant_records(parent_cls: type, vcls: type) -> list[JsonRecord]:
    """Build default JsonRecord list for a variant."""
    model = parent_cls.__new__(parent_cls).modulation_config().device_name
    records = [JsonRecord("model", "", model, "DATA_STRING")]
    seen = {"model"}

    for name, spec in parent_cls._fields:
        if isinstance(spec, BitsSpec) and not name.startswith("_") and name not in seen:
            records.append(JsonRecord(name, "", FieldRef(name), "DATA_INT"))
            seen.add(name)

    for name, spec in vcls._fields:
        if isinstance(spec, BitsSpec) and not name.startswith("_") and name not in seen:
            records.append(JsonRecord(name, "", FieldRef(name), "DATA_INT"))
            seen.add(name)

    return records


# ---------------------------------------------------------------------------
# Dispatcher code generation
# ---------------------------------------------------------------------------


def emit_dispatcher(dispatcher_name: str, delegates: list[Decoder]) -> CodeSnippet:
    """Emit the dispatcher decode function.

    Saves the bitbuffer by value before each delegate call so that one
    delegate's in-place pipeline mutations don't affect the next.
    """
    snippet = CodeSnippet()
    with snippet.block(
        f"static int {dispatcher_name}_decode(r_device *decoder, bitbuffer_t *bitbuffer)"
    ):
        snippet.append_line("bitbuffer_t saved = *bitbuffer;")
        snippet.append_line("int ret = DECODE_ABORT_EARLY;")
        for i, delegate in enumerate(delegates):
            d_name = _decoder_name(delegate)
            if i > 0:
                snippet.append_line("*bitbuffer = saved;")
            snippet.append_line(f"ret = {d_name}_decode(decoder, bitbuffer);")
            snippet.append_line("if (ret > 0) return ret;")
        snippet.append_line("return ret;")
    return snippet


# ---------------------------------------------------------------------------
# output_fields array
# ---------------------------------------------------------------------------


def emit_output_fields(sources: list[Protocol]) -> CodeSnippet:
    """Emit the output_fields[] array from one or more source instances.

    Unions json_records() keys from each source and its variants.
    """
    snippet = CodeSnippet()
    seen: list[str] = []
    seen_set: set[str] = set()

    def add(key: str):
        if key not in seen_set:
            seen.append(key)
            seen_set.add(key)

    for src in sources:
        cls = type(src)
        for rec in cls.json_records():
            add(rec.key)
        for vcls in cls._variants:
            for rec in vcls.json_records():
                add(rec.key)

    snippet.append_line("static char const *const output_fields[] = {")
    with snippet.with_indent(4):
        for key in seen:
            snippet.append_line(f'"{key}",')
        snippet.append_line("NULL,")
    snippet.append_line("};")
    return snippet


# ---------------------------------------------------------------------------
# r_device struct emission
# ---------------------------------------------------------------------------


def emit_r_device(decoder_name: str, instance: Protocol) -> CodeSnippet:
    """Emit the r_device const struct. Returns a CodeSnippet."""
    cfg: ModulationConfig = instance.modulation_config()
    snippet = CodeSnippet()
    snippet.append_line(f"r_device const {decoder_name} = {{")
    with snippet.with_indent(4):
        snippet.append_line(f'.name        = "{cfg.device_name}",')
        snippet.append_line(f".modulation  = {cfg.modulation.name},")
        snippet.append_line(f".short_width = {float(cfg.short_width)},")
        snippet.append_line(f".long_width  = {float(cfg.long_width)},")
        snippet.append_line(f".reset_limit = {float(cfg.reset_limit)},")
        if cfg.gap_limit is not None:
            snippet.append_line(f".gap_limit   = {float(cfg.gap_limit)},")
        if cfg.sync_width is not None:
            snippet.append_line(f".sync_width  = {float(cfg.sync_width)},")
        if cfg.tolerance is not None:
            snippet.append_line(f".tolerance   = {float(cfg.tolerance)},")
        if cfg.disabled is not None:
            snippet.append_line(f".disabled    = {int(cfg.disabled)},")
        snippet.append_line(f".decode_fn   = &{decoder_name}_decode,")
        snippet.append_line(".fields      = output_fields,")
    snippet.append_line("};")
    return snippet


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------


def _emit_file_header(instance: Protocol) -> CodeSnippet:
    """Emit the `// Generated from ...` comment and the `/** @file */` doc block."""
    decoder_name = type(instance).__name__
    snippet = CodeSnippet()
    snippet.append_line(f"// Generated from {decoder_name}.py")
    snippet.append_line("/** @file")
    snippet.append_line(f"    {instance.modulation_config().device_name} decoder.")
    snippet.append_line("*/")
    snippet.append_line("")
    snippet.append_line('#include "decoder.h"')
    return snippet


def _compile_single_decoder(instance: Decoder) -> str:
    """Compile a non-dispatching Decoder instance to a complete C source file."""
    cls = type(instance)
    decoder_name = cls.__name__
    result = _emit_file_header(instance)

    if cls.requires_external_helpers():
        result.append_line(f'#include "{decoder_name}.h"')
    result.append_line("")

    # Emit static constexpr C functions for all inline callables
    for c_name, entry in cls._callables.items():
        fn_snippet = emit_callable(decoder_name, c_name, entry)
        if fn_snippet.lines():
            result += fn_snippet
            result.append_line("")
    for vcls in cls._variants:
        for c_name, entry in vcls._callables.items():
            fn_snippet = emit_callable(decoder_name, c_name, entry, vcls.__name__)
            if fn_snippet.lines():
                result += fn_snippet
                result.append_line("")

    result += emit_decode_fn(decoder_name, instance)
    result.append_line("")

    result += emit_output_fields([instance])
    result.append_line("")

    result += emit_r_device(decoder_name, instance)
    result.append_line("")

    return result.render()


def _compile_dispatcher(instance: Dispatcher) -> str:
    """Compile a Dispatcher instance to a complete C source file."""
    cls = type(instance)
    decoder_name = cls.__name__
    delegates = list(instance.dispatch())
    result = _emit_file_header(instance)

    if any(type(d).requires_external_helpers() for d in delegates):
        result.append_line(f'#include "{decoder_name}.h"')
    result.append_line("")

    # Emit static constexpr C functions for each delegate's callables
    for delegate in delegates:
        d_name = type(delegate).__name__
        for c_name, entry in type(delegate)._callables.items():
            fn_snippet = emit_callable(d_name, c_name, entry)
            if fn_snippet.lines():
                result += fn_snippet
                result.append_line("")

    # Emit each delegate's decode function, then the dispatcher
    for delegate in delegates:
        d_name = type(delegate).__name__
        result += emit_decode_fn(d_name, delegate)
        result.append_line("")
    result += emit_dispatcher(decoder_name, delegates)
    result.append_line("")

    result += emit_output_fields([instance] + delegates)
    result.append_line("")

    result += emit_r_device(decoder_name, instance)
    result.append_line("")

    return result.render()


def compile_decoder(instance: Protocol) -> str:
    """Compile a Decoder or Dispatcher instance to a C source file string."""
    if isinstance(instance, Dispatcher):
        return _compile_dispatcher(instance)
    return _compile_single_decoder(instance)
