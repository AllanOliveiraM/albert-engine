#!/usr/bin/env python3
"""Summarize visual_test media-captures (Pillow + numpy, analysis only)."""
import csv
import statistics
from collections import defaultdict
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1] / 'docs/media-gpu'
data = {}
for version in ('before', 'after'):
    passes = defaultdict(list)
    with (root / version / 'passes.csv').open() as source:
        for row in csv.DictReader(source):
            passes[row['capture'], row['pass']].append(float(row['ms']))
    data[version] = {key: statistics.median(values) for key, values in passes.items()}
    for path in (root / version).glob('*.ppm'):
        Image.open(path).save(path.with_suffix('.png'))

names = list(dict.fromkeys(name for name, _ in data['before']))
labels = {'gas-0': 'bloom / vacuum envelope', 'gas-1': 'burn / collar', 'gas-2': 'mist', 'gas-3': 'hot-stage jets', 'scene': 'clear / resolve'}
rows = ['# Local media GPU comparison', '',
        '1920×1080, quality 1, 4× MSAA. Adapter: **llvmpipe (LLVM 21.1.8, 256 bits), software Vulkan**. These are GPU timestamp durations on the software adapter, not MX350 measurements or a thermal test. No working NVIDIA driver is available here.', '',
        'Frozen renderer telemetry uses the example stack, booster and ship appearances. The shore eye is at (750, 1.7, 661.44) metres; the orbit eye is near the stack. Boostback includes both large envelopes. Scenes use fixed lighting and a flat ground fixture, without the full live mission terrain, atmospheric tables or launch tower. Frames are identical between runs; no dynamics or guidance advance during capture.', '',
        'Each reported number is the median of 8 synchronized frames after 3 warmup frames. Runs were sequential. Repeated passes of one category are summed per frame before taking the median. The native baseline was captured before the final empty-lighting and face-culling optimizations. `ALBERT_MEDIA_NATIVE=1` remains a full-resolution visual reference; its current timings include the later shader algebra changes.', '',
        '[Interactive capture comparison](index.html) · [Raw before timings](before/passes.csv) · [Raw after timings](after/passes.csv)', '']
summary = []
comparisons = []
with (root / 'summary.csv').open('w') as output:
    writer = csv.writer(output)
    writer.writerow(('capture', 'pass', 'before_ms', 'after_ms'))
    for name in names:
        a = np.asarray(Image.open(root / 'before' / f'{name}.png')).astype(float)
        b = np.asarray(Image.open(root / 'after' / f'{name}.png')).astype(float)
        difference = np.abs(a - b)
        mae = difference.mean()
        p99 = np.percentile(difference, 99)
        over = 100 * (difference.max(2) > 10).mean()
        comparisons.append((name, mae, p99, over))
        rows.extend((f'## {name}', '', f'[Before](before/{name}.png) · [After](after/{name}.png). RGB mean absolute difference: {mae:.4f}/255; channel p99: {p99:.0f}; pixels with any channel differing by >10: {over:.3f}%.', '', '| Pass | Before ms | After ms |', '|---|---:|---:|'))
        keys = sorted({key for version in data.values() for capture, key in version if capture == name})
        for key in keys:
            before = data['before'].get((name, key), 0)
            after = data['after'].get((name, key), 0)
            rows.append(f'| {labels.get(key, key)} | {before:.3f} | {after:.3f} |')
            writer.writerow((name, key, f'{before:.6f}', f'{after:.6f}'))
        media = {'gas-0', 'gas-1', 'gas-2', 'gas-3', 'launch-cloud', 'engine-cloud', 'splashdown', 'media-depth', 'media-composite'}
        old = sum(data['before'].get((name, key), 0) for key in media)
        new = sum(data['after'].get((name, key), 0) for key in media)
        rows.extend(('', f'Sum of pass medians for local media, including new depth and composite passes: **{old:.3f} → {new:.3f} ms** ({old/new:.2f}×).', ''))
        summary.append((name, old, new, mae, p99, over))

