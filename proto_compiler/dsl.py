"""
Annotation-style DSL for declaring binary protocol field layouts.

Protocol subclasses use type annotations to describe bit-level fields.
A companion compiler reads these annotations and emits C++ decoder code.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
import inspect
from typing import Any, NamedTuple, Union


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
# Protocol configuration bundle
# ---------------------------------------------------------------------------


class ProtocolConfig:
    """Bundle of protocol-level radio/device parameters.

    Subclass this inside a Protocol class (as a nested ``config`` class) and
    override only the attributes that differ from the base.  Derived Protocol
    classes automatically inherit their parent's ``config`` via Python's normal
    class attribute lookup.
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
    preamble: int | None = None
    preamble_bit_length: int | None = None
    manchester: bool = False
    manchester_start_offset_bits: int | None = None
    manchester_decode_max_bits: int = 0
    scan_rows: str = "first"
    invert: bool = False
    reflect: bool = False

    @property
    def resolved_preamble_bit_length(self) -> int:
        if self.preamble_bit_length is not None:
            return self.preamble_bit_length
        if self.preamble is not None:
            return max(1, self.preamble.bit_length())
        return 0

    @property
    def resolved_manchester_start_offset_bits(self) -> int:
        if self.manchester_start_offset_bits is not None:
            return self.manchester_start_offset_bits
        return self.resolved_preamble_bit_length


# ---------------------------------------------------------------------------
# Protocol and Variant base classes
# ---------------------------------------------------------------------------


_EXCLUDED_CALLABLES: frozenset = frozenset(("decode", "from_hex", "to_json"))
_EXPR_TYPES = (FieldRef, Expr, UnaryExpr, int, float)


def _iter_user_callables(cls: type) -> list[tuple[str, Any]]:
    """Return (name, fn) pairs from a single class's __dict__.

    Skips Protocol/Variant base classes, dunder names, nested types,
    non-callables, and reserved framework method names.
    """
    if cls is Protocol or cls is Variant or cls is object:
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


def _find_fn(cls: type, name: str) -> Any:
    """Return the most-derived definition of *name* in cls's MRO, or None."""
    for base in cls.__mro__:
        fn = base.__dict__.get(name)
        if fn is not None and callable(fn):
            return fn
    return None


def _json_data_type_for(py_type: type) -> str:
    return {float: "DATA_DOUBLE", int: "DATA_INT", str: "DATA_STRING"}.get(py_type, "DATA_INT")


