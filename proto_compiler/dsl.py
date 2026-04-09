"""
Annotation-style DSL for declaring binary protocol field layouts.

Protocol subclasses use type annotations to describe bit-level fields.
A companion compiler reads these annotations and emits C++ decoder code.
"""

from __future__ import annotations

import ast
from dataclasses import dataclass
from enum import Enum, auto
import inspect
import sys
import textwrap
from typing import Any, ClassVar, NamedTuple, Union


class DecodeFail(str, Enum):
    """rtl_433 ``decode_return_codes`` (``r_device.h``); use as ``fail_value=`` on ``validate_*``."""

    MIC = "DECODE_FAIL_MIC"
    SANITY = "DECODE_FAIL_SANITY"
    ABORT_EARLY = "DECODE_ABORT_EARLY"
    ABORT_LENGTH = "DECODE_ABORT_LENGTH"


# ---------------------------------------------------------------------------
# Expression tree – :class:`FieldRef` and operator overloading on :class:`Expr`
# ---------------------------------------------------------------------------


class Expr:
    """Base for composable expressions. Operators build :class:`BinaryExpr` nodes."""

    __slots__ = ()

    def __eq__(self, other: Any) -> BinaryExpr:  # type: ignore[override]
        return BinaryExpr("==", self, _wrap(other))

    def __ne__(self, other: Any) -> BinaryExpr:  # type: ignore[override]
        return BinaryExpr("!=", self, _wrap(other))

    def __lt__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("<", self, _wrap(other))

    def __le__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("<=", self, _wrap(other))

    def __gt__(self, other: Any) -> BinaryExpr:
        return BinaryExpr(">", self, _wrap(other))

    def __ge__(self, other: Any) -> BinaryExpr:
        return BinaryExpr(">=", self, _wrap(other))

    def __and__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("&", self, _wrap(other))

    def __rand__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("&", _wrap(other), self)

    def __or__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("|", self, _wrap(other))

    def __ror__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("|", _wrap(other), self)

    def __xor__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("^", self, _wrap(other))

    def __rxor__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("^", _wrap(other), self)

    def __invert__(self) -> UnaryExpr:
        return UnaryExpr("~", self)

    def __add__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("+", self, _wrap(other))

    def __radd__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("+", _wrap(other), self)

    def __sub__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("-", self, _wrap(other))

    def __rsub__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("-", _wrap(other), self)

    def __mul__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("*", self, _wrap(other))

    def __rmul__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("*", _wrap(other), self)

    def __truediv__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("/", self, _wrap(other))

    def __rtruediv__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("/", _wrap(other), self)

    def __floordiv__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("/", self, _wrap(other))

    def __rfloordiv__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("/", _wrap(other), self)

    def __mod__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("%", self, _wrap(other))

    def __neg__(self) -> UnaryExpr:
        return UnaryExpr("-", self)

    def __lshift__(self, other: Any) -> BinaryExpr:
        return BinaryExpr("<<", self, _wrap(other))

    def __rshift__(self, other: Any) -> BinaryExpr:
        return BinaryExpr(">>", self, _wrap(other))


@dataclass(frozen=True, eq=False)
class ExprIntLiteral(Expr):
    """Integer constant in the expression AST (``bool`` is not an ``int`` here)."""

    value: int

    def __hash__(self) -> int:
        return hash(("ExprIntLiteral", self.value))


@dataclass(frozen=True, eq=False)
class ExprFloatLiteral(Expr):
    value: float

    def __hash__(self) -> int:
        return hash(("ExprFloatLiteral", self.value))


@dataclass(frozen=True, eq=False)
class ExprBoolLiteral(Expr):
    value: bool

    def __hash__(self) -> int:
        return hash(("ExprBoolLiteral", self.value))


@dataclass(frozen=True, eq=False)
class BinaryExpr(Expr):
    """Binary operator node (``left op right``).

    ``eq=False`` so :class:`Expr` comparison dunders build DSL nodes; dataclass
    structural ``==`` would make ``(expr == 3)`` evaluate to Python ``bool``.
    """

    op: str
    left: Any
    right: Any

    def __hash__(self) -> int:
        return hash(("BinaryExpr", self.op, self.left, self.right))


