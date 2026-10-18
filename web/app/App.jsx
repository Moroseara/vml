import React, { useState, useEffect, useRef } from "react";
import EXAMPLES from "../examples.js";
import VmlWeb from "../vml-web.js";

const ALL_MODULES = [
  "io", "os", "fs", "env", "time", "random",
  "math", "fmt", "json", "bytes", "bit", "task",
  "coroutine", "debug"
];

export default function App() {
  const [vml, setVml] = useState(null);
  const [vmVersion, setVmVersion] = useState("0.1");
  const [status, setStatus] = useState("loading"); // loading, ready, running, error
  const [statusText, setStatusText] = useState("Loading VM...");

  const [activeTab, setActiveTab] = useState(() => {
    try {
      const saved = localStorage.getItem("vml.tab");
      if (saved && EXAMPLES.some(e => e.id === saved)) return saved;
    } catch {}
    return EXAMPLES[0]?.id || "hello";
  });

  const [code, setCode] = useState(() => {
    try {
      const savedTab = localStorage.getItem("vml.tab") || (EXAMPLES[0]?.id || "hello");
      const draft = localStorage.getItem("vml.draft." + savedTab);
      if (draft !== null) return draft;
    } catch {}
    return EXAMPLES[0]?.code || "";
  });

  const [output, setOutput] = useState({
    stdout: "",
    stderr: "",
    code: null,
    ms: 0,
  });

  const [fuel, setFuel] = useState(() => {
    try {
      return Number(localStorage.getItem("vml.fuel")) || 50000000;
    } catch {
      return 50000000;
    }
  });

  const [gcStress, setGcStress] = useState(() => {
    try {
      return localStorage.getItem("vml.gc") === "1";
    } catch {
      return false;
    }
  });

  const [denied, setDenied] = useState(() => {
    try {
      const saved = localStorage.getItem("vml.deny");
      return saved ? saved.split(",").filter(Boolean) : [];
    } catch {
      return [];
    }
  });

  const textareaRef = useRef(null);

  // Initialize WebAssembly VM
  useEffect(() => {
    VmlWeb.load("vml.wasm")
      .then((instance) => {
        setVml(instance);
        setVmVersion(instance.version || "0.1");
        setStatus("ready");
        setStatusText("Ready");
        execute(instance, code, fuel, denied, gcStress);
      })
      .catch((err) => {
        setStatus("error");
        setStatusText("VM Load Failed");
        setOutput({
          stdout: "",
          stderr: "Failed to load vml.wasm: " + err.message,
          code: 1,
          ms: 0,
        });
      });
  }, []);

  function execute(vmInstance, src, currentFuel, currentDenied, currentGc) {
    const targetVm = vmInstance || vml;
    if (!targetVm) return;

    setStatus("running");
    setStatusText("Running...");

    setTimeout(() => {
      try {
        const res = targetVm.run(src, {
          fuel: currentFuel,
          deny: currentDenied,
          gcStress: currentGc,
        });
        setOutput(res);
        setStatus(res.code === 0 && !res.stderr ? "ready" : "error");
        setStatusText(
          res.code === 0 && !res.stderr
            ? `Exit 0 (${res.ms.toFixed(1)} ms)`
            : `Exit ${res.code} (${res.ms.toFixed(1)} ms)`
        );
      } catch (e) {
        setOutput({
          stdout: "",
          stderr: "Execution error: " + e.message,
          code: -1,
          ms: 0,
        });
        setStatus("error");
        setStatusText("Crash");
      }
    }, 16);
  }

  function handleRun() {
    execute(vml, code, fuel, denied, gcStress);
  }

  function handleSelectTab(id) {
    try {
      localStorage.setItem("vml.draft." + activeTab, code);
      localStorage.setItem("vml.tab", id);
    } catch {}

    setActiveTab(id);
    let nextCode = "";
    try {
      const draft = localStorage.getItem("vml.draft." + id);
      nextCode = draft !== null ? draft : (EXAMPLES.find(e => e.id === id)?.code || "");
    } catch {
      nextCode = EXAMPLES.find(e => e.id === id)?.code || "";
    }
    setCode(nextCode);
    execute(vml, nextCode, fuel, denied, gcStress);
  }

  function handleReset() {
    const orig = EXAMPLES.find(e => e.id === activeTab)?.code || "";
    setCode(orig);
    try {
      localStorage.removeItem("vml.draft." + activeTab);
    } catch {}
    execute(vml, orig, fuel, denied, gcStress);
  }

  function handleCodeChange(e) {
    const val = e.target.value;
    setCode(val);
    try {
      localStorage.setItem("vml.draft." + activeTab, val);
    } catch {}
  }

  function handleKeyDown(e) {
    if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
      e.preventDefault();
      handleRun();
      return;
    }

    const textarea = textareaRef.current;
    if (!textarea) return;

    if (e.key === "Tab" && !e.shiftKey) {
      e.preventDefault();
      const start = textarea.selectionStart;
      const end = textarea.selectionEnd;
      const updated = code.substring(0, start) + "    " + code.substring(end);
      setCode(updated);
      setTimeout(() => {
        textarea.selectionStart = textarea.selectionEnd = start + 4;
      }, 0);
    } else if (e.key === "Enter" && !e.altKey) {
      const start = textarea.selectionStart;
      const lineStart = code.lastIndexOf("\n", start - 1) + 1;
      const indent = (code.slice(lineStart, start).match(/^ */) || [""])[0];
      const extra = /\{\s*$/.test(code.slice(lineStart, start)) ? "    " : "";
      e.preventDefault();
      const insert = "\n" + indent + extra;
      const updated = code.substring(0, start) + insert + code.substring(textarea.selectionEnd);
      setCode(updated);
      setTimeout(() => {
        textarea.selectionStart = textarea.selectionEnd = start + insert.length;
      }, 0);
    }
  }

  function toggleModule(mod) {
    const fullName = "std." + mod;
    let next;
    if (denied.includes(fullName)) {
      next = denied.filter(m => m !== fullName);
    } else {
      next = [...denied, fullName];
    }
    setDenied(next);
    try {
      localStorage.setItem("vml.deny", next.join(","));
    } catch {}
  }

  function allowAllModules() {
    setDenied([]);
    try {
      localStorage.setItem("vml.deny", "");
    } catch {}
  }

  function handleFuelChange(e) {
    const val = Number(e.target.value);
    setFuel(val);
    try {
      localStorage.setItem("vml.fuel", String(val));
    } catch {}
  }

  function handleGcToggle(e) {
    const val = e.target.checked;
    setGcStress(val);
    try {
      localStorage.setItem("vml.gc", val ? "1" : "0");
    } catch {}
  }

  const isMac = typeof navigator !== "undefined" && /Mac|iPhone|iPad/.test(navigator.platform || "");

  return (
    <div className="page-wrapper">
      {/* Header */}
      <header className="site-header">
        <a className="brand-title" href="#top">
          <span className="brand-name">vml</span>
          <span className="brand-ver">v{vmVersion}</span>
        </a>
        <nav className="nav-menu">
          <a href="#playground">Playground</a>
          <a href="#tour">Language</a>
          <a href="#quickstart">Quickstart</a>
          <a href="#status">Status</a>
          <a href="https://github.com/Moroseara/vml" target="_blank" rel="noreferrer">GitHub</a>
        </nav>
      </header>

      {/* Overview Card */}
      <div className="overview-card">
        <h1>vml — compact bytecode language & fast register VM</h1>
        <p>
          Statically typed, errors as values, clean package layout, and single-file executable output.
          This playground runs the compiler and register VM entirely inside your browser via WebAssembly.
        </p>
      </div>

      {/* Playground Area */}
      <div className="playground-grid" id="playground">
        {/* Editor Panel */}
        <div className="panel-card">
          <div className="tabs-header" role="tablist">
            {EXAMPLES.map((ex) => (
              <button
                key={ex.id}
                role="tab"
                type="button"
                className={`tab-item ${activeTab === ex.id ? "active" : ""}`}
                onClick={() => handleSelectTab(ex.id)}
              >
                {ex.title}
              </button>
            ))}
          </div>

          <div className="editor-container">
            <textarea
              ref={textareaRef}
              className="editor-textarea"
              spellCheck="false"
              autoCapitalize="off"
              autoComplete="off"
              value={code}
              onChange={handleCodeChange}
              onKeyDown={handleKeyDown}
              aria-label="vml source code"
            />
          </div>

          <div className="panel-footer">
            <div style={{ display: "flex", gap: "8px", alignItems: "center" }}>
              <button
                type="button"
                className="btn btn-primary"
                disabled={status === "loading" || status === "running"}
                onClick={handleRun}
              >
                Run ({isMac ? "⌘↵" : "Ctrl+↵"})
              </button>
              <button
                type="button"
                className="btn btn-sm"
                onClick={handleReset}
                title="Reset to original example code"
              >
                Reset
              </button>
            </div>
            <div className="status-indicator">
              <span className={`status-circle ${status}`} />
              <span>{statusText}</span>
            </div>
          </div>
        </div>

        {/* Output Panel */}
        <div className="panel-card">
          <div className="output-header">
            <span>Output (stdout / stderr)</span>
            <span>{output.ms > 0 ? `${output.ms.toFixed(1)} ms` : ""}</span>
          </div>

          <div className="output-body">
            {output.stdout && <div>{output.stdout}</div>}
            {output.stderr && <div className="err-msg">{output.stderr}</div>}
            {!output.stdout && !output.stderr && (
              <span className="empty-hint">
                {status === "loading" ? "Initializing WebAssembly VM..." : "Output will appear here."}
              </span>
            )}
          </div>

          <div className="panel-footer">
            <span style={{ fontSize: "12px", color: "var(--text-muted)", fontFamily: "var(--font-mono)" }}>
              Exit code: <strong>{output.code !== null ? output.code : 0}</strong>
            </span>
            <button
              type="button"
              className="btn btn-sm"
              onClick={() => setOutput({ stdout: "", stderr: "", code: null, ms: 0 })}
            >
              Clear
            </button>
          </div>
        </div>
      </div>

      {/* Sandbox Controls Card */}
      <div className="sandbox-card">
        <h2>Host Sandbox & Permissions</h2>
        <p className="sandbox-desc">
          Controls which standard modules the host allows scripts to import, plus loop fuel limits to prevent runaway scripts.
        </p>

        <div className="module-tags">
          {ALL_MODULES.map((m) => {
            const fullName = "std." + m;
            const isDenied = denied.includes(fullName);
            return (
              <label key={m} className={`module-tag ${isDenied ? "denied" : ""}`}>
                <input
                  type="checkbox"
                  checked={!isDenied}
                  onChange={() => toggleModule(m)}
                />
                <span>{fullName}</span>
              </label>
            );
          })}
        </div>

        <div className="sandbox-options">
          <label>
            Fuel limit:{" "}
            <select className="clean-select" value={fuel} onChange={handleFuelChange}>
              <option value="1000000">1,000,000 loops</option>
              <option value="50000000">50,000,000 loops</option>
              <option value="0">Unlimited (0)</option>
            </select>
          </label>

          <label>
            <input
              type="checkbox"
              checked={gcStress}
              onChange={handleGcToggle}
              style={{ marginRight: "6px" }}
            />
            GC stress (collect at every safepoint)
          </label>

          <button type="button" className="btn btn-sm" onClick={allowAllModules}>
            Allow all modules
          </button>
        </div>
      </div>

      {/* Language Overview Section */}
      <div id="tour">
        <h2 className="section-heading">Language Features</h2>
        <div className="cards-grid">
          <div className="doc-box">
            <h3>Errors as Values</h3>
            <p>Functions that can fail declare it in their return type: <code>T ! E</code>. Callers handle it with <code>catch</code> or propagate with <code>?</code>.</p>
            <pre className="snippet-pre"><code>{`func parse_int(s: string): int ! string {
    if #s == 0 { throw "empty string" }
    return "0123456789":find(s)?
}
let n = parse_int("42") catch 0`}</code></pre>
          </div>

          <div className="doc-box">
            <h3>Packages & Namespaces</h3>
            <p>Packages correspond directly to folders. Shared functions and structures are exported cleanly using <code>pub</code>.</p>
            <pre className="snippet-pre"><code>{`## math/vector.vml
module math.vector
pub struct Vec2 { pub x: float, pub y: float }
pub func (v: Vec2) length(): float {
    return (v.x * v.x + v.y * v.y):sqrt()
}`}</code></pre>
          </div>

          <div className="doc-box">
            <h3>Single-File Binaries</h3>
            <p><code>vml build</code> outputs standalone runnable programs carrying the runtime with them. No installation required on target machines.</p>
            <pre className="snippet-pre"><code>{`$ vml build
built dist/app
$ ./dist/app
ready.`}</code></pre>
          </div>

          <div className="doc-box">
            <h3>C Embedding API</h3>
            <p>Integrate directly into C applications. Register custom host functions, configure execution fuel, and isolate environments.</p>
            <pre className="snippet-pre"><code>{`VML_State *L = vml_new(&opts);
vml_set_args(L, argc, argv);
vml_load_file(L, "script.vml");`}</code></pre>
          </div>
        </div>
      </div>

      {/* Quickstart Section */}
      <div id="quickstart">
        <h2 className="section-heading">Quickstart</h2>
        <div className="doc-box">
          <p>Clone and build from source using any standard C compiler on macOS or Linux:</p>
          <pre className="snippet-pre"><code>{`# 1. Clone repository
git clone https://github.com/Moroseara/vml.git
cd vml

# 2. Build and run tests
make -j8 && make test

# 3. Create and run a new project
./vml new myproject
cd myproject
../vml run`}</code></pre>
        </div>
      </div>

      {/* Project Status */}
      <div id="status">
        <h2 className="section-heading">Project Status</h2>
        <table className="clean-table">
          <thead>
            <tr>
              <th style={{ width: "90px" }}>Status</th>
              <th>Feature / Component</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td><span className="badge-done">DONE</span></td>
              <td>Register-based bytecode VM with garbage collector and loop fuel</td>
            </tr>
            <tr>
              <td><span className="badge-done">DONE</span></td>
              <td>Compiler pipeline: lexer, recursive-descent parser, type checker, emitter</td>
            </tr>
            <tr>
              <td><span className="badge-done">DONE</span></td>
              <td>Error handling: typed errors, <code>?</code> operator, <code>catch</code> blocks</td>
            </tr>
            <tr>
              <td><span className="badge-done">DONE</span></td>
              <td>Standard library: string, array, map, math, json, fmt, bytes, fs, os</td>
            </tr>
            <tr>
              <td><span className="badge-done">DONE</span></td>
              <td>WebAssembly build for in-browser playground execution</td>
            </tr>
            <tr>
              <td><span className="badge-next">NEXT</span></td>
              <td>Coroutines and cooperatively yielding tasks</td>
            </tr>
            <tr>
              <td><span className="badge-next">NEXT</span></td>
              <td>Direct C Foreign Function Interface (FFI) bindings</td>
            </tr>
            <tr>
              <td><span className="badge-next">NEXT</span></td>
              <td>Language Server Protocol (LSP) and code formatter</td>
            </tr>
          </tbody>
        </table>
      </div>

      {/* Footer */}
      <footer className="clean-footer">
        <div>vml — bytecode language & register VM</div>
        <div>Built with React & WebAssembly</div>
      </footer>
    </div>
  );
}
