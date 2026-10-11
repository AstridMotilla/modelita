// node web/og.mjs > web/og.svg: the link preview's source. The tower is built
// by the engine and drawn the way figures.js draws the site's figures: flat,
// outlined, coloured by role in Byrne's red, yellow and blue. `make og` turns
// the SVG into site/og-<fingerprint>.png (needs Inkscape, Archivo and DM Mono).
import Modelita from "./site/modelita.js";

const W = 1200, H = 630;
const PAPER = "#fdfdfb", INK = "#1a1a19", GREY = "#737370";
const RED = "#d63c2a", YELLOW = "#f2b927", BLUE = "#1f57a4", SEED = "#fbfaf5";
const ROLE = { seed: SEED, top: YELLOW, rim: YELLOW, edge: YELLOW, merged: YELLOW, side: BLUE, tube: BLUE, face: BLUE,
               tent: RED, inner: RED, vertex: RED, flip: RED };

const engine = await Modelita();
const program = "storey(h) = x[inner](h) i[top](0.3)\nroof = k[top](0.8)\nroof storey(1.2)^3 x[+z]{tower} C";
const r = JSON.parse(engine.ccall("modelita_run", "string", ["string"], [program])).at(-1);

// --- the tower, as figures.js draws it ---------------------------------------------------------
const P = [];
for (let i = 0; i < r.vertices.length; i += 3) P.push([r.vertices[i], r.vertices[i + 1], r.vertices[i + 2]]);
const lo = [0, 1, 2].map(k => Math.min(...P.map(p => p[k]))), hi = [0, 1, 2].map(k => Math.max(...P.map(p => p[k])));
const c = lo.map((l, k) => (l + hi[k]) / 2);
const radius = Math.max(...P.map(p => Math.hypot(p[0] - c[0], p[1] - c[1], p[2] - c[2])));
const Q = P.map(p => p.map((x, k) => (x - c[k]) / radius));
const normal = f => {
  let nx = 0, ny = 0, nz = 0;
  for (let k = 0; k < f.length; k++) {
    const a = Q[f[k]], b = Q[f[(k + 1) % f.length]];
    nx += (a[1] - b[1]) * (a[2] + b[2]); ny += (a[2] - b[2]) * (a[0] + b[0]); nz += (a[0] - b[0]) * (a[1] + b[1]);
  }
  const l = Math.hypot(nx, ny, nz) || 1;
  return [nx / l, ny / l, nz / l];
};
const yaw = -0.8, pitch = 0.42;
const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
const view = p => { const x = p[0] * cy - p[1] * sy, y = p[0] * sy + p[1] * cy; return [x, p[2] * cp + y * sp, p[2] * sp - y * cp]; };
const T = Q.map(view);
const LIGHT = (() => { const l = [-0.5, 0.45, 0.75], n = Math.hypot(...l); return l.map(x => x / n); })();
const scale = 262, ox = 880, oy = 316;
const shade = (hex, k) => "#" + [1, 3, 5].map(i => Math.round(parseInt(hex.slice(i, i + 2), 16) * k).toString(16).padStart(2, "0")).join("");
const polys = r.faces.map((f, i) => [f.reduce((d, v) => d + T[v][2], 0) / f.length, i])
  .sort((a, b) => a[0] - b[0])
  .map(([, i]) => {
    const n = view(normal(r.faces[i]));
    if (n[2] < -1e-3) return "";
    const light = Math.max(0, n[0] * LIGHT[0] + n[1] * LIGHT[1] + n[2] * LIGHT[2]);
    const pts = r.faces[i].map(v => `${(ox + T[v][0] * scale).toFixed(1)},${(oy - T[v][1] * scale).toFixed(1)}`).join(" ");
    return `<polygon points="${pts}" fill="${shade(ROLE[r.roles[i]] ?? SEED, 0.86 + 0.14 * light)}"/>`;
  }).join("\n");

// --- the words ------------------------------------------------------------------------------------
// DM Mono's characters are all 0.6 em wide, so the underlines can be placed by counting.
const size = 27, em = 0.6 * size, x0 = 96, yWord = 468;
const tokens = [["roof", RED], ["storey(1.2)^3", BLUE], ["x[+z]{tower}", YELLOW], ["C", INK]];
let x = x0;
const word = tokens.map(([t, colour]) => {
  const w = t.length * em, out = `<text x="${x}" y="${yWord}">${t}</text><rect x="${x}" y="${yWord + 12}" width="${w}" height="4" fill="${colour}"/>`;
  x += w + em;
  return out;
}).join("\n");

console.log(`<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">
<!-- The link preview, made by web/og.mjs: the tower is engine output, drawn
     like the site's figures. Text is Archivo and DM Mono. -->
<rect width="${W}" height="${H}" fill="${PAPER}"/>
<g stroke="${INK}" stroke-width="2" stroke-linejoin="round">
${polys}
</g>
<text x="${x0 - 4}" y="262" fill="${INK}" font-family="Archivo" font-size="112" font-weight="300" style="font-variation-settings:'wdth' 120, 'wght' 300" letter-spacing="2">Modelita</text>
<text x="${x0}" y="322" fill="${GREY}" font-family="Archivo" font-style="italic" font-size="34" font-weight="380" style="font-variation-settings:'wdth' 100, 'wght' 380">a little language for shapes</text>
<g fill="${INK}" font-family="DM Mono" font-size="${size}">
${word}
</g>
</svg>`);
