// OpenScanner web UI — minimal, dependency-free WebGL2 point-cloud viewer.
// No framework, no build step, no animation beyond the point cloud data
// itself: this exists to prove and measure the render/latency pipeline,
// not to look polished yet.

(function () {
  "use strict";

  const params = new URLSearchParams(window.location.search);
  const port = params.get("port") || "8787";
  const host = params.get("host") || window.location.hostname || "127.0.0.1";
  const wsUrl = "ws://" + host + ":" + port + "/";

  const hud = document.getElementById("hud");
  let canvas = document.getElementById("gl");
  const backendParam = params.get("backend"); // "webgpu" | "webgl2" | null (auto)
  let renderer = null;

  // Renderer selection: WebGPU when available (and not forced off), else the
  // WebGL2 fallback. Both implement the same interface (see ui/render/).
  // Every path fails softly: a rejected adapter/device or a pipeline
  // validation error falls through to WebGL2, and a canvas claimed by
  // WebGPU is replaced so the fallback can bind a fresh one.
  async function initRenderer() {
    if (backendParam !== "webgl2") {
      try {
        renderer = await window.createWebGPURenderer(canvas);
      } catch (e) {
        console.warn("WebGPU init failed; falling back to WebGL2:", e);
        renderer = null;
      }
      if (!renderer) {
        // A canvas that already returned a WebGPU context cannot also
        // return a WebGL2 one; swap in a fresh element.
        let probe = null;
        try { probe = canvas.getContext("webgl2"); } catch (e) { probe = null; }
        if (!probe) {
          const fresh = canvas.cloneNode(false);
          canvas.parentNode.replaceChild(fresh, canvas);
          canvas = fresh;
        }
      }
    }
    if (!renderer) {
      try {
        renderer = window.createWebGL2Renderer(canvas);
      } catch (e) {
        console.warn("WebGL2 init failed:", e);
        renderer = null;
      }
    }
    if (!renderer) {
      hud.textContent = "Neither WebGPU nor WebGL2 is available in this browser.";
      return false;
    }
    hud.dataset.backend = renderer.name;
    return true;
  }

  function resize() {
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.floor(window.innerWidth * dpr);
    canvas.height = Math.floor(window.innerHeight * dpr);
    if (renderer) renderer.resize(canvas.width, canvas.height);
  }
  window.addEventListener("resize", resize);

  // --- Minimal matrix math (no library needed for this) -------------------

  function perspective(fovyRad, aspect, near, far) {
    const f = 1.0 / Math.tan(fovyRad / 2);
    const nf = 1 / (near - far);
    return new Float32Array([
      f / aspect, 0, 0, 0,
      0, f, 0, 0,
      0, 0, (far + near) * nf, -1,
      0, 0, 2 * far * near * nf, 0,
    ]);
  }

  function multiply(a, b) {
    const out = new Float32Array(16);
    for (let row = 0; row < 4; row++) {
      for (let col = 0; col < 4; col++) {
        let sum = 0;
        for (let k = 0; k < 4; k++) sum += a[k * 4 + row] * b[col * 4 + k];
        out[col * 4 + row] = sum;
      }
    }
    return out;
  }

  function translation(x, y, z) {
    return new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1]);
  }

  // --- WebSocket data stream -----------------------------------------------

  let pointCount = 0;
  let lastFrameId = -1;
  let framesReceived = 0;
  let bytesReceived = 0;
  let cameraMode = "orbit"; // "follow" tracks the scanner pose
  let scannerPose = null;
  // Client backpressure: at most one received frame may be pending render
  // at a time; later arrivals are dropped instead of queuing unboundedly.
  let framePending = false;
  // Accumulator for the on-demand full-detail final mesh: while a full-mesh
  // transfer is in progress, incoming binary frames are appended here
  // instead of replacing the live view, and rendered once it completes.
  let fullMeshAccum = null;
  let fullMeshReceived = 0;
  let fullMeshTotal = 0;

  // Real scan data (mm-scale, arbitrary origin) has a totally different
  // scale/offset than the synthetic demo sphere (unit-radius, centered at
  // the origin) the fixed camera below was tuned for. Auto-fit to each
  // incoming frame's bounding sphere so both sources render visibly
  // instead of real data showing as a tiny off-center speck.
  let viewCenter = [0, 0, 0];
  let viewDistance = 4.2;
  let viewRadius = 1.0;

  // Coverage (G2): a voxel grid over our own live data. Visited voxels are
  // those the scanner has been near (pose translations); seen voxels are
  // those with cloud points. Under-covered = seen but not visited, painted
  // red as a second point set. Cell size follows the scene scale.
  const coverageVisited = new Set();
  const coverageSeen = new Set();
  let underPositions = new Float32Array(0);
  let underCount = 0;
  const kUnderCoveredBudget = 20000;
  function coverageCell() {
    return Math.max(viewRadius / 20, 1e-4);
  }
  function voxelKey(x, y, z, cell) {
    return Math.round(x / cell) + "," + Math.round(y / cell) + "," + Math.round(z / cell);
  }
  // Debug/acceptance hook: a hardware run can read the coverage counters
  // from the console without relying on the HUD's rAF refresh.
  window.openScannerCoverage = () => ({
    visited: coverageVisited.size, seen: coverageSeen.size, underCoveredPoints: underCount,
  });
  function markVisited(x, y, z) {
    const cell = coverageCell();
    const r = 2; // ~2 cells of scanner proximity counts as visited
    const cx = Math.round(x / cell), cy = Math.round(y / cell), cz = Math.round(z / cell);
    for (let i = -r; i <= r; i++) {
      for (let j = -r; j <= r; j++) {
        for (let k = -r; k <= r; k++) {
          coverageVisited.add((cx + i) + "," + (cy + j) + "," + (cz + k));
        }
      }
    }
  }

  // Driven by the server's status messages (see handleStatus below), not
  // read locally: "zoom" here means the scanner's own field of view
  // (ObScanFov in obscan_api.h — a real two-position switch on the actual
  // hardware, Normal/Wide, not a continuous control), so the camera
  // always reflects whatever the server says is actually configured
  // rather than a value chosen client-side.
  let fovDeg = 45;
  function fovRad() {
    return (fovDeg * Math.PI) / 180;
  }

  // Also server-driven, from the exposure/brightness control (see
  // handleStatus): maps the scanner's combined exposure/laser-current
  // value (or the synthetic demo's stand-in for it) onto a multiplier
  // applied to rendered point color, so the control has a visible effect
  // even though this viewer isn't literally re-rendering a camera image.
  let brightness = 1.0;

  function updateViewFit(positions, count) {
    if (count === 0) return;
    // Mean + RMS spread rather than a min/max bounding box: a real
    // reconstructed mesh can have a handful of far stray vertices, and a
    // pure min/max box lets a few outliers blow up the fit distance,
    // leaving the dense, visually-relevant cluster a tiny speck in the
    // middle of an otherwise-empty frame. RMS distance from the mean is
    // dominated by where most of the points actually are.
    let sx = 0, sy = 0, sz = 0;
    for (let i = 0; i < count; i++) {
      sx += positions[i * 3];
      sy += positions[i * 3 + 1];
      sz += positions[i * 3 + 2];
    }
    const cx = sx / count, cy = sy / count, cz = sz / count;
    let sumSq = 0;
    for (let i = 0; i < count; i++) {
      const dx = positions[i * 3] - cx, dy = positions[i * 3 + 1] - cy, dz = positions[i * 3 + 2] - cz;
      sumSq += dx * dx + dy * dy + dz * dz;
    }
    const rms = Math.sqrt(sumSq / count);
    const radius = Math.max(rms * 2.0, 0.001);  // ~2 sigma covers the dense cluster.
    if (cameraMode !== "follow") viewCenter = [cx, cy, cz];
    viewRadius = radius;
    // Distance needed to fit `radius` within the vertical half-FOV, plus
    // headroom so the cloud isn't touching the viewport edges.
    viewDistance = (radius / Math.tan(fovRad() / 2)) * 1.6;
  }
  const latencySamples = [];
  const kLatencyWindow = 30;

  let connectionState = "connecting";
  let lastError = "";
  let ws = null;
  let latestStatus = null;

  // Tracking-status hysteresis (row #81; findings tracking-mechanism.md §1):
  // the vendor reports FAILURE_MARKERLESS (5) as LOST (1) for its first two
  // consecutive occurrences and 5 from the third; any non-5 resets. We
  // mirror the same window client-side so the HUD's reason is deliberate.
  const kTrackingNames = ["SUCCESS", "LOST", "AMBIGUOUS_PLANE", "FAILURE_INSUFF_VALID_POINTS",
                          "FAILURE_TEXTURELESS", "FAILURE_MARKERLESS", "FAILURE_MOVING_TOO_FAST"];
  let markerlessRun = 0;
  function effectiveTracking(raw) {
    if (raw === 5) {
      markerlessRun += 1;
      return markerlessRun >= 3 ? 5 : 1;
    }
    markerlessRun = 0;
    return raw;
  }

  function sendCommand(cmd) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(cmd));
    }
  }

  function connect() {
    ws = new WebSocket(wsUrl);
    ws.binaryType = "arraybuffer";

    ws.onopen = () => {
      connectionState = "connected";
    };
    ws.onclose = () => {
      connectionState = "disconnected";
      latestStatus = null;
      updateControlsFromStatus();
      setTimeout(connect, 1000); // simple retry; this is a dev tool, not a product flow yet
    };
    ws.onerror = () => {
      lastError = "connection error";
    };
    ws.onmessage = (event) => {
      if (typeof event.data === "string") {
        let msg = null;
        try { msg = JSON.parse(event.data); } catch (e) { msg = null; }
        if (msg && msg.type === "full_mesh_start") {
          fullMeshAccum = new Float32Array(msg.total * 3);
          fullMeshReceived = 0;
          fullMeshTotal = msg.total;
          return;
        }
        if (msg && msg.type === "full_mesh_done") {
          if (fullMeshAccum) {
            renderer.upload(fullMeshAccum);
            updateViewFit(fullMeshAccum, fullMeshReceived);
            pointCount = fullMeshReceived;
            lastFrameId = -1;
            framesReceived++;
            fullMeshAccum = null;
          }
          return;
        }
        handleStatusMessage(event.data);
        return;
      }
      const buf = event.data;
      const poseFrame = FrameProtocol.decodePose(buf);
      if (poseFrame) {
        scannerPose = poseFrame.translation;
        markVisited(scannerPose[0], scannerPose[1], scannerPose[2]);
        if (cameraMode === "follow") viewCenter = scannerPose.slice();
        return;
      }
      // One decode, then branch: full-detail chunks are a bounded sequence
      // (never dropped), live ticks are latest-wins.
      const frame = FrameProtocol.decodeCloudSnapshot(buf);
      if (!frame) return;
      if (fullMeshAccum) {
        fullMeshAccum.set(frame.positions, fullMeshReceived * 3);
        fullMeshReceived += frame.count;
        framesReceived++;
        bytesReceived += buf.byteLength;
        return;
      }

      if (framePending) return; // drop live frame: renderer is behind
      bytesReceived += buf.byteLength;
      framePending = true;
      const frameId = frame.seq;
      const captureTimestampMs = frame.tCapture;
      const count = frame.count;

      renderer.upload(frame.positions);
      updateViewFit(frame.positions, count);
      pointCount = count;
      lastFrameId = frameId;
      framesReceived++;

      // Coverage: mark every point's voxel as seen; points in unseen
      // (under-covered) voxels go to the red overlay, capped for the wire.
      const cell = coverageCell();
      const under = [];
      for (let i = 0; i < count; i++) {
        const x = frame.positions[i * 3], y = frame.positions[i * 3 + 1], z = frame.positions[i * 3 + 2];
        const key = voxelKey(x, y, z, cell);
        coverageSeen.add(key);
        if (!coverageVisited.has(key) && under.length < kUnderCoveredBudget) {
          under.push(x, y, z);
        }
      }
      underCount = under.length / 3;
      underPositions = new Float32Array(under);
      renderer.uploadOverlay(underPositions);

      const latency = Date.now() - captureTimestampMs;
      latencySamples.push(latency);
      if (latencySamples.length > kLatencyWindow) latencySamples.shift();
    };
  }
  initRenderer().then((ok) => {
    if (!ok) return;
    resize();
    connect();
  }).catch((e) => {
    console.error("renderer init threw:", e);
    hud.textContent = "Renderer init failed: " + e;
  });

  // --- Scan controls (start/pause/finish, exposure, FOV) -------------------
  // Purely reactive to the server's status broadcasts (see
  // tools/scan_server/main.cpp's BuildStatusJson) rather than tracking
  // local optimistic state, so the UI always shows what the server
  // actually applied — important in live mode, where e.g. a FOV change
  // can't take effect mid-scan and the server reports that back via
  // fovControllable rather than the UI just guessing.

  const btnStartPause = document.getElementById("btn-start-pause");
  const btnFinish = document.getElementById("btn-finish");
  const btnCameraMode = document.getElementById("btn-camera-mode");
  if (btnCameraMode) btnCameraMode.addEventListener("click", () => {
    cameraMode = cameraMode === "follow" ? "orbit" : "follow";
    btnCameraMode.textContent = "Camera: " + cameraMode;
    if (cameraMode === "follow" && scannerPose) viewCenter = scannerPose.slice();
  });
  const autoExposureCheckbox = document.getElementById("auto-exposure");
  const exposureSlider = document.getElementById("exposure");
  const exposureValueLabel = document.getElementById("exposure-value");
  const fovNormalBtn = document.getElementById("fov-normal");
  const fovWideBtn = document.getElementById("fov-wide");
  const fovNote = document.getElementById("fov-note");

  function handleStatusMessage(raw) {
    let status;
    try {
      status = JSON.parse(raw);
    } catch (e) {
      return;
    }
    if (!status || status.type !== "status") return;
    if (typeof status.tracking === "number") {
      status.effectiveTracking = effectiveTracking(status.tracking);
    }
    latestStatus = status;

    // FOV is a real two-position switch on the actual scanner hardware
    // (see obscan_api.h's ObScanFov) — map it to a wider/narrower camera
    // angle here rather than pretending it's a continuous zoom.
    fovDeg = status.fov === "wide" ? 75 : 45;

    const lo = status.exposureMin || 1;
    const hi = status.exposureMax || 1000;
    const t = (status.exposure - lo) / Math.max(1, hi - lo);
    brightness = 0.4 + t * 1.2; // Visible but never fully black or blown out.

    updateControlsFromStatus();
  }

  function updateControlsFromStatus() {
    const connected = connectionState === "connected" && latestStatus !== null;
    btnStartPause.disabled = !connected;
    btnFinish.disabled = !connected || latestStatus.state === "idle";
    autoExposureCheckbox.disabled = !connected;
    fovNormalBtn.disabled = !connected || !latestStatus.fovControllable;
    fovWideBtn.disabled = !connected || !latestStatus.fovControllable;

    if (!connected) {
      btnStartPause.textContent = "Start";
      exposureSlider.disabled = true;
      fovNote.textContent = "";
      return;
    }

    btnStartPause.textContent = latestStatus.state === "scanning" ? "Pause" : "Start";
    autoExposureCheckbox.checked = latestStatus.autoExposure;
    exposureSlider.min = latestStatus.exposureMin;
    exposureSlider.max = latestStatus.exposureMax;
    exposureSlider.value = latestStatus.exposure;
    exposureSlider.disabled = latestStatus.autoExposure;
    exposureValueLabel.textContent = latestStatus.exposure;

    fovNormalBtn.classList.toggle("active", latestStatus.fov === "normal");
    fovWideBtn.classList.toggle("active", latestStatus.fov === "wide");
    fovNote.textContent = latestStatus.fovControllable
      ? ""
      : "Fixed for this live session (the real scanner can't change FOV mid-scan).";
  }

  btnStartPause.addEventListener("click", () => {
    if (!latestStatus) return;
    sendCommand({ cmd: latestStatus.state === "scanning" ? "pause" : "start" });
  });
  btnFinish.addEventListener("click", () => sendCommand({ cmd: "finish" }));
  autoExposureCheckbox.addEventListener("change", () => {
    if (autoExposureCheckbox.checked) {
      sendCommand({ cmd: "set_exposure", auto: true });
    } else {
      sendCommand({ cmd: "set_exposure", auto: false, value: Number(exposureSlider.value) });
    }
  });
  exposureSlider.addEventListener("input", () => {
    exposureValueLabel.textContent = exposureSlider.value;
  });
  exposureSlider.addEventListener("change", () => {
    sendCommand({ cmd: "set_exposure", auto: false, value: Number(exposureSlider.value) });
  });
  fovNormalBtn.addEventListener("click", () => sendCommand({ cmd: "set_fov", value: "normal" }));
  fovWideBtn.addEventListener("click", () => sendCommand({ cmd: "set_fov", value: "wide" }));

  // --- Render loop (decoupled from the data rate) --------------------------

  let renderFrames = 0;
  let lastFpsSampleTime = performance.now();
  let renderFps = 0;
  let dataFps = 0;
  let lastDataFpsSampleFrames = 0;

  function draw() {
    if (!renderer) return;
    const aspect = canvas.width / canvas.height;
    // Fixed 0.1/100.0 near/far planes (fine for the synthetic unit
    // sphere) clip almost all of a real scan's mm-scale geometry, since
    // viewDistance there is easily in the hundreds — scale the clip
    // planes to the fitted distance/radius instead so content at any
    // scale stays inside the frustum.
    const near = Math.max(viewDistance - viewRadius * 4, viewDistance * 0.01, 0.001);
    const far = viewDistance + viewRadius * 4;
    const proj = perspective(fovRad(), aspect, near, far);
    const view = translation(-viewCenter[0], -viewCenter[1], -viewCenter[2] - viewDistance);
    const matrix = multiply(proj, view);
    renderer.draw(matrix, brightness, pointCount, underCount);
    framePending = false;
    renderFrames++;

    const now = performance.now();
    if (now - lastFpsSampleTime >= 500) {
      renderFps = (renderFrames * 1000) / (now - lastFpsSampleTime);
      dataFps = ((framesReceived - lastDataFpsSampleFrames) * 1000) / (now - lastFpsSampleTime);
      lastDataFpsSampleFrames = framesReceived;
      renderFrames = 0;
      lastFpsSampleTime = now;
      updateHud();
    }

    requestAnimationFrame(draw);
  }

  function trackingText() {
    if (!latestStatus || latestStatus.tracking === undefined) return "--";
    const raw = latestStatus.tracking;
    const eff = latestStatus.effectiveTracking !== undefined ? latestStatus.effectiveTracking : raw;
    const name = kTrackingNames[eff] !== undefined ? kTrackingNames[eff] : ("status " + eff);
    if (eff !== raw) return name + " (raw " + raw + ", markerless window " + markerlessRun + "/3)";
    return name + " (" + raw + ")";
  }

  function coverageText() {
    if (coverageSeen.size === 0) return "--";
    let visited = 0;
    for (const key of coverageSeen) if (coverageVisited.has(key)) visited++;
    const pct = (100 * visited) / coverageSeen.size;
    return pct.toFixed(0) + "% covered, " + underCount + " under-covered pts";
  }

  function avgLatency() {
    if (latencySamples.length === 0) return null;
    return latencySamples.reduce((a, b) => a + b, 0) / latencySamples.length;
  }

  function updateHud() {
    const avg = avgLatency();
    const latencyText = avg === null ? "--" : avg.toFixed(1) + " ms";
    const latencyClass = avg !== null && avg > 80 ? "warn" : "ok";
    const scanState = latestStatus ? latestStatus.state : "--";
    const scanMode = latestStatus ? latestStatus.mode : "--";
    hud.innerHTML =
      '<span class="label">backend</span>  <span class="value">' + (renderer ? renderer.name : "--") + "</span>\n" +
      '<span class="label">status</span>  <span class="value">' + connectionState + "</span>\n" +
      '<span class="label">scan</span>    <span class="value">' + scanState + " (" + scanMode + ")</span>\n" +
      '<span class="label">latency</span>  <span class="' + latencyClass + '">' + latencyText + "</span>\n" +
      '<span class="label">data</span>     <span class="value">' + dataFps.toFixed(1) + " fps</span>\n" +
      '<span class="label">render</span>   <span class="value">' + renderFps.toFixed(1) + " fps</span>\n" +
      '<span class="label">points</span>   <span class="value">' + pointCount + "</span>\n" +
      '<span class="label">coverage</span> <span class="value">' + coverageText() + "</span>\n" +
      '<span class="label">tracking</span> <span class="value">' + trackingText() + "</span>\n" +
      '<span class="label">frame</span>    <span class="value">#' + lastFrameId + "</span>";
  }

  requestAnimationFrame(draw);
})();
