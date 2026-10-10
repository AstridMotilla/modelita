import Modelita from "./modelita.js";
import { EXAMPLES, FIRST_LINE, classify, tokenize, buildSteps, encode, decode } from "./lang.js";


// --- the engine --------------------------------------------------------------------------
const engineBadge = document.getElementById("engine");
let engine = null;
try {
  engine = await Modelita();
  engineBadge.textContent = "WebAssembly";
  engineBadge.dataset.state = "ready";
} catch (e) {
  engineBadge.textContent = "engine failed";
  engineBadge.dataset.state = "failed";
  showMessage(`<p>The Modelita engine couldn't start in this browser (${escapeHtml(String(e.message || e))}). ` +
              `It needs WebAssembly, which current versions of Firefox, Chrome, Safari and Edge all have.</p>`);
}
function run(text) {
  return JSON.parse(engine.ccall("modelita_run", "string", ["string"], [text]));
}


// --- state ---------------------------------------------------------------------------------
const textarea = document.getElementById("program");
const gutter = document.getElementById("gutter");
const focusBar = document.getElementById("focus-bar");
const notes = document.getElementById("notes");
const stepsEl = document.getElementById("steps");
const wordEl = document.getElementById("word");
const wordSub = document.getElementById("word-sub");
const countsEl = document.getElementById("counts");
const legendEl = document.getElementById("legend");
const messageEl = document.getElementById("message");
const tip = document.getElementById("tip");

const PREVIEW_SEED = "x[+z] C";
let state = {
  focusLine: 0,       // index of the line the view follows
  steps: [],          // [{token, result}] from the seed up
  stepIndex: -1,      // which step is shown
  mode: "role",
  highlight: null,    // (face index) => bool, from the legend
  shape: null,
};

// --- running the program --------------------------------------------------------------------
let lineResults = new Map();

function runAll() {
  if (!engine) return;
  const text = textarea.value;
  const lines = text.split("\n");
  lineResults = new Map(run(text).map(r => [r.line, r]));
  // gutter
  const rows = [];
  lines.forEach((line, i) => {
    const r = lineResults.get(i + 1), c = classify(line);
    let cls = "", mark = "";
    if (r && r.kind === "error") { cls = "error"; mark = "×"; }
    else if (r && r.kind === "shape") { cls = "shape"; mark = "●"; }
    else if (r && r.kind === "operator") { cls = "operator"; mark = "○"; }
    else if (c.kind === "define") { cls = "define"; mark = "="; }
    rows.push(`<div class="${cls}"><span>${i + 1}</span><span class="mark">${mark}</span></div>`);
  });
  gutter.innerHTML = rows.join("");
  syncScroll();
  // errors
  const errors = [...lineResults.values()].filter(r => r.kind === "error");
  notes.innerHTML = errors.map(r =>
    `<div class="note" data-line="${r.line}"><b>line ${r.line}</b><span>${escapeHtml(r.message)} <span class="cat">${r.category}</span></span></div>`).join("");
  updateFocus(true);
  saveToHash();
}

notes.addEventListener("click", e => {
  const n = e.target.closest(".note");
  if (n) goToLine(Number(n.dataset.line) - 1);
});

function goToLine(index) {
  const lines = textarea.value.split("\n");
  let pos = 0;
  for (let i = 0; i < index; i++) pos += lines[i].length + 1;
  textarea.focus();
  textarea.setSelectionRange(pos, pos + lines[index].length);
  updateFocus(false);
}

// --- the focused word, step by step -----------------------------------------------------------
function caretLine() {
  return textarea.value.slice(0, textarea.selectionStart).split("\n").length - 1;
}

