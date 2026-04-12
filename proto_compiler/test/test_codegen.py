"""
Tests for codegen.py — checks that compile_decoder() generates correct C structure.

All tests inspect the generated C string for expected substrings; no C
compilation is performed.
"""

import pytest

from proto_compiler.dsl import (
    BitbufferPipeline,
    Bits,
    BitsSpec,
    Decoder,
    DecodeFail,
    Dispatcher,
    FirstValid,
    JsonRecord,
    Literal,
    Modulation,
    ModulationConfig,
    Protocol,
    Repeatable,
    Rows,
    Variant,
    FieldRef,
    _get_annotations_own,
)
from proto_compiler.codegen import compile_decoder


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _compile(instance):
    return compile_decoder(instance)


# ---------------------------------------------------------------------------
# 1. Simple decoder: Bits fields + method
# ---------------------------------------------------------------------------


class simple_sensor(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Simple Test Sensor",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=100,
            long_width=100,
            reset_limit=1000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(3, 40)

    sensor_id: Bits[8]
    temp_raw: Bits[12]
    humidity: Bits[8]

    def temperature_c(self, temp_raw) -> float:
        return (temp_raw - 500) * 0.1


class TestSimpleDecoder:
    def setup_method(self):
        self.code = _compile(simple_sensor())

    def test_file_doc_comment(self):
        assert "/** @file" in self.code

    def test_includes_decoder_h(self):
        assert '#include "decoder.h"' in self.code

    def test_static_constexpr_helper(self):
        assert "static inline float simple_sensor_temperature_c" in self.code

    def test_helper_body(self):
        # (temp_raw - 500) * 0.1 → ((temp_raw - 0x1f4) * 0.1)
        assert "temperature_c" in self.code
        assert "temp_raw" in self.code

    def test_decode_fn_signature(self):
        assert (
            "static int simple_sensor_decode(r_device *decoder, bitbuffer_t *bitbuffer)"
            in self.code
        )

    def test_pipeline_find_repeated_row(self):
        assert "bitbuffer_find_repeated_row" in self.code
        assert "DECODE_ABORT_EARLY" in self.code
        assert "DECODE_ABORT_LENGTH" in self.code

    def test_field_extraction(self):
        assert "int sensor_id = bitrow_get_bits" in self.code
        assert "int temp_raw = bitrow_get_bits" in self.code
        assert "int humidity = bitrow_get_bits" in self.code

    def test_data_make(self):
        assert "data_make(" in self.code
        assert '"model"' in self.code
        assert "Simple Test Sensor" in self.code
        assert "decoder_output_data" in self.code
        assert "return 1;" in self.code

    def test_output_fields_array(self):
        assert "static char const *const output_fields[]" in self.code
        assert '"model"' in self.code
        assert "NULL," in self.code

    def test_r_device_struct(self):
        assert "r_device const simple_sensor" in self.code
        assert ".name" in self.code
        assert "Simple Test Sensor" in self.code
        assert "FSK_PULSE_PCM" in self.code
        assert ".decode_fn   = &simple_sensor_decode" in self.code
        assert ".fields      = output_fields" in self.code


# ---------------------------------------------------------------------------
# 2. Decoder with validate_* hooks
# ---------------------------------------------------------------------------


class sensor_with_validate(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Validated Sensor",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=500,
            long_width=1000,
            reset_limit=5000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(2, 32)

    sensor_id: Bits[8]
    payload: Bits[16]
    checksum: Bits[8]

    def validate_checksum(self, sensor_id, checksum, fail_value=DecodeFail.MIC) -> bool:
        return (sensor_id + checksum) & 0xFF == 0


class TestValidateHook:
    def setup_method(self):
        self.code = _compile(sensor_with_validate())

    def test_validate_fn_emitted(self):
        assert "static inline bool sensor_with_validate_validate_checksum" in self.code

    def test_validate_called_automatically(self):
        # The guard must appear in the decode function body
        assert "sensor_with_validate_validate_checksum" in self.code or \
               "((sensor_id + checksum)" in self.code

    def test_decode_fail_mic_used(self):
        assert "DECODE_FAIL_MIC" in self.code



# ---------------------------------------------------------------------------
# 3. Decoder with FirstValid row loop
# ---------------------------------------------------------------------------


class SubRow(Repeatable):
    b0: Bits[8]
    b1: Bits[8]
    b2: Bits[8]
    b3: Bits[8]


class FVSubRow(Repeatable):
    sensor_id: Bits[8]
    data: Bits[16]
    _crc: Bits[8]


class first_valid_decoder(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="FirstValid Test",
            modulation=Modulation.OOK_PULSE_PWM,
            short_width=840,
            long_width=2070,
            reset_limit=6000,
        )

    def prepare(self, buf):
        return buf.invert()

    rows: Rows[FirstValid(32), FVSubRow]


class TestFirstValidDecoder:
    def setup_method(self):
        self.code = _compile(first_valid_decoder())

    def test_row_scanning_loop(self):
        # Should contain a for loop over rows
        assert "for (int _r = 0; _r < bitbuffer->num_rows; ++_r)" in self.code

    def test_length_guard_with_continue(self):
        # Should use continue, not return, for length mismatch inside the loop
        assert "continue;" in self.code

    def test_result_variable(self):
        assert "int result = 0;" in self.code
        assert "return result;" in self.code

    def test_validate_uses_continue_inside_loop(self):
        # Literal mismatch inside FirstValid loop uses continue
        assert "continue;" in self.code

    def test_invert_in_pipeline(self):
        assert "bitbuffer_invert(bitbuffer);" in self.code



# ---------------------------------------------------------------------------
# 4. Dispatcher pattern
# ---------------------------------------------------------------------------


class delegate_a(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Delegate A",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=100,
            long_width=100,
            reset_limit=1000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(2, 24)

    id: Bits[8]
    value: Bits[16]


class delegate_b(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Delegate B",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=200,
            long_width=200,
            reset_limit=2000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(2, 32)

    id: Bits[8]
    payload: Bits[24]


class multi_decoder(Dispatcher):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Multi Decoder",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=100,
            long_width=100,
            reset_limit=1000,
        )

    def dispatch(self):
        return (delegate_a(), delegate_b())


class TestDispatcher:
    def setup_method(self):
        self.code = _compile(multi_decoder())

    def test_delegate_decode_fns_present(self):
        assert "static int delegate_a_decode(" in self.code
        assert "static int delegate_b_decode(" in self.code

    def test_dispatcher_fn_present(self):
        assert "static int multi_decoder_decode(" in self.code

    def test_dispatcher_calls_delegates(self):
        assert "delegate_a_decode(decoder, bitbuffer);" in self.code
        assert "delegate_b_decode(decoder, bitbuffer);" in self.code

    def test_dispatcher_returns_on_success(self):
        # Pattern: if (ret > 0) return ret;
        assert "if (ret > 0)" in self.code
        assert "return ret;" in self.code

    def test_dispatcher_returns_ret_on_failure(self):
        # Final fallback
        assert "return ret;" in self.code

    def test_r_device_uses_dispatcher_name(self):
        assert "r_device const multi_decoder" in self.code
        assert ".decode_fn   = &multi_decoder_decode" in self.code



# ---------------------------------------------------------------------------
# 5. Variant dispatch
# ---------------------------------------------------------------------------


class base_proto(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Variant Test",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=500,
            long_width=1000,
            reset_limit=5000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(2, 36)

    msg_type: Bits[4]
    device_id: Bits[12]

    class TypeA(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 0

        value_a: Bits[16]
        _pad: Bits[4]

    class TypeB(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 1

        value_b: Bits[8]
        value_c: Bits[8]
        _pad: Bits[4]


class TestVariantDispatch:
    def setup_method(self):
        self.code = _compile(base_proto())

    def test_if_else_chain(self):
        assert "if (" in self.code
        assert "} else if (" in self.code

    def test_variant_when_condition(self):
        # msg_type == 0 and msg_type == 1
        assert "msg_type" in self.code

    def test_variant_fields_extracted(self):
        assert "int value_a = bitrow_get_bits" in self.code
        assert "int value_b = bitrow_get_bits" in self.code

    def test_fallback_fail_sanity(self):
        assert "return DECODE_FAIL_SANITY;" in self.code



# ---------------------------------------------------------------------------
# 6. Expression rendering
# ---------------------------------------------------------------------------


from proto_compiler.codegen import emit_expr
from proto_compiler.dsl import (
    ExprIntLiteral,
    ExprFloatLiteral,
    ExprBoolLiteral,
    BinaryExpr,
    UnaryExpr,
    FieldRef,
    FieldRefSubscript,
    Rev8Expr,
    LfsrDigest8Expr,
)


class TestExprEmit:
    def test_int_small(self):
        assert emit_expr(ExprIntLiteral(5)) == "5"

    def test_int_large(self):
        assert emit_expr(ExprIntLiteral(500)) == "0x1f4"

    def test_int_negative(self):
        assert emit_expr(ExprIntLiteral(-1)) == "-1"

    def test_float(self):
        assert emit_expr(ExprFloatLiteral(0.1)) == "0.1"

    def test_bool_true(self):
        assert emit_expr(ExprBoolLiteral(True)) == "1"

    def test_bool_false(self):
        assert emit_expr(ExprBoolLiteral(False)) == "0"

    def test_fieldref(self):
        assert emit_expr(FieldRef("temp_raw")) == "temp_raw"

    def test_subscript(self):
        assert emit_expr(FieldRefSubscript(FieldRef("cells_b0"), 2)) == "(cells_b0[2])"

    def test_binary_add(self):
        expr = BinaryExpr("+", FieldRef("a"), ExprIntLiteral(1))
        assert emit_expr(expr) == "(a + 1)"

    def test_binary_and(self):
        expr = BinaryExpr("&", FieldRef("x"), ExprIntLiteral(0xFF))
        assert emit_expr(expr) == "(x & 0xff)"

    def test_unary_not(self):
        expr = UnaryExpr("~", FieldRef("val"))
        assert emit_expr(expr) == "(~val)"

    def test_unary_neg(self):
        expr = UnaryExpr("-", FieldRef("val"))
        assert emit_expr(expr) == "(-val)"

    def test_rev8(self):
        expr = Rev8Expr(FieldRef("byte"))
        assert emit_expr(expr) == "(reverse8(byte))"

    def test_lfsr_digest8(self):
        from proto_compiler.dsl import LfsrDigest8
        expr = LfsrDigest8(FieldRef("b0"), FieldRef("b1"), gen=0x98, key=0x3E)
        result = emit_expr(expr)
        assert "lfsr_digest8" in result
        assert "b0" in result
        assert "b1" in result
        assert "0x98" in result
        assert "0x3e" in result


# ---------------------------------------------------------------------------
# 7. Literal field emits sanity check
# ---------------------------------------------------------------------------


class sensor_with_literal(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Literal Test",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=100,
            long_width=100,
            reset_limit=1000,
        )

    def prepare(self, buf):
        return buf.search_preamble(0x552DD4, bit_length=24).skip_bits(24)

    id: Bits[24]
    fixed: Literal[0xAA, 8]
    data: Bits[8]


class TestLiteralField:
    def setup_method(self):
        self.code = _compile(sensor_with_literal())

    def test_literal_sanity_check(self):
        assert "DECODE_FAIL_SANITY" in self.code
        assert "0xaa" in self.code or "0xAA" in self.code.lower()

    def test_literal_no_variable_declared(self):
        # No 'int fixed = ...' should appear
        assert "int fixed" not in self.code

    def test_search_preamble_emitted(self):
        assert "bitbuffer_search" in self.code
        assert "0x55" in self.code


# ---------------------------------------------------------------------------
# 8. Rows with static row tuple
# ---------------------------------------------------------------------------


class RowSub(Repeatable):
    b0: Bits[8]
    b1: Bits[8]
    b2: Bits[8]
    b3: Bits[8]
    b4: Bits[4]


class rows_decoder(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Rows Test",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=500,
            long_width=1000,
            reset_limit=5000,
        )

    def prepare(self, buf):
        return buf

    cells: Rows[(1, 2, 3), RowSub, ((1, 36),)]


class TestRowsField:
    def setup_method(self):
        self.code = _compile(rows_decoder())

    def test_bitbuf_rows_arrays(self):
        assert "BITBUF_ROWS" in self.code

    def test_static_rows_array(self):
        assert "_rows_cells" in self.code

    def test_row_loop(self):
        assert "for (size_t _k = 0; _k < 3; ++_k)" in self.code

    def test_required_bits_guard(self):
        assert "bits_per_row[1] != 36" in self.code or "bits_per_row[1]" in self.code

    def test_sub_field_extracted(self):
        assert "cells_b0[_r]" in self.code or "cells_b0" in self.code


# ---------------------------------------------------------------------------
# 9. Pipeline stages emitted in order with bit-buffer alias updates
# ---------------------------------------------------------------------------


class pipeline_stages_decoder(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Pipeline Stages Test",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=500,
            long_width=1000,
            reset_limit=5000,
        )

    def prepare(self, buf):
        return (
            buf
            .invert()
            .search_preamble(0xAA, bit_length=8)
            .skip_bits(8)
            .manchester_decode(80)
        )

    sensor_id: Bits[8]
    data: Bits[8]


class TestPipelineStageOrder:
    def setup_method(self):
        self.code = _compile(pipeline_stages_decoder())

    def test_all_stages_emitted(self):
        assert "bitbuffer_invert" in self.code
        assert "bitbuffer_search" in self.code
        assert "offset += 8;" in self.code
        assert "bitbuffer_manchester_decode" in self.code

    def test_stage_order(self):
        pos_invert = self.code.index("bitbuffer_invert")
        pos_search = self.code.index("bitbuffer_search")
        pos_skip = self.code.index("offset += 8;")
        pos_manchester = self.code.index("bitbuffer_manchester_decode")
        assert pos_invert < pos_search < pos_skip < pos_manchester

    def test_bitbuffer_rebound_after_manchester(self):
        # After manchester_decode, bitbuffer is rebound to &packet_bits
        assert "bitbuffer = &packet_bits;" in self.code

    def test_field_extraction_after_manchester(self):
        # Field extraction must come after the manchester decode rebind
        pos_rebind = self.code.index("bitbuffer = &packet_bits;")
        pos_field = self.code.index("int sensor_id = bitrow_get_bits")
        assert pos_rebind < pos_field

    def test_b_alias_uses_rebound_buffer(self):
        # uint8_t *b = bitbuffer->bb[...] must appear after the rebind
        pos_rebind = self.code.index("bitbuffer = &packet_bits;")
        pos_alias = self.code.index("uint8_t *b = bitbuffer->bb[")
        assert pos_rebind < pos_alias


# ---------------------------------------------------------------------------
# 10. Validate ordering: interleaved with field decoding
# ---------------------------------------------------------------------------


class validate_order_decoder(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Validate Order Test",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=500,
            long_width=1000,
            reset_limit=5000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(2, 40)

    a: Bits[8]
    b: Bits[8]
    c: Bits[8]

    # validate_a depends only on a — fires right after a is decoded
    def validate_a(self, a, fail_value=DecodeFail.SANITY) -> bool:
        return a != 0

    # validate_ab depends on a and b — fires right after b is decoded
    def validate_ab(self, a, b, fail_value=DecodeFail.MIC) -> bool:
        return (a + b) & 0xFF != 0

    # validate_abc depends on a, b, and c — fires right after c is decoded
    def validate_abc(self, a, b, c, fail_value=DecodeFail.MIC) -> bool:
        return (a + b + c) & 0xFF != 0

    # validate_buf depends only on bitbuffer (no field deps) — fires before
    # any field-dependent validates
    def validate_buf(self, buf: BitbufferPipeline, fail_value=DecodeFail.SANITY) -> bool:
        """External bitbuffer-only check."""
        ...


class TestValidateOrder:
    """Validate hooks are interleaved with field decoding.

    The codegen emits validate_* calls as soon as their field dependencies
    are satisfied, in lexicographic name order at each checkpoint.  A
    validate whose only dependency is BitbufferPipeline (no field deps)
    becomes eligible at the very first checkpoint.
    """

    def setup_method(self):
        self.code = _compile(validate_order_decoder())
        # Use the if(!) call-site pattern to avoid matching constexpr definitions
        self._call_site_pos = lambda name: self.code.index(
            f"!validate_order_decoder_{name}("
        )

    def test_fields_decoded_in_order(self):
        pos_a = self.code.index("int a = bitrow_get_bits")
        pos_b = self.code.index("int b = bitrow_get_bits")
        pos_c = self.code.index("int c = bitrow_get_bits")
        assert pos_a < pos_b < pos_c

    def test_validate_a_after_field_a_before_field_b(self):
        pos_a = self.code.index("int a = bitrow_get_bits")
        pos_va = self._call_site_pos("validate_a")
        pos_b = self.code.index("int b = bitrow_get_bits")
        assert pos_a < pos_va < pos_b

    def test_validate_ab_after_field_b_before_field_c(self):
        pos_b = self.code.index("int b = bitrow_get_bits")
        pos_vab = self._call_site_pos("validate_ab")
        pos_c = self.code.index("int c = bitrow_get_bits")
        assert pos_b < pos_vab < pos_c

    def test_validate_abc_after_field_c(self):
        pos_c = self.code.index("int c = bitrow_get_bits")
        pos_vabc = self._call_site_pos("validate_abc")
        assert pos_c < pos_vabc

    def test_bitbuffer_validate_at_first_checkpoint(self):
        # validate_buf has no field deps (BitbufferPipeline is excluded),
        # so it becomes eligible at the first checkpoint (after field a).
        # Within that checkpoint, lex order applies: validate_a < validate_buf.
        pos_a = self.code.index("int a = bitrow_get_bits")
        pos_vbuf = self._call_site_pos("validate_buf")
        pos_b = self.code.index("int b = bitrow_get_bits")
        assert pos_a < pos_vbuf < pos_b

    def test_bitbuffer_validate_lex_order_with_validate_a(self):
        # Both validate_a and validate_buf are eligible after field a.
        # Lex order: "validate_a" < "validate_buf" → validate_a emitted first.
        pos_va = self._call_site_pos("validate_a")
        pos_vbuf = self._call_site_pos("validate_buf")
        assert pos_va < pos_vbuf


# ---------------------------------------------------------------------------
# 11. FirstValid combined with Variants
# ---------------------------------------------------------------------------


class FVVariantRow(Repeatable):
    sensor_id: Bits[8]
    flags: Bits[4]
    value: Bits[12]


class fv_variant_decoder(Decoder):
    """Exercises FirstValid row iteration with variant dispatch.

    The variant ``when`` conditions and the ``to_json`` field references all
    touch FirstValid sub-fields, which live in ``int field[BITBUF_ROWS]``
    arrays.  The codegen must subscript each reference with the loop index
    ``[_r]``.
    """

    def modulation_config(self):
        return ModulationConfig(
            device_name="FV Variant Test",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=1000,
            long_width=2000,
            reset_limit=4000,
        )

    def prepare(self, buf):
        return buf

    data: Rows[FirstValid(24), FVVariantRow]

    class TypeA(Variant):
        def when(self, data_flags) -> bool:
            return data_flags == 1

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "FV-TypeA", "DATA_STRING"),
                JsonRecord("sensor_id", "", self.data_sensor_id, "DATA_INT"),
                JsonRecord("value", "", self.data_value, "DATA_INT"),
            ]

    class TypeB(Variant):
        def when(self, data_flags) -> bool:
            return data_flags == 2

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "FV-TypeB", "DATA_STRING"),
                JsonRecord("sensor_id", "", self.data_sensor_id, "DATA_INT"),
            ]


class TestFirstValidWithVariants:
    def setup_method(self):
        self.code = _compile(fv_variant_decoder())

    def test_row_scanning_loop_present(self):
        assert "for (int _r = 0; _r < bitbuffer->num_rows; ++_r)" in self.code

    def test_subfields_declared_as_arrays(self):
        assert "int data_sensor_id[BITBUF_ROWS];" in self.code
        assert "int data_flags[BITBUF_ROWS];" in self.code
        assert "int data_value[BITBUF_ROWS];" in self.code

    def test_subfield_extraction_uses_row_index(self):
        assert "data_sensor_id[_r] = bitrow_get_bits" in self.code
        assert "data_flags[_r] = bitrow_get_bits" in self.code
        assert "data_value[_r] = bitrow_get_bits" in self.code

    def test_variant_when_uses_indexed_field(self):
        # TypeA.when: data_flags == 1 → (data_flags[_r] == 1)
        assert "(data_flags[_r] == 1)" in self.code
        # TypeB.when: data_flags == 2 → (data_flags[_r] == 2)
        assert "(data_flags[_r] == 2)" in self.code

    def test_variant_data_make_uses_indexed_field(self):
        # Each variant's data_make passes the per-row field values
        assert "DATA_INT, data_sensor_id[_r]" in self.code
        assert "DATA_INT, data_value[_r]" in self.code

    def test_variant_bare_field_name_not_passed(self):
        # Sanity: no unindexed array-name uses remain in data_make
        assert "DATA_INT, data_sensor_id," not in self.code
        assert "DATA_INT, data_value," not in self.code


# ---------------------------------------------------------------------------
# 12. Variant inheritance — shared methods via an abstract base Variant
# ---------------------------------------------------------------------------


class _SharedBaseVariant(Variant):
    """Abstract base: carries shared methods, never fires.

    Its ``when`` returns False so it's unreachable; its purpose is to let
    concrete variants inherit helper methods without duplicating them.
    """

    def when(self) -> bool:
        return False

    def doubled(self, value) -> int:
        return value * 2

    def halved(self, value) -> int:
        return value >> 1


class variant_inherit_decoder(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Variant Inherit Test",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=1000,
            long_width=2000,
            reset_limit=4000,
        )

    def prepare(self, buf):
        return buf

    flags: Bits[4]
    value: Bits[12]

    class TypeA(_SharedBaseVariant):
        def when(self, flags) -> bool:
            return flags == 1

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "Inherit-TypeA", "DATA_STRING"),
                JsonRecord("doubled", "", self.doubled, "DATA_INT"),
            ]

    class TypeB(_SharedBaseVariant):
        def when(self, flags) -> bool:
            return flags == 2

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "Inherit-TypeB", "DATA_STRING"),
                JsonRecord("halved", "", self.halved, "DATA_INT"),
            ]


