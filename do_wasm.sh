#!/bin/bash
set -e
mkdir -p build
cd build 

cat > wasmapi.c <<-EOF
#include "rtl_433.h"
#include "r_private.h"
#include "r_api.h"

uint32_t analyseBuffer[1024];
int analyse(uint32_t* pulse, int count)
{
    r_cfg_t g_cfg;
    r_cfg_t* cfg = &g_cfg;
    r_init_cfg(cfg);
    cfg->report_time = REPORT_TIME_OFF;
    register_all_protocols(cfg, 0);

    pulse_data_t data = {0};
    data.sample_rate = 1000000;

    for (int i=0; i<count; i+=2)
    {
        data.pulse[i/2] = pulse[i];
        data.gap[i/2] = i < count ? pulse[i+1] : 0;
        data.num_pulses++;
    }

    add_json_output(cfg, NULL);
    return run_ook_demods(&cfg->demod->r_devs, &data);
}
EOF

cat > wasmtest.html <<-EOF
<script src="wasm.js"></script>
<script>
var HEAPU8;
var HEAPU32;
var TOTAL_MEMORY = 67108864;
var WASM_PAGE_SIZE = 4096;

var env = {
  emscripten_memcpy_big: (dest, src, num) => {
    HEAPU8.copyWithin(dest, src, src + num);
  },
  emscripten_date_now: () => { return Date.now(); },
  fd_write:(fd, iov, iovcnt, pnum) => {
    var ret = 0;
    for (var i = 0; i < iovcnt; i++) {
      var ptr = HEAPU32[iov>>2];
      var len = HEAPU32[(iov+4)>>2];
      iov += 8;
      var msg = "";
      for (var j=ptr; j<ptr+len; j++)
        msg += String.fromCharCode(HEAPU8[j]);
      if (msg != "" && msg != "\n")
        console.log(msg);
      ret += len;
    }
    HEAPU32[pnum>>2] = ret;
    return 0;
  },
  emscripten_resize_heap: () => { throw "emscripten_resize_heap not implemented" },
  memory:new WebAssembly.Memory({ 'initial': TOTAL_MEMORY / WASM_PAGE_SIZE, 'maximum': TOTAL_MEMORY / WASM_PAGE_SIZE }),
};

for (var i of ["__assert_fail", "exit", "strftime", "fd_close", "_tzset_js", "_mktime_js", 
  "_localtime_js", "__syscall_fcntl64", "__syscall_openat", "__syscall_ioctl", "fd_read", 
  "emscripten_resize_heap", "fd_seek"])
{
  env[i] = () => { throw "not implemented" };
}

WebAssembly.instantiate(wasmBinary, {env:env, wasi_snapshot_preview1: env}).then(obj =>
{
  var exports = obj.instance.exports;
  HEAPU8 = new Uint8Array(exports.memory.buffer);
  HEAPU32 = new Uint32Array(exports.memory.buffer);

  exports.__wasm_call_ctors();

  var analyseBuffer = new Uint32Array(
    exports.memory.buffer,
    exports.analyseBuffer.value,
    1024*4
  );

  var testSignal = [520, 500, 420, 520, 400, 520, 440, 520, 400, 520, 400, 540, 400, 520, 440, 500, 440, 480, 440, 500, 440, 500, 460, 440, 460, 500, 420, 500, 440, 520, 420, 520, 420, 500, 460, 460, 480, 440, 480, 440, 500, 460, 480, 460, 460, 460, 480, 920, 980, 940, 960, 460, 500, 440, 500, 440, 480, 460, 480, 920, 500, 460, 500, 420, 980, 920, 980, 940, 480, 440, 500, 460, 460, 460, 960, 940, 980, 920, 500, 460, 480, 440, 480, 440, 960, 460, 520, 920, 960, 480, 480, 420, 500, 460, 480, 440, 500, 920, 480, 440, 500, 440, 500, 420, 500, 440, 500, 440, 980, 960, 440, 460, 480, 460, 500, 420, 520, 440, 480, 460, 480, 420, 500, 460, 480, 440, 500, 440, 500, 440, 480, 440, 480, 460, 980, 440, 480, 960, 960, 440, 500, 460, 460, 960, 460, 460, 500, 420, 980, 440, 500, 440, 500, 920, 960, 960, 960, 940, 980, 920, 960, 460, 500, 940, 960, 960, 460, 460, 960, 460, 480, 0]
  for (var i=0; i<testSignal.length; i++)
    analyseBuffer[i] = testSignal[i];

  console.log("result", exports.analyse(exports.analyseBuffer.value, testSignal.length));
});

</script>
EOF

INCLUDES="-I ../include"
SOURCES1=$(find ../src -maxdepth 1 -name '*.c' -print)
SOURCES2=$(find ../src/devices -maxdepth 1 -name '*.c' -print)
SOURCES="$SOURCES1 $SOURCES2 wasmapi.c"
EXPORTED="['_analyse', '_analyseBuffer']"
emcc ${INCLUDES} ${SOURCES} -gsource-map -g3 -O3 -s TOTAL_STACK=1048576 -s TOTAL_MEMORY=67108864 -s MINIMAL_RUNTIME=1 -s WASM=1 -s EXPORTED_FUNCTIONS="${EXPORTED}" -o rtl433.js -DEMSCRIPTEN -s ERROR_ON_UNDEFINED_SYMBOLS=0 --source-map-base http://localhost:8081/build/

node <<-EOF
var fs = require("fs");
var prefix = 'wasmBinary = (() => { var wasmcode="';
var suffix = '"; return Uint8Array.from(atob(wasmcode), c => c.charCodeAt(0));})();';
fs.writeFileSync("wasm.js", prefix + fs.readFileSync("rtl433.wasm").toString("base64") + suffix);
EOF
