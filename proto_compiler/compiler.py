"""
Compiler: reads a Protocol subclass and emits a C++ decoder file for rtl_433.
"""

from __future__ import annotations
from contextlib import contextmanager
from pathlib import PurePosixPath
from typing import Any

from .dsl import (
    BitsSpec,
    CondSpec,
    Expr,
    FieldRef,
    JsonRecord,
    LiteralSpec,
    ProtocolConfig,
    RepeatSpec,
    UnaryExpr,
)


def compile(protocol_cls: type, source_path: str | None = None) -> str:
    """Compile a Protocol subclass into a complete C++ decoder source string."""
    return _CompileContext(protocol_cls, source_path=source_path).emit()


def _preamble_to_bytes(value: int) -> bytes:
    """Convert an int like 0x552DD4 to big-endian bytes."""
    if value == 0:
        return b"\x00"
    length = (value.bit_length() + 7) // 8
    return value.to_bytes(length, "big")


def _emit_expr(node: Any) -> str:
    """Recursively render an expression tree node as a C expression string."""
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
    if isinstance(node, Expr):
        return f"({_emit_expr(node.left)} {node.op} {_emit_expr(node.right)})"
    if isinstance(node, UnaryExpr):
        return f"({node.op}{_emit_expr(node.operand)})"
    raise TypeError(f"Cannot emit expression node: {type(node).__name__}")


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