function updateFocus(force) {
  if (!engine) return;
  const lines = textarea.value.split("\n");
  let index = Math.min(caretLine(), lines.length - 1);
  // On a blank line or a comment, keep showing the last word, or else the first one there is.
  const wordy = i => i >= 0 && i < lines.length && !["blank", "directive"].includes(classify(lines[i]).kind);
  if (!wordy(index)) {
    const first = lines.findIndex((_, i) => wordy(i));
    index = wordy(state.focusLine) ? state.focusLine : first >= 0 ? first : index;
  }
  moveFocusBar(index);
  if (!force && index === state.focusLine && state.steps.length) return;
  state.focusLine = index;
  const c = classify(lines[index] ?? "");
  if (c.kind === "blank" || c.kind === "directive") return showSteps(null, [], "Put the cursor on a word to see its shape.");
  if (c.kind === "define" && c.params.length)
    return showSteps(c, [], `${c.name} takes ${c.params.length === 1 ? "a number" : "numbers"}: use it in a word, like ${c.name}(${c.params.map(() => "1").join(", ")}) ${PREVIEW_SEED}`);

  // Everything above this line, without the words it shows, then each step of this word.
  const base = lines.slice(0, index).map(l => (classify(l).kind === "show" ? "" : l));
  const tokens = tokenize(c.word);
  let operator = false;
  let steps = buildSteps(run, base, tokens, "");
  if (steps.length && steps[steps.length - 1].result?.kind === "operator") {
    operator = true;
    steps = [{ token: PREVIEW_SEED, result: run(base.concat([PREVIEW_SEED]).join("\n")).find(r => r.line === base.length + 1) }]
      .concat(buildSteps(run, base, tokens, " " + PREVIEW_SEED));
  }
  showSteps(c, steps, operator ? `steps, shown on ${PREVIEW_SEED}` : c.kind === "define" ? `${c.name}, as defined` : "");
}


function showSteps(c, steps, note) {
  state.steps = steps;
  const firstError = steps.findIndex(s => s.result?.kind === "error");
  state.stepIndex = firstError >= 0 ? firstError : steps.length - 1;
  const fitShape = [...steps].reverse().find(s => s.result?.kind === "shape")?.result;
  wordEl.textContent = c ? (c.kind === "define" ? `${c.name} = ${c.word}` : c.word) : "";
  wordSub.textContent = note || "";
  renderChips();
  showStep(state.stepIndex, fitShape);
}

function renderChips() {
  // Written order, left to right; the seed is on the right and runs first.
  const n = state.steps.length;
  stepsEl.innerHTML = state.steps.map((s, k) => ({ s, k })).reverse().map(({ s, k }) => {
    const cls = ["chip", k > state.stepIndex ? "pending" : "", s.result?.kind === "error" ? "failed" : ""].join(" ");
    return `<button type="button" class="${cls}" data-k="${k}" aria-pressed="${k === state.stepIndex}" title="${k === 0 ? "the seed" : "after step " + k + " of " + (n - 1)}">${escapeHtml(s.token)}</button>`;
  }).join("");
}

stepsEl.addEventListener("click", e => {
  const b = e.target.closest(".chip");
  if (!b) return;
  state.stepIndex = Number(b.dataset.k);
  renderChips();
  showStep(state.stepIndex);
});

function showStep(k, fitShape) {
  const step = state.steps[k];
  const r = step?.result;
  hideMessage();
  if (!r) { setShape(null); countsEl.textContent = ""; legendEl.innerHTML = ""; return; }
  if (r.kind === "error") {
    setShape(null);
    countsEl.textContent = "";
    legendEl.innerHTML = "";
    showMessage(`<p><span class="err">${r.category} error</span><br>${escapeHtml(r.message)}</p>`);
    return;
  }
  setShape(r, fitShape);
  const V = r.vertices.length / 3, F = r.faces.length;
  const E = r.faces.reduce((n, f) => n + f.length, 0) / 2;
  const holes = (2 - (V - E + F)) / 2;
  countsEl.innerHTML = `V ${V} · E ${E} · F ${F}<br>${holes ? `${holes} hole${holes === 1 ? "" : "s"}` : "no holes"}` +
    (r.skipped ? `<br>${r.skipped} skipped` : "");
}

// --- colours (the blog's: one hue per role, HLS like Python's colorsys) ------------------------
const ROLE_HUES = { seed: 186, top: 28, side: 186, tent: 265, rim: 140, inner: 330, tube: 300,
                    vertex: 205, edge: 52, face: 5, merged: 75, flip: 100 };
function hls(h, l, s) {
  const m2 = l <= 0.5 ? l * (1 + s) : l + s - l * s, m1 = 2 * l - m2;
  const v = hue => {
    hue = hue - Math.floor(hue);
    if (hue < 1 / 6) return m1 + (m2 - m1) * hue * 6;
    if (hue < 0.5) return m2;
    if (hue < 2 / 3) return m1 + (m2 - m1) * (2 / 3 - hue) * 6;
    return m1;
  };
  return s === 0 ? [l, l, l] : [v(h / 360 + 1 / 3), v(h / 360), v(h / 360 - 1 / 3)];
}
const css = rgb => `rgb(${rgb.map(c => Math.round(c * 255)).join(" ")})`;

