// The figures on the site's pages, drawn by the engine itself: every
// <canvas data-word="..."> runs its program and draws the last shape, flat
// and outlined like a plate in Byrne's Euclid. And every <a data-play="...">
// becomes a link that opens its program in the playground.
//
//   data-word    the program (lines joined by &#10;); its last shape is drawn
//   data-focus   "last": colour only what the last step made, the rest white
//   data-spin    turn slowly on its own
//   data-still   don't turn when dragged
//   data-yaw, data-pitch   the starting view, in radians
import Modelita from "/modelita.js";
import { encode } from "/lang.js";

const engine = await Modelita();
const run = text => JSON.parse(engine.ccall("modelita_run", "string", ["string"], [text]));

for (const a of document.querySelectorAll("a[data-play]")) a.href = "/play/#" + await encode(a.dataset.play);

// --- colours -------------------------------------------------------------------------------
const ROLES = ["seed", "top", "side", "inner", "rim", "tent", "tube", "vertex", "edge", "face", "merged", "flip", "reshaped"];
let colours = {}, edgeColour = "#000";
function readColours() {
  const style = getComputedStyle(document.documentElement);
  for (const r of ROLES) colours[r] = rgb(style.getPropertyValue("--r-" + r).trim());
  edgeColour = style.getPropertyValue("--edge").trim();
}
function rgb(s) {
  const m = s.match(/^#([0-9a-f]{6})$/i);
  return m ? [0, 2, 4].map(k => parseInt(m[1].slice(k, k + 2), 16)) : [200, 200, 200];
}

// --- shapes --------------------------------------------------------------------------------
function prepare(r, focus) {
  // Centred on the origin and scaled to fit a unit sphere, with a normal and a colour key per face.
  const P = [];
  for (let i = 0; i < r.vertices.length; i += 3) P.push([r.vertices[i], r.vertices[i + 1], r.vertices[i + 2]]);
  const lo = [0, 1, 2].map(k => Math.min(...P.map(p => p[k]))), hi = [0, 1, 2].map(k => Math.max(...P.map(p => p[k])));
  const c = lo.map((l, k) => (l + hi[k]) / 2);
  const radius = Math.max(...P.map(p => Math.hypot(p[0] - c[0], p[1] - c[1], p[2] - c[2]))) || 1;
  const Q = P.map(p => p.map((x, k) => (x - c[k]) / radius));
  const last = Math.max(...r.steps);
  const keys = r.faces.map((_, i) => {
    if (focus !== "last" || r.steps[i] === last) return r.roles[i] === "seed" && focus === "last" && last > 0 ? "reshaped" : r.roles[i];
    return "seed";
  });
  const normals = r.faces.map(f => {
    let nx = 0, ny = 0, nz = 0;
    for (let k = 0; k < f.length; k++) {
      const a = Q[f[k]], b = Q[f[(k + 1) % f.length]];
      nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1]);
    }
    const l = Math.hypot(nx, ny, nz) || 1;
    return [nx / l, ny / l, nz / l];
  });
  return { points: Q, faces: r.faces, keys, normals };
}

// --- drawing -------------------------------------------------------------------------------
const LIGHT = (() => { const l = [-0.5, 0.45, 0.75], n = Math.hypot(...l); return l.map(x => x / n); })();

function draw(fig) {
  const { canvas, shape } = fig;
  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (!w || !h) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const g = canvas.getContext("2d");
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, w, h);
  if (!shape) return;
  // z up. Turn about z by yaw, then tip the top towards the viewer by pitch.
  // View space: x right, y up, z towards the viewer.
  const cy = Math.cos(fig.yaw), sy = Math.sin(fig.yaw), cp = Math.cos(fig.pitch), sp = Math.sin(fig.pitch);
  const view = p => {
    const x = p[0] * cy - p[1] * sy, y = p[0] * sy + p[1] * cy;
    return [x, p[2] * cp + y * sp, p[2] * sp - y * cp];
  };
  const T = shape.points.map(view);
  const s = Math.min(w, h) * 0.5 * fig.scale, ox = w / 2, oy = h / 2;
  // Painter's order: far faces first. Faces turned away are skipped.
  const order = shape.faces.map((f, i) => [f.reduce((d, v) => d + T[v][2], 0) / f.length, i]).sort((a, b) => a[0] - b[0]);
  g.lineJoin = "round";
  g.lineWidth = Math.max(0.8, s / 190);
  g.strokeStyle = edgeColour;
  for (const [, i] of order) {
    const n = view(shape.normals[i]);
    if (n[2] < -1e-3) continue;
    const light = Math.max(0, n[0] * LIGHT[0] + n[1] * LIGHT[1] + n[2] * LIGHT[2]);
    const k = 0.86 + 0.14 * light;                                // nearly flat, like Byrne's plates
    g.fillStyle = `rgb(${(colours[shape.keys[i]] ?? colours.seed).map(c => Math.round(c * k)).join(" ")})`;
    g.beginPath();
    shape.faces[i].forEach((v, j) => { const x = ox + T[v][0] * s, y = oy - T[v][1] * s; j ? g.lineTo(x, y) : g.moveTo(x, y); });
    g.closePath();
    g.fill();
    g.stroke();
  }
}

// --- the figures on this page ----------------------------------------------------------------
readColours();
const figs = [...document.querySelectorAll("canvas[data-word]")].map(canvas => {
  const d = canvas.dataset;
  const result = run(d.word).filter(r => r.kind === "shape").pop();
  if (!result) console.error("figure has no shape:", d.word, run(d.word));
  const fig = {
    canvas, shape: result ? prepare(result, d.focus) : null,
    yaw: d.yaw ? Number(d.yaw) : -0.75, pitch: d.pitch ? Number(d.pitch) : 0.5,
    scale: d.scale ? Number(d.scale) : 0.8, spin: "spin" in d, held: false,
  };
  if (!("still" in d)) {
    let last = null;
    canvas.addEventListener("pointerdown", e => { last = [e.clientX, e.clientY]; fig.held = true; canvas.setPointerCapture(e.pointerId); });
    canvas.addEventListener("pointermove", e => {
      if (!last) return;
      fig.yaw += (e.clientX - last[0]) * 0.01;
      fig.pitch = Math.max(-1.3, Math.min(1.3, fig.pitch + (e.clientY - last[1]) * 0.01));
      last = [e.clientX, e.clientY];
      draw(fig);
    });
    const up = () => { last = null; fig.held = false; };
    canvas.addEventListener("pointerup", up);
    canvas.addEventListener("pointercancel", up);
  }
  return fig;
});

const drawAll = () => figs.forEach(draw);
new ResizeObserver(drawAll).observe(document.body);
const recolour = () => { readColours(); drawAll(); };
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", recolour);
new MutationObserver(recolour).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
drawAll();

if (figs.some(f => f.spin) && !matchMedia("(prefers-reduced-motion: reduce)").matches) {
  let before = performance.now();
  const tick = now => {
    const dt = Math.min(0.05, (now - before) / 1000);
    before = now;
    for (const f of figs) if (f.spin && !f.held) { f.yaw += 0.08 * dt; draw(f); }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}
