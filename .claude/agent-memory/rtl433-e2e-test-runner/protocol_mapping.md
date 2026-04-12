---
name: Proto-Compiled Protocol Mapping
description: Which rtl_433 protocols have definitions in proto_compiler/protocols and are compiled
type: reference
---

## Proto-Compiled Protocols
The following protocols have Python DSL definitions in `/Users/ali/rtl_433/proto_compiler/protocols/` and are compiled into C:

- `abmt` (abmt.py)
- `acurite_01185m` (acurite_01185m.py)
- `akhan_100F14` (akhan_100F14.py)
- `alectov1` (alectov1.py)
- `ambient_weather` (ambient_weather.py)
- `current_cost` (current_cost.py)
- `honeywell_wdb` (honeywell_wdb.py) — base protocol with variants
- `honeywell_wdb_fsk` (honeywell_wdb_fsk.py) — FSK variant
- `lacrosse_tx31u` (lacrosse_tx31u.py)
- `thermopro_tp211b` (thermopro_tp211b.py)
- `tpms_ford` (tpms_ford.py)

(Supporting utilities: `honeywell_wdb_common.py`)

## How to Identify Proto-Compiled Decoders in E2E Tests

If an e2e test fails for a protocol:
1. Check if a `.py` file exists in `proto_compiler/protocols/`
2. If yes, the failure is plausibly a proto-compiler bug
3. If no, it's a hand-written C decoder — investigate in the C source

## Generated C Files
Each proto-compiled protocol produces a generated C source file in `src/devices/`. These files have auto-generation headers and are regenerated on each build from the Python DSL definitions.
