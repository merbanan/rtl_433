"""
DSL for declaring binary protocol field layouts (proto-compiler v2).

Protocol subclasses use type annotations to describe bit-level fields.
codegen.py reads these annotations and emits C decoder code for rtl_433.

Key classes:
  Protocol, Repeatable, Variant, Decoder
  ModulationConfig, Modulation, DecodeFail
  Bits, Literal, Repeat, Rows, FirstValid
  FieldRef, JsonRecord
  Rev8, LfsrDigest8
  Expr, BinaryExpr, UnaryExpr, ExprIntLiteral, ExprFloatLiteral,
  ExprBoolLiteral, FieldRefSubscript
"""

from __future__ import annotations

import inspect
import sys
from dataclasses import dataclass
from enum import Enum, auto
from typing import Any, NamedTuple, Union


# ---------------------------------------------------------------------------
# DecodeFail enum
# ---------------------------------------------------------------------------


class DecodeFail(str, Enum):
    """rtl_433 decode return codes; use as ``fail_value=`` on ``validate_*``."""

    MIC = "DECODE_FAIL_MIC"
    SANITY = "DECODE_FAIL_SANITY"
    ABORT_EARLY = "DECODE_ABORT_EARLY"
    ABORT_LENGTH = "DECODE_ABORT_LENGTH"


# ---------------------------------------------------------------------------
# Expression tree
# ---------------------------------------------------------------------------


class Expr:
    """Base for composable DSL expressions. Operators build expression tree nodes."""

    __slots__ = ()

    def __eq__(self, other: Any) -> "BinaryExpr":  # type: ignore[override]
        return BinaryExpr("==", self, _wrap(other))

    def __ne__(self, other: Any) -> "BinaryExpr":  # type: ignore[override]
        return BinaryExpr("!=", self, _wrap(other))

    def __lt__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("<", self, _wrap(other))

    def __le__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("<=", self, _wrap(other))

    def __gt__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr(">", self, _wrap(other))

    def __ge__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr(">=", self, _wrap(other))

    def __and__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("&", self, _wrap(other))

    def __rand__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("&", _wrap(other), self)

    def __or__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("|", self, _wrap(other))

    def __ror__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("|", _wrap(other), self)

    def __xor__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("^", self, _wrap(other))

    def __rxor__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("^", _wrap(other), self)

    def __invert__(self) -> "UnaryExpr":
        return UnaryExpr("~", self)

    def __add__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("+", self, _wrap(other))

    def __radd__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("+", _wrap(other), self)

    def __sub__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("-", self, _wrap(other))

    def __rsub__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("-", _wrap(other), self)

    def __mul__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("*", self, _wrap(other))

    def __rmul__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("*", _wrap(other), self)

    def __truediv__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("/", self, _wrap(other))

    def __rtruediv__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("/", _wrap(other), self)

    def __floordiv__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("/", self, _wrap(other))

    def __rfloordiv__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("/", _wrap(other), self)

    def __mod__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("%", self, _wrap(other))

    def __rmod__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("%", _wrap(other), self)

    def __neg__(self) -> "UnaryExpr":
        return UnaryExpr("-", self)

    def __lshift__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("<<", self, _wrap(other))

    def __rlshift__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr("<<", _wrap(other), self)

    def __rshift__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr(">>", self, _wrap(other))

    def __rrshift__(self, other: Any) -> "BinaryExpr":
        return BinaryExpr(">>", _wrap(other), self)

    def __hash__(self) -> int:
        return id(self)


@dataclass(frozen=True, eq=False)
class ExprIntLiteral(Expr):
    """Integer constant in the expression tree."""
    value: int

    def __hash__(self) -> int:
        return hash(("ExprIntLiteral", self.value))


@dataclass(frozen=True, eq=False)
class ExprFloatLiteral(Expr):
    """Float constant in the expression tree."""
    value: float

    def __hash__(self) -> int:
        return hash(("ExprFloatLiteral", self.value))


