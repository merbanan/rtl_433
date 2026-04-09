"""
Compiler: reads a Protocol subclass and emits a C++ decoder file for rtl_433.
"""

from __future__ import annotations
import inspect
from contextlib import contextmanager
from pathlib import PurePosixPath
from typing import Any

from .dsl import (
    BinaryExpr,
    BitsSpec,
    CondSpec,
    DecodeFail,
    ExprBoolLiteral,
    ExprFloatLiteral,
    ExprIntLiteral,
    FieldRef,
    method_inline_expr,
    property_inline_expr,
    validation_hook_inline_expr,
    when_inline_expr,
    FindRepeatedRow,
    FirstValidRow,
    Invert,
    JsonRecord,
    LiteralSpec,
    LfsrDigest8Expr,
    ManchesterDecode,
    ModulationConfig,
    Reflect,
    RepeatSpec,
    Rev8Expr,
    RowsSpec,
    SearchPreamble,
    SkipBits,
    Subscript,
    UnaryExpr,
)


def compile(
    protocol_cls: type, source_path: str | None = None, module_doc: str | None = None
) -> str:
    """Compile a Protocol subclass into a complete C++ decoder source string."""
    return _CompileContext(
        protocol_cls, source_path=source_path, module_doc=module_doc
    ).emit()


def _preamble_to_bytes(value: int) -> bytes:
    """Convert an int like 0x552DD4 to big-endian bytes."""
    if value == 0:
        return b"\x00"
    length = (value.bit_length() + 7) // 8
    return value.to_bytes(length, "big")


def _emit_expr(node: Any) -> str:
    """Recursively render an expression tree node as a C expression string."""
    if isinstance(node, ExprBoolLiteral):
        return "1" if node.value else "0"
    if isinstance(node, ExprIntLiteral):
        v = node.value
        if v < 0:
            return str(v)
        return hex(v) if v > 9 else str(v)
    if isinstance(node, ExprFloatLiteral):
        return repr(node.value)
    if isinstance(node, bool):
        return "1" if node else "0"
    if isinstance(node, int):
        if node < 0:
            return str(node)
        return hex(node) if node > 9 else str(node)
    if isinstance(node, float):
        return repr(node)
    if isinstance(node, FieldRef):
        return node.name
    if isinstance(node, Subscript):
        return f"({node.base.name}[{node.index}])"
    if isinstance(node, Rev8Expr):
        return f"(reverse8({_emit_expr(node.operand)}))"
    if isinstance(node, LfsrDigest8Expr):
        parts = [_emit_expr(x) for x in node.bytes_args]
        n = len(parts)
        inner = ", ".join(parts)
        return (
            f"(lfsr_digest8((uint8_t const[]){{{inner}}}, {n}, "
            f"{_emit_expr(node.gen)}, {_emit_expr(node.key)}))"
        )
    if isinstance(node, BinaryExpr):
        return f"({_emit_expr(node.left)} {node.op} {_emit_expr(node.right)})"
    if isinstance(node, UnaryExpr):
        return f"({node.op}{_emit_expr(node.operand)})"
    raise TypeError(f"Cannot emit expression node: {type(node).__name__}")


def _collect_count_expr_fieldrefs(expr: Any, sink: set[str]) -> None:
    sink.update(_field_refs_in_expr(expr))


def _field_refs_in_expr(node: Any) -> set[str]:
    """Names referenced as :class:`FieldRef` or subscript base in a DSL expression."""
    if isinstance(node, (int, float, bool)) or node is None:
        return set()
    found: set[str] = set()
    stack: list[Any] = [node]
    while stack:
        cur = stack.pop()
        if isinstance(cur, (int, float, bool)) or cur is None:
            continue
        if isinstance(cur, (ExprIntLiteral, ExprFloatLiteral, ExprBoolLiteral)):
            continue
        if isinstance(cur, FieldRef):
            found.add(cur.name)
        elif isinstance(cur, Subscript):
            found.add(cur.base.name)
            stack.append(cur.base)
        elif isinstance(cur, BinaryExpr):
            stack.append(cur.left)
            stack.append(cur.right)
        elif isinstance(cur, UnaryExpr):
            stack.append(cur.operand)
        elif isinstance(cur, Rev8Expr):
            stack.append(cur.operand)
        elif isinstance(cur, LfsrDigest8Expr):
            for a in cur.bytes_args:
                stack.append(a)
    return found


def _decode_fail_c_token(fn: Any) -> str:
    """C token from inline ``validate_*`` hook ``fail_value=DecodeFail.*`` default."""
    sig = inspect.signature(fn)
    names = [n for n in sig.parameters if n != "self"]
    if not names or names[-1] != "fail_value":
        raise TypeError(
            f"{getattr(fn, '__qualname__', fn)!r}: validate_* hooks must declare "
            "fail_value=DecodeFail.... as the last parameter (after self)."
        )
    p = sig.parameters["fail_value"]
    if p.default is inspect.Parameter.empty:
        raise TypeError(
            f"{getattr(fn, '__qualname__', fn)!r}: fail_value must have a default DecodeFail value."
        )
    d = p.default
    if not isinstance(d, DecodeFail):
        raise TypeError(
            f"{getattr(fn, '__qualname__', fn)!r}: fail_value default must be DecodeFail.*, "
            f"got {type(d).__name__!r}"
        )
    return d.value


def _c_type_for(py_type: type) -> str:
    """Map a Python type annotation to a C type."""
    return {float: "float", int: "int", str: "const char *"}.get(py_type, "int")


def _data_make_type(py_type: type) -> str:
    """Return DATA_* macro string for a Python type."""
    return {float: "DATA_DOUBLE", int: "DATA_INT", str: "DATA_STRING"}.get(
        py_type, "DATA_INT"
    )


def _data_make_cast(name: str, py_type: type) -> str:
    """Return the value expression for data_make, with cast if needed."""
    if py_type is float:
        return f"(double){name}"
    return name