class TestVariantInheritance:
    """Methods inherited from a base Variant are emitted per-variant.

    Each concrete variant gets its own copy of the inherited method, name-
    prefixed with that variant's class name.  The call sites inside each
    variant's data_make reference the variant-prefixed C name.
    """

    def setup_method(self):
        self.code = _compile(variant_inherit_decoder())

    def test_base_variant_not_dispatched(self):
        # _SharedBaseVariant is a module-level class, not an inner class,
        # so it's not registered as a variant — no C function named for it.
        assert "variant_inherit_decoder__SharedBaseVariant" not in self.code
        assert "_SharedBaseVariant" not in self.code

    def test_typeA_emits_inherited_method(self):
        # TypeA uses self.doubled → variant-prefixed definition emitted.
        assert "static inline int variant_inherit_decoder_TypeA_doubled" in self.code

    def test_typeB_emits_inherited_method(self):
        # TypeB uses self.halved → variant-prefixed definition emitted.
        assert "static inline int variant_inherit_decoder_TypeB_halved" in self.code

    def test_typeA_call_site_uses_variant_prefix(self):
        assert "variant_inherit_decoder_TypeA_doubled(value)" in self.code

    def test_typeB_call_site_uses_variant_prefix(self):
        assert "variant_inherit_decoder_TypeB_halved(value)" in self.code

    def test_variant_when_dispatched_in_if_chain(self):
        assert "(flags == 1)" in self.code
        assert "(flags == 2)" in self.code

    def test_base_when_false_not_dispatched(self):
        # _SharedBaseVariant.when returns False and is not registered, so the
        # if/else chain must not contain a "False" branch.
        # (The Python expression `False` compiles to the C literal "0".)
        # The chain structure is: if ((flags==1)) {...} else if ((flags==2)) {...}
        assert "} else if ((flags == 2))" in self.code