@dataclass(frozen=True, eq=False)
class Rev8Expr(Expr):
    """``reverse8`` applied to a sub-expression (rtl_433 bit_util).

    ``eq=False`` so comparisons delegate to :class:`Expr` (DSL AST), not dataclass
    field equality.
    """

    operand: Any

    def __hash__(self) -> int:
        return hash(("Rev8Expr", self.operand))


@dataclass(frozen=True, eq=False)
class UnaryExpr(Expr):
    """Unary expression node (e.g. bitwise NOT, negation)."""

    op: str
    operand: Any

    def __hash__(self) -> int:
        return hash(("UnaryExpr", self.op, self.operand))


def Rev8(operand: Expr | int) -> Rev8Expr:
    """Build ``reverse8(operand)`` in generated C."""
    if isinstance(operand, float):
        raise TypeError("Rev8 does not accept float")
    return Rev8Expr(operand=_wrap(operand))


@dataclass(frozen=True, eq=False)
class LfsrDigest8Expr(Expr):
    """``lfsr_digest8`` over a byte list (``bit_util.h``)."""

    bytes_args: tuple[Any, ...]
    gen: int
    key: int

    def __hash__(self) -> int:
        return hash(("LfsrDigest8Expr", self.bytes_args, self.gen, self.key))


def LfsrDigest8(*bytes_args: Expr | int, gen: int, key: int) -> LfsrDigest8Expr:
    """MIC-style digest: ``lfsr_digest8(bytes, n, gen, key)`` in emitted C."""
    wrapped: list[Expr] = []
    for a in bytes_args:
        if isinstance(a, bool):
            raise TypeError("LfsrDigest8 does not accept bool")
        if isinstance(a, float):
            raise TypeError("LfsrDigest8 does not accept float")
        wrapped.append(_wrap(a))
    return LfsrDigest8Expr(bytes_args=tuple(wrapped), gen=gen, key=key)


@dataclass(frozen=True, eq=False)
class FieldRef(Expr):
    """Reference to a previously parsed protocol field."""

    name: str

    def __getitem__(self, row: int) -> Subscript:
        if not isinstance(row, int):
            raise TypeError("Row index must be int")
        return Subscript(self, row)

    def __hash__(self) -> int:
        return hash(("FieldRef", self.name))


def _wrap(value: Any) -> Expr:
    """Wrap scalars as literal :class:`Expr` nodes; pass through existing ``Expr``."""
    if isinstance(value, bool):
        return ExprBoolLiteral(value)
    if isinstance(value, int):
        return ExprIntLiteral(value)
    if isinstance(value, float):
        return ExprFloatLiteral(value)
    if isinstance(value, Expr):
        return value
    raise TypeError(f"Cannot use {type(value).__name__} in a field expression")


def _coerce_protocol_expr_result(value: Any) -> Expr | None:
    """Normalize hook/property return values to a single :class:`Expr` root, or ``None``."""
    if isinstance(value, Expr):
        return value
    if isinstance(value, bool):
        return ExprBoolLiteral(value)
    if isinstance(value, int):
        return ExprIntLiteral(value)
    if isinstance(value, float):
        return ExprFloatLiteral(value)
    return None


@dataclass(frozen=True, eq=False)
class Subscript(Expr):
    """Index into a Rows-generated array, e.g. ``cells_b0[5]`` (``FieldRef`` + ``[row]``)."""

    base: FieldRef
    index: int

    def __hash__(self) -> int:
        return hash(("Subscript", self.base, self.index))


class _JsonProxy:
    def __init__(self, protocol_cls: type):
        self.__protocol_cls__ = protocol_cls

    def __getattr__(self, name: str) -> FieldRef:
        if name.startswith("_"):
            raise AttributeError(name)
        return FieldRef(name)


# ---------------------------------------------------------------------------
# Field spec descriptors – produced by __class_getitem__ on Bits, Literal, etc.
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class BitsSpec:
    width: int


@dataclass(frozen=True)
class LiteralSpec:
    value: int
    width: int


@dataclass(frozen=True)
class CondSpec:
    expr: Expr
    true_type: BitsSpec | LiteralSpec
    false_type: BitsSpec | LiteralSpec | None