@dataclass(frozen=True, eq=False)
class ExprBoolLiteral(Expr):
    """Boolean constant in the expression tree."""
    value: bool

    def __hash__(self) -> int:
        return hash(("ExprBoolLiteral", self.value))


@dataclass(frozen=True, eq=False)
class BinaryExpr(Expr):
    """Binary operator node ``(left op right)``."""
    op: str
    left: Any
    right: Any

    def __hash__(self) -> int:
        return hash(("BinaryExpr", self.op, id(self.left), id(self.right)))


@dataclass(frozen=True, eq=False)
class UnaryExpr(Expr):
    """Unary operator node ``(op operand)``."""
    op: str
    operand: Any

    def __hash__(self) -> int:
        return hash(("UnaryExpr", self.op, id(self.operand)))


@dataclass(frozen=True, eq=False)
class FieldRef(Expr):
    """Reference to a previously parsed protocol field by name."""
    name: str

    def __getitem__(self, index: int) -> "FieldRefSubscript":
        if not isinstance(index, int):
            raise TypeError(f"FieldRef subscript index must be int, got {type(index).__name__}")
        return FieldRefSubscript(self, index)

    def __hash__(self) -> int:
        return hash(("FieldRef", self.name))


@dataclass(frozen=True, eq=False)
class FieldRefSubscript(Expr):
    """Subscript into a Rows-generated sparse array: ``name[index]``."""
    base: FieldRef
    index: int

    def __getattr__(self, name: str) -> "RowFieldAccess":
        if name.startswith("_"):
            raise AttributeError(name)
        return RowFieldAccess(self.base.name, self.index, name)

    def __hash__(self) -> int:
        return hash(("FieldRefSubscript", self.base.name, self.index))


@dataclass(frozen=True, eq=False)
class RowFieldAccess(Expr):
    """Access a sub-field of a Rows element: ``cells[1].b0``

    Emitted in C as ``cells_b0[1]`` (flat array indexed by row).
    """
    rows_name: str
    row_index: int
    field_name: str

    def __hash__(self) -> int:
        return hash(("RowFieldAccess", self.rows_name, self.row_index, self.field_name))


@dataclass(frozen=True, eq=False)
class Rev8Expr(Expr):
    """``reverse8(operand)`` applied to an expression."""
    operand: Any

    def __hash__(self) -> int:
        return hash(("Rev8Expr", id(self.operand)))


@dataclass(frozen=True, eq=False)
class LfsrDigest8Expr(Expr):
    """``lfsr_digest8(bytes, n, gen, key)`` over a byte list."""
    bytes_args: tuple
    gen: int
    key: int

    def __hash__(self) -> int:
        return hash(("LfsrDigest8Expr", self.bytes_args, self.gen, self.key))


def _wrap(value: Any) -> Expr:
    """Wrap scalars as literal Expr nodes; pass through existing Expr objects."""
    if isinstance(value, bool):
        return ExprBoolLiteral(value)
    if isinstance(value, int):
        return ExprIntLiteral(value)
    if isinstance(value, float):
        return ExprFloatLiteral(value)
    if isinstance(value, Expr):
        return value
    raise TypeError(f"Cannot use {type(value).__name__!r} in a DSL expression")


def Rev8(operand: Any) -> Rev8Expr:
    """Build ``reverse8(operand)`` in generated C."""
    if isinstance(operand, float):
        raise TypeError("Rev8 does not accept float")
    return Rev8Expr(operand=_wrap(operand))


def LfsrDigest8(*bytes_args: Any, gen: int, key: int) -> LfsrDigest8Expr:
    """Build ``lfsr_digest8(bytes, n, gen, key)`` in generated C."""
    wrapped: list[Expr] = []
    for a in bytes_args:
        if isinstance(a, bool):
            raise TypeError("LfsrDigest8 does not accept bool")
        if isinstance(a, float):
            raise TypeError("LfsrDigest8 does not accept float")
        wrapped.append(_wrap(a))
    return LfsrDigest8Expr(bytes_args=tuple(wrapped), gen=gen, key=key)