def _hex_byte_literal(b: bytes) -> str:
    """Format bytes as a C array initializer: {0x55, 0x2d, 0xd4}"""
    return ", ".join(f"0x{byte:02x}" for byte in b)


def _extract_bits(arr: str, offset, width: int) -> str:
    """Emit a call to ``bitrow_get_bits(arr, offset, width)``."""
    return f"bitrow_get_bits({arr}, {offset}, {width})"


def _sanitize_c_comment(text: str) -> str:
    """Make arbitrary text safe to embed inside a C block comment."""
    return text.replace("*/", "* /")


class LazyLine:
    def __init__(self, text: str) -> None:
        self._text = text
        self._done = False

    def emit(self, ctx: Any, line: str | None = None) -> bool:
        if self._done:
            return False
        ctx._line(line if line is not None else self._text)
        self._done = True
        return True


class _CompileContext:
    def __init__(
        self, cls: type, source_path: str | None = None, module_doc: str | None = None
    ):
        self.cls = cls
        self.prefix = cls.__name__
        self.source_path = source_path
        self.module_doc = module_doc
        self.lines: list[str] = []
        self._indent = 0
        self.fields = cls.fields
        self.variants = cls.variants
        self.methods = cls.methods
        self.properties = cls.properties
        self.explicit_args_map = cls.explicit_args()
        self.json_records = cls.json_records()
        self.delegates = self.cls.delegated_protocols()
        self._inline = self._try_inline_on(cls)
        self._inline_props_emitted: set[str] = set()
        self._inline_methods_emitted: set[str] = set()

    def _decode_uses_b_array(self) -> bool:
        if not hasattr(self.cls, "prepare"):
            return False
        steps = self.cls().prepare().steps
        if any(isinstance(s, ManchesterDecode) for s in steps):
            return True
        return self._total_payload_bits(self.cls) > 0

    def _implicit_external_dep_names(
        self, cls_or_variant: type, variant_cls: type | None
    ) -> set[str]:
        names: set[str] = {"b"}
        if variant_cls is not None:
            for n in self._all_field_names_up_to(variant_cls):
                names.add(n)
        else:
            for name, spec in self.fields:
                if isinstance(spec, BitsSpec):
                    names.add(name)
                elif isinstance(spec, RepeatSpec):
                    for sub_name, sub_spec in spec.sub_protocol.fields:
                        if isinstance(sub_spec, BitsSpec):
                            names.add(f"{name}_{sub_name}")
                    _collect_count_expr_fieldrefs(spec.count_expr, names)
        return names

    def _validation_hook_dep_names(
        self, cls_or_variant: type, hook_name: str, *, variant_cls: type | None = None
    ) -> set[str]:
        field_params = cls_or_variant.explicit_args().get(hook_name)
        if field_params:
            return set(field_params)
        inline = validation_hook_inline_expr(cls_or_variant, hook_name)
        if inline is not None:
            return _field_refs_in_expr(inline)
        return self._implicit_external_dep_names(cls_or_variant, variant_cls)

    def _initial_available_symbols(self) -> set[str]:
        s = {"decoder", "bitbuffer"}
        if self._decode_uses_b_array():
            s.add("b")
        return s

    def _symbols_from_top_level_field(self, fname: str, spec: Any) -> set[str]:
        """C names that become valid after emitting one top-level (or variant) field."""
        if isinstance(spec, BitsSpec) and not fname.startswith("_"):
            return {fname}
        if isinstance(spec, CondSpec):
            return {fname}
        if isinstance(spec, RepeatSpec):
            out: set[str] = set()
            for sub_name, sub_spec in spec.sub_protocol.fields:
                if isinstance(sub_spec, BitsSpec):
                    out.add(f"{fname}_{sub_name}")
            return out
        if isinstance(spec, RowsSpec):
            out = set()
            for sub_name, sub_spec in spec.sub_protocol.fields:
                if isinstance(sub_spec, (BitsSpec, LiteralSpec)):
                    out.add(f"{fname}_{sub_name}")
            return out
        return set()

    def _union_symbols_for_fields(self, fields: list) -> set[str]:
        u: set[str] = set()
        for fname, spec in fields:
            u |= self._symbols_from_top_level_field(fname, spec)
        return u

    def _flush_runnable_validation_hooks(
        self,
        cls_or_variant: type,
        variant_cls: type | None,
        inline_try: Any,
        available: set[str],
        pending: set[str],
    ) -> None:
        """Emit every ``validate_*`` in *pending* whose dependencies ⊆ *available* (lex order)."""
        while True:
            batch = [
                h
                for h in sorted(pending)
                if self._validation_hook_dep_names(
                    cls_or_variant, h, variant_cls=variant_cls
                )
                <= available
            ]
            if not batch:
                break
            for hook in batch:
                self._emit_one_validation_hook(
                    cls_or_variant, hook, variant_cls, inline_try
                )
                pending.discard(hook)

    def _flush_inline_properties_and_validates(
        self,
        cls_or_variant: type,
        variant_cls: type | None,
        inline_try: Any,
        available: set[str],
        pending_val: set[str],
        emitted_props: set[str],
        emitted_methods: set[str],
    ) -> None:
        """Emit inline properties and void methods when declared deps ⊆ *available*; run validates. Repeat."""
        prop_items = list(cls_or_variant.properties)
        method_items = list(cls_or_variant.methods)
        while True:
            progressed = False
            for prop_name, prop_type in prop_items:
                if prop_name in emitted_props:
                    continue
                inline = inline_try(prop_name)
                if inline is None:
                    continue
                ex = cls_or_variant.explicit_args().get(prop_name)
                deps = set(ex or ())
                if deps <= available:
                    c_type = _c_type_for(prop_type)
                    self._line(f"{c_type} {prop_name} = {_emit_expr(inline)};")
                    emitted_props.add(prop_name)
                    available.add(prop_name)
                    progressed = True
            for method_name in sorted(method_items):
                if method_name in emitted_methods:
                    continue
                inline = inline_try(method_name)
                if inline is None:
                    continue
                ex = cls_or_variant.explicit_args().get(method_name)
                deps = set(ex or ())
                if deps <= available:
                    self._line(f"{_emit_expr(inline)};")
                    emitted_methods.add(method_name)
                    progressed = True
            self._flush_runnable_validation_hooks(
                cls_or_variant, variant_cls, inline_try, available, pending_val
            )
            if not progressed:
                break

    def _emit_one_validation_hook(
        self,
        cls_or_variant: type,
        hook_name: str,
        variant_cls: type | None,
        inline_try: Any,
    ) -> None:
        fn = getattr(cls_or_variant, hook_name)
        inline = inline_try(hook_name)
        if inline is not None:
            fail_c = _decode_fail_c_token(fn)
            self._line(f"if (!({_emit_expr(inline)}))")
            with self.indent():
                self._line(f"return {fail_c};")
            self._blank()
        else:
            if variant_cls is not None:
                func_name = f"{self.prefix}_{variant_cls.__name__}_{hook_name}"
            else:
                func_name = f"{self.prefix}_{hook_name}"
            args = self._c_call_args_string(cls_or_variant, hook_name, variant_cls)
            vname = f"vret_{hook_name}"
            self._line(f"int {vname} = {func_name}({args});")
            self._line(f"if ({vname} != 0)")
            with self.indent():
                self._line(f"return {vname};")
            self._blank()

    def _try_inline_on(self, cls_or_variant: type):
        """Return a closure that tries to inline a callable as a pure expression."""
        prop_names = {n for n, _ in cls_or_variant.properties}

        def try_one(name: str) -> Any:
            if name.startswith("validate_"):
                return validation_hook_inline_expr(cls_or_variant, name)
            if name in prop_names:
                return property_inline_expr(cls_or_variant, name)
            if name in cls_or_variant.methods:
                return method_inline_expr(cls_or_variant, name)
            return None

        return try_one

    def _line(self, text: str = ""):
        self.lines.append("    " * self._indent + text)

    def _blank(self):
        self.lines.append("")

    @contextmanager
    def indent(self):
        self._indent += 1
        yield
        self._indent -= 1

    @contextmanager
    def braced_block(self, header: str = "", footer: str = ""):
        """Emit ``header {``, indent the body, then emit ``}footer``."""
        self._line(f"{header} {{" if header else "{")
        with self.indent():
            yield
        self._line(f"}}{footer}")

    def emit(self) -> str:
        self._emit_header()
        for delegate_cls in self.delegates:
            child = _CompileContext(
                delegate_cls, source_path=None, module_doc=self.module_doc
            )
            child._emit_fn_doc(f"{delegate_cls.__name__}_decode")
            child._emit_decode_fn()
            self.lines.extend(child.lines)
            self._blank()
        sa_targets = (
            [f"{d.__name__}_decode" for d in self.delegates] if self.delegates else None
        )
        self._emit_fn_doc(f"{self.prefix}_decode", sa_targets=sa_targets)
        self._emit_decode_fn()
        self._blank()
        self._emit_output_fields()
        self._blank()
        self._emit_r_device()
        return "\n".join(self.lines) + "\n"

    def _has_external_helpers(self) -> bool:
        return self.cls.requires_external_helpers()

    def _explicit_args(self, name: str) -> list[str] | None:
        args = self.explicit_args_map.get(name)
        if args is None:
            return None
        return list(args)

    def _emit_file_doc(self):
        """Emit a ``/** @file … */`` block from the module docstring."""
        doc = self.module_doc
        if doc:
            doc = _sanitize_c_comment(doc.strip())
            self._line("/** @file")
            for line in doc.splitlines():
                self._line(f"    {line}".rstrip())
            self._line("*/")
        else:
            cfg = getattr(self.cls, "modulation_config", ModulationConfig)
            name = cfg.device_name or self.prefix
            self._line("/** @file")
            self._line(f"    {name} decoder.")
            self._line("*/")

    def _emit_fn_doc(self, fn_name: str, sa_targets: list[str] | None = None):
        """Emit a ``/** @fn … */`` block before a decode function."""
        self._line(
            f"/** @fn static int {fn_name}(r_device *decoder, bitbuffer_t *bitbuffer)"
        )
        doc = self.module_doc
        if doc:
            doc = _sanitize_c_comment(doc.strip())
            for line in doc.splitlines():
                self._line(f"    {line}".rstrip())
        if sa_targets:
            for target in sa_targets:
                self._line(f"    @sa {target}()")
        self._line("*/")

    def _emit_header(self):
        if self.source_path:
            self._line(f"// Generated from {PurePosixPath(self.source_path).name}")
        self._emit_file_doc()
        self._blank()
        self._line('#include "decoder.h"')
        if self._has_external_helpers():
            if not self.delegates:
                self._line(f'#include "{self.prefix}.h"')
        self._blank()

    def _emit_decode_fn(self):
        if self.delegates:
            self._line(
                f"static int {self.prefix}_decode(r_device *decoder, bitbuffer_t *bitbuffer)"
            )
            with self.braced_block():
                self._line("int ret = DECODE_ABORT_EARLY;")
                for idx, delegate_cls in enumerate(self.delegates):
                    self._line(
                        f"ret = {delegate_cls.__name__}_decode(decoder, bitbuffer);"
                    )
                    self._line("if (ret > 0)")
                    with self.indent():
                        self._line("return ret;")
                    if idx + 1 < len(self.delegates):
                        proxy = delegate_cls()
                        for s in proxy.prepare().steps:
                            if isinstance(s, ManchesterDecode):
                                break
                            if isinstance(s, Invert):
                                self._line("bitbuffer_invert(bitbuffer);")
                            elif isinstance(s, Reflect):
                                with self.braced_block(
                                    "for (int i = 0; i < bitbuffer->num_rows; ++i)"
                                ):
                                    self._line(
                                        "reflect_bytes(bitbuffer->bb[i], (bitbuffer->bits_per_row[i] + 7) / 8);"
                                    )
                self._line("return ret;")
            return

        self._line(
            f"static int {self.prefix}_decode(r_device *decoder, bitbuffer_t *bitbuffer)"
        )
        with self.braced_block():
            proxy = self.cls()
            if not hasattr(self.cls, "prepare"):
                raise RuntimeError(
                    f"Protocol {self.cls.__name__} must define a prepare() method"
                )
            steps = proxy.prepare().steps
            if any(isinstance(s, FirstValidRow) for s in steps[:-1]):
                raise RuntimeError(
                    f"Protocol {self.cls.__name__}: FirstValidRow must be the last "
                    "pipeline step"
                )
            fvr = steps[-1] if steps and isinstance(steps[-1], FirstValidRow) else None
            has_rows = any(isinstance(s, RowsSpec) for _, s in self.fields)
            if fvr is not None:
                if has_rows:
                    raise RuntimeError(
                        f"Protocol {self.cls.__name__}: Rows[] is not supported "
                        "with FirstValidRow"
                    )
                if self.variants:
                    raise RuntimeError(
                        f"Protocol {self.cls.__name__}: FirstValidRow is not supported "
                        "with variants"
                    )
                pre = steps[:-1]
                if any(isinstance(s, ManchesterDecode) for s in pre):
                    raise RuntimeError(
                        f"Protocol {self.cls.__name__}: FirstValidRow cannot be combined "
                        "with ManchesterDecode"
                    )
                self._emit_first_valid_row_decode(pre, fvr)
            else:
                for _, rspec in self.fields:
                    if isinstance(rspec, RowsSpec):
                        self._emit_rows_guards(rspec)
                self._emit_pipeline(steps)
                self._emit_fields(self.fields)

                if not self.variants:
                    self._emit_method_calls()
                    self._emit_property_calls()
                    self._emit_data_make()

    def _emit_first_valid_row_decode(self, pre_steps: list, fvr: FirstValidRow) -> None:
        """Emit decode body: optional pre-pipeline, then row loop with payload."""
        self._line("int result = 0;")
        self._emit_pipeline(pre_steps, skip_terminal_extract=True)
        nbytes = (fvr.bits + 7) // 8
        with self.braced_block("for (int row = 0; row < bitbuffer->num_rows; ++row)"):
            with self.braced_block(
                f"if (bitbuffer->bits_per_row[row] != {fvr.bits})"
            ):
                self._line("result = DECODE_ABORT_LENGTH;")
                self._line("continue;")
            self._line("uint8_t *b = bitbuffer->bb[row];")
            if fvr.reflect:
                self._line(f"reflect_bytes(b, {nbytes});")
            self._blank()
            if fvr.checksum_over_bytes > 0:
                n = fvr.checksum_over_bytes
                self._line(f"int sum = add_bytes(b, {n});")
                with self.braced_block(f"if ((sum & 0xff) != b[{n}])"):
                    self._line("result = DECODE_FAIL_MIC;")
                    self._line("continue;")
                with self.braced_block("if (sum == 0)"):
                    self._line("return DECODE_FAIL_SANITY;")
                self._blank()
            self._emit_fields(self.fields)
            self._emit_method_calls()
            self._emit_property_calls()
            self._emit_data_make()
        self._line("return result;")

    def _emit_pipeline(self, steps: list, *, skip_terminal_extract: bool = False) -> None:
        """Walk pipeline steps and emit corresponding C code for each."""
        has_manchester = any(isinstance(s, ManchesterDecode) for s in steps)
        has_scan_all = any(
            isinstance(s, SearchPreamble) and s.scan_all_rows for s in steps
        )
        needs_search_row = has_manchester or has_scan_all

        row_expr = "0"
        offset_decl = LazyLine("unsigned offset = 0;")
        search_row_decl = LazyLine("unsigned search_row = 0;")
        post_manchester = False

        for step in steps:
            if isinstance(step, Invert):
                if post_manchester:
                    self._line("bitbuffer_invert(&packet_bits);")
                else:
                    self._line("bitbuffer_invert(bitbuffer);")
                self._blank()

            elif isinstance(step, Reflect):
                if post_manchester:
                    self._line(
                        "reflect_bytes(packet_bits.bb[0], "
                        "(packet_bits.bits_per_row[0] + 7) / 8);"
                    )
                else:
                    with self.braced_block(
                        "for (int i = 0; i < bitbuffer->num_rows; ++i)"
                    ):
                        self._line(
                            "reflect_bytes(bitbuffer->bb[i], "
                            "(bitbuffer->bits_per_row[i] + 7) / 8);"
                        )
                self._blank()

            elif isinstance(step, FindRepeatedRow):
                self._line(
                    f"int row = bitbuffer_find_repeated_row(bitbuffer, "
                    f"{step.min_repeats}, {step.min_bits});"
                )
                self._line("if (row < 0)")
                with self.indent():
                    self._line("return DECODE_ABORT_EARLY;")
                if step.max_bits is not None:
                    self._line(f"if (bitbuffer->bits_per_row[row] > {step.max_bits})")
                else:
                    self._line(f"if (bitbuffer->bits_per_row[row] != {step.min_bits})")
                with self.indent():
                    self._line("return DECODE_ABORT_LENGTH;")
                row_expr = "row"
                self._blank()

            elif isinstance(step, SearchPreamble):
                if needs_search_row:
                    search_row_decl.emit(
                        self, f"unsigned search_row = {row_expr};"
                    )
                preamble_bytes = _preamble_to_bytes(step.pattern)
                preamble_bits = step.resolved_bit_length
                self._line(
                    f"uint8_t const preamble[] = "
                    f"{{{_hex_byte_literal(preamble_bytes)}}};"
                )

                self._line("if (bitbuffer->num_rows < 1)")
                with self.indent():
                    self._line("return DECODE_ABORT_LENGTH;")

                if step.scan_all_rows:
                    offset_decl.emit(self)
                    self._line("int preamble_found = 0;")
                    self._line(
                        "for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows "
                        "&& !preamble_found; ++row) {"
                    )
                    with self.indent():
                        self._line(
                            f"unsigned pos = bitbuffer_search(bitbuffer, row, 0, "
                            f"preamble, {preamble_bits});"
                        )
                        self._line("if (pos < bitbuffer->bits_per_row[row]) {")
                        with self.indent():
                            self._line("search_row = row;")
                            self._line("offset = pos;")
                            self._line("preamble_found = 1;")
                        self._line("}")
                    self._line("}")
                    self._line("if (!preamble_found)")
                    with self.indent():
                        self._line("return DECODE_ABORT_EARLY;")
                else:
                    offset_decl.emit(
                        self,
                        f"unsigned offset = bitbuffer_search(bitbuffer, "
                        f"{row_expr}, 0, preamble, {preamble_bits});",
                    )
                    self._line(f"if (offset >= bitbuffer->bits_per_row[{row_expr}])")
                    with self.indent():
                        self._line("return DECODE_ABORT_EARLY;")

            elif isinstance(step, SkipBits):
                if step.count < 0:
                    back = -step.count
                    self._line(f"if (offset < {back})")
                    with self.indent():
                        self._line("return DECODE_FAIL_SANITY;")
                    self._line(f"offset -= {back};")
                else:
                    self._line(f"offset += {step.count};")
                self._blank()

            elif isinstance(step, ManchesterDecode):
                if needs_search_row:
                    search_row_decl.emit(
                        self, f"unsigned search_row = {row_expr};"
                    )
                offset_decl.emit(self)
                manchester_row = "search_row" if needs_search_row else row_expr
                self._line("bitbuffer_t packet_bits = {0};")
                self._line(
                    f"bitbuffer_manchester_decode(bitbuffer, {manchester_row}, "
                    f"offset, &packet_bits, {step.max_bits});"
                )
                self._blank()
                post_manchester = True

        if post_manchester:
            self._line("if (packet_bits.bits_per_row[0] < 8)")
            with self.indent():
                self._line("return DECODE_ABORT_LENGTH;")
            self._line("uint8_t *b = packet_bits.bb[0];")
            self._blank()
        else:
            if skip_terminal_extract:
                pass
            else:
                total_bits = self._total_payload_bits(self.cls)
                total_bytes = (total_bits + 7) // 8
                if total_bytes > 0:
                    if offset_decl.emit(self):
                        self._blank()
                    self._line(f"uint8_t b[{total_bytes}];")
                    extract_row = (
                        "search_row" if (has_scan_all and not post_manchester) else row_expr
                    )
                    self._line(
                        f"bitbuffer_extract_bytes(bitbuffer, {extract_row}, offset, "
                        f"b, {total_bits});"
                    )
                    self._blank()

    def _emit_fields(self, fields: list) -> int:
        """Emit C code for a list of (name, spec) fields.

        Returns the updated static bit offset (or -1 if dynamic).
        """
        byte_array = "b"
        bit_offset = 0
        is_dynamic = False

        pending_val = set(self.cls.validation_hooks)
        available_sym = self._initial_available_symbols()
        self._inline_props_emitted.clear()
        self._inline_methods_emitted.clear()
        self._flush_inline_properties_and_validates(
            self.cls,
            None,
            self._inline,
            available_sym,
            pending_val,
            self._inline_props_emitted,
            self._inline_methods_emitted,
        )

        for name, spec in fields:
            if isinstance(spec, BitsSpec):
                off = "bit_pos" if is_dynamic else bit_offset
                expr = _extract_bits(byte_array, off, spec.width)
                if name.startswith("_"):
                    self._line(f"(void)({expr});")
                    if is_dynamic:
                        self._line(f"bit_pos += {spec.width};")
                    else:
                        bit_offset += spec.width
                else:
                    self._line(f"int {name} = {expr};")
                    if is_dynamic:
                        self._line(f"bit_pos += {spec.width};")
                    else:
                        bit_offset += spec.width

            elif isinstance(spec, LiteralSpec):
                off = "bit_pos" if is_dynamic else bit_offset
                expr = _extract_bits(byte_array, off, spec.width)
                self._line(f"if ({expr} != {hex(spec.value)})")
                with self.indent():
                    self._line("return DECODE_FAIL_SANITY;")
                if is_dynamic:
                    self._line(f"bit_pos += {spec.width};")
                else:
                    bit_offset += spec.width

            elif isinstance(spec, CondSpec):
                bit_offset = self._emit_cond_field(name, spec, byte_array, bit_offset)

            elif isinstance(spec, RepeatSpec):
                self._emit_repeat_field(name, spec, byte_array, bit_offset)
                is_dynamic = True
                bit_offset = -1

            elif isinstance(spec, RowsSpec):
                self._emit_rows_field(name, spec)

            available_sym |= self._symbols_from_top_level_field(name, spec)
            self._flush_inline_properties_and_validates(
                self.cls,
                None,
                self._inline,
                available_sym,
                pending_val,
                self._inline_props_emitted,
                self._inline_methods_emitted,
            )

        if pending_val:
            raise RuntimeError(
                f"{self.cls.__name__}: cannot run validation hooks {sorted(pending_val)!r}; "
                f"dependencies never satisfied (have {sorted(available_sym)!r})"
            )

        # Emit variant branches if present
        if self.variants:
            self._blank()
            self._emit_method_calls()
            self._emit_variant_branches(self.variants, byte_array, bit_offset)

        self._blank()
        return bit_offset

    def _emit_cond_field(
        self, name: str, spec: CondSpec, byte_array: str, bit_offset: int
    ) -> int:
        cond_expr = _emit_expr(spec.expr)

        if spec.false_type is None:
            self._line(f"int {name} = 0;")
            with self.braced_block(f"if ({cond_expr})"):
                self._line(
                    f"{name} = {_extract_bits(byte_array, bit_offset, spec.true_type.width)};"
                )
            return bit_offset + spec.true_type.width
        else:
            self._line(f"int {name};")
            with self.braced_block(f"if ({cond_expr})"):
                self._line(
                    f"{name} = {_extract_bits(byte_array, bit_offset, spec.true_type.width)};"
                )
            self._line("} else {")
            with self.indent():
                self._line(
                    f"{name} = {_extract_bits(byte_array, bit_offset, spec.false_type.width)};"
                )
            self._line("}")
            return bit_offset + max(spec.true_type.width, spec.false_type.width)

    def _emit_variant_branches(self, variants: list, byte_array: str, bit_offset: int):
        for i, variant_cls in enumerate(variants):
            when_ast = when_inline_expr(variant_cls)
            if when_ast is None:
                raise RuntimeError(
                    f"{variant_cls.__name__}: when() must return an inline bool expression "
                    "(same rules as properties; use explicit parameters for dependencies)"
                )
            when_expr = _emit_expr(when_ast)
            keyword = "if" if i == 0 else "} else if"
            # when_expr is already parenthesized by _emit_expr; avoid "if ((x == y))"
            self._line(f"{keyword} {when_expr} {{")
            with self.indent():
                var_offset = bit_offset
                variant_inline = self._try_inline_on(variant_cls)
                pending_v = set(variant_cls.validation_hooks)
                available_v = self._initial_available_symbols() | self._union_symbols_for_fields(
                    self.fields
                )
                emitted_variant_inline_props: set[str] = set()
                emitted_variant_inline_methods: set[str] = set()
                self._flush_inline_properties_and_validates(
                    variant_cls,
                    variant_cls,
                    variant_inline,
                    available_v,
                    pending_v,
                    emitted_variant_inline_props,
                    emitted_variant_inline_methods,
                )
                for name, spec in variant_cls.fields:
                    if isinstance(spec, BitsSpec):
                        if not name.startswith("_"):
                            self._line(
                                f"int {name} = {_extract_bits(byte_array, var_offset, spec.width)};"
                            )
                        var_offset += spec.width
                    elif isinstance(spec, LiteralSpec):
                        self._line(
                            f"if ({_extract_bits(byte_array, var_offset, spec.width)} != {hex(spec.value)})"
                        )
                        with self.indent():
                            self._line("return DECODE_FAIL_SANITY;")
                        var_offset += spec.width
                    available_v |= self._symbols_from_top_level_field(name, spec)
                    self._flush_inline_properties_and_validates(
                        variant_cls,
                        variant_cls,
                        variant_inline,
                        available_v,
                        pending_v,
                        emitted_variant_inline_props,
                        emitted_variant_inline_methods,
                    )

                if pending_v:
                    raise RuntimeError(
                        f"{variant_cls.__name__}: cannot run validation hooks "
                        f"{sorted(pending_v)!r}; dependencies never satisfied "
                        f"(have {sorted(available_v)!r})"
                    )

                for method_name in variant_cls.methods:
                    if method_name in emitted_variant_inline_methods:
                        continue
                    inline = variant_inline(method_name)
                    if inline is not None:
                        self._line(f"{_emit_expr(inline)};")
                    else:
                        func_name = (
                            f"{self.prefix}_{variant_cls.__name__}_{method_name}"
                        )
                        args = self._c_call_args_string(
                            variant_cls, method_name, variant_cls
                        )
                        self._line(f"{func_name}({args});")

                for prop_name, prop_type in variant_cls.properties:
                    if prop_name in emitted_variant_inline_props:
                        continue
                    inline = variant_inline(prop_name)
                    if inline is not None:
                        c_type = _c_type_for(prop_type)
                        self._line(f"{c_type} {prop_name} = {_emit_expr(inline)};")
                    else:
                        func_name = f"{self.prefix}_{variant_cls.__name__}_{prop_name}"
                        c_type = _c_type_for(prop_type)
                        v_ex = variant_cls.explicit_args().get(prop_name)
                        if v_ex:
                            args = ", ".join(v_ex)
                        else:
                            field_args = self._all_field_names_up_to(variant_cls)
                            args = ", ".join(field_args)
                        self._line(f"{c_type} {prop_name} = {func_name}({args});")

                self._emit_variant_data_make(variant_cls)

        self._line("}")
        self._blank()
        self._line("return DECODE_FAIL_SANITY;")

    def _emit_variant_data_make(self, variant_cls: type):
        """Emit data_make for a specific variant."""
        use_json_records = "to_json" in variant_cls.__dict__
        if use_json_records:
            records = variant_cls.json_records()
            overrides = self._prepare_json_value_overrides(records)
            self._blank()
            self._line("/* clang-format off */")
            self._line("data_t *data = data_make(")
            with self.indent():
                for idx, record in enumerate(records):
                    self._emit_json_record(record, value_override=overrides.get(idx))
                self._line("NULL);")
            self._line("/* clang-format on */")
            self._line("decoder_output_data(decoder, data);")
            self._line("return 1;")
            return

        shared_fields = [
            (n, s)
            for n, s in self.fields
            if not n.startswith("_") and isinstance(s, BitsSpec)
        ]
        variant_fields = [
            (n, s)
            for n, s in variant_cls.fields
            if not n.startswith("_") and isinstance(s, BitsSpec)
        ]
        variant_props = variant_cls.properties

        self._blank()
        self._line("/* clang-format off */")
        self._line("data_t *data = data_make(")
        with self.indent():
            cfg = getattr(self.cls, "modulation_config", ModulationConfig)
            model_name = cfg.output_model or cfg.device_name or self.cls.__name__
            self._line(f'"model", "", DATA_STRING, "{model_name}",')

            for name, spec in shared_fields:
                self._line(f'"{name}", "", DATA_INT, {name},')

            for name, spec in variant_fields:
                self._line(f'"{name}", "", DATA_INT, {name},')

            for prop_name, prop_type in variant_props:
                dtype = _data_make_type(prop_type)
                cast = _data_make_cast(prop_name, prop_type)
                self._line(f'"{prop_name}", "", {dtype}, {cast},')

            self._line("NULL);")
        self._line("/* clang-format on */")
        self._line("decoder_output_data(decoder, data);")
        self._line("return 1;")

    def _emit_rows_guards(self, spec: RowsSpec) -> None:
        max_row = max(spec.rows)
        self._line(f"if (bitbuffer->num_rows <= {max_row})")
        with self.indent():
            self._line("return DECODE_ABORT_LENGTH;")
        for row, bits in spec.required_bits:
            self._line(f"if (bitbuffer->bits_per_row[{row}] != {bits})")
            with self.indent():
                self._line("return DECODE_ABORT_LENGTH;")
        self._blank()

    def _emit_rows_field(self, name: str, spec: RowsSpec) -> None:
        """Parse sub_protocol fields from each listed bitbuffer row into sparse arrays."""
        sub_fields = spec.sub_protocol.fields
        for sub_name, sub_spec in sub_fields:
            if isinstance(sub_spec, (BitsSpec, LiteralSpec)):
                self._line(f"int {name}_{sub_name}[BITBUF_ROWS];")
            else:
                raise RuntimeError(
                    f"Protocol {self.cls.__name__}: Rows sub_protocol field "
                    f"{sub_name!r} must be Bits or Literal in this compiler version"
                )
        rows_csv = ", ".join(str(r) for r in spec.rows)
        nrows = len(spec.rows)
        self._line(f"static uint16_t const _rows_{name}[] = {{{rows_csv}}};")
        self._line(
            f"for (size_t _kir_{name} = 0; _kir_{name} < {nrows}; ++_kir_{name}) {{"
        )
        with self.indent():
            self._line(f"unsigned row_{name} = _rows_{name}[_kir_{name}];")
            self._line(f"uint8_t *rowb_{name} = bitbuffer->bb[row_{name}];")
            self._line(f"unsigned _roff_{name} = 0;")
            for sub_name, sub_spec in sub_fields:
                if isinstance(sub_spec, BitsSpec):
                    ex = _extract_bits(f"rowb_{name}", f"_roff_{name}", sub_spec.width)
                    if sub_name.startswith("_"):
                        self._line(f"(void)({ex});")
                    else:
                        self._line(f"{name}_{sub_name}[row_{name}] = {ex};")
                    self._line(f"_roff_{name} += {sub_spec.width};")
                elif isinstance(sub_spec, LiteralSpec):
                    ex = _extract_bits(f"rowb_{name}", f"_roff_{name}", sub_spec.width)
                    self._line(f"if ({ex} != {hex(sub_spec.value)})")
                    with self.indent():
                        self._line("return DECODE_FAIL_SANITY;")
                    self._line(f"_roff_{name} += {sub_spec.width};")
        self._line("}")
        self._blank()

    def _emit_repeat_field(
        self, name: str, spec: RepeatSpec, byte_array: str, bit_offset: int
    ):
        """Emit a counted-repeat loop.

        After this block, the C variable ``bit_pos`` holds the running
        offset, so subsequent fields switch to dynamic extraction.
        """
        count_expr = _emit_expr(spec.count_expr)
        sub_fields = spec.sub_protocol.fields
        max_count = 8
        for sub_name, sub_spec in sub_fields:
            if isinstance(sub_spec, BitsSpec):
                self._line(f"int {name}_{sub_name}[{max_count}];")

        self._line(f"int bit_pos = {bit_offset};")
        with self.braced_block(
            f"for (int _i = 0; _i < {count_expr} && _i < {max_count}; _i++)"
        ):
            for sub_name, sub_spec in sub_fields:
                if isinstance(sub_spec, BitsSpec):
                    self._line(
                        f"{name}_{sub_name}[_i] = {_extract_bits(byte_array, 'bit_pos', sub_spec.width)};"
                    )
                    self._line(f"bit_pos += {sub_spec.width};")
        self._blank()

    def _c_call_args_string(
        self, cls_or_variant: type, method_name: str, variant_cls: type | None
    ) -> str:
        ex = cls_or_variant.explicit_args().get(method_name)
        if ex:
            return ", ".join(ex)
        if variant_cls is not None:
            return ", ".join(["b"] + self._all_field_names_up_to(variant_cls))
        return ", ".join(["b"] + self._func_args())

    def _emit_method_calls(self):
        for method_name in self.methods:
            if method_name in self._inline_methods_emitted:
                continue
            inline = self._inline(method_name)
            if inline is not None:
                self._line(f"{_emit_expr(inline)};")
            else:
                func_name = f"{self.prefix}_{method_name}"
                args = ", ".join(
                    self._explicit_args(method_name) or (["b"] + self._func_args())
                )
                self._line(f"{func_name}({args});")

    def _emit_property_calls(self):
        property_items = self.properties
        if not property_items:
            return
        for prop_name, prop_type in property_items:
            if prop_name in self._inline_props_emitted:
                continue
            inline = self._inline(prop_name)
            if inline is not None:
                c_type = _c_type_for(prop_type)
                self._line(f"{c_type} {prop_name} = {_emit_expr(inline)};")
            else:
                func_name = f"{self.prefix}_{prop_name}"
                c_type = _c_type_for(prop_type)
                args = ", ".join(self._explicit_args(prop_name) or self._func_args())
                self._line(f"{c_type} {prop_name} = {func_name}({args});")
        self._blank()

    def _func_args(self) -> list:
        """Build the argument list for user-provided C functions.

        Scalar fields are passed by value. Repeat fields contribute
        the sub-field arrays and the count expression.
        """
        args: list[str] = []
        for name, spec in self.fields:
            if isinstance(spec, BitsSpec):
                args.append(name)
            elif isinstance(spec, RepeatSpec):
                for sub_name, sub_spec in spec.sub_protocol.fields:
                    if isinstance(sub_spec, BitsSpec):
                        args.append(f"{name}_{sub_name}")
                args.append(_emit_expr(spec.count_expr))
        return args

    def _emit_json_record(self, record: JsonRecord, value_override: str | None = None):
        key = record.key.replace('"', '\\"')
        label = record.label.replace('"', '\\"')
        dtype = record.data_type
        if value_override is not None:
            value = value_override
        elif isinstance(record.value, str):
            value = '"' + record.value.replace('"', '\\"') + '"'
        else:
            value = _emit_expr(record.value)

        parts = [f'"{key}"', f'"{label}"']
        if record.when is not None:
            parts.extend(["DATA_COND", _emit_expr(record.when)])
        if record.fmt is not None:
            parts.extend(["DATA_FORMAT", '"' + record.fmt.replace('"', '\\"') + '"'])
        parts.extend([dtype, value])
        self._line(", ".join(parts) + ",")

    def _prepare_json_value_overrides(
        self, records: tuple[JsonRecord, ...] | list[JsonRecord]
    ) -> dict[int, str]:
        overrides: dict[int, str] = {}
        for idx, record in enumerate(records):
            if (
                record.data_type == "DATA_STRING"
                and record.fmt is not None
                and not isinstance(record.value, str)
            ):
                var_name = f"json_str_{idx}"
                fmt = record.fmt.replace('"', '\\"')
                self._line(f"char {var_name}[64];")
                self._line(
                    f'snprintf({var_name}, sizeof({var_name}), "{fmt}", {_emit_expr(record.value)});'
                )
                overrides[idx] = var_name
        if overrides:
            self._blank()
        return overrides

    def _emit_data_make(self):
        if self.variants:
            return

        records = self.json_records
        overrides = self._prepare_json_value_overrides(records)

        self._line("/* clang-format off */")
        self._line("data_t *data = data_make(")
        with self.indent():
            for idx, record in enumerate(records):
                self._emit_json_record(record, value_override=overrides.get(idx))

            self._line("NULL);")
        self._line("/* clang-format on */")
        self._line("decoder_output_data(decoder, data);")
        self._line("return 1;")

    def _emit_output_fields(self):
        if self.delegates:
            names = []
            for delegate_cls in self.delegates:
                for name in delegate_cls.output_fields():
                    if name not in names:
                        names.append(name)
                for variant_cls in delegate_cls.variants:
                    for name in variant_cls.output_fields():
                        if name not in names:
                            names.append(name)
        else:
            names = list(self.cls.output_fields())
            for variant_cls in self.variants:
                for name in variant_cls.output_fields():
                    if name not in names:
                        names.append(name)
        with self.braced_block("static char const *const output_fields[] =", ";"):
            for name in names:
                self._line(f'"{name}",')
            self._line("NULL,")

    def _emit_r_device(self):
        device_cls = self.delegates[0] if self.delegates else self.cls
        cfg = getattr(device_cls, "modulation_config", ModulationConfig)
        if cfg.modulation is None:
            raise RuntimeError(
                f"Protocol {device_cls.__name__} must set modulation=Modulation.*"
            )
        name = cfg.device_name or device_cls.__name__
        with self.braced_block(f"r_device const {self.prefix} =", ";"):
            self._line(f'.name        = "{name}",')
            self._line(f".modulation  = {cfg.modulation.name},")
            self._line(f".short_width = {float(cfg.short_width)},")
            self._line(f".long_width  = {float(cfg.long_width)},")
            self._line(f".reset_limit = {float(cfg.reset_limit)},")
            if cfg.gap_limit is not None:
                self._line(f".gap_limit   = {float(cfg.gap_limit)},")
            if cfg.sync_width is not None:
                self._line(f".sync_width  = {float(cfg.sync_width)},")
            if cfg.tolerance is not None:
                self._line(f".tolerance   = {float(cfg.tolerance)},")
            self._line(f".decode_fn   = &{self.prefix}_decode,")
            self._line(".fields      = output_fields,")
            disabled = getattr(cfg, "disabled", None)
            if disabled is not None:
                self._line(f".disabled    = {int(disabled)},")

    def _total_payload_bits(self, cls: type) -> int:
        """Estimate max payload bits for bitbuffer_extract_bytes size."""
        total = 0
        max_repeat = 8
        for _, spec in cls.fields:
            if isinstance(spec, BitsSpec):
                total += spec.width
            elif isinstance(spec, LiteralSpec):
                total += spec.width
            elif isinstance(spec, CondSpec):
                if spec.false_type:
                    total += max(spec.true_type.width, spec.false_type.width)
                else:
                    total += spec.true_type.width
            elif isinstance(spec, RepeatSpec):
                sub_bits = sum(
                    s.width
                    for _, s in spec.sub_protocol.fields
                    if isinstance(s, (BitsSpec, LiteralSpec))
                )
                total += sub_bits * max_repeat
            elif isinstance(spec, RowsSpec):
                pass

        max_var_bits = 0
        for variant_cls in cls.variants:
            var_bits = sum(
                s.width
                for _, s in variant_cls.fields
                if isinstance(s, (BitsSpec, LiteralSpec))
            )
            max_var_bits = max(max_var_bits, var_bits)
        total += max_var_bits

        return total

    def _all_field_names_up_to(self, variant_cls: type) -> list:
        return [n for n, s in self.fields if isinstance(s, BitsSpec)] + [
            n for n, s in variant_cls.fields if isinstance(s, BitsSpec)
        ]