@dataclass(frozen=True)
class RepeatSpec:
    count_expr: Expr
    sub_protocol: type  # a Protocol subclass


@dataclass(frozen=True)
class RowsSpec:
    """Repeat sub-fields over fixed bitbuffer row indices (see ``Rows``)."""

    rows: tuple[int, ...]
    sub_protocol: type
    required_bits: tuple[tuple[int, int], ...] = ()


FieldSpec = Union[BitsSpec, LiteralSpec, CondSpec, RepeatSpec, RowsSpec]


class JsonRecord(NamedTuple):
    key: str
    label: str
    value: Expr | int | float | str
    data_type: str  # "DATA_STRING" | "DATA_INT" | "DATA_DOUBLE"
    fmt: str | None = None
    when: Expr | bool | None = None


# ---------------------------------------------------------------------------
# Pipeline step descriptors – returned by BitbufferPipeline builder methods
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Invert:
    """Invert all bits in the current buffer."""


@dataclass(frozen=True)
class Reflect:
    """Reflect (bit-reverse) bytes in each row."""


@dataclass(frozen=True)
class FindRepeatedRow:
    """Find a row repeated at least *min_repeats* times with *min_bits* bits."""
    min_repeats: int
    min_bits: int
    max_bits: int | None = None


@dataclass(frozen=True)
class SearchPreamble:
    """Search for a bit pattern in the bitbuffer."""
    pattern: int
    bit_length: int | None = None
    scan_all_rows: bool = False

    @property
    def resolved_bit_length(self) -> int:
        if self.bit_length is not None:
            return self.bit_length
        return max(1, self.pattern.bit_length())


@dataclass(frozen=True)
class SkipBits:
    """Adjust offset by *count* bits (negative = backward)."""
    count: int


@dataclass(frozen=True)
class ManchesterDecode:
    """Manchester-decode from the current row/offset into a local packet buffer."""
    max_bits: int = 0


@dataclass(frozen=True)
class FirstValidRow:
    """Row-selection strategy: scan rows until one matches and decodes."""
    bits: int
    reflect: bool = False
    checksum_over_bytes: int = 0


PipelineStep = Union[
    Invert,
    Reflect,
    FindRepeatedRow,
    SearchPreamble,
    SkipBits,
    ManchesterDecode,
    FirstValidRow,
]


class BitbufferPipeline:
    """Fluent builder that records an ordered sequence of pipeline steps."""

    def __init__(self) -> None:
        self.steps: list[PipelineStep] = []

    def _append(self, step: PipelineStep) -> BitbufferPipeline:
        self.steps.append(step)
        return self

    def invert(self) -> BitbufferPipeline:
        return self._append(Invert())

    def reflect(self) -> BitbufferPipeline:
        return self._append(Reflect())

    def find_repeated_row(
        self, min_repeats: int, min_bits: int, *, max_bits: int | None = None
    ) -> BitbufferPipeline:
        return self._append(FindRepeatedRow(min_repeats, min_bits, max_bits))

    def search_preamble(
        self,
        pattern: int,
        *,
        bit_length: int | None = None,
        scan_all_rows: bool = False,
    ) -> BitbufferPipeline:
        return self._append(SearchPreamble(pattern, bit_length, scan_all_rows))

    def skip_bits(self, count: int) -> BitbufferPipeline:
        return self._append(SkipBits(count))

    def manchester_decode(self, max_bits: int = 0) -> BitbufferPipeline:
        return self._append(ManchesterDecode(max_bits))

    def first_valid_row(
        self,
        bits: int,
        *,
        reflect: bool = False,
        checksum_over_bytes: int = 0,
    ) -> BitbufferPipeline:
        return self._append(
            FirstValidRow(bits, reflect, checksum_over_bytes)
        )


class Modulation(Enum):
    OOK_PULSE_MANCHESTER_ZEROBIT = auto()
    OOK_PULSE_PCM = auto()
    OOK_PULSE_RZ = auto()
    OOK_PULSE_PPM = auto()
    OOK_PULSE_PWM = auto()
    OOK_PULSE_PIWM_RAW = auto()
    OOK_PULSE_PIWM_DC = auto()
    OOK_PULSE_DMC = auto()
    OOK_PULSE_PWM_OSV1 = auto()
    OOK_PULSE_NRZS = auto()
    FSK_PULSE_PCM = auto()
    FSK_PULSE_PWM = auto()
    FSK_PULSE_MANCHESTER_ZEROBIT = auto()