# ---------------------------------------------------------------------------
# Field spec types
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class BitsSpec:
    """Bits[n] — extract n bits as unsigned int."""
    width: int


@dataclass(frozen=True)
class LiteralSpec:
    """Literal[value, n] — extract n bits and assert == value."""
    value: int
    width: int


@dataclass(frozen=True)
class RepeatSpec:
    """Repeat[count_expr, sub] — parse Repeatable sub-protocol count times."""
    count_expr: Any  # Expr or ExprIntLiteral
    sub_protocol: type  # Repeatable subclass


@dataclass(frozen=True)
class FirstValid:
    """Row selection strategy for Rows[]: scan rows for the first matching bits."""
    bits: int


@dataclass(frozen=True)
class RowsSpec:
    """Rows[row_spec, sub, required_bits=()] — parse sub-protocol over selected rows."""
    row_spec: Any  # tuple[int, ...] or FirstValid
    sub_protocol: type  # Repeatable subclass
    required_bits: tuple = ()  # tuple of (row_index, exact_bits)


FieldSpec = Union[BitsSpec, LiteralSpec, RepeatSpec, RowsSpec]


# ---------------------------------------------------------------------------
# JsonRecord
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class JsonRecord:
    """A single entry in the data_make call."""

    key: str
    label: str
    value: Any
    data_type: str
    fmt: "str | None" = None
    when: Any = None


# ---------------------------------------------------------------------------
# Callable entry
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class CallableEntry:
    """Metadata for a user-defined callable collected from a Protocol subclass."""

    kind: str  # "validate" | "prop" | "method"
    fn: Any
    expr_tree: Any  # Expr | None; None only for is_external
    return_type: Any  # Python type | None
    params: tuple  # param names, excludes 'self' and 'fail_value'
    param_types: dict  # param name -> Python type
    fail_c: str | None
    is_external: bool


# ---------------------------------------------------------------------------
# Pipeline step descriptors
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Invert:
    """Invert all bits in the current buffer."""


@dataclass(frozen=True)
class Reflect:
    """Reflect (bit-reverse) bytes in each row."""


@dataclass(frozen=True)
class FindRepeatedRow:
    """Find a row repeated at least min_repeats times with min_bits bits."""
    min_repeats: int
    min_bits: int
    max_bits: "int | None" = None


@dataclass(frozen=True)
class SearchPreamble:
    """Search for a bit pattern in the bitbuffer."""
    pattern: int
    bit_length: "int | None" = None
    scan_all_rows: bool = False

    @property
    def resolved_bit_length(self) -> int:
        if self.bit_length is not None:
            return self.bit_length
        return max(1, self.pattern.bit_length())


@dataclass(frozen=True)
class SkipBits:
    """Adjust offset by count bits (negative = backward)."""
    count: int


@dataclass(frozen=True)
class ManchesterDecode:
    """Manchester-decode from current row/offset into a local packet buffer."""
    max_bits: int = 0


PipelineStep = Union[
    Invert, Reflect, FindRepeatedRow, SearchPreamble,
    SkipBits, ManchesterDecode,
]


# ---------------------------------------------------------------------------
# BitbufferPipeline builder
# ---------------------------------------------------------------------------