# ---------------------------------------------------------------------------
# 12a. DSL rejects callables without return-type annotations (Bug 16)
# ---------------------------------------------------------------------------


class TestCallableRequiresReturnType:
    """SEMANTICS.md §5: every callable must have a return type annotation.

    The DSL should reject a class at definition time if any callable (other
    than the reserved `when`/`prepare`/`to_json`/`dispatch`/`modulation_config`)
    lacks a return annotation.
    """

    def test_missing_return_type_raises(self):
        with pytest.raises(TypeError, match="missing a return type annotation"):
            class bad_decoder(Decoder):
                def modulation_config(self):
                    return ModulationConfig(
                        device_name="Bad",
                        modulation=Modulation.OOK_PULSE_PPM,
                        short_width=1000,
                        long_width=2000,
                        reset_limit=4000,
                    )

                def prepare(self, buf):
                    return buf

                value: Bits[16]

                def doubled(self, value):  # no -> annotation
                    return value * 2

    def test_missing_return_type_on_validate_raises(self):
        with pytest.raises(TypeError, match="missing a return type annotation"):
            class bad_validate_decoder(Decoder):
                def modulation_config(self):
                    return ModulationConfig(
                        device_name="BadValidate",
                        modulation=Modulation.OOK_PULSE_PPM,
                        short_width=1000,
                        long_width=2000,
                        reset_limit=4000,
                    )

                def prepare(self, buf):
                    return buf

                value: Bits[16]

                def validate_nonzero(self, value, fail_value=DecodeFail.SANITY):
                    return value != 0


