// WebGL2 point-cloud renderer (the fallback path).
// Implements the same interface as webgpu_renderer.js: draw() takes an
// optional under-covered overlay (a second point set painted a flat color).
(function () {
  "use strict";

  function createWebGL2Renderer(canvas) {
    const gl = canvas.getContext("webgl2");
    if (!gl) return null;

    const vertexSrc = `#version 300 es
      layout(location = 0) in vec3 a_position;
      uniform mat4 u_matrix;
      out float v_depth;
      void main() {
        gl_Position = u_matrix * vec4(a_position, 1.0);
        gl_PointSize = 2.5;
        v_depth = a_position.z;
      }
    `;
    const fragmentSrc = `#version 300 es
      precision mediump float;
      in float v_depth;
      uniform float u_brightness;
      uniform float u_override;
      uniform vec3 u_override_color;
      out vec4 outColor;
      void main() {
        float t = clamp((v_depth + 1.0) * 0.5, 0.0, 1.0);
        vec3 near = vec3(1.0, 0.85, 0.35);
        vec3 far = vec3(0.25, 0.55, 1.0);
        vec3 base = mix(far, near, t) * u_brightness;
        if (u_override > 0.5) base = u_override_color;
        outColor = vec4(base, 1.0);
      }
    `;
    function compile(type, source) {
      const s = gl.createShader(type);
      gl.shaderSource(s, source);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    }
    const program = gl.createProgram();
    gl.attachShader(program, compile(gl.VERTEX_SHADER, vertexSrc));
    gl.attachShader(program, compile(gl.FRAGMENT_SHADER, fragmentSrc));
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
    const matrixLocation = gl.getUniformLocation(program, "u_matrix");
    const brightnessLocation = gl.getUniformLocation(program, "u_brightness");
    const overrideLocation = gl.getUniformLocation(program, "u_override");
    const overrideColorLocation = gl.getUniformLocation(program, "u_override_color");

    function makeVao() {
      const buffer = gl.createBuffer();
      const vao = gl.createVertexArray();
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
      gl.bindVertexArray(null);
      return { buffer, vao };
    }
    const main = makeVao();
    const overlay = makeVao();
    gl.enable(gl.DEPTH_TEST);
    // LEQUAL so the under-covered overlay (same positions, drawn second)
    // is not depth-rejected by the cloud's equal-depth fragments.
    gl.depthFunc(gl.LEQUAL);
    gl.clearColor(0.07, 0.07, 0.08, 1.0);

    function uploadTo(target, positions) {
      gl.bindBuffer(gl.ARRAY_BUFFER, target.buffer);
      gl.bufferData(gl.ARRAY_BUFFER, positions, gl.DYNAMIC_DRAW);
    }

    return {
      name: "webgl2",
      resize(w, h) { gl.viewport(0, 0, w, h); },
      upload(positions) { uploadTo(main, positions); },
      uploadOverlay(positions) { uploadTo(overlay, positions); },
      draw(matrix, brightness, count, overlayCount = 0) {
        gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
        gl.useProgram(program);
        gl.uniformMatrix4fv(matrixLocation, false, matrix);
        if (count > 0) {
          gl.bindVertexArray(main.vao);
          gl.uniform1f(brightnessLocation, brightness);
          gl.uniform1f(overrideLocation, 0.0);
          gl.drawArrays(gl.POINTS, 0, count);
        }
        if (overlayCount > 0) {
          gl.bindVertexArray(overlay.vao);
          gl.uniform1f(brightnessLocation, 1.0);
          gl.uniform1f(overrideLocation, 1.0);
          gl.uniform3f(overrideColorLocation, 1.0, 0.15, 0.1); // under-covered = red
          gl.drawArrays(gl.POINTS, 0, overlayCount);
        }
        gl.bindVertexArray(null);
      },
    };
  }

  window.createWebGL2Renderer = createWebGL2Renderer;
})();
