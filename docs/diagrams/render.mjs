// Export every docs/diagrams/*.excalidraw to SVG with Excalidraw's own exportToSvg,
// in headless Chromium. The hand-drawn font is embedded so the SVGs render on GitHub.
// Run through render_svg.sh (Docker), not directly.
import { chromium } from "playwright";
import fs from "node:fs";
import path from "node:path";

const DIR = process.argv[2] || "/diagrams";
// Excalidraw appends /dist/excalidraw-assets/ to the asset path itself
const EXCALIDRAW = "https://unpkg.com/@excalidraw/excalidraw@0.17.6";
const PAGE = `<!doctype html><html><head><meta charset="utf-8">
<script>window.EXCALIDRAW_ASSET_PATH = "${EXCALIDRAW}";</script>
<script src="https://unpkg.com/react@18.2.0/umd/react.production.min.js"></script>
<script src="https://unpkg.com/react-dom@18.2.0/umd/react-dom.production.min.js"></script>
<script src="${EXCALIDRAW}/dist/excalidraw.production.min.js"></script>
</head><body></body></html>`;

const fontCache = new Map();
async function inlineFonts(svg) {
    // Keep only the Virgil font face (the only font the diagrams use) and embed it
    svg = svg.replace(/@font-face\s*{[^}]*}/g, (face) => (/Virgil/.test(face) ? face : ""));
    const urls = [...svg.matchAll(/url\("?([^")]+)"?\)/g)].map((m) => m[1]).filter((u) => u.startsWith("http"));
    for (const url of new Set(urls)) {
        if (!fontCache.has(url)) {
            const res = await fetch(url);
            if (!res.ok) throw new Error(`font ${url}: ${res.status}`);
            fontCache.set(url, Buffer.from(await res.arrayBuffer()).toString("base64"));
        }
        svg = svg.split(url).join(`data:font/woff2;base64,${fontCache.get(url)}`);
    }
    return svg;
}

const browser = await chromium.launch();
const page = await browser.newPage();
await page.setContent(PAGE, { waitUntil: "networkidle" });

const files = fs.readdirSync(DIR).filter((f) => f.endsWith(".excalidraw")).sort();
for (const file of files) {
    const scene = JSON.parse(fs.readFileSync(path.join(DIR, file), "utf8"));
    const svg = await page.evaluate(async (scene) => {
        const el = await window.ExcalidrawLib.exportToSvg({
            elements: scene.elements,
            appState: { ...scene.appState, exportBackground: true, exportWithDarkMode: false, exportPadding: 20 },
            files: scene.files || {},
        });
        return el.outerHTML;
    }, scene);
    const out = path.join(DIR, file.replace(/\.excalidraw$/, ".svg"));
    fs.writeFileSync(out, (await inlineFonts(svg)) + "\n");
    console.log(`${out}: ${fs.statSync(out).size} bytes`);
}
await browser.close();