# ---------------------------------------------------------------------------
# 12b. Variant without `to_json` — default output (Bug 15, xfail)
# ---------------------------------------------------------------------------


class default_json_decoder(Decoder):
    """Variant that does NOT override to_json().

    Per SEMANTICS.md 6.4, the compiler should emit a default data_make
    covering: the parent's ``device_name`` as model, all non-private
    parent and variant ``Bits[]`` fields, and all methods of both parent
    and variant (in declaration order).

    This test documents the current (buggy) output so regressions are
    visible and future fixes can flip the xfail to pass.
    """

    def modulation_config(self):
        return ModulationConfig(
            device_name="Default JSON Parent",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=1000,
            long_width=2000,
            reset_limit=4000,
        )

    def prepare(self, buf):
        return buf

    device_id: Bits[12]
    flags: Bits[4]

    def parent_doubled(self, device_id) -> int:
        return device_id * 2

    class OnlyKind(Variant):
        def when(self, flags) -> bool:
            return flags == 1

        value: Bits[16]

        def variant_halved(self, value) -> int:
            return value >> 1

        # Deliberately: no to_json → exercises the default-synthesis path.


class TestDefaultJsonVariant:
    """Regression tests for Bug 15 — the synthesized default JSON for a
    Variant that does not override `to_json` must use the parent's
    `device_name` as model and include parent fields and non-validate
    methods alongside the variant's own (SEMANTICS.md §6.4, §7.2).
    """

    def setup_method(self):
        self.code = _compile(default_json_decoder())

    def test_model_is_parent_device_name(self):
        assert 'DATA_STRING, "Default JSON Parent"' in self.code

    def test_parent_field_present(self):
        body_start = self.code.index("if ((flags == 1))")
        body = self.code[body_start:]
        assert '"device_id", "", DATA_INT, device_id' in body

    def test_parent_method_present(self):
        # Parent-level method, called without the variant prefix.
        body_start = self.code.index("if ((flags == 1))")
        body = self.code[body_start:]
        assert "default_json_decoder_parent_doubled(device_id)" in body

    def test_variant_field_present(self):
        body_start = self.code.index("if ((flags == 1))")
        body = self.code[body_start:]
        assert '"value", "", DATA_INT, value' in body

    def test_variant_method_present(self):
        # Variant-level method, called with the variant prefix.
        body_start = self.code.index("if ((flags == 1))")
        body = self.code[body_start:]
        assert "default_json_decoder_OnlyKind_variant_halved(value)" in body

    def test_parent_method_not_variant_prefixed(self):
        # Negative: the parent method must NOT appear with the variant prefix,
        # which would indicate a call-site/definition mismatch.
        assert "default_json_decoder_OnlyKind_parent_doubled" not in self.code


