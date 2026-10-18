import React, { useState, useEffect, useRef } from "react";
import EXAMPLES from "../examples.js";
import VmlWeb from "../vml-web.js";

const ALL_MODULES = [
  "io", "os", "fs", "env", "time", "random",
  "math", "fmt", "json", "bytes", "bit", "task",
  "coroutine", "debug"
];

export default function App() {
  const [theme, setTheme] = useState(() => {
    try {
      return localStorage.getItem("vml.theme") ||
        (window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
    } catch {
      return "light";
    }
  });

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

  // Sync theme to root DOM
  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    try {
      localStorage.setItem("vml.theme", theme);
    } catch {}
  }, [theme]);

  // Load WebAssembly VM
  useEffect(() => {
    VmlWeb.load("vml.wasm")
      .then((instance) => {
        setVml(instance);
        setVmVersion(instance.version || "0.1");
        setStatus("ready");
        setStatusText("Ready");
        // Initial run
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

    // Allow UI to paint before blocking synchronous execution
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
          stderr: "VM Execution Exception: " + e.message,
          code: -1,
          ms: 0,
        });
        setStatus("error");
        setStatusText("Execution Crash");
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
    <div className="container">
      {/* Top Header */}
      <header className="top-bar">
        <a className="brand" href="#top">
          <span>vml</span>
          <span className="badge">v{vmVersion}</span>
        </a>
        <nav className="nav-links">
          <a href="#workbench">[Playground]</a>
          <a href="#tour">[Language]</a>
          <a href="#start">[Quickstart]</a>
          <a href="#status">[Status]</a>
          <a href="https://github.com/Moroseara/vml" target="_blank" rel="noreferrer">[GitHub]</a>
          <button
            type="button"
            className="btn btn-sm"
            onClick={() => setTheme(theme === "dark" ? "light" : "dark")}
            title="Toggle Light / Dark mode"
          >
            {theme === "dark" ? "☼ LIGHT" : "☾ DARK"}
          </button>
        </nav>
      </header>

      {/* Hero Summary */}
      <div className="hero-box">
        <h1>vml — compact bytecode language & fast register VM</h1>
        <p>
          Statically typed, errors as values, zero-fluff syntax, and builds into a single self-contained binary.
          The interactive playground below runs real <code>vml</code> via WebAssembly directly in your browser.
        </p>
      </div>

      {/* Main Workbench Grid */}
      <div className="workbench" id="workbench">
        {/* Editor Box */}
        <div className="box">
          <div className="tab-bar" role="tablist">
            {EXAMPLES.map((ex) => (
              <button
                key={ex.id}
                role="tab"
                type="button"
                className={`tab-btn ${activeTab === ex.id ? "active" : ""}`}
                onClick={() => handleSelectTab(ex.id)}
              >
                {ex.title}
              </button>
            ))}
          </div>

          <div className="editor-wrapper">
            <textarea
              ref={textareaRef}
              className="code-editor"
              spellCheck="false"
              autoCapitalize="off"
              autoComplete="off"
              value={code}
              onChange={handleCodeChange}
              onKeyDown={handleKeyDown}
              aria-label="vml source code editor"
            />
          </div>

          <div className="action-bar">
            <div style={{ display: "flex", gap: "8px", alignItems: "center" }}>
              <button
                type="button"
                className="btn btn-primary"
                disabled={status === "loading" || status === "running"}
                onClick={handleRun}
              >
                ▶ RUN ({isMac ? "⌘↵" : "Ctrl+↵"})
              </button>
              <button
                type="button"
                className="btn btn-sm"
                onClick={handleReset}
                title="Restore default code for this example"
              >
                Reset
              </button>
            </div>
            <div className="status-pill">
              <span className={`status-dot ${status}`} />
              <span>{statusText}</span>
            </div>
          </div>
        </div>

        {/* Output Console Box */}
        <div className="box">
          <div className="term-head">
            <span>TERMINAL OUTPUT (stdout / stderr)</span>
            <span>{output.ms > 0 ? `${output.ms.toFixed(1)} ms` : ""}</span>
          </div>

          <div className="terminal-window">
            {output.stdout && <div>{output.stdout}</div>}
            {output.stderr && <div className="term-err">{output.stderr}</div>}
            {!output.stdout && !output.stderr && (
              <span className="term-dim">
                {status === "loading" ? "Initializing WebAssembly runtime..." : "# Output will appear here."}
              </span>
            )}
          </div>

          <div className="action-bar">
            <span style={{ fontFamily: "var(--font-mono)", fontSize: "0.82rem", color: "var(--muted)" }}>
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

      {/* Sandbox & Limits Fieldset */}
      <fieldset className="retro-box">
        <legend>[ Host Sandbox & Limits ]</legend>
        <p style={{ margin: "0 0 8px 0", fontSize: "0.88rem", color: "var(--muted)" }}>
          Simulates an embedding host (e.g. game engine or microservice) disabling specific standard modules or restricting CPU execution fuel.
        </p>

        <div className="sandbox-mods">
          {ALL_MODULES.map((m) => {
            const fullName = "std." + m;
            const isDenied = denied.includes(fullName);
            return (
              <label key={m} className={`mod-item ${isDenied ? "denied" : ""}`}>
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

        <div className="knobs-row">
          <label>
            Fuel Limit:{" "}
            <select className="retro-select" value={fuel} onChange={handleFuelChange}>
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
            GC Stress (collect at each safepoint)
          </label>

          <button type="button" className="btn btn-sm" onClick={allowAllModules}>
            Allow All Modules
          </button>
        </div>
      </fieldset>

      {/* Language Tour Cards */}
      <div id="tour" style={{ marginTop: "32px" }}>
        <h2 style={{ fontFamily: "var(--font-mono)", fontSize: "1.3rem", borderBottom: "2px solid var(--border)", paddingBottom: "6px" }}>
          [ Language Features ]
        </h2>
        <div className="section-grid">
          <div className="doc-card">
            <h3>Errors as Values</h3>
            <p>Functions that can fail state it explicitly in their return type: <code>T ! E</code>. Handle errors with <code>catch</code> or propagate with <code>?</code>.</p>
            <pre className="code-snippet"><code>{`func parse_int(s: string): int ! string {
    if #s == 0 { throw "empty string" }
    return "0123456789":find(s)?
}
let n = parse_int("42") catch 0`}</code></pre>
          </div>

          <div className="doc-card">
            <h3>Packages & Namespaces</h3>
            <p>Packages correspond cleanly to directories. Public functions and types are exported with <code>pub</code>, keeping internal implementation private.</p>
            <pre className="code-snippet"><code>{`## math/vector.vml
module math.vector
pub struct Vec2 { pub x: float, pub y: float }
pub func (v: Vec2) length(): float {
    return (v.x * v.x + v.y * v.y):sqrt()
}`}</code></pre>
          </div>

          <div className="doc-card">
            <h3>Single-File Executable Binaries</h3>
            <p><code>vml build</code> bundles the bytecode runtime and all compiled dependencies into one standalone binary with zero runtime prerequisites.</p>
            <pre className="code-snippet"><code>{`$ vml build
built dist/app
$ ./dist/app --verbose
ready.`}</code></pre>
          </div>

          <div className="doc-card">
            <h3>Fast C Embedding API</h3>
            <p>Expose native C functions, register host hooks, inject configurations, and enforce strict execution fuel limits for sandboxing untrusted scripts.</p>
            <pre className="code-snippet"><code>{`VML_State *L = vml_new(&opts);
vml_set_args(L, argc, argv);
vml_load_file(L, "script.vml");`}</code></pre>
          </div>
        </div>
      </div>

      {/* Getting Started */}
      <div id="start" style={{ marginTop: "24px" }}>
        <h2 style={{ fontFamily: "var(--font-mono)", fontSize: "1.3rem", borderBottom: "2px solid var(--border)", paddingBottom: "6px" }}>
          [ Quick Start ]
        </h2>
        <div className="doc-card">
          <p>Build from source with standard C compiler and tools on macOS and Linux:</p>
          <pre className="code-snippet"><code>{`# 1. Clone repository
git clone https://github.com/Moroseara/vml.git
cd vml

# 2. Build and run tests
make -j8 && make test

# 3. Create a new project
./vml new myproject
cd myproject
../vml run`}</code></pre>
        </div>
      </div>

      {/* Project Status */}
      <div id="status" style={{ marginTop: "24px" }}>
        <h2 style={{ fontFamily: "var(--font-mono)", fontSize: "1.3rem", borderBottom: "2px solid var(--border)", paddingBottom: "6px" }}>
          [ Project Status & Roadmap ]
        </h2>
        <table className="status-table">
          <thead>
            <tr>
              <th>Status</th>
              <th>Component / Capability</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td className="status-done">[DONE]</td>
              <td>Register-based bytecode VM with garbage collection and loop fuel sandboxing</td>
            </tr>
            <tr>
              <td className="status-done">[DONE]</td>
              <td>Compiler pipeline: lexer, parser, type checker, bytecode emitter</td>
            </tr>
            <tr>
              <td className="status-done">[DONE]</td>
              <td>Error handling: typed errors, <code>?</code> operator, <code>catch</code> blocks</td>
            </tr>
            <tr>
              <td className="status-done">[DONE]</td>
              <td>Standard library: string, array, map, math, json, fmt, bytes, fs, os</td>
            </tr>
            <tr>
              <td className="status-done">[DONE]</td>
              <td>WebAssembly build for in-browser playground execution</td>
            </tr>
            <tr>
              <td className="status-next">[NEXT]</td>
              <td>Coroutines and cooperatively yielding tasks</td>
            </tr>
            <tr>
              <td className="status-next">[NEXT]</td>
              <td>Direct C Foreign Function Interface (FFI) bindings</td>
            </tr>
            <tr>
              <td className="status-next">[NEXT]</td>
              <td>Language Server Protocol (LSP) and code formatter</td>
            </tr>
          </tbody>
        </table>
      </div>

      {/* Footer */}
      <footer className="retro-footer">
        <div>vml — bytecode language & register VM</div>
        <div>Built with React & WebAssembly</div>
      </footer>
    </div>
  );
}