class BitbufferPipeline:
    """Fluent builder recording an ordered sequence of pipeline steps."""

    def __init__(self) -> None:
        self.steps: list[PipelineStep] = []

    def _append(self, step: PipelineStep) -> "BitbufferPipeline":
        self.steps.append(step)
        return self

    def invert(self) -> "BitbufferPipeline":
        return self._append(Invert())

    def reflect(self) -> "BitbufferPipeline":
        return self._append(Reflect())

    def find_repeated_row(
        self, min_repeats: int, min_bits: int, *, max_bits: "int | None" = None
    ) -> "BitbufferPipeline":
        return self._append(FindRepeatedRow(min_repeats, min_bits, max_bits))

    def search_preamble(
        self,
        pattern: int,
        *,
        bit_length: "int | None" = None,
        scan_all_rows: bool = False,
    ) -> "BitbufferPipeline":
        return self._append(SearchPreamble(pattern, bit_length, scan_all_rows))

    def skip_bits(self, count: int) -> "BitbufferPipeline":
        return self._append(SkipBits(count))

    def manchester_decode(self, max_bits: int = 0) -> "BitbufferPipeline":
        return self._append(ManchesterDecode(max_bits))


# ---------------------------------------------------------------------------
# Modulation
# ---------------------------------------------------------------------------


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


class ModulationConfig(NamedTuple):
    """Bundle of modulation-level radio parameters for the r_device struct.

    Required fields (device_name, modulation, short_width, long_width,
    reset_limit) must be supplied. Optional fields default to None and are
    omitted from the generated r_device entry when None.
    """

    device_name: str
    modulation: Modulation
    short_width: float
    long_width: float
    reset_limit: float
    gap_limit: float | None = None
    sync_width: float | None = None
    tolerance: float | None = None
    disabled: int | None = None



# ---------------------------------------------------------------------------
# Field spec classes (DSL subscript syntax)
# ---------------------------------------------------------------------------


class Bits:
    """``Bits[24]`` -> BitsSpec(width=24)"""

    def __class_getitem__(cls, width: int) -> BitsSpec:
        if not isinstance(width, int) or isinstance(width, bool) or width <= 0:
            raise ValueError(f"Bits width must be a positive int, got {width!r}")
        return BitsSpec(width=width)


class Literal:
    """``Literal[0xAA, 8]`` -> LiteralSpec(value=0xAA, width=8)"""

    def __class_getitem__(cls, params: tuple) -> LiteralSpec:
        if not isinstance(params, tuple) or len(params) != 2:
            raise ValueError("Literal requires (value, width), e.g. Literal[0xAA, 8]")
        value, width = params
        if not isinstance(value, int) or not isinstance(width, int) or width <= 0:
            raise ValueError(f"Literal requires (int, positive_int), got {params!r}")
        return LiteralSpec(value=value, width=width)


class Repeat:
    """``Repeat[count_expr, SubProtocol]`` -> RepeatSpec"""

    def __class_getitem__(cls, params: tuple) -> RepeatSpec:
        if not isinstance(params, tuple) or len(params) != 2:
            raise ValueError("Repeat requires (count_expr, sub_protocol)")
        count_expr, sub_protocol = params
        if isinstance(count_expr, bool):
            raise TypeError("Repeat count cannot be bool")
        if isinstance(count_expr, float):
            raise TypeError("Repeat count cannot be float")
        if isinstance(count_expr, int):
            count_expr = ExprIntLiteral(count_expr)
        elif not isinstance(count_expr, Expr):
            raise TypeError(
                f"Repeat count must be int or Expr, got {type(count_expr).__name__}"
            )
        return RepeatSpec(count_expr=count_expr, sub_protocol=sub_protocol)


