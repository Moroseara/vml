// vml in the browser: loads vml.wasm (the same compiler + VM as the vml binary) and runs source.
// Minimal WASI: stdout/stderr are captured, there is no filesystem and no environment.
//   const vml = await VmlWeb.load("vml.wasm");
//   const r = vml.run(source);   // { stdout, stderr, code, ms }
(function (root) {
  "use strict";
  const EBADF = 8, ENOSYS = 52, ESPIPE = 70;

  function makeWasi(state) {
    const view = () => new DataView(state.memory.buffer);
    const bytes = () => new Uint8Array(state.memory.buffer);
    const decoder = new TextDecoder();
    return {
      fd_write(fd, iovs, iovsLen, nwrittenPtr) {
        const dv = view();
        let total = 0;
        for (let i = 0; i < iovsLen; i++) {
          const ptr = dv.getUint32(iovs + i * 8, true);
          const len = dv.getUint32(iovs + i * 8 + 4, true);
          const chunk = decoder.decode(bytes().subarray(ptr, ptr + len), { stream: true });
          if (fd === 1) state.stdout += chunk;
          else if (fd === 2) state.stderr += chunk;
          else return EBADF;
          total += len;
        }
        dv.setUint32(nwrittenPtr, total, true);
        return 0;
      },
      fd_read(fd, iovs, iovsLen, nreadPtr) {          // stdin is always empty (end of input)
        view().setUint32(nreadPtr, 0, true);
        return fd === 0 ? 0 : EBADF;
      },
      fd_close: () => 0,
      fd_seek: () => ESPIPE,
      fd_fdstat_get(fd, statPtr) {
        if (fd > 2) return EBADF;
        const dv = view();
        dv.setUint8(statPtr, 2);                      // character device
        dv.setUint16(statPtr + 2, 0, true);
        dv.setBigUint64(statPtr + 8, 0n, true);
        dv.setBigUint64(statPtr + 16, 0n, true);
        return 0;
      },
      fd_prestat_get: () => EBADF,                   // no preopened folders: fs.* returns errors
      fd_prestat_dir_name: () => EBADF,
      path_open: () => ENOSYS,
      path_filestat_get: () => ENOSYS,
      path_unlink_file: () => ENOSYS,
      environ_sizes_get(countPtr, sizePtr) {
        const dv = view();
        dv.setUint32(countPtr, 0, true);
        dv.setUint32(sizePtr, 0, true);
        return 0;
      },
      environ_get: () => 0,
      clock_time_get(id, precision, timePtr) {
        const ns = id === 0
          ? BigInt(Date.now()) * 1000000n
          : BigInt(Math.round(((typeof performance !== "undefined") ? performance.now() : Date.now()) * 1e6));
        view().setBigUint64(timePtr, ns, true);
        return 0;
      },
      proc_exit(code) { throw new Error("vml exited with " + code); },
    };
  }

  async function load(url) {
    const state = { memory: null, stdout: "", stderr: "" };
    const imports = { wasi_snapshot_preview1: makeWasi(state) };
    let instance;
    if (typeof url !== "string") {
      instance = (await WebAssembly.instantiate(url, imports)).instance;   // bytes (node / tests)
    } else if (WebAssembly.instantiateStreaming) {
      try {
        instance = (await WebAssembly.instantiateStreaming(fetch(url), imports)).instance;
      } catch (e) {
        instance = (await WebAssembly.instantiate(await (await fetch(url)).arrayBuffer(), imports)).instance;
      }
    } else {
      instance = (await WebAssembly.instantiate(await (await fetch(url)).arrayBuffer(), imports)).instance;
    }
    const ex = instance.exports;
    state.memory = ex.memory;
    ex._initialize();
    const encoder = new TextEncoder();
    const cstr = (ptr) => {
      const b = new Uint8Array(state.memory.buffer);
      let end = ptr;
      while (b[end]) end++;
      return new TextDecoder().decode(b.subarray(ptr, end));
    };
    return {
      version: cstr(ex.vml_web_version()),
      // fuel: max loop iterations per run (stops endless loops); 0 = unlimited
      run(source, fuel = 50000000) {
        state.stdout = "";
        state.stderr = "";
        const data = encoder.encode(source);
        const ptr = ex.vml_web_alloc(data.length);
        new Uint8Array(state.memory.buffer).set(data, ptr);
        new Uint8Array(state.memory.buffer)[ptr + data.length] = 0;
        const t0 = (typeof performance !== "undefined") ? performance.now() : Date.now();
        let code;
        try {
          code = ex.vml_web_run(ptr, BigInt(fuel));
        } catch (e) {
          state.stderr += "\n[vml crashed: " + e.message + "]";
          code = 3;
        }
        const ms = ((typeof performance !== "undefined") ? performance.now() : Date.now()) - t0;
        ex.vml_web_free(ptr);
        return { stdout: state.stdout, stderr: state.stderr, code, ms };
      },
    };
  }

  const api = { load };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.VmlWeb = api;
})(typeof self !== "undefined" ? self : this);