class Bits:
    """``Bits[24]`` -> ``BitsSpec(width=24)``"""

    def __class_getitem__(cls, width: int) -> BitsSpec:
        if not isinstance(width, int) or width <= 0:
            raise ValueError(f"Bits width must be a positive int, got {width!r}")
        return BitsSpec(width=width)


class Literal:
    """``Literal[0xAA, 8]`` -> ``LiteralSpec(value=0xAA, width=8)``"""

    def __class_getitem__(cls, params: tuple[int, int]) -> LiteralSpec:
        if not isinstance(params, tuple) or len(params) != 2:
            raise ValueError("Literal requires (value, width), e.g. Literal[0xAA, 8]")
        value, width = params
        return LiteralSpec(value=value, width=width)


class Cond:
    """
    ``Cond[FieldRef('flags') & 1, Bits[16], Bits[12]]`` -> CondSpec with both branches
    ``Cond[FieldRef('flags') & 2, Bits[8]]`` -> CondSpec, false_type=None (optional)
    """

    def __class_getitem__(cls, params: tuple) -> CondSpec:
        if not isinstance(params, tuple):
            raise ValueError("Cond requires at least (expr, true_type)")
        if len(params) == 2:
            expr, true_type = params
            return CondSpec(expr=expr, true_type=true_type, false_type=None)
        if len(params) == 3:
            expr, true_type, false_type = params
            return CondSpec(expr=expr, true_type=true_type, false_type=false_type)
        raise ValueError("Cond takes 2 or 3 arguments: (expr, true_type[, false_type])")


class Repeat:
    """``Repeat[FieldRef('measurements'), SensorReading]`` or ``Repeat[3, Sub]`` -> RepeatSpec"""

    def __class_getitem__(cls, params: tuple) -> RepeatSpec:
        if not isinstance(params, tuple) or len(params) != 2:
            raise ValueError("Repeat requires (count_expr, sub_protocol)")
        count_expr, sub_protocol = params
        if isinstance(count_expr, bool):
            raise TypeError("Repeat count cannot be bool")
        if isinstance(count_expr, int):
            count_expr = ExprIntLiteral(count_expr)
        elif isinstance(count_expr, float):
            raise TypeError("Repeat count cannot be float")
        elif not isinstance(count_expr, Expr):
            raise TypeError(
                f"Repeat count must be int or Expr, got {type(count_expr).__name__}"
            )
        return RepeatSpec(count_expr=count_expr, sub_protocol=sub_protocol)


class Rows:
    """``Rows[(1, 2), RowProto]`` or ``Rows[..., RowProto, ((1, 36),)]`` -> ``RowsSpec``"""

    def __class_getitem__(cls, params: tuple) -> RowsSpec:
        if not isinstance(params, tuple) or len(params) not in (2, 3):
            raise ValueError(
                "Rows requires (rows_tuple, sub_protocol) or "
                "(rows_tuple, sub_protocol, required_bits)"
            )
        rows_t, sub = params[0], params[1]
        req: tuple[tuple[int, int], ...] = params[2] if len(params) == 3 else ()
        if not isinstance(rows_t, tuple) or len(rows_t) == 0:
            raise ValueError(
                "Rows: first argument must be a non-empty tuple of int row indices"
            )
        for x in rows_t:
            if not isinstance(x, int) or x < 0:
                raise ValueError(f"Rows: invalid row index {x!r}")
        if not isinstance(req, tuple):
            raise ValueError("Rows: required_bits must be a tuple of (row, bits) pairs")
        for pair in req:
            if (
                not isinstance(pair, tuple)
                or len(pair) != 2
                or not isinstance(pair[0], int)
                or not isinstance(pair[1], int)
            ):
                raise ValueError(f"Rows: invalid required_bits entry {pair!r}")
            if pair[0] not in rows_t:
                raise ValueError(
                    f"Rows: required_bits row {pair[0]} is not listed in rows tuple"
                )
        return RowsSpec(rows=tuple(rows_t), sub_protocol=sub, required_bits=req)