class Rows:
    """``Rows[(1, 2), SubProtocol]`` or ``Rows[FirstValid(36), SubProtocol, ...]``"""

    def __class_getitem__(cls, params: tuple) -> RowsSpec:
        if not isinstance(params, tuple) or len(params) not in (2, 3):
            raise ValueError(
                "Rows requires (row_spec, sub_protocol) or "
                "(row_spec, sub_protocol, required_bits)"
            )
        row_spec = params[0]
        sub_protocol = params[1]
        required_bits: tuple = params[2] if len(params) == 3 else ()

        if isinstance(row_spec, FirstValid):
            pass
        elif isinstance(row_spec, tuple):
            if len(row_spec) == 0:
                raise ValueError("Rows: row tuple must be non-empty")
            for x in row_spec:
                if not isinstance(x, int) or x < 0:
                    raise ValueError(f"Rows: invalid row index {x!r}")
        else:
            raise ValueError(
                f"Rows: first argument must be a tuple of ints or FirstValid, got {row_spec!r}"
            )

        if not isinstance(required_bits, tuple):
            raise ValueError("Rows: required_bits must be a tuple")
        for pair in required_bits:
            if (
                not isinstance(pair, tuple)
                or len(pair) != 2
                or not isinstance(pair[0], int)
                or not isinstance(pair[1], int)
            ):
                raise ValueError(f"Rows: invalid required_bits entry {pair!r}")

        return RowsSpec(row_spec=row_spec, sub_protocol=sub_protocol, required_bits=required_bits)


# ---------------------------------------------------------------------------
# Helpers for callable introspection
# ---------------------------------------------------------------------------


def _get_annotations_own(cls: type) -> dict:
    """Return only this class's own __annotations__ (not inherited)."""
    return dict(cls.__dict__.get("__annotations__", {}))


def _get_fn_annotations(fn: Any) -> dict:
    """Return annotations for a function/method."""
    return dict(fn.__annotations__) if hasattr(fn, "__annotations__") else {}


_RESERVED_NAMES = frozenset(
    ("prepare", "to_json", "when", "dispatch", "modulation_config")
)


def _is_user_callable(name: str, value: Any) -> bool:
    """True if name/value in a class dict is a user-defined callable to process."""
    if name.startswith("_"):
        return False
    if isinstance(value, (type, classmethod, staticmethod, property)):
        return False
    if not callable(value):
        return False
    if name in _RESERVED_NAMES:
        return False
    return True


def _callable_is_ellipsis(fn: Any) -> bool:
    """True if the function body is just ``...`` (optionally preceded by a docstring)."""
    import ast
    import textwrap

    tree = ast.parse(textwrap.dedent(inspect.getsource(fn)))
    fd = tree.body[0]
    assert isinstance(fd, (ast.FunctionDef, ast.AsyncFunctionDef))
    body = fd.body
    # Strip leading docstring if present
    if (
        len(body) >= 2
        and isinstance(body[0], ast.Expr)
        and isinstance(body[0].value, ast.Constant)
        and isinstance(body[0].value.value, str)
    ):
        body = body[1:]
    if len(body) != 1:
        return False
    st = body[0]
    return (
        isinstance(st, ast.Expr)
        and isinstance(st.value, ast.Constant)
        and st.value.value is ...
    )


class _FieldRefProxy:
    """Proxy 'self' inside a callable — attribute accesses return FieldRef."""
    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            raise AttributeError(name)
        return FieldRef(name)


def _call_inline(fn: Any) -> "Expr | None":
    """Call fn with FieldRef proxies and return the resulting expression tree.

    Returns None only if the body produced a value of an unsupported type;
    any error inside fn propagates so the user sees the real failure.
    """
    sig = inspect.signature(fn)
    params = [p for p in sig.parameters if p != "self"]
    kwargs: dict[str, Any] = {}
    for p in params:
        if p == "fail_value":
            default = sig.parameters[p].default
            kwargs[p] = default if default is not inspect.Parameter.empty else DecodeFail.SANITY
        else:
            kwargs[p] = FieldRef(p)
    result = fn(_FieldRefProxy(), **kwargs)
    if isinstance(result, bool):
        return ExprBoolLiteral(result)
    if isinstance(result, int):
        return ExprIntLiteral(result)
    if isinstance(result, float):
        return ExprFloatLiteral(result)
    if isinstance(result, Expr):
        return result
    return None