rows.extend(('## Validation', '',
'The requested build completed successfully ([build log](build.log)). `ctest -R visual --output-on-failure` passed `visual_controls`; `visual_webgpu` failed at the unchanged pending assertion, “Vent became invisible or an opaque white ball” ([CTest log](ctest-visual.log)). The complete `webgpu-keep-going` run reached the end with exactly that one failed assertion ([all checks](all-checks.log)). No assertion was removed or relaxed.', '',
'The reduced-media checks passed with 1×/4× MSAA, outside/inside views, deterministic repeated frames, hidden effects, steam, and analytic mist. Cloud RGB MAE: 0.0367 outside / 0.1465 inside; steam MAE: 0.01143; mist intensity ratio: 1.01224 ([media checks](media-check.log)). The capture runs reported zero WebGPU validation errors. Captures were also inspected visually.', '',
'Worktree: `/unreal/allan/albert-codex`, branch `codex/visual`. No commit was made. `examples/starship.hpp`, `examples/ship_return.hpp`, `examples/descent.hpp`, `examples/countdown.hpp` and `src/physics` are unchanged.', '',
'## Changes by file', '',
'- `src/gpu/local_media.hpp`: persistent RGBA16F scratch targets, nearest-depth MSAA snapshot, reduced depth test, four-tap depth-aware upsample, premultiplied composite. Each target is allocated on demand and reused; source depth bindings are cached.',
'- `src/gpu/scene.cpp`: projected footprint and inside-camera detection; half resolution for broad shells and quarter resolution above 65% of screen at 1080p; native resolution below 64 pixels or on targets below 480 rows; independent half/quarter layers; flush at transparent sort boundaries; composite scissor; one-sided shell rasterization; offscreen shell culling. Detailed timing splits are enabled only for profiling.',
'- `src/gpu/scene.hpp`: native-reference switch and last-completed per-pass timing accessor.',
'- `src/gpu/pass_timing.hpp`: optional timestamp-query instrumentation, asynchronous readback and per-pass averages printed every two seconds with `ALBERT_GPU_TIMING=1`; explicit unsupported-feature message.',
'- `src/render/return_gas_shader.hpp`: projected-size sample budget with six-step minimum at quality 1; negligible-transmission exit only when no unattenuated ignition remains; exact empty axial support skip; defer atmospheric lighting for empty bloom/mist rays and remove unused hot-stage lighting.',
'- `src/render/launch_cloud_shader.hpp`: bounded step budget (20→19 for an inside quality-1 bank; exterior rays keep 20), exact empty half-space skip. Existing optical-depth termination retained. Splashdown inherits this integration.',
'- `src/render/splashdown_shader.hpp`: exact empty lower half-space skip for steam.',
'- `tests/media_captures.hpp`: repeatable five-view 1080p benchmark with explicit poses/flow/pressure and per-frame CSV.',
'- `tests/media_visuals.hpp`: native/reduced comparisons for outside/inside cameras, MSAA 1/4, steam, repeatability and invisible effects.',
'- `tests/visual_test.cpp`: capture/check entry points and an optional `webgpu-keep-going` mode that reports failed assertions and still exits nonzero. The vent assertion is unchanged.',
'- `scripts/media_gpu_report.py`: converts captures and generates the timing/image comparison artifacts.', '',
'## Reproduction', '',
'From `build/linux`:', '', '```sh',
'/usr/bin/cmake --build . --target AlbertEngine albert_flight_client visual_test -j2',
'ALBERT_GPU_TIMING=1 ALBERT_MEDIA_NATIVE=1 ./tests/visual_test media-captures ../../docs/media-gpu/reference',
'ALBERT_GPU_TIMING=1 ./tests/visual_test media-captures ../../docs/media-gpu/optimized',
'ctest -R visual --output-on-failure',
'ALBERT_GPU=software ./tests/visual_test webgpu-keep-going', '```', '',
'Do not run the two benchmarks concurrently. Omit `ALBERT_GPU=software` to use the hardware adapter. `ALBERT_BENCH_FRAMES` changes the measurement count (default 8). The native-reference environment variable disables reduced targets, adaptive counts, culling and opacity termination for comparison. On a live client, `ALBERT_GPU_TIMING=1` prints categories while the mission runs.', '',
'## Limits', '',
'Quality-1 vacuum envelopes retain six integration steps: reducing them to five produced a visible change and was rejected. High-quality gas can reduce twelve steps to ten/eleven. Quality-1 exterior cloud and steam rays retain their sample positions because changing their phase grid moved visible lobes. Interior rays reduce conservatively by one; higher-quality rays can use thirty of thirty-two steps. Exact empty support is skipped without shifting any retained sample. Thin analytic jets and tiny effects stay native when their footprint is small.', '',
'Unattenuated emission cannot terminate on opacity alone without changing the image. It retains the complete bounded integration. Geometry, physical telemetry, guidance and mission examples were not changed.', '',
'The pass count and depth/composite cost matter: reduced shading does not promise a fourfold reduction of the entire frame. Profiling introduces render-pass boundaries for category timings. Software adapter timings include large raster/resolve overheads and cannot predict MX350 speed or prove that thermal shutdowns are eliminated. Live MX350 timings, a thermal soak, full mission scenery and motion comparisons remain to be done on hardware.', ''))
(root / 'README.md').write_text('\n'.join(rows))

html = ['<!doctype html><meta charset="utf-8"><title>Albert local media comparison</title><style>body{background:#111820;color:#e2e8ef;font:16px system-ui;margin:2rem auto;max-width:1300px}a{color:#9dd2ff}section{margin:3rem 0}.pair{position:relative;aspect-ratio:16/9;width:100%}.pair img{position:absolute;width:100%;height:100%}.after{clip-path:inset(0 0 0 50%)}input{width:100%}small{color:#aec1d0}</style><h1>Albert local media</h1><p>1920×1080 · quality 1 · software llvmpipe. <a href="README.md">Timings, changes and limitations</a></p><p>Left: before. Right: after. Drag the divider or use the image links for full-size inspection.</p>']
for name, mae, p99, over in comparisons:
    html.append(f'<section><h2>{name}</h2><small>RGB MAE {mae:.4f}/255 · p99 {p99:.0f} · pixels &gt;10: {over:.3f}%</small><p><a href="before/{name}.png">Before</a> · <a href="after/{name}.png">After</a></p><div class="pair"><img src="before/{name}.png"><img class="after" src="after/{name}.png"></div><input aria-label="Before and after divider" type="range" min="0" max="100" value="50" oninput="this.previousElementSibling.lastElementChild.style.clipPath=`inset(0 0 0 ${{this.value}}%)`"></section>')
(root / 'index.html').write_text('\n'.join(html))
for row in summary:
    print(row)