function faceColours(r) {
  // A colour per face, and the legend that explains them.
  const F = r.faces.length;
  const out = new Array(F);
  const legend = [];
  if (state.mode === "role") {
    const seen = new Map();
    r.roles.forEach((role, i) => {
      out[i] = hls(ROLE_HUES[role] ?? 186, 0.55, 0.7);
      if (!seen.has(role)) seen.set(role, out[i]);
    });
    for (const [role, c] of seen) legend.push({ name: role, colour: c, test: i => r.roles[i] === role });
  } else if (state.mode === "label") {
    const names = [...new Set(r.labels.flat())].sort();
    const hueOf = new Map(names.map((n, k) => [n, (28 + k * 137.5) % 360]));
    r.labels.forEach((ls, i) => {
      out[i] = ls.length ? hls(hueOf.get(ls[ls.length - 1]), 0.55, 0.7) : hls(0, 0.5, 0);
    });
    for (const n of names) legend.push({ name: n, colour: hls(hueOf.get(n), 0.55, 0.7), test: i => r.labels[i].includes(n) });
    legend.push({ name: names.length ? "no label" : "no labels yet: add {name} to a step", colour: hls(0, 0.5, 0), test: i => !r.labels[i].length });
  } else {
    const max = Math.max(1, ...r.steps);
    r.steps.forEach((s, i) => { out[i] = hls(210 - 182 * (s / max), 0.52, 0.65); });
    const steps = [...new Set(r.steps)].sort((a, b) => a - b);
    for (const s of steps) legend.push({ name: s === 0 ? "seed" : `step ${s}`, colour: hls(210 - 182 * (s / max), 0.52, 0.65), test: i => r.steps[i] === s });
  }
  return { colours: out, legend };
}

document.getElementById("colour-by").addEventListener("click", e => {
  const b = e.target.closest("button");
  if (!b) return;
  state.mode = b.dataset.mode;
  for (const x of e.currentTarget.querySelectorAll("button")) x.setAttribute("aria-pressed", String(x === b));
  state.highlight = null;
  recolour();
});

function renderLegend(legend) {
  legendEl.innerHTML = legend.map((l, k) => `<span data-k="${k}"><i></i>${escapeHtml(l.name)}</span>`).join("");
  legendEl.querySelectorAll("i").forEach((swatch, k) => { swatch.style.background = css(legend[k].colour); });
  legendEl.onmouseover = e => { const s = e.target.closest("span[data-k]"); if (s) { state.highlight = legend[Number(s.dataset.k)].test; recolour(true); } };
  legendEl.onmouseleave = () => { state.highlight = null; recolour(true); };
}

// --- WebGL ------------------------------------------------------------------------------------
const canvas = document.getElementById("view");
const wrap = document.getElementById("canvas-wrap");
const gl = canvas.getContext("webgl2", { antialias: true });
if (!gl) showMessage("<p>This browser can't draw WebGL 2, so the shape can't be shown.</p>");

function compile(vs, fs) {
  const p = gl.createProgram();
  for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    gl.attachShader(p, s);
  }
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  return p;
}

let faceProg, lineProg, mesh = null, pickTarget = null;
if (gl) {
  faceProg = compile(`#version 300 es
    in vec3 aPos; in vec3 aNormal; in vec3 aColour; in float aId;
    uniform mat4 uProj, uView;
    out vec3 vN; out vec3 vC; flat out float vId;
    void main() { gl_Position = uProj * uView * vec4(aPos, 1.0); vN = mat3(uView) * aNormal; vC = aColour; vId = aId; }`,
  `#version 300 es
    precision highp float;
    in vec3 vN; in vec3 vC; flat in float vId;
    uniform int uPick;
    out vec4 o;
    void main() {
      if (uPick == 1) { o = vec4(mod(vId, 256.0) / 255.0, mod(floor(vId / 256.0), 256.0) / 255.0, floor(vId / 65536.0) / 255.0, 1.0); return; }
      vec3 n = normalize(vN);
      if (!gl_FrontFacing) n = -n;
      float d = max(dot(n, normalize(vec3(-0.45, 0.55, 0.7))), 0.0);   // the blog's light, from the upper left
      o = vec4(vC * (0.3 + 0.7 * d), 1.0);
    }`);
  lineProg = compile(`#version 300 es
    in vec3 aPos; uniform mat4 uProj, uView;
    void main() { gl_Position = uProj * uView * vec4(aPos, 1.0); }`,
  `#version 300 es
    precision highp float; uniform vec3 uColour; out vec4 o;
    void main() { o = vec4(uColour, 1.0); }`);
}