# ---------------------------------------------------------------------------
# Modulation configuration bundle
# ---------------------------------------------------------------------------


class ModulationConfig:
    """Bundle of modulation-level radio parameters for the r_device struct.

    Subclass this inside a Protocol class (as a nested ``modulation_config``
    class) and override only the attributes that differ from the base.
    """

    device_name: str | None = None
    output_model: str | None = None
    modulation: Modulation | None = None
    short_width: float = 0.0
    long_width: float = 0.0
    reset_limit: float = 0.0
    gap_limit: float | None = None
    sync_width: float | None = None
    tolerance: float | None = None
    disabled: int | None = None


ProtocolConfig = ModulationConfig


# ---------------------------------------------------------------------------
# Protocol and Variant base classes
# ---------------------------------------------------------------------------


_EXCLUDED_CALLABLES: frozenset = frozenset(("decode", "from_hex", "to_json", "prepare"))


def _callable_body_is_ellipsis(fn: Any) -> bool:
    """True if the function body is only ``...`` (stub for hand-written C)."""
    try:
        src = inspect.getsource(fn)
    except (OSError, TypeError):
        return False
    try:
        tree = ast.parse(textwrap.dedent(src))
    except SyntaxError:
        return False
    if not tree.body:
        return False
    fd = tree.body[0]
    if not isinstance(fd, (ast.FunctionDef, ast.AsyncFunctionDef)):
        return False
    if len(fd.body) != 1:
        return False
    st = fd.body[0]
    return bool(
        isinstance(st, ast.Expr)
        and isinstance(st.value, ast.Constant)
        and st.value.value is Ellipsis
    )


def _call_with_fieldref_kwargs(
    fn: Any, proxy: Any, param_names: tuple[str, ...] | None
) -> Any:
    kwargs = {p: FieldRef(p) for p in (param_names or ())}
    return fn(proxy, **kwargs)


def property_inline_expr(cls_or_variant: type, prop_name: str) -> Any:
    """If a return-annotated property inlines to C, return its expression AST; else ``None``."""
    prop_names = {n for n, _ in cls_or_variant.properties}
    if prop_name not in prop_names:
        return None
    fn = getattr(cls_or_variant, prop_name)
    if _callable_body_is_ellipsis(fn):
        return None
    arg_names = cls_or_variant.explicit_args().get(prop_name)
    proxy = cls_or_variant()
    out = _call_with_fieldref_kwargs(fn, proxy, arg_names)
    return _coerce_protocol_expr_result(out)


def method_inline_expr(cls_or_variant: type, method_name: str) -> Any:
    """If a void method inlines to a single C expression statement, return it; else ``None``."""
    if method_name not in cls_or_variant.methods:
        return None
    fn = getattr(cls_or_variant, method_name)
    if _callable_body_is_ellipsis(fn):
        return None
    arg_names = cls_or_variant.explicit_args().get(method_name)
    proxy = cls_or_variant()
    out = _call_with_fieldref_kwargs(fn, proxy, arg_names)
    return _coerce_protocol_expr_result(out)


def when_inline_expr(variant_cls: type) -> Any:
    """If ``Variant.when`` inlines to the C branch predicate, return it; else ``None``."""
    _Variant = globals().get("Variant")
    if _Variant is None or not issubclass(variant_cls, _Variant) or variant_cls is _Variant:
        return None
    fn = getattr(variant_cls, "when", None)
    if not callable(fn):
        return None
    if _callable_body_is_ellipsis(fn):
        return None
    arg_names = variant_cls.explicit_args().get("when")
    proxy = variant_cls()
    out = _call_with_fieldref_kwargs(fn, proxy, arg_names)
    return _coerce_protocol_expr_result(out)