def _param_names(fn: Any, *, exclude_fail_value: bool = False) -> tuple:
    """Return parameter names of fn, excluding 'self' (and optionally 'fail_value')."""
    sig = inspect.signature(fn)
    params = [p for p in sig.parameters if p != "self"]
    if exclude_fail_value and params and params[-1] == "fail_value":
        params = params[:-1]
    return tuple(params)


def _fail_value_default(fn: Any) -> str:
    """Return the C token for fail_value default on a validate_* fn."""
    sig = inspect.signature(fn)
    params = list(sig.parameters)
    if not params or params[-1] != "fail_value":
        raise TypeError(
            f"{fn.__qualname__}: validate_* must have "
            "fail_value=DecodeFail.* as last param"
        )
    p = sig.parameters["fail_value"]
    if p.default is inspect.Parameter.empty:
        raise TypeError(
            f"{fn.__qualname__}: fail_value must have a DecodeFail default"
        )
    d = p.default
    if not isinstance(d, DecodeFail):
        raise TypeError(
            f"{fn.__qualname__}: fail_value default must be DecodeFail.*, "
            f"got {type(d).__name__}"
        )
    return d.value


class RowsParamType:
    """Marker type for a callable parameter that refers to a Rows field.

    Carries the sub-field names so codegen can expand a single Python
    parameter like ``cells`` into multiple C pointer parameters like
    ``int *cells_b0, int *cells_b1, ...``.
    """
    __slots__ = ("sub_fields",)

    def __init__(self, sub_fields: tuple) -> None:
        self.sub_fields = sub_fields


def _infer_param_type(cls: type, param_name: str) -> Any:
    """Infer the Python type for an unannotated callable parameter.

    Checks, in order:
    1. Top-level BitsSpec fields of cls and its MRO → int
    2. Top-level Rows/Repeat field with this name → RowsParamType
    3. BitsSpec fields of any Repeatable reached via Repeat/Rows specs → int
    4. Return type of any callable with that name → its return_type
    5. Default → int (all DSL primitives are integer-typed)
    """
    # 1. Top-level scalar fields
    for name, spec in cls._fields:
        if name == param_name and isinstance(spec, BitsSpec):
            return int

    # 2. Rows/Repeat field → RowsParamType with sub-field names
    for name, spec in cls._fields:
        if name == param_name and isinstance(spec, (RowsSpec, RepeatSpec)):
            sub_cls = spec.sub_protocol
            sub_names = tuple(
                n for n, s in getattr(sub_cls, "_fields", ())
                if isinstance(s, BitsSpec) and not n.startswith("_")
            )
            return RowsParamType(sub_names)

    # 3. Fields of nested Repeatables (flat-name access like cells_b0)
    for _, spec in cls._fields:
        sub_cls = None
        if isinstance(spec, RepeatSpec):
            sub_cls = spec.sub_protocol
        elif isinstance(spec, RowsSpec):
            sub_cls = spec.sub_protocol
        if sub_cls is not None:
            for name, sub_spec in getattr(sub_cls, "_fields", ()):
                if name == param_name and isinstance(sub_spec, BitsSpec):
                    return int

    # 4. Callable return type
    existing = cls._callables.get(param_name)
    if existing is not None and existing.return_type is not None:
        return existing.return_type

    # 5. Default: all bit-level data is integer
    return int


# ---------------------------------------------------------------------------
# Protocol base class
# ---------------------------------------------------------------------------