const camera = { yaw: -0.95, pitch: 0.5, dist: 8, target: [0, 0, 0], radius: 2 };

function normalOf(vs, f) {
  let nx = 0, ny = 0, nz = 0;
  for (let k = 0; k < f.length; k++) {
    const p = f[k] * 3, q = f[(k + 1) % f.length] * 3;
    nx += (vs[p + 1] - vs[q + 1]) * (vs[p + 2] + vs[q + 2]);
    ny += (vs[p + 2] - vs[q + 2]) * (vs[p] + vs[q]);
    nz += (vs[p] - vs[q]) * (vs[p + 1] + vs[q + 1]);
  }
  const l = Math.hypot(nx, ny, nz) || 1;
  return [nx / l, ny / l, nz / l];
}

function setShape(r, fitShape) {
  state.shape = r;
  state.highlight = null;
  tip.hidden = true;
  if (!gl) return;
  if (mesh) { for (const b of mesh.buffers) gl.deleteBuffer(b); gl.deleteVertexArray(mesh.vao); gl.deleteVertexArray(mesh.lineVao); mesh = null; }
  if (!r) { draw(); return; }
  const vs = r.vertices;
  let tris = 0, edges = 0;
  for (const f of r.faces) { tris += f.length - 2; edges += f.length; }
  const pos = new Float32Array(tris * 9), nor = new Float32Array(tris * 9), ids = new Float32Array(tris * 3);
  const faceOfVertex = new Uint32Array(tris * 3);
  const lines = new Float32Array(edges * 6);
  let t = 0, e = 0;
  r.faces.forEach((f, i) => {
    const n = normalOf(vs, f);
    for (let k = 1; k + 1 < f.length; k++) {                      // a fan: fine for drawing
      for (const v of [f[0], f[k], f[k + 1]]) {
        pos.set([vs[3 * v], vs[3 * v + 1], vs[3 * v + 2]], t * 3);
        nor.set(n, t * 3);
        ids[t] = i + 1;
        faceOfVertex[t] = i;
        t++;
      }
    }
    for (let k = 0; k < f.length; k++) {
      const a = f[k], b = f[(k + 1) % f.length];
      lines.set([vs[3 * a], vs[3 * a + 1], vs[3 * a + 2], vs[3 * b], vs[3 * b + 1], vs[3 * b + 2]], e * 6);
      e++;
    }
  });
  const buffer = (data, loc, size, prog) => {
    const b = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, b);
    gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    const l = gl.getAttribLocation(prog, loc);
    gl.enableVertexAttribArray(l);
    gl.vertexAttribPointer(l, size, gl.FLOAT, false, 0, 0);
    return b;
  };
  const vao = gl.createVertexArray();
  gl.bindVertexArray(vao);
  const colourBuffer = gl.createBuffer();
  const buffers = [buffer(pos, "aPos", 3, faceProg), buffer(nor, "aNormal", 3, faceProg), buffer(ids, "aId", 1, faceProg), colourBuffer];
  gl.bindBuffer(gl.ARRAY_BUFFER, colourBuffer);
  const cl = gl.getAttribLocation(faceProg, "aColour");
  gl.enableVertexAttribArray(cl);
  gl.vertexAttribPointer(cl, 3, gl.FLOAT, false, 0, 0);
  const lineVao = gl.createVertexArray();
  gl.bindVertexArray(lineVao);
  buffers.push(buffer(lines, "aPos", 3, lineProg));
  gl.bindVertexArray(null);
  mesh = { vao, lineVao, buffers, colourBuffer, count: t, lineCount: e * 2, faceOfVertex };
  if (fitShape) fit(fitShape);
  recolour();
}