def validation_hook_inline_expr(cls_or_variant: type, hook_name: str) -> Any:
    """If ``validate_*`` can be compiled as one expression, return it; else ``None``.

    Explicit parameters (except ``fail_value``) are bound to :class:`FieldRef` names
    so hooks may depend on parsed fields or earlier inline properties.
    """
    if not hook_name.startswith("validate_"):
        return None
    fn = getattr(cls_or_variant, hook_name)
    if _callable_body_is_ellipsis(fn):
        return None
    proxy = cls_or_variant()
    arg_names = cls_or_variant.explicit_args()[hook_name]
    sig = inspect.signature(fn)
    kwargs: dict[str, Any] = {}
    if arg_names:
        for p in arg_names:
            kwargs[p] = FieldRef(p)
    if "fail_value" in sig.parameters:
        fd = sig.parameters["fail_value"].default
        if fd is inspect.Parameter.empty:
            return None
        kwargs["fail_value"] = fd
    out = fn(proxy, **kwargs)
    return _coerce_protocol_expr_result(out)


def _iter_user_callables(cls: type) -> list[tuple[str, Any]]:
    """Return (name, fn) pairs from a single class's __dict__.

    Skips Protocol/Variant base classes, dunder names, nested types,
    non-callables, and reserved framework method names.
    """
    _Variant = globals().get("Variant")
    if cls is Protocol or cls is _Variant or cls is object:
        return []
    result = []
    for attr_name, attr_value in cls.__dict__.items():
        if attr_name.startswith("_"):
            continue
        if isinstance(attr_value, type):
            continue
        if not callable(attr_value):
            continue
        if attr_name in _EXCLUDED_CALLABLES:
            continue
        result.append((attr_name, attr_value))
    return result


def _json_data_type_for(py_type: type) -> str:
    return {float: "DATA_DOUBLE", int: "DATA_INT", str: "DATA_STRING"}.get(py_type, "DATA_INT")


def _evaluated_class_annotations(cls: type) -> dict[str, Any]:
    """Return evaluated class ``__annotations__`` (PEP 649–safe on Python 3.14+)."""
    if sys.version_info >= (3, 10):
        return dict(inspect.get_annotations(cls))
    return dict(getattr(cls, "__annotations__", {}))


def _evaluated_callable_annotations(fn: Any) -> dict[str, Any]:
    """Return evaluated annotations for a function/method (PEP 649–safe)."""
    if sys.version_info >= (3, 10):
        return dict(inspect.get_annotations(fn))
    return dict(getattr(fn, "__annotations__", {}))


