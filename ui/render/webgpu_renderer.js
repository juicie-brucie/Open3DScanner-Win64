// WebGPU point-cloud renderer. Implements the same interface as the
// WebGL2 fallback: draw() takes an optional under-covered overlay (a second
// point set painted a flat color).
(function () {
  "use strict";

  async function createWebGPURenderer(canvas) {
    if (!navigator.gpu) return null;
    const adapter = await navigator.gpu.requestAdapter();
    if (!adapter) return null;
    const device = await adapter.requestDevice();
    const format = navigator.gpu.getPreferredCanvasFormat();

    const shader = device.createShaderModule({
      code: `
        struct Uniforms {
          matrix: mat4x4<f32>,
          brightness: f32,
          overrideFlag: f32,
          color: vec3<f32>,
        };
        @group(0) @binding(0) var<uniform> u: Uniforms;
        struct VSOut { @builtin(position) pos: vec4<f32>, @location(0) depth: f32, };
        @vertex fn vs(@location(0) a_position: vec3<f32>) -> VSOut {
          var out: VSOut;
          out.pos = u.matrix * vec4<f32>(a_position, 1.0);
          // WebGPU clip space is z in [0,1]; the shared WebGL-style
          // perspective maps to [-1,1]. Remap so nothing is silently culled.
          out.pos.z = out.pos.z * 0.5 + out.pos.w * 0.5;
          out.depth = a_position.z;
          return out;
        }
        @fragment fn fs(in: VSOut) -> @location(0) vec4<f32> {
          let t = clamp((in.depth + 1.0) * 0.5, 0.0, 1.0);
          let near = vec3<f32>(1.0, 0.85, 0.35);
          let far = vec3<f32>(0.25, 0.55, 1.0);
          var base = mix(far, near, t) * u.brightness;
          if (u.overrideFlag > 0.5) { base = u.color; }
          return vec4<f32>(base, 1.0);
        }
      `,
    });
    const pipeline = device.createRenderPipeline({
      layout: "auto",
      vertex: { module: shader, entryPoint: "vs", buffers: [{
        arrayStride: 12,
        attributes: [{ shaderLocation: 0, offset: 0, format: "float32x3" }],
      }] },
      fragment: { module: shader, entryPoint: "fs", targets: [{ format }] },
      primitive: { topology: "point-list" },
      // less-equal so the overlay drawn second at the same depth paints over
      // the cloud instead of being rejected by the equal-depth tie.
      depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less-equal" },
    });

    // 96-byte uniform block: matrix (64), brightness (4), override (4),
    // pad (8), color (12). Two blocks so an overlay draw can use a flat
    // color without re-writing the main buffer mid-pass.
    const UNIFORM_SIZE = 96;
    function makeBindGroup() {
      const buffer = device.createBuffer({ size: UNIFORM_SIZE, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      const bindGroup = device.createBindGroup({
        layout: pipeline.getBindGroupLayout(0),
        entries: [{ binding: 0, resource: { buffer } }],
      });
      return { buffer, bindGroup };
    }
    const mainUniforms = makeBindGroup();
    const overlayUniforms = makeBindGroup();

    function writeUniforms(target, matrix, brightness, override, r, g, b) {
      const u = new Float32Array(UNIFORM_SIZE / 4);
      u.set(matrix, 0);
      u[16] = brightness;
      u[17] = override;
      u[20] = r; u[21] = g; u[22] = b;
      device.queue.writeBuffer(target.buffer, 0, u);
    }

    function makePointBuffer() {
      return { buffer: null, capacity: 0 };
    }
    function uploadTo(target, positions) {
      const bytes = positions.byteLength;
      if (!target.buffer || bytes > target.capacity) {
        if (target.buffer) target.buffer.destroy();
        target.capacity = Math.max(bytes, 12 * 1024);
        target.buffer = device.createBuffer({ size: target.capacity, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
      }
      device.queue.writeBuffer(target.buffer, 0, positions);
    }
    const main = makePointBuffer();
    const overlay = makePointBuffer();

    let depthTexture = null;
    let width = 1, height = 1;

    function ensureDepth() {
      if (depthTexture && depthTexture.width === width && depthTexture.height === height) return;
      if (depthTexture) depthTexture.destroy();
      depthTexture = device.createTexture({ size: [width, height], format: "depth24plus",
        usage: GPUTextureUsage.RENDER_ATTACHMENT });
    }

    // Canvas is claimed only after every other fallible step.
    const context = canvas.getContext("webgpu");
    if (!context) return null;
    context.configure({ device, format, alphaMode: "opaque" });

    return {
      name: "webgpu",
      resize(w, h) { width = w; height = h; ensureDepth(); },
      upload(positions) { uploadTo(main, positions); },
      uploadOverlay(positions) { uploadTo(overlay, positions); },
      draw(matrix, brightness, count, overlayCount = 0) {
        ensureDepth();
        writeUniforms(mainUniforms, matrix, brightness, 0.0, 0, 0, 0);
        writeUniforms(overlayUniforms, matrix, 1.0, 1.0, 1.0, 0.15, 0.1);
        const encoder = device.createCommandEncoder();
        const pass = encoder.beginRenderPass({
          colorAttachments: [{ view: context.getCurrentTexture().createView(),
            clearValue: { r: 0.07, g: 0.07, b: 0.08, a: 1.0 }, loadOp: "clear", storeOp: "store" }],
          depthStencilAttachment: { view: depthTexture.createView(), depthClearValue: 1.0,
            depthLoadOp: "clear", depthStoreOp: "store" },
        });
        pass.setPipeline(pipeline);
        if (count > 0 && main.buffer) {
          pass.setBindGroup(0, mainUniforms.bindGroup);
          pass.setVertexBuffer(0, main.buffer);
          pass.draw(count, 1, 0, 0);
        }
        if (overlayCount > 0 && overlay.buffer) {
          pass.setBindGroup(0, overlayUniforms.bindGroup);
          pass.setVertexBuffer(0, overlay.buffer);
          pass.draw(overlayCount, 1, 0, 0);
        }
        pass.end();
        device.queue.submit([encoder.finish()]);
      },
    };
  }

  window.createWebGPURenderer = createWebGPURenderer;
})();