function recolour(keepLegend) {
  if (!state.shape) return;
  const { colours, legend } = faceColours(state.shape);
  if (!keepLegend) renderLegend(legend);
  if (!mesh) return;
  const data = new Float32Array(mesh.count * 3);
  const hl = state.highlight, dim = hls(186, 0.3, 0.06);
  for (let t = 0; t < mesh.count; t++) {
    const i = mesh.faceOfVertex[t];
    data.set(hl && !hl(i) ? dim : colours[i], t * 3);
  }
  gl.bindBuffer(gl.ARRAY_BUFFER, mesh.colourBuffer);
  gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
  draw();
}

function fit(r) {
  const vs = r.vertices;
  let lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < vs.length; i += 3) for (let a = 0; a < 3; a++) { lo[a] = Math.min(lo[a], vs[i + a]); hi[a] = Math.max(hi[a], vs[i + a]); }
  camera.target = lo.map((l, a) => (l + hi[a]) / 2);
  camera.radius = Math.max(1e-6, Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2);
  camera.dist = camera.radius / Math.sin((FOV / 2) * Math.PI / 180) * 1.12;
}

const FOV = 32;
function matrices() {
  const aspect = canvas.width / Math.max(1, canvas.height);
  const { yaw, pitch, dist, target } = camera;
  const eye = [target[0] + dist * Math.cos(pitch) * Math.cos(yaw), target[1] + dist * Math.cos(pitch) * Math.sin(yaw), target[2] + dist * Math.sin(pitch)];
  const f = norm(sub(target, eye)), s = norm(cross(f, [0, 0, 1])), u = cross(s, f);
  const view = new Float32Array([s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0,
    -dot(s, eye), -dot(u, eye), dot(f, eye), 1]);
  const near = Math.max(dist - camera.radius * 3, dist * 0.01), far = dist + camera.radius * 3;
  const fy = 1 / Math.tan((FOV / 2) * Math.PI / 180);
  // when the canvas is narrow, widen the view so the shape still fits
  const fx = aspect < 1 ? fy : fy / aspect, fyy = aspect < 1 ? fy * aspect : fy;
  const proj = new Float32Array([fx, 0, 0, 0, 0, fyy, 0, 0, 0, 0, (far + near) / (near - far), -1, 0, 0, (2 * far * near) / (near - far), 0]);
  return { view, proj };
}
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = a => { const l = Math.hypot(...a) || 1; return a.map(x => x / l); };

function themeColour(name) {
  const c = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  const m = c.match(/^#([0-9a-f]{6})$/i);
  return m ? [0, 2, 4].map(k => parseInt(m[1].slice(k, k + 2), 16) / 255) : [0.12, 0.13, 0.13];
}

let frame = 0;
function draw() {
  if (!gl || frame) return;
  frame = requestAnimationFrame(() => {
    frame = 0;
    const stage = themeColour("--stage"), edge = themeColour("--edge");
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(stage[0], stage[1], stage[2], 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    if (!mesh) return;
    drawMesh(false, edge);
  });
}

function drawMesh(pick, edge) {
  const { view, proj } = matrices();
  gl.enable(gl.DEPTH_TEST);
  gl.enable(gl.POLYGON_OFFSET_FILL);
  gl.polygonOffset(1, 1);
  gl.useProgram(faceProg);
  gl.uniformMatrix4fv(gl.getUniformLocation(faceProg, "uProj"), false, proj);
  gl.uniformMatrix4fv(gl.getUniformLocation(faceProg, "uView"), false, view);
  gl.uniform1i(gl.getUniformLocation(faceProg, "uPick"), pick ? 1 : 0);
  gl.bindVertexArray(mesh.vao);
  gl.drawArrays(gl.TRIANGLES, 0, mesh.count);
  if (!pick) {
    gl.useProgram(lineProg);
    gl.uniformMatrix4fv(gl.getUniformLocation(lineProg, "uProj"), false, proj);
    gl.uniformMatrix4fv(gl.getUniformLocation(lineProg, "uView"), false, view);
    gl.uniform3fv(gl.getUniformLocation(lineProg, "uColour"), edge);
    gl.bindVertexArray(mesh.lineVao);
    gl.drawArrays(gl.LINES, 0, mesh.lineCount);
  }
  gl.bindVertexArray(null);
}

function pickFace(x, y) {
  // Draw face numbers into an offscreen buffer and read the one under the pointer.
  if (!gl || !mesh) return -1;
  const w = canvas.width, h = canvas.height;
  if (!pickTarget || pickTarget.w !== w || pickTarget.h !== h) {
    if (pickTarget) { gl.deleteFramebuffer(pickTarget.fb); gl.deleteRenderbuffer(pickTarget.c); gl.deleteRenderbuffer(pickTarget.d); }
    const fb = gl.createFramebuffer(), c = gl.createRenderbuffer(), d = gl.createRenderbuffer();
    gl.bindRenderbuffer(gl.RENDERBUFFER, c); gl.renderbufferStorage(gl.RENDERBUFFER, gl.RGBA8, w, h);
    gl.bindRenderbuffer(gl.RENDERBUFFER, d); gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);
    gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, c);
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, d);
    pickTarget = { fb, c, d, w, h };
  }
  gl.bindFramebuffer(gl.FRAMEBUFFER, pickTarget.fb);
  gl.viewport(0, 0, w, h);
  gl.clearColor(0, 0, 0, 1);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  drawMesh(true);
  const px = new Uint8Array(4);
  gl.readPixels(Math.round(x), Math.round(h - y), 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  return px[0] + px[1] * 256 + px[2] * 65536 - 1;
}