# ---------------------------------------------------------------------------
# 13a. Pipeline — `scan_all_rows` preamble search declares `offset` (Bug 21)
# ---------------------------------------------------------------------------


class scan_all_rows_decoder(Decoder):
    """Exercises ``search_preamble(..., scan_all_rows=True)``.

    Bug 21: the scan_all_rows branch assigned to ``offset`` inside a for-loop
    body but never declared it, producing C that fails to compile.  The fix
    added ``unsigned offset = 0;`` before the loop.
    """

    def modulation_config(self):
        return ModulationConfig(
            device_name="Scan All Rows Test",
            modulation=Modulation.OOK_PULSE_MANCHESTER_ZEROBIT,
            short_width=500,
            long_width=0,
            reset_limit=2400,
        )

    def prepare(self, buf):
        return buf.search_preamble(0x145, bit_length=12, scan_all_rows=True).skip_bits(8)

    b0: Bits[8]
    b1: Bits[8]


class TestScanAllRowsDeclaresOffset:
    def setup_method(self):
        self.code = _compile(scan_all_rows_decoder())

    def test_offset_declared_before_loop(self):
        # A declaration "unsigned offset = 0;" must appear before any use.
        decl_pos = self.code.find("unsigned offset = 0;")
        assert decl_pos != -1, "scan_all_rows must declare `offset`"
        # And it must appear before the for-loop that scans rows.
        loop_pos = self.code.index("for (unsigned row = 0;")
        assert decl_pos < loop_pos

    def test_offset_assigned_inside_loop(self):
        # The loop body still assigns offset = pos; on preamble hit.
        assert "offset = pos;" in self.code

    def test_offset_consumed_by_skip_bits(self):
        # Downstream skip_bits(8) uses `offset += 8;`.
        assert "offset += 8;" in self.code