class Protocol:
    """Base class for all protocol definitions.

    Subclasses use class-level annotations for field declarations and regular
    methods for callables (properties/methods/validate_* hooks).

    Class-level attributes set by __init_subclass__:
      _fields: tuple of (name, FieldSpec) in MRO order
      _variants: tuple of nested Variant subclasses
      _callables: dict mapping name -> callable entry dict
    """

    _fields: tuple = ()
    _variants: tuple = ()
    _callables: dict = {}  # dict[str, CallableEntry]

    def __getattr__(self, name: str) -> FieldRef:
        if name.startswith("_"):
            raise AttributeError(name)
        return FieldRef(name)

    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)

        # 1. Collect fields: inherit from parent _fields, then add own annotations
        merged_fields: dict[str, Any] = dict(cls._fields)
        for name, ann in _get_annotations_own(cls).items():
            if isinstance(ann, (BitsSpec, LiteralSpec, RepeatSpec, RowsSpec)):
                merged_fields[name] = ann
        cls._fields = tuple(merged_fields.items())

        # 2. Collect nested Variant subclasses
        merged_variants: dict[str, type] = {v.__name__: v for v in cls._variants}
        for attr_name, attr_val in cls.__dict__.items():
            if (
                isinstance(attr_val, type)
                and attr_name not in ("__class__",)
                and issubclass(attr_val, Protocol)
                and attr_val is not Protocol
            ):
                # Check if it's a Variant subclass (Variant not yet defined for first pass)
                _Variant = globals().get("Variant")
                if _Variant is not None and issubclass(attr_val, _Variant):
                    merged_variants[attr_name] = attr_val
        cls._variants = tuple(merged_variants.values())

        # 3. Collect callables (methods, props, validate_*)
        merged_callables: dict[str, CallableEntry] = dict(cls._callables)
        for attr_name, attr_val in cls.__dict__.items():
            if not _is_user_callable(attr_name, attr_val):
                continue
            fn = attr_val
            fn_anns = _get_fn_annotations(fn)
            return_type = fn_anns.get("return")
            is_external = _callable_is_ellipsis(fn)

            if attr_name.startswith("validate_"):
                params = _param_names(fn, exclude_fail_value=True)
                kind = "validate"
                entry_return_type: Any = bool
                try:
                    fail_c = _fail_value_default(fn)
                except TypeError:
                    fail_c = "DECODE_FAIL_SANITY"
            elif return_type is not None:
                params = _param_names(fn)
                kind = "prop"
                entry_return_type = return_type
                fail_c = None
            else:
                params = _param_names(fn)
                kind = "method"
                entry_return_type = None
                fail_c = None

            param_types: dict[str, Any] = {}
            for p in params:
                ann = fn_anns.get(p)
                if ann is None:
                    ann = _infer_param_type(cls, p)
                param_types[p] = ann

            expr_tree = None if is_external else _call_inline(fn)
            if not is_external and expr_tree is None:
                raise TypeError(
                    f"{cls.__name__}.{attr_name}: callable body could not be compiled to an expression"
                )

            entry = CallableEntry(
                kind=kind,
                fn=fn,
                expr_tree=expr_tree,
                return_type=entry_return_type,
                params=params,
                param_types=param_types,
                fail_c=fail_c,
                is_external=is_external,
            )

            merged_callables[attr_name] = entry
        cls._callables = merged_callables

        # 4. Validate: Variant subclasses must define when() with a compilable body
        _Variant = globals().get("Variant")
        if (
            _Variant is not None
            and issubclass(cls, _Variant)
            and cls is not _Variant
        ):
            when_fn = cls.__dict__.get("when") or getattr(cls, "when", None)
            if not callable(when_fn):
                raise ValueError(
                    f"{cls.__name__}: Variant must define when(self, ...) -> bool"
                )
            if _call_inline(when_fn) is None:
                raise TypeError(
                    f"{cls.__name__}.when: body could not be compiled to an expression"
                )

    def to_json(self) -> "list[JsonRecord] | None":
        """Override in a subclass to customize the data_make output.

        Default implementation returns None, which signals json_records() to
        synthesize a default record list from the declared fields and props.
        """
        return None

    @classmethod
    def json_records(cls) -> tuple:
        """Return the JsonRecord tuple from to_json(), or a synthesized default."""
        result = cls.to_json(_FieldRefProxy())
        if result is None:
            return cls._default_json_records()
        if not isinstance(result, (list, tuple)):
            raise TypeError(
                f"{cls.__name__}.to_json must return list/tuple of JsonRecord"
            )
        return tuple(result)

    @classmethod
    def _default_json_records(cls) -> tuple:
        modulation_config = getattr(cls, "modulation_config", None)
        if callable(modulation_config):
            model = modulation_config(cls.__new__(cls)).device_name
        else:
            model = cls.__name__
        records: list[JsonRecord] = [JsonRecord("model", "", model, "DATA_STRING")]
        for name, spec in cls._fields:
            if isinstance(spec, BitsSpec) and not name.startswith("_"):
                records.append(JsonRecord(name, "", FieldRef(name), "DATA_INT"))
        for name, entry in cls._callables.items():
            if entry.kind == "prop":
                dtype = {float: "DATA_DOUBLE", int: "DATA_INT", str: "DATA_STRING"}.get(
                    entry.return_type, "DATA_INT"
                )
                records.append(JsonRecord(name, "", FieldRef(name), dtype))
        return tuple(records)

    @classmethod
    def requires_external_helpers(cls) -> bool:
        """True if any callable needs an external C implementation."""
        for entry in cls._callables.values():
            if entry.is_external:
                return True
        for vcls in cls._variants:
            if vcls.requires_external_helpers():
                return True
            when_fn = getattr(vcls, "when", None)
            if callable(when_fn) and _callable_is_ellipsis(when_fn):
                return True
        return False