new ResizeObserver(() => {
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = Math.max(1, Math.round(wrap.clientWidth * dpr));
  canvas.height = Math.max(1, Math.round(wrap.clientHeight * dpr));
  draw();
}).observe(wrap);
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", draw);
new MutationObserver(draw).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });

// --- moving the camera, and pointing at faces --------------------------------------------------
const pointers = new Map();
let pinch = 0;
wrap.addEventListener("pointerdown", e => { wrap.setPointerCapture(e.pointerId); pointers.set(e.pointerId, [e.clientX, e.clientY]); tip.hidden = true; });
wrap.addEventListener("pointerup", e => { pointers.delete(e.pointerId); pinch = 0; });
wrap.addEventListener("pointercancel", e => { pointers.delete(e.pointerId); pinch = 0; });
wrap.addEventListener("pointermove", e => {
  if (pointers.has(e.pointerId)) {
    const [px, py] = pointers.get(e.pointerId);
    pointers.set(e.pointerId, [e.clientX, e.clientY]);
    if (pointers.size === 1) {
      camera.yaw -= (e.clientX - px) * 0.008;
      camera.pitch = Math.max(-1.5, Math.min(1.5, camera.pitch + (e.clientY - py) * 0.008));
    } else if (pointers.size === 2) {
      const [a, b] = [...pointers.values()];
      const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
      if (pinch) camera.dist *= pinch / d;
      pinch = d;
    }
    draw();
    return;
  }
  hover(e);
});
wrap.addEventListener("pointerleave", () => { tip.hidden = true; });
wrap.addEventListener("wheel", e => { e.preventDefault(); camera.dist *= Math.exp(e.deltaY * 0.0012); draw(); }, { passive: false });
wrap.addEventListener("dblclick", () => { if (state.shape) { fit(state.shape); draw(); } });

let hoverQueued = null;
function hover(e) {
  if (!state.shape || e.pointerType === "touch") return;
  if (hoverQueued) { hoverQueued = e; return; }
  hoverQueued = e;
  requestAnimationFrame(() => {
    const ev = hoverQueued;
    hoverQueued = null;
    const rect = canvas.getBoundingClientRect();
    const dpr = canvas.width / rect.width;
    const i = pickFace((ev.clientX - rect.left) * dpr, (ev.clientY - rect.top) * dpr);
    const r = state.shape;
    if (i < 0 || !r || i >= r.faces.length) { tip.hidden = true; return; }
    tip.innerHTML = `face #${i} · ${r.roles[i]} · ${r.steps[i] ? "step " + r.steps[i] : "seed"}` +
      (r.labels[i].length ? ` · {${r.labels[i].join(", ")}}` : "") + `<br>${r.faces[i].length} corners`;
    tip.style.left = (ev.clientX - wrap.getBoundingClientRect().left) + "px";
    tip.style.top = (ev.clientY - wrap.getBoundingClientRect().top) + "px";
    tip.hidden = false;
  });
}