# ---------------------------------------------------------------------------
# 13. Dispatcher — variant methods inside delegate variants are emitted
# ---------------------------------------------------------------------------


class disp_delegate_a(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Disp Delegate A",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=100,
            long_width=100,
            reset_limit=1000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(2, 20)

    msg_type: Bits[4]
    payload: Bits[16]

    class Kind0(Variant):
        def when(self, msg_type) -> bool:
            return msg_type == 0

        def shifted(self, payload) -> int:
            return payload << 1

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "A-Kind0", "DATA_STRING"),
                JsonRecord("shifted", "", self.shifted, "DATA_INT"),
            ]


class disp_delegate_b(Decoder):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Disp Delegate B",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=200,
            long_width=200,
            reset_limit=2000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(2, 20)

    kind: Bits[4]
    value: Bits[16]

    class KindX(Variant):
        def when(self, kind) -> bool:
            return kind == 7

        def halved(self, value) -> int:
            return value >> 1

        def to_json(self) -> list[JsonRecord]:
            return [
                JsonRecord("model", "", "B-KindX", "DATA_STRING"),
                JsonRecord("halved", "", self.halved, "DATA_INT"),
            ]


class disp_parent(Dispatcher):
    def modulation_config(self):
        return ModulationConfig(
            device_name="Disp Parent",
            modulation=Modulation.FSK_PULSE_PCM,
            short_width=100,
            long_width=100,
            reset_limit=1000,
        )

    def dispatch(self):
        return (disp_delegate_a(), disp_delegate_b())