class Protocol:
    """Base class for protocol definitions."""

    #: Consolidated field list, base-to-derived (derived overrides base).
    fields: ClassVar[tuple[tuple[str, FieldSpec], ...]] = ()
    #: Consolidated nested ``Variant`` subclasses, base-to-derived.
    variants: ClassVar[tuple[type, ...]] = ()
    #: Consolidated void method names (no return annotation), base-to-derived.
    methods: ClassVar[tuple[str, ...]] = ()
    #: ``validate_<suffix>`` hook names in lexicographic order (see compiler).
    validation_hooks: ClassVar[tuple[str, ...]] = ()
    #: Consolidated return-annotated callables as ``(name, py_type)``, base-to-derived.
    properties: ClassVar[tuple[tuple[str, type], ...]] = ()

    @property
    def bitbuffer(self) -> BitbufferPipeline:
        """Return a fresh pipeline builder for ``prepare()`` chains."""
        return BitbufferPipeline()

    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)

        merged_fields = dict(cls.fields)
        for name, ann in _evaluated_class_annotations(cls).items():
            if isinstance(ann, (BitsSpec, LiteralSpec, CondSpec, RepeatSpec, RowsSpec)):
                if isinstance(ann, RowsSpec):
                    sp = ann.sub_protocol
                    if not isinstance(sp, type) or not issubclass(sp, Protocol):
                        raise ValueError(
                            f"{cls.__name__}.{name}: Rows sub_protocol must be a "
                            f"Protocol subclass, got {sp!r}"
                        )
                    for sn, sspec in sp.fields:
                        if isinstance(sspec, (RowsSpec, RepeatSpec)):
                            raise ValueError(
                                f"{cls.__name__}.{name}: Rows sub_protocol must not "
                                f"contain Rows or Repeat (found on field {sn!r})"
                            )
                merged_fields[name] = ann
        cls.fields = tuple(merged_fields.items())

        _Variant = globals().get("Variant")
        merged_variants = {v.__name__: v for v in cls.variants}
        if _Variant is not None:
            for name, val in cls.__dict__.items():
                if isinstance(val, type) and issubclass(val, _Variant):
                    merged_variants[name] = val
        cls.variants = tuple(merged_variants.values())

        merged_methods = dict.fromkeys(cls.methods)
        merged_props = dict(cls.properties)
        merged_vhooks = dict.fromkeys(getattr(cls, "validation_hooks", ()))
        for name, fn in _iter_user_callables(cls):
            if name == "when":
                continue
            if name == "validate":
                raise ValueError(
                    f"{cls.__name__}.{name}: bare 'validate' is not supported; use "
                    "validate_<purpose>(...) instead (see README: validation hooks)."
                )
            if name.startswith("validate_"):
                merged_vhooks[name] = None
                merged_methods.pop(name, None)
                merged_props.pop(name, None)
                continue
            ret = _evaluated_callable_annotations(fn).get("return")
            if ret is None:
                merged_methods[name] = None
                merged_props.pop(name, None)
            else:
                merged_props[name] = ret
                merged_methods.pop(name, None)
        cls.methods = tuple(merged_methods.keys())
        cls.validation_hooks = tuple(sorted(merged_vhooks.keys()))
        cls.properties = tuple(merged_props.items())

        if _Variant is not None and issubclass(cls, _Variant) and cls is not _Variant:
            w = getattr(cls, "when", None)
            if not callable(w):
                raise ValueError(
                    f"{cls.__name__}: Variant must define when(self, ...) -> bool "
                    "(non-zero in C selects this branch)"
                )

    def __getattr__(self, name: str) -> FieldRef:
        if name.startswith("_"):
            raise AttributeError(name)
        return FieldRef(name)

    # ------------------------------------------------------------------
    # Introspection classmethods – used by the compiler
    # ------------------------------------------------------------------

    @classmethod
    def explicit_args(cls) -> dict[str, tuple[str, ...] | None]:
        """Collect explicit argument lists (excluding self) for methods and properties.

        For ``validate_*``, a trailing ``fail_value`` parameter (inline hooks only)
        is compile-time only and omitted from the C argument list.
        """
        result: dict[str, tuple[str, ...] | None] = {}
        _Variant = globals().get("Variant")
        if (
            _Variant is not None
            and issubclass(cls, _Variant)
            and cls is not _Variant
        ):
            wfn = getattr(cls, "when", None)
            if callable(wfn):
                wparams = [
                    p for p in inspect.signature(wfn).parameters if p != "self"
                ]
                result["when"] = tuple(wparams) if wparams else None
        for name in cls.methods + cls.validation_hooks + tuple(n for n, _ in cls.properties):
            params = [p for p in inspect.signature(getattr(cls, name)).parameters if p != "self"]
            if name.startswith("validate_") and params and params[-1] == "fail_value":
                params = params[:-1]
            result[name] = tuple(params) if params else None
        return result

    @classmethod
    def json_records(cls) -> tuple[JsonRecord, ...]:
        """Evaluate the class's to_json via a proxy (inherits Protocol.to_json)."""
        json_fn = getattr(cls, "to_json")
        records = json_fn(_JsonProxy(cls))
        if not isinstance(records, (list, tuple)):
            raise ValueError(
                f"{cls.__name__}.to_json must return a list or tuple of JsonRecord, "
                f"got {type(records).__name__}"
            )
        for i, rec in enumerate(records):
            if not isinstance(rec, JsonRecord):
                raise ValueError(
                    f"{cls.__name__}.to_json item {i} must be JsonRecord, got {type(rec).__name__}"
                )
        return tuple(records)

    @classmethod
    def requires_external_helpers(cls) -> bool:
        """True if any method or property cannot be inlined as a pure expression."""
        for name in cls.methods:
            if method_inline_expr(cls, name) is None:
                return True
        for name in cls.validation_hooks:
            if validation_hook_inline_expr(cls, name) is None:
                return True
        for name, _ in cls.properties:
            if property_inline_expr(cls, name) is None:
                return True
        for variant_cls in cls.variants:
            if when_inline_expr(variant_cls) is None:
                return True
            for name in variant_cls.methods:
                if method_inline_expr(variant_cls, name) is None:
                    return True
            for name in variant_cls.validation_hooks:
                if validation_hook_inline_expr(variant_cls, name) is None:
                    return True
            for name, _ in variant_cls.properties:
                if property_inline_expr(variant_cls, name) is None:
                    return True
        return False

    @classmethod
    def output_fields(cls) -> tuple[str, ...]:
        """Ordered output field names for the r_device struct."""
        recs = cls.json_records()
        if recs:
            names: list[str] = []
            for rec in recs:
                if rec.key not in names:
                    names.append(rec.key)
            return tuple(names)

        names = ["model"]
        for name, spec in cls.fields:
            if not name.startswith("_") and isinstance(spec, BitsSpec):
                names.append(name)
        for prop_name, _ in cls.properties:
            if prop_name not in names:
                names.append(prop_name)
        for variant_cls in cls.variants:
            for name, spec in variant_cls.fields:
                if not name.startswith("_") and isinstance(spec, BitsSpec) and name not in names:
                    names.append(name)
            for prop_name, _ in variant_cls.properties:
                if prop_name not in names:
                    names.append(prop_name)
        return tuple(names)

    @classmethod
    def referenced_protocols(cls) -> tuple[type[Protocol], ...]:
        """Sub-protocols referenced by Repeat[] or Rows[] fields."""
        result: list[type[Protocol]] = []
        for fname, spec in cls.fields:
            if isinstance(spec, RepeatSpec):
                sp = spec.sub_protocol
                if not isinstance(sp, type) or not issubclass(sp, Protocol):
                    raise ValueError(
                        f"{cls.__name__} field {fname!r}: Repeat sub_protocol must be a Protocol subclass, got {sp!r}"
                    )
                result.append(sp)
            elif isinstance(spec, RowsSpec):
                sp = spec.sub_protocol
                if not isinstance(sp, type) or not issubclass(sp, Protocol):
                    raise ValueError(
                        f"{cls.__name__} field {fname!r}: Rows sub_protocol must be a Protocol subclass, got {sp!r}"
                    )
                result.append(sp)
        return tuple(result)

    @classmethod
    def delegated_protocols(cls) -> tuple[type[Protocol], ...]:
        """Sub-protocols referenced via a wrapper ``delegates`` attribute."""
        delegates = getattr(cls, "delegates", None)
        if delegates is None:
            return ()
        if not isinstance(delegates, tuple):
            raise ValueError(
                f"{cls.__name__}.delegates must be a tuple of Protocol subclasses, "
                f"got {type(delegates).__name__}"
            )
        if len(delegates) == 0:
            raise ValueError(f"{cls.__name__}.delegates must not be empty")
        for i, d in enumerate(delegates):
            if not isinstance(d, type) or not issubclass(d, Protocol):
                raise ValueError(
                    f"{cls.__name__}.delegates[{i}] must be a Protocol subclass, got {d!r}"
                )
        return tuple(delegates)

    def to_json(self) -> list[JsonRecord]:
        protocol_cls = getattr(self, "__protocol_cls__", None) or self.__class__
        records: list[JsonRecord] = []
        cfg = getattr(protocol_cls, "modulation_config", ModulationConfig)
        model_name = cfg.output_model or cfg.device_name or protocol_cls.__name__
        records.append(
            JsonRecord(key="model", label="", value=model_name, data_type="DATA_STRING")
        )
        for name, spec in protocol_cls.fields:
            if isinstance(spec, BitsSpec) and not name.startswith("_"):
                records.append(
                    JsonRecord(key=name, label="", value=FieldRef(name), data_type="DATA_INT")
                )
        for prop_name, prop_type in protocol_cls.properties:
            records.append(
                JsonRecord(
                    key=prop_name,
                    label="",
                    value=FieldRef(prop_name),
                    data_type=_json_data_type_for(prop_type),
                )
            )
        return records


class Variant(Protocol):
    """Base class for variant payload definitions (nested in a Protocol).

    Subclasses must implement ``when(self, ...) -> bool`` with the same
    explicit-dependency style as properties; the body must compile to a C
    predicate (non-zero means this variant matches).
    """

    repeat_count = None
