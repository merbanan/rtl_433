"""
Annotation-style DSL for declaring binary protocol field layouts.

Protocol subclasses use type annotations to describe bit-level fields.
A companion compiler reads these annotations and emits C++ decoder code.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
import inspect
import sys
from typing import Any, ClassVar, NamedTuple, Union


# ---------------------------------------------------------------------------
# Expression tree – built by the F proxy via operator overloading
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class FieldRef:
    """Reference to a previously parsed protocol field."""

    name: str

    # Comparison
    def __eq__(self, other: Any) -> Expr:  # type: ignore[override]
        return Expr("==", self, _wrap(other))

    def __ne__(self, other: Any) -> Expr:  # type: ignore[override]
        return Expr("!=", self, _wrap(other))

    def __lt__(self, other: Any) -> Expr:
        return Expr("<", self, _wrap(other))

    def __le__(self, other: Any) -> Expr:
        return Expr("<=", self, _wrap(other))

    def __gt__(self, other: Any) -> Expr:
        return Expr(">", self, _wrap(other))

    def __ge__(self, other: Any) -> Expr:
        return Expr(">=", self, _wrap(other))

    # Bitwise
    def __and__(self, other: Any) -> Expr:
        return Expr("&", self, _wrap(other))

    def __rand__(self, other: Any) -> Expr:
        return Expr("&", _wrap(other), self)

    def __or__(self, other: Any) -> Expr:
        return Expr("|", self, _wrap(other))

    def __ror__(self, other: Any) -> Expr:
        return Expr("|", _wrap(other), self)

    def __xor__(self, other: Any) -> Expr:
        return Expr("^", self, _wrap(other))

    def __rxor__(self, other: Any) -> Expr:
        return Expr("^", _wrap(other), self)

    def __invert__(self) -> UnaryExpr:
        return UnaryExpr("~", self)

    # Arithmetic
    def __add__(self, other: Any) -> Expr:
        return Expr("+", self, _wrap(other))

    def __radd__(self, other: Any) -> Expr:
        return Expr("+", _wrap(other), self)

    def __sub__(self, other: Any) -> Expr:
        return Expr("-", self, _wrap(other))

    def __rsub__(self, other: Any) -> Expr:
        return Expr("-", _wrap(other), self)

    def __mul__(self, other: Any) -> Expr:
        return Expr("*", self, _wrap(other))

    def __rmul__(self, other: Any) -> Expr:
        return Expr("*", _wrap(other), self)

    def __truediv__(self, other: Any) -> Expr:
        return Expr("/", self, _wrap(other))

    def __rtruediv__(self, other: Any) -> Expr:
        return Expr("/", _wrap(other), self)

    def __floordiv__(self, other: Any) -> Expr:
        return Expr("/", self, _wrap(other))

    def __rfloordiv__(self, other: Any) -> Expr:
        return Expr("/", _wrap(other), self)

    def __mod__(self, other: Any) -> Expr:
        return Expr("%", self, _wrap(other))

    def __neg__(self) -> UnaryExpr:
        return UnaryExpr("-", self)

    # Shift
    def __lshift__(self, other: Any) -> Expr:
        return Expr("<<", self, _wrap(other))

    def __rshift__(self, other: Any) -> Expr:
        return Expr(">>", self, _wrap(other))

    def __hash__(self) -> int:
        return hash(("FieldRef", self.name))


@dataclass(frozen=True)
class Expr:
    """Binary expression node in the expression tree."""

    op: str
    left: FieldRef | Expr | UnaryExpr | int
    right: FieldRef | Expr | UnaryExpr | int

    # Allow further chaining: (F.x & 0x0F) == 3
    def __eq__(self, other: Any) -> Expr:  # type: ignore[override]
        return Expr("==", self, _wrap(other))

    def __ne__(self, other: Any) -> Expr:  # type: ignore[override]
        return Expr("!=", self, _wrap(other))

    def __lt__(self, other: Any) -> Expr:
        return Expr("<", self, _wrap(other))

    def __le__(self, other: Any) -> Expr:
        return Expr("<=", self, _wrap(other))

    def __gt__(self, other: Any) -> Expr:
        return Expr(">", self, _wrap(other))

    def __ge__(self, other: Any) -> Expr:
        return Expr(">=", self, _wrap(other))

    def __and__(self, other: Any) -> Expr:
        return Expr("&", self, _wrap(other))

    def __rand__(self, other: Any) -> Expr:
        return Expr("&", _wrap(other), self)

    def __or__(self, other: Any) -> Expr:
        return Expr("|", self, _wrap(other))

    def __ror__(self, other: Any) -> Expr:
        return Expr("|", _wrap(other), self)

    def __xor__(self, other: Any) -> Expr:
        return Expr("^", self, _wrap(other))

    def __invert__(self) -> UnaryExpr:
        return UnaryExpr("~", self)

    def __add__(self, other: Any) -> Expr:
        return Expr("+", self, _wrap(other))

    def __radd__(self, other: Any) -> Expr:
        return Expr("+", _wrap(other), self)

    def __sub__(self, other: Any) -> Expr:
        return Expr("-", self, _wrap(other))

    def __rsub__(self, other: Any) -> Expr:
        return Expr("-", _wrap(other), self)

    def __mul__(self, other: Any) -> Expr:
        return Expr("*", self, _wrap(other))

    def __rmul__(self, other: Any) -> Expr:
        return Expr("*", _wrap(other), self)

    def __truediv__(self, other: Any) -> Expr:
        return Expr("/", self, _wrap(other))

    def __rtruediv__(self, other: Any) -> Expr:
        return Expr("/", _wrap(other), self)

    def __floordiv__(self, other: Any) -> Expr:
        return Expr("/", self, _wrap(other))

    def __rfloordiv__(self, other: Any) -> Expr:
        return Expr("/", _wrap(other), self)

    def __mod__(self, other: Any) -> Expr:
        return Expr("%", self, _wrap(other))

    def __neg__(self) -> UnaryExpr:
        return UnaryExpr("-", self)

    def __lshift__(self, other: Any) -> Expr:
        return Expr("<<", self, _wrap(other))

    def __rshift__(self, other: Any) -> Expr:
        return Expr(">>", self, _wrap(other))

    def __hash__(self) -> int:
        return hash(("Expr", self.op, self.left, self.right))


@dataclass(frozen=True)
class UnaryExpr:
    """Unary expression node (e.g. bitwise NOT)."""

    op: str
    operand: FieldRef | Expr | UnaryExpr | int

    def __eq__(self, other: Any) -> Expr:  # type: ignore[override]
        return Expr("==", self, _wrap(other))

    def __ne__(self, other: Any) -> Expr:  # type: ignore[override]
        return Expr("!=", self, _wrap(other))

    def __and__(self, other: Any) -> Expr:
        return Expr("&", self, _wrap(other))

    def __or__(self, other: Any) -> Expr:
        return Expr("|", self, _wrap(other))

    def __hash__(self) -> int:
        return hash(("UnaryExpr", self.op, self.operand))


def _wrap(value: Any) -> FieldRef | Expr | UnaryExpr | int | float:
    """Ensure expression tree leaves are valid node types."""
    if isinstance(value, (FieldRef, Expr, UnaryExpr, int, float)):
        return value
    raise TypeError(f"Cannot use {type(value).__name__} in a field expression")


class _FieldProxy:
    """Singleton proxy: ``F.field_name`` produces a ``FieldRef``."""

    def __getattr__(self, name: str) -> FieldRef:
        if name.startswith("_"):
            raise AttributeError(name)
        return FieldRef(name)


F = _FieldProxy()


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
    expr: Expr | UnaryExpr | FieldRef
    true_type: BitsSpec | LiteralSpec
    false_type: BitsSpec | LiteralSpec | None


@dataclass(frozen=True)
class RepeatSpec:
    count_expr: Expr | UnaryExpr | FieldRef | int
    sub_protocol: type  # a Protocol subclass


FieldSpec = Union[BitsSpec, LiteralSpec, CondSpec, RepeatSpec]


class JsonRecord(NamedTuple):
    key: str
    label: str
    value: FieldRef | Expr | UnaryExpr | int | float | str
    data_type: str  # "DATA_STRING" | "DATA_INT" | "DATA_DOUBLE"
    fmt: str | None = None
    when: FieldRef | Expr | UnaryExpr | bool | None = None


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
    """Row-selection strategy: scan rows until one matches and decodes.

    Alternative to :class:`FindRepeatedRow` (which picks a *repeated* row index).
    Use ``FirstValidRow`` when the slicer may deliver multiple candidate rows and
    the decoder must try each until length, checksum, and sanity pass.

    Emitted as a ``for`` loop over ``bitbuffer->num_rows``. Only rows with
    exactly *bits* are considered. If *reflect* is true, ``reflect_bytes`` is
    applied to that row before checks. If *checksum_over_bytes* is N > 0, the
    compiler emits ``add_bytes(b, N)`` vs ``b[N]`` (``continue`` on MIC mismatch,
    ``return DECODE_FAIL_SANITY`` if the sum is zero).

    Must be the **last** step in ``prepare()``; no steps may follow it. Do not
    combine with :class:`FindRepeatedRow` in the same pipeline.
    """
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
    ``Cond[F.flags & 1, Bits[16], Bits[12]]``  -> CondSpec with both branches
    ``Cond[F.flags & 2, Bits[8]]``              -> CondSpec, false_type=None (optional)
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
    """``Repeat[F.measurements, SensorReading]`` -> RepeatSpec"""

    def __class_getitem__(cls, params: tuple) -> RepeatSpec:
        if not isinstance(params, tuple) or len(params) != 2:
            raise ValueError("Repeat requires (count_expr, sub_protocol)")
        count_expr, sub_protocol = params
        return RepeatSpec(count_expr=count_expr, sub_protocol=sub_protocol)


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


ProtocolConfig = ModulationConfig


# ---------------------------------------------------------------------------
# Protocol and Variant base classes
# ---------------------------------------------------------------------------


_EXCLUDED_CALLABLES: frozenset = frozenset(("decode", "from_hex", "to_json", "prepare"))
_EXPR_TYPES = (FieldRef, Expr, UnaryExpr, int, float)


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
            if isinstance(ann, (BitsSpec, LiteralSpec, CondSpec, RepeatSpec)):
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
        for name, fn in _iter_user_callables(cls):
            ret = _evaluated_callable_annotations(fn).get("return")
            if ret is None:
                merged_methods[name] = None
                merged_props.pop(name, None)
            else:
                merged_props[name] = ret
                merged_methods.pop(name, None)
        cls.methods = tuple(merged_methods.keys())
        cls.properties = tuple(merged_props.items())

    def __getattr__(self, name: str) -> FieldRef:
        if name.startswith("_"):
            raise AttributeError(name)
        return FieldRef(name)

    # ------------------------------------------------------------------
    # Introspection classmethods – used by the compiler
    # ------------------------------------------------------------------

    @classmethod
    def explicit_args(cls) -> dict[str, tuple[str, ...] | None]:
        """Collect explicit argument lists (excluding self) for methods and properties."""
        result: dict[str, tuple[str, ...] | None] = {}
        for name in cls.methods + tuple(n for n, _ in cls.properties):
            params = [p for p in inspect.signature(getattr(cls, name)).parameters if p != "self"]
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
        proxy = cls()
        explicit = cls.explicit_args()
        for name in cls.methods:
            if explicit.get(name) or not isinstance(getattr(proxy, name)(), _EXPR_TYPES):
                return True
        for name, _ in cls.properties:
            if explicit.get(name) or not isinstance(getattr(proxy, name)(), _EXPR_TYPES):
                return True
        for variant_cls in cls.variants:
            if variant_cls.methods:
                return True
            v_proxy = variant_cls()
            v_explicit = variant_cls.explicit_args()
            for name, _ in variant_cls.properties:
                if v_explicit.get(name) or not isinstance(getattr(v_proxy, name)(), _EXPR_TYPES):
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
        """Sub-protocols referenced by Repeat[] fields."""
        result: list[type[Protocol]] = []
        for fname, spec in cls.fields:
            if isinstance(spec, RepeatSpec):
                sp = spec.sub_protocol
                if not isinstance(sp, type) or not issubclass(sp, Protocol):
                    raise ValueError(
                        f"{cls.__name__} field {fname!r}: Repeat sub_protocol must be a Protocol subclass, got {sp!r}"
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
    """Base class for variant payload definitions (nested in a Protocol)."""

    when = None
    repeat_count = None