// --- the editor ------------------------------------------------------------------------------
function syncScroll() {
  gutter.scrollTop = textarea.scrollTop;
  moveFocusBar(state.focusLine);
}
function moveFocusBar(index) {
  const lh = parseFloat(getComputedStyle(textarea).lineHeight);
  focusBar.style.transform = `translateY(${10 + index * lh - textarea.scrollTop}px)`;
}
textarea.addEventListener("scroll", syncScroll);
let timer = 0;
textarea.addEventListener("input", () => { clearTimeout(timer); timer = setTimeout(runAll, 160); });
for (const ev of ["click", "keyup", "focus"]) textarea.addEventListener(ev, () => updateFocus(false));
textarea.addEventListener("keydown", e => {
  if (e.key === "Tab" && !e.shiftKey) { e.preventDefault(); document.execCommand?.("insertText", false, "  ") || textarea.setRangeText("  ", textarea.selectionStart, textarea.selectionEnd, "end"); }
});

// --- examples, links and OBJ ------------------------------------------------------------------
const examples = document.getElementById("examples");
examples.innerHTML = `<option value="" disabled>Examples</option>` + Object.keys(EXAMPLES).map(k => `<option>${k}</option>`).join("");
examples.addEventListener("change", () => loadProgram(EXAMPLES[examples.value], FIRST_LINE[examples.value] ?? 0));

function loadProgram(text, line) {
  textarea.value = text;
  state.focusLine = -1;
  runAll();
  goToLine(Math.min(line, text.split("\n").length - 1));
  textarea.scrollTop = 0;
  syncScroll();
}

let hashTimer = 0;
function saveToHash() {
  clearTimeout(hashTimer);
  hashTimer = setTimeout(async () => {
    try { history.replaceState(null, "", "#" + await encode(textarea.value)); } catch {}
    try { localStorage.setItem("modelita-program", textarea.value); } catch {}
  }, 400);
}

document.getElementById("share").addEventListener("click", async () => {
  const url = location.href.split("#")[0] + "#" + await encode(textarea.value);
  try { history.replaceState(null, "", url); } catch {}
  try { await navigator.clipboard.writeText(url); toast("Link copied: it holds the whole program"); }
  catch { toast("Copy the address from the address bar: it holds the whole program"); }
});

function shapeToObj(r) {
  // OBJ is read with y up: (x y z) is written as (x z -y). One group per role.
  const lines = [`# ${wordEl.textContent} — made with Modelita`];
  for (let i = 0; i < r.vertices.length; i += 3)
    lines.push(`v ${+r.vertices[i].toFixed(6)} ${+r.vertices[i + 2].toFixed(6)} ${+(-r.vertices[i + 1]).toFixed(6)}`);
  let group = null;
  r.faces.forEach((f, i) => {
    if (r.roles[i] !== group) lines.push(`g ${group = r.roles[i]}`);
    lines.push("f " + f.map(v => v + 1).join(" "));
  });
  return lines.join("\n") + "\n";
}

document.getElementById("copy-obj").addEventListener("click", async () => {
  const r = state.shape;
  if (!r) return toast("There's no shape on this line to copy");
  try { await navigator.clipboard.writeText(shapeToObj(r)); toast(`Copied ${r.faces.length} faces as OBJ`); }
  catch { toast("This browser didn't allow copying; use Download OBJ"); }
});

document.getElementById("download-obj").addEventListener("click", () => {
  const r = state.shape;
  if (!r) return toast("There's no shape on this line to download");
  // named after the line's name, if it has one
  const name = (classify(textarea.value.split("\n")[state.focusLine] ?? "").name || "modelita").replace(/[^A-Za-z0-9_-]/g, "");
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([shapeToObj(r)], { type: "model/obj" }));
  a.download = `${name || "modelita"}.obj`;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
});

let toastTimer = 0;
function toast(text) {
  const t = document.getElementById("toast");
  t.textContent = text;
  t.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.hidden = true; }, 2600);
}

function showMessage(html) { messageEl.innerHTML = html; messageEl.hidden = false; }
function hideMessage() { messageEl.hidden = true; }
function escapeHtml(s) { return String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]); }

// --- start ---------------------------------------------------------------------------------------
if (matchMedia("(max-width: 760px)").matches) document.getElementById("reference").open = false;
let start = null;
if (location.hash.length > 2) { try { start = await decode(location.hash.slice(1)); } catch {} }
if (start === null) { try { start = localStorage.getItem("modelita-program"); } catch {} }
if (engine) {
  if (start) loadProgram(start, 0); else loadProgram(EXAMPLES.Tower, FIRST_LINE.Tower);
  examples.value = "";
}