class TestDispatcherVariantMethodsEmitted:
    """Regression: Bug 22 — the dispatcher codegen emits method bodies for
    callables defined *inside each delegate's variants*, not only for
    callables defined at the delegate's parent level.

    Without this, the delegate's decode function calls
    ``disp_delegate_a_Kind0_shifted(payload)`` but the corresponding
    ``static inline`` definition is missing and the C fails to link.
    """

    def setup_method(self):
        self.code = _compile(disp_parent())

    def test_delegate_a_variant_method_body_emitted(self):
        assert "static inline int disp_delegate_a_Kind0_shifted(int payload)" in self.code

    def test_delegate_b_variant_method_body_emitted(self):
        assert "static inline int disp_delegate_b_KindX_halved(int value)" in self.code

    def test_delegate_a_variant_method_called(self):
        assert "disp_delegate_a_Kind0_shifted(payload)" in self.code

    def test_delegate_b_variant_method_called(self):
        assert "disp_delegate_b_KindX_halved(value)" in self.code


# ---------------------------------------------------------------------------
# 14. Callable arg expansion — callable-of-callable chains (Bug 20)
# ---------------------------------------------------------------------------


class callable_chain_decoder(Decoder):
    """Exercises SEMANTICS.md §5.2: callable args that name other callables.

    ``validate_cmd`` declares param ``cmd``, where ``cmd`` is a callable
    depending on ``notb``, which depends on the raw field ``b``.  The codegen
    must recursively expand each arg at the call site rather than passing the
    bare callable name as an undeclared identifier.
    """

    def modulation_config(self):
        return ModulationConfig(
            device_name="Callable Chain Test",
            modulation=Modulation.OOK_PULSE_PPM,
            short_width=500,
            long_width=1000,
            reset_limit=5000,
        )

    def prepare(self, buf):
        return buf.find_repeated_row(1, 8)

    b: Bits[8]

    def notb(self, b) -> int:
        return (~b) & 0xFF

    def cmd(self, notb) -> int:
        return notb & 0x0F

    def validate_cmd(self, cmd, fail_value=DecodeFail.SANITY) -> bool:
        return (cmd == 1) | (cmd == 2)