class _CompileContext:
    def __init__(self, cls: type, source_path: str | None = None):
        self.cls = cls
        self.prefix = cls.__name__
        self.source_path = source_path
        self.lines: list[str] = []
        self._indent = 0
        self.fields = cls.fields()
        self.variants = cls.variants()
        self.methods = cls.methods()
        self.properties = cls.properties()
        self.method_exprs = cls.method_exprs()
        self.property_exprs = cls.property_exprs()
        self.explicit_args_map = cls.explicit_args()
        self.json_records = cls.json_records()
        self.delegates = self._delegate_protocols()

    def _delegate_protocols(self) -> tuple[type, ...]:
        return self.cls.delegated_protocols()

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
            child = _CompileContext(delegate_cls, source_path=None)
            child._emit_decode_fn()
            self.lines.extend(child.lines)
            self._blank()
        self._emit_decode_fn()
        self._blank()
        self._emit_output_fields()
        self._blank()
        self._emit_r_device()
        return "\n".join(self.lines) + "\n"

    # -- header --

    def _has_external_helpers(self) -> bool:
        if self.delegates:
            return any(delegate_cls.requires_external_helpers() for delegate_cls in self.delegates)
        return self.cls.requires_external_helpers()

    def _explicit_args(self, name: str) -> list[str] | None:
        args = self.explicit_args_map.get(name)
        if args is None:
            return None
        return list(args)

    def _emit_header(self):
        if self.source_path:
            self._line(f"// Generated from {PurePosixPath(self.source_path).name}")
        self._line('#include "decoder.h"')
        if self._has_external_helpers():
            if self.delegates:
                for delegate_cls in self.delegates:
                    if delegate_cls.requires_external_helpers():
                        self._line(f'#include "{delegate_cls.__name__}.h"')
            else:
                self._line(f'#include "{self.prefix}.h"')
        self._blank()

    # -- decode function --

    def _emit_decode_fn(self):
        if self.delegates:
            self._line(
                f"static int {self.prefix}_decode(r_device *decoder, bitbuffer_t *bitbuffer)"
            )
            with self.braced_block():
                self._line("int ret = DECODE_ABORT_EARLY;")
                for idx, delegate_cls in enumerate(self.delegates):
                    self._line(f"ret = {delegate_cls.__name__}_decode(decoder, bitbuffer);")
                    self._line("if (ret > 0)")
                    with self.indent():
                        self._line("return ret;")
                    if idx + 1 < len(self.delegates):
                        cfg = getattr(delegate_cls, "config", ProtocolConfig)
                        if cfg.invert:
                            self._line("bitbuffer_invert(bitbuffer);")
                        if cfg.reflect:
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
            self._emit_transforms()
            self._emit_preamble_search()
            self._emit_field_extraction()

            has_variants = bool(self.variants)
            if not has_variants:
                self._emit_method_calls()
                self._emit_property_calls()
                self._emit_data_make()

    # -- pre-processing transforms --

    def _emit_transforms(self):
        cfg = getattr(self.cls, "config", ProtocolConfig)
        if cfg.invert:
            self._line("bitbuffer_invert(bitbuffer);")
            self._blank()

        if cfg.reflect:
            with self.braced_block("for (int i = 0; i < bitbuffer->num_rows; ++i)"):
                self._line(
                    "reflect_bytes(bitbuffer->bb[i], (bitbuffer->bits_per_row[i] + 7) / 8);"
                )
            self._blank()

    # -- preamble search --

    def _emit_preamble_search(self):
        cfg = getattr(self.cls, "config", ProtocolConfig)()
        preamble = cfg.preamble
        manchester = cfg.manchester
        scan_rows = cfg.scan_rows
        need_search_row = manchester or scan_rows == "all"

        if need_search_row:
            self._line("unsigned search_row = 0;")

        if preamble is not None:
            preamble_bytes = _preamble_to_bytes(preamble)
            preamble_bits = cfg.resolved_preamble_bit_length
            start_offset = cfg.resolved_manchester_start_offset_bits
            self._line(
                f"uint8_t const preamble[] = {{{_hex_byte_literal(preamble_bytes)}}};"
            )

            self._line("if (bitbuffer->num_rows < 1)")
            with self.indent():
                self._line("return DECODE_ABORT_LENGTH;")

            if scan_rows == "all":
                self._line("unsigned offset = 0;")
                self._line("int preamble_found = 0;")
                self._line(
                    "for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows && !preamble_found; ++row) {"
                )
                with self.indent():
                    self._line(
                        f"unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, {preamble_bits});"
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
                self._line(
                    f"unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, {preamble_bits});"
                )
                self._line("if (offset >= bitbuffer->bits_per_row[0])")
                with self.indent():
                    self._line("return DECODE_ABORT_EARLY;")
            self._line(f"offset += {start_offset};")
            self._blank()
        else:
            self._line("unsigned offset = 0;")
            self._blank()

        if manchester:
            decode_max_bits = cfg.manchester_decode_max_bits
            self._line("bitbuffer_t packet_bits = {0};")
            self._line(
                f"bitbuffer_manchester_decode(bitbuffer, {'search_row' if need_search_row else '0'}, offset, &packet_bits, {decode_max_bits});"
            )
            self._blank()
            self._line("if (packet_bits.bits_per_row[0] < 8)")
            with self.indent():
                self._line("return DECODE_ABORT_LENGTH;")
            self._line("uint8_t *b = packet_bits.bb[0];")
            self._blank()
        else:
            total_bits = self._total_payload_bits(self.cls)
            total_bytes = (total_bits + 7) // 8
            if total_bytes > 0:
                self._line(f"uint8_t b[{total_bytes}];")
                self._line(
                    f"bitbuffer_extract_bytes(bitbuffer, 0, offset, b, {total_bits});"
                )
                self._blank()

    # -- field extraction --

    def _emit_field_extraction(self) -> int:
        """Emit extraction code for all fields. Returns the final bit offset."""
        return self._emit_fields(self.fields, "b", 0, False)

    def _emit_fields(
        self, fields: list, byte_array: str, bit_offset: int, dynamic: bool
    ) -> int:
        """Emit C code for a list of (name, spec) fields.

        When *dynamic* is True, a C variable ``bit_pos`` holds the current
        offset at runtime and ``_extract_dynamic`` is used instead of the
        static ``_extract_field_expr``.

        Returns the updated static bit offset (or -1 if dynamic).
        """
        for name, spec in fields:
            if isinstance(spec, BitsSpec):
                off = "bit_pos" if dynamic else bit_offset
                expr = _extract_bits(byte_array, off, spec.width)
                if name.startswith("_"):
                    if dynamic:
                        self._line(f"bit_pos += {spec.width};")
                    else:
                        bit_offset += spec.width
                else:
                    self._line(f"int {name} = {expr};")
                    if dynamic:
                        self._line(f"bit_pos += {spec.width};")
                    else:
                        bit_offset += spec.width

            elif isinstance(spec, LiteralSpec):
                off = "bit_pos" if dynamic else bit_offset
                expr = _extract_bits(byte_array, off, spec.width)
                self._line(f"if ({expr} != {hex(spec.value)})")
                with self.indent():
                    self._line("return DECODE_FAIL_SANITY;")
                if dynamic:
                    self._line(f"bit_pos += {spec.width};")
                else:
                    bit_offset += spec.width

            elif isinstance(spec, CondSpec):
                bit_offset = self._emit_cond_field(name, spec, byte_array, bit_offset)

            elif isinstance(spec, RepeatSpec):
                self._emit_repeat_field(name, spec, byte_array, bit_offset)
                dynamic = True
                bit_offset = -1

        # Emit variant branches if present
        variants = self.variants
        if variants:
            self._blank()
            self._emit_variant_branches(variants, byte_array, bit_offset)

        self._blank()
        return bit_offset

    # -- cond codegen --

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

    # -- variant codegen --

    def _emit_variant_branches(self, variants: list, byte_array: str, bit_offset: int):
        for i, variant_cls in enumerate(variants):
            when_expr = _emit_expr(variant_cls.when)
            keyword = "if" if i == 0 else "} else if"
            # when_expr is already parenthesized by _emit_expr; avoid "if ((x == y))"
            self._line(f"{keyword} {when_expr} {{")
            with self.indent():
                var_offset = bit_offset
                for name, spec in variant_cls.fields():
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

                method_exprs = variant_cls.method_exprs()
                for method_name in variant_cls.methods():
                    inline = method_exprs.get(method_name)
                    if inline is not None:
                        if method_name == "validate":
                            self._line(f"if (!({_emit_expr(inline)}))")
                            with self.indent():
                                self._line("return DECODE_FAIL_SANITY;")
                        else:
                            self._line(f"{_emit_expr(inline)};")
                    else:
                        func_name = (
                            f"{self.prefix}_{variant_cls.__name__}_{method_name}"
                        )
                        all_field_names = self._all_field_names_up_to(
                            variant_cls, byte_array
                        )
                        args = ", ".join(["b"] + all_field_names)
                        if method_name == "validate":
                            self._line(f"int valid = {func_name}({args});")
                            self._line("if (valid != 0)")
                            with self.indent():
                                self._line("return valid;")
                        else:
                            self._line(f"{func_name}({args});")

                property_exprs = variant_cls.property_exprs()
                for prop_name, prop_type in variant_cls.properties():
                    inline = property_exprs.get(prop_name)
                    if inline is not None:
                        c_type = _c_type_for(prop_type)
                        self._line(f"{c_type} {prop_name} = {_emit_expr(inline)};")
                    else:
                        func_name = f"{self.prefix}_{variant_cls.__name__}_{prop_name}"
                        c_type = _c_type_for(prop_type)
                        field_args = self._all_field_names_up_to(
                            variant_cls, byte_array
                        )
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
            for n, s in variant_cls.fields()
            if not n.startswith("_") and isinstance(s, BitsSpec)
        ]
        variant_props = variant_cls.properties()

        self._blank()
        self._line("/* clang-format off */")
        self._line("data_t *data = data_make(")
        with self.indent():
            model_name = (
                getattr(self.cls, "config", ProtocolConfig).device_name
                or self.cls.__name__
            )
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

    # -- repeat codegen --

    def _emit_repeat_field(
        self, name: str, spec: RepeatSpec, byte_array: str, bit_offset: int
    ):
        """Emit a counted-repeat loop.

        After this block, the C variable ``bit_pos`` holds the running
        offset, so subsequent fields switch to dynamic extraction.
        """
        count_expr = _emit_expr(spec.count_expr)
        sub_fields = spec.sub_protocol.fields()
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

    # -- method calls --

    def _emit_method_calls(self):
        method_exprs = self.method_exprs
        for method_name in self.methods:
            inline = method_exprs.get(method_name)
            if inline is not None:
                if method_name == "validate":
                    self._line(f"if (!({_emit_expr(inline)}))")
                    with self.indent():
                        self._line("return DECODE_FAIL_SANITY;")
                    self._blank()
                else:
                    self._line(f"{_emit_expr(inline)};")
            else:
                func_name = f"{self.prefix}_{method_name}"
                args = ", ".join(
                    self._explicit_args(method_name) or (["b"] + self._func_args())
                )
                if method_name == "validate":
                    self._line(f"int valid = {func_name}({args});")
                    self._line("if (valid != 0)")
                    with self.indent():
                        self._line("return valid;")
                    self._blank()
                else:
                    self._line(f"{func_name}({args});")

    # -- property calls --

    def _emit_property_calls(self):
        property_items = self.properties
        if not property_items:
            return
        property_exprs = self.property_exprs
        for prop_name, prop_type in property_items:
            inline = property_exprs.get(prop_name)
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
                for sub_name, sub_spec in spec.sub_protocol.fields():
                    if isinstance(sub_spec, BitsSpec):
                        args.append(f"{name}_{sub_name}")
                args.append(_emit_expr(spec.count_expr))
        return args

    # -- data_make --

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

    # -- output_fields --

    def _emit_output_fields(self):
        if self.delegates:
            names = []
            for delegate_cls in self.delegates:
                for name in delegate_cls.output_fields():
                    if name not in names:
                        names.append(name)
                for variant_cls in delegate_cls.variants():
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

    # -- r_device struct --

    def _emit_r_device(self):
        device_cls = self.delegates[0] if self.delegates else self.cls
        cfg = getattr(device_cls, "config", ProtocolConfig)
        if cfg.modulation is None:
            raise RuntimeError(
                f"Protocol {device_cls.__name__} must set modulation=Modulation.*"
            )
        name = cfg.device_name or device_cls.__name__
        with self.braced_block(f"r_device const {self.prefix} =", ";"):
            self._line(f'.name        = "{name}",')
            self._line(f".modulation  = {cfg.modulation.name},")
            self._line(f".short_width = {float(cfg.short_width or 0.0)},")
            self._line(f".long_width  = {float(cfg.long_width or 0.0)},")
            self._line(f".reset_limit = {float(cfg.reset_limit or 0.0)},")
            if cfg.gap_limit is not None:
                self._line(f".gap_limit   = {float(cfg.gap_limit)},")
            if cfg.sync_width is not None:
                self._line(f".sync_width  = {float(cfg.sync_width)},")
            if cfg.tolerance is not None:
                self._line(f".tolerance   = {float(cfg.tolerance)},")
            self._line(f".decode_fn   = &{self.prefix}_decode,")
            self._line(".fields      = output_fields,")

    # -- utility --

    def _total_payload_bits(self, cls: type) -> int:
        """Estimate max payload bits for bitbuffer_extract_bytes size."""
        total = 0
        max_repeat = 8
        for _, spec in cls.fields():
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
                    for _, s in spec.sub_protocol.fields()
                    if isinstance(s, (BitsSpec, LiteralSpec))
                )
                total += sub_bits * max_repeat

        max_var_bits = 0
        for variant_cls in cls.variants():
            var_bits = sum(
                s.width
                for _, s in variant_cls.fields()
                if isinstance(s, (BitsSpec, LiteralSpec))
            )
            max_var_bits = max(max_var_bits, var_bits)
        total += max_var_bits

        return total

    def _all_field_names_up_to(self, variant_cls: type, byte_array: str) -> list:
        """Collect field names from the parent protocol + the variant."""
        names = []
        for n, s in self.fields:
            if isinstance(s, BitsSpec):
                names.append(n)
        for n, s in variant_cls.fields():
            if isinstance(s, BitsSpec):
                names.append(n)
        return names
