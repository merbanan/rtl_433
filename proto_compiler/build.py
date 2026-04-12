"""
Generate a C device decoder from a proto_compiler protocol module.

Usage:
    python proto_compiler/build.py <proto_module.py> <output.c>

The protocol module must define a module-level ``decoders`` tuple containing
the Decoder/Dispatcher instance(s) to compile. The generated C source for each
instance is concatenated into the output file.
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

# Allow running directly from the repo root without installing the package.
sys.path.insert(0, str(Path(__file__).parent.parent))

from proto_compiler.codegen import compile_decoder  # noqa: E402


def generate(proto_path: Path, out_path: Path) -> None:
    spec = importlib.util.spec_from_file_location(proto_path.stem, proto_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    decoders = module.decoders
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(compile_decoder(d) for d in decoders))


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <proto_module.py> <output.c>", file=sys.stderr)
        sys.exit(1)
    generate(Path(sys.argv[1]), Path(sys.argv[2]))


if __name__ == "__main__":
    main()