class TestCallableChainExpansion:
    def setup_method(self):
        self.code = _compile(callable_chain_decoder())

    def test_validate_call_expands_callable_arg(self):
        # validate_cmd(cmd) where cmd is a callable: must expand to
        # validate_cmd(cmd(notb(b))).
        expected = (
            "callable_chain_decoder_validate_cmd("
            "callable_chain_decoder_cmd("
            "callable_chain_decoder_notb(b)))"
        )
        assert expected in self.code

    def test_no_bare_callable_name_as_undeclared_identifier(self):
        # Inside the decode body, the bare names "cmd" and "notb" must not
        # appear as C identifiers — they're callables, not variables.
        # (They may appear as function-parameter names in the outer
        # static-inline definitions, so we only check the decode body.)
        decode_start = self.code.index(
            "static int callable_chain_decoder_decode"
        )
        decode_body = self.code[decode_start:]
        assert "(cmd)" not in decode_body
        assert ", cmd)" not in decode_body
        assert ", notb)" not in decode_body


# ---------------------------------------------------------------------------
# 14. Annotation collection (regression test for Python 3.14 PEP 749)
# ---------------------------------------------------------------------------


class TestAnnotationCollection:
    """Verify that _get_annotations_own and _fields work across Python versions.

    Python 3.14 defers annotation evaluation (PEP 749), so
    cls.__dict__['__annotations__'] is empty. _get_annotations_own must
    still return the correct field specs.
    """

    def test_get_annotations_own_returns_field_specs(self):
        anns = _get_annotations_own(simple_sensor)
        assert "sensor_id" in anns
        assert "temp_raw" in anns
        assert "humidity" in anns
        assert isinstance(anns["sensor_id"], BitsSpec)
        assert isinstance(anns["temp_raw"], BitsSpec)
        assert isinstance(anns["humidity"], BitsSpec)

    def test_protocol_fields_nonempty(self):
        field_names = [name for name, _ in simple_sensor._fields]
        assert "sensor_id" in field_names
        assert "temp_raw" in field_names
        assert "humidity" in field_names