class Protocol:
    """Base class for protocol definitions."""

    # ------------------------------------------------------------------
    # Introspection classmethods – used by the compiler
    # ------------------------------------------------------------------

    @classmethod
    def fields(cls) -> list[tuple[str, FieldSpec]]:
        """Walk MRO base-to-derived and collect annotated FieldSpec entries.

        Uses ``base.__annotations__`` so that PEP 649 lazy annotations
        (Python 3.14+) are evaluated correctly.
        """
        field_dict: dict[str, FieldSpec] = {}
        for base in reversed(cls.__mro__):
            anns = getattr(base, "__annotations__", {})
            for name, ann in anns.items():
                if isinstance(ann, (BitsSpec, LiteralSpec, CondSpec, RepeatSpec)):
                    field_dict[name] = ann
        return list(field_dict.items())

    @classmethod
    def variants(cls) -> list[type]:
        """Walk MRO base-to-derived and collect nested Variant subclasses."""
        variant_dict: dict[str, type] = {}
        for base in reversed(cls.__mro__):
            for name, val in base.__dict__.items():
                if isinstance(val, type) and issubclass(val, Variant):
                    variant_dict[name] = val
        return list(variant_dict.values())

    @classmethod
    def methods(cls) -> list[str]:
        """Collect void method names across MRO in base-to-derived order."""
        method_dict: dict[str, None] = {}
        for base in reversed(cls.__mro__):
            for name, fn in _iter_user_callables(base):
                if getattr(fn, "__annotations__", {}).get("return") is None:
                    method_dict[name] = None
        return list(method_dict.keys())

    @classmethod
    def properties(cls) -> list[tuple[str, type]]:
        """Collect (name, return_type) for return-annotated callables across MRO."""
        prop_dict: dict[str, type] = {}
        for base in reversed(cls.__mro__):
            for name, fn in _iter_user_callables(base):
                ret = getattr(fn, "__annotations__", {}).get("return")
                if ret is not None:
                    prop_dict[name] = ret
        return list(prop_dict.items())

    @classmethod
    def method_exprs(cls) -> dict[str, Any]:
        """Try to inline each method as a pure expression tree using the F proxy."""
        exprs: dict[str, Any] = {}
        explicit = cls.explicit_args()
        for name in cls.methods():
            fn = _find_fn(cls, name)
            if fn is not None:
                if explicit.get(name):
                    exprs[name] = None
                    continue
                result = fn(F)
                exprs[name] = result if isinstance(result, _EXPR_TYPES) else None
        return exprs

    @classmethod
    def property_exprs(cls) -> dict[str, Any]:
        """Try to inline each property as a pure expression tree using the F proxy."""
        exprs: dict[str, Any] = {}
        explicit = cls.explicit_args()
        for name, _ in cls.properties():
            fn = _find_fn(cls, name)
            if fn is not None:
                if explicit.get(name):
                    exprs[name] = None
                    continue
                result = fn(F)
                exprs[name] = result if isinstance(result, _EXPR_TYPES) else None
        return exprs

    @classmethod
    def explicit_args(cls) -> dict[str, tuple[str, ...] | None]:
        """Collect explicit argument lists (excluding self) for methods and properties."""
        result: dict[str, tuple[str, ...] | None] = {}
        for name in cls.methods() + [n for n, _ in cls.properties()]:
            fn = _find_fn(cls, name)
            if fn is not None:
                params = [p for p in inspect.signature(fn).parameters if p != "self"]
                result[name] = tuple(params) if params else None
            else:
                result[name] = None
        return result

    @classmethod
    def json_records(cls) -> tuple[JsonRecord, ...]:
        """Evaluate the class's to_json (or Protocol.to_json fallback) via a proxy."""
        json_fn = cls.__dict__.get("to_json", Protocol.to_json)
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
        me = cls.method_exprs()
        if any(me.get(n) is None for n in cls.methods()):
            return True
        pe = cls.property_exprs()
        if any(pe.get(n) is None for n, _ in cls.properties()):
            return True
        for variant_cls in cls.variants():
            if variant_cls.methods():
                return True
            v_pe = variant_cls.property_exprs()
            if any(v_pe.get(n) is None for n, _ in variant_cls.properties()):
                return True
        return False

    @classmethod
    def output_fields(cls) -> tuple[str, ...]:
        """Ordered output field names for the r_device struct."""
        recs = list(cls.json_records())
        if recs:
            names: list[str] = []
            for rec in recs:
                if rec.key not in names:
                    names.append(rec.key)
            return tuple(names)

        names = ["model"]
        for name, spec in cls.fields():
            if not name.startswith("_") and isinstance(spec, BitsSpec):
                names.append(name)
        for prop_name, _ in cls.properties():
            if prop_name not in names:
                names.append(prop_name)
        for variant_cls in cls.variants():
            for name, spec in variant_cls.fields():
                if not name.startswith("_") and isinstance(spec, BitsSpec) and name not in names:
                    names.append(name)
            for prop_name, _ in variant_cls.properties():
                if prop_name not in names:
                    names.append(prop_name)
        return tuple(names)

    @classmethod
    def referenced_protocols(cls) -> tuple[type[Protocol], ...]:
        """Sub-protocols referenced by Repeat[] fields."""
        result: list[type[Protocol]] = []
        for fname, spec in cls.fields():
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
        for base in cls.__mro__:
            if "delegates" in base.__dict__:
                delegates = base.__dict__["delegates"]
                break
        else:
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
        cfg = getattr(protocol_cls, "config", ProtocolConfig)
        model_name = cfg.output_model or cfg.device_name or protocol_cls.__name__
        records.append(
            JsonRecord(key="model", label="", value=model_name, data_type="DATA_STRING")
        )
        for name, spec in protocol_cls.fields():
            if isinstance(spec, BitsSpec) and not name.startswith("_"):
                records.append(
                    JsonRecord(key=name, label="", value=FieldRef(name), data_type="DATA_INT")
                )
        for prop_name, prop_type in protocol_cls.properties():
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