# ---------------------------------------------------------------------------
# Repeatable and Variant
# ---------------------------------------------------------------------------


class Repeatable(Protocol):
    """Protocol that may only contain Bits[] and Literal[] fields."""

    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)
        # Validate: no Repeat[] or Rows[] in own annotations
        for name, ann in _get_annotations_own(cls).items():
            if isinstance(ann, (RepeatSpec, RowsSpec)):
                raise ValueError(
                    f"{cls.__name__}.{name}: Repeatable subclasses may only contain "
                    "Bits[] and Literal[] fields, not Repeat[] or Rows[]"
                )


class Variant(Repeatable):
    """Conditionally-decoded payload extension nested inside a Decoder.

    Must define when(self, ...) -> bool.
    """
    pass


def _validate_modulation_config(cls: type) -> None:
    """Verify that cls.modulation_config(self) returns a ModulationConfig."""
    fn = getattr(cls, "modulation_config", None)
    if not callable(fn):
        raise TypeError(
            f"{cls.__name__}: must define modulation_config(self) -> ModulationConfig"
        )
    instance = cls.__new__(cls)
    cfg = fn(instance)
    if not isinstance(cfg, ModulationConfig):
        raise TypeError(
            f"{cls.__name__}.modulation_config() must return a ModulationConfig instance, "
            f"got {type(cfg).__name__}"
        )


class Decoder(Protocol):
    """Top-level decoder class.

    Must define:
      - modulation_config(self) -> ModulationConfig
      - prepare(self, buf) -> BitbufferPipeline
    """

    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)
        if cls is Decoder:
            return
        _validate_modulation_config(cls)


class Dispatcher(Protocol):
    """Top-level multi-decoder class.

    A Dispatcher does not parse bits itself. It defines:
      - modulation_config(self) -> ModulationConfig: produces the r_device entry
      - dispatch(self) -> tuple[Decoder, ...]: returns the delegate decoders
        the generated dispatcher tries in order.
    """

    def __init_subclass__(cls, **kwargs: Any) -> None:
        super().__init_subclass__(**kwargs)
        if cls is Dispatcher:
            return
        _validate_modulation_config(cls)
        dispatch = cls.__dict__.get("dispatch") or getattr(cls, "dispatch", None)
        if not callable(dispatch):
            raise TypeError(
                f"{cls.__name__}: Dispatcher subclass must define dispatch(self) -> tuple[Decoder, ...]"
            )


