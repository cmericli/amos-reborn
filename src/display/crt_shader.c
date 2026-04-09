/*
 * crt_shader.c — CRT post-processing shader pipeline
 *
 * Ported from Blue Steel (gpu_backend.c).
 * Two-pass FBO rendering: render AMOS screens to texture, then apply
 * CRT effects (scanlines, bloom, barrel distortion, chromatic aberration,
 * shadow mask, noise, flicker, vignette, color tint).
 *
 * Uses SDL2 + OpenGL 3.3 Core (macOS/Linux) or WebGL 2.0 (Emscripten).
 */

#include "amos.h"

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Embedded Shaders (ported from Blue Steel) ───────────────────── */

static const char *crt_vert_src =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 pos = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1) * 2.0 - 1.0;\n"
    "    v_uv = pos * 0.5 + 0.5;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char *crt_frag_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_screen;\n"
    "uniform vec2 u_resolution;\n"
    "uniform float u_scanline_intensity;\n"
    "uniform float u_bloom_intensity;\n"
    "uniform float u_curvature;\n"
    "uniform float u_mask_intensity;\n"
    "uniform float u_chroma_offset;\n"
    "uniform float u_vignette_intensity;\n"
    "uniform float u_noise_intensity;\n"
    "uniform float u_flicker;\n"
    "uniform float u_time;\n"
    "uniform vec3 u_tint;\n"
    "out vec4 frag_color;\n"
    "\n"
    "vec2 barrel(vec2 uv, float k) {\n"
    "    vec2 c = uv - 0.5;\n"
    "    float r2 = dot(c, c);\n"
    "    c *= 1.0 + k * r2;\n"
    "    return c + 0.5;\n"
    "}\n"
    "\n"
    "float hash(vec2 p) {\n"
    "    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec2 uv = barrel(v_uv, u_curvature);\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
    "        frag_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "\n"
    "    float ca = u_chroma_offset;\n"
    "    float r = texture(u_screen, barrel(v_uv + vec2( ca, 0.0), u_curvature)).r;\n"
    "    float g = texture(u_screen, uv).g;\n"
    "    float b = texture(u_screen, barrel(v_uv + vec2(-ca, 0.0), u_curvature)).b;\n"
    "    vec3 color = vec3(r, g, b);\n"
    "\n"
    "    if (u_bloom_intensity > 0.0) {\n"
    "        vec3 bloom = vec3(0.0);\n"
    "        float px = 1.0 / u_resolution.x;\n"
    "        float py = 1.0 / u_resolution.y;\n"
    "        bloom += texture(u_screen, uv + vec2(-2.0*px, 0.0)).rgb;\n"
    "        bloom += texture(u_screen, uv + vec2( 2.0*px, 0.0)).rgb;\n"
    "        bloom += texture(u_screen, uv + vec2(0.0, -2.0*py)).rgb;\n"
    "        bloom += texture(u_screen, uv + vec2(0.0,  2.0*py)).rgb;\n"
    "        bloom += texture(u_screen, uv + vec2(-px, -py)).rgb;\n"
    "        bloom += texture(u_screen, uv + vec2( px, -py)).rgb;\n"
    "        bloom += texture(u_screen, uv + vec2(-px,  py)).rgb;\n"
    "        bloom += texture(u_screen, uv + vec2( px,  py)).rgb;\n"
    "        bloom *= 0.125;\n"
    "        color += bloom * u_bloom_intensity;\n"
    "    }\n"
    "\n"
    "    if (u_scanline_intensity > 0.0) {\n"
    "        float brightness = dot(color, vec3(0.299, 0.587, 0.114));\n"
    "        float scan = sin(uv.y * u_resolution.y * 3.14159) * 0.5 + 0.5;\n"
    "        float adaptive = mix(1.0, scan, u_scanline_intensity * (0.5 + brightness * 0.5));\n"
    "        color *= adaptive;\n"
    "    }\n"
    "\n"
    "    if (u_mask_intensity > 0.0) {\n"
    "        float px = uv.x * u_resolution.x;\n"
    "        int col = int(mod(px, 3.0));\n"
    "        vec3 mask = vec3(1.0);\n"
    "        if (col == 0) mask = vec3(1.0, 1.0 - u_mask_intensity, 1.0 - u_mask_intensity);\n"
    "        else if (col == 1) mask = vec3(1.0 - u_mask_intensity, 1.0, 1.0 - u_mask_intensity);\n"
    "        else mask = vec3(1.0 - u_mask_intensity, 1.0 - u_mask_intensity, 1.0);\n"
    "        color *= mask;\n"
    "    }\n"
    "\n"
    "    float noise = hash(uv * u_resolution + vec2(u_time * 137.0, u_time * 59.0));\n"
    "    color += (noise - 0.5) * u_noise_intensity;\n"
    "    color *= 1.0 + sin(u_time * 60.0) * u_flicker;\n"
    "\n"
    "    if (u_vignette_intensity > 0.0) {\n"
    "        vec2 vc = uv - 0.5;\n"
    "        float vign = 1.0 - dot(vc, vc) * u_vignette_intensity * 4.0;\n"
    "        color *= clamp(vign, 0.0, 1.0);\n"
    "    }\n"
    "\n"
    "    color *= u_tint;\n"
    "    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);\n"
    "}\n";

/* ── Screen quad vertex shader (for blitting AMOS framebuffer to texture) ─ */

static const char *screen_vert_src =
    "#version 330 core\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 pos = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1) * 2.0 - 1.0;\n"
    "    v_uv = vec2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char *screen_frag_src =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_screen;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = texture(u_screen, v_uv);\n"
    "}\n";

/* ── CRT Presets ─────────────────────────────────────────────────── */

static const crt_params_t crt_presets[CRT_PRESET_COUNT] = {
    /* CLEAN: pixel-perfect, no effects */
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    /* VGA: subtle scanlines + bloom */
    {0.15f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    /* CRT: full CRT experience */
    {0.3f, 0.4f, 0.08f, 0.15f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    /* AMBER: monochrome amber */
    {0.3f, 0.4f, 0.08f, 0.15f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 1.0f, 0.7f, 0.2f},
    /* GREEN: monochrome green */
    {0.5f, 0.4f, 0.08f, 0.15f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.2f, 1.0f, 0.3f},
    /* TV: RF composite */
    {0.0f, 0.3f, 0.15f, 0.0f, 0.004f, 0.0f, 0.04f, 0.03f, 0.0f, 1.0f, 1.0f, 1.0f},
    /* COMMODORE: 1084S monitor — warm, slight curvature, moderate scanlines */
    {0.25f, 0.35f, 0.06f, 0.1f, 0.001f, 0.2f, 0.01f, 0.01f, 0.0f, 1.0f, 0.95f, 0.9f},
};

static const char *crt_preset_names[CRT_PRESET_COUNT] = {
    "clean", "vga", "crt", "amber", "green", "tv", "commodore"
};

/* ── OpenGL State ────────────────────────────────────────────────── */

static GLuint g_crt_program = 0;
static GLuint g_screen_program = 0;
static GLuint g_crt_vao = 0;
static GLuint g_fbo = 0;
static GLuint g_fbo_tex = 0;
static GLuint g_screen_tex = 0;     /* texture for uploading AMOS framebuffer */
static int g_fbo_w = 0, g_fbo_h = 0;

/* ── Shader Compilation ──────────────────────────────────────────── */

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[CRT] Shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vert, GLuint frag)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vert);
    glAttachShader(p, frag);
    glLinkProgram(p);

    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[CRT] Program link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    glDeleteShader(vert);
    glDeleteShader(frag);
    return p;
}

/* ── FBO Management ──────────────────────────────────────────────── */

static void create_or_resize_fbo(int w, int h)
{
    if (w == g_fbo_w && h == g_fbo_h && g_fbo != 0) return;

    if (g_fbo_tex) { glDeleteTextures(1, &g_fbo_tex); g_fbo_tex = 0; }
    if (g_fbo) { glDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }

    g_fbo_w = w;
    g_fbo_h = h;

    glGenTextures(1, &g_fbo_tex);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &g_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_fbo_tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[CRT] FBO incomplete: 0x%x\n", status);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ── Public API ──────────────────────────────────────────────────── */

int crt_init(amos_state_t *state)
{
    /* Compile screen blit shader */
    GLuint sv = compile_shader(GL_VERTEX_SHADER, screen_vert_src);
    GLuint sf = compile_shader(GL_FRAGMENT_SHADER, screen_frag_src);
    if (!sv || !sf) return -1;
    g_screen_program = link_program(sv, sf);
    if (!g_screen_program) return -1;

    glUseProgram(g_screen_program);
    glUniform1i(glGetUniformLocation(g_screen_program, "u_screen"), 0);
    glUseProgram(0);

    /* Compile CRT shader */
    GLuint cv = compile_shader(GL_VERTEX_SHADER, crt_vert_src);
    GLuint cf = compile_shader(GL_FRAGMENT_SHADER, crt_frag_src);
    if (!cv || !cf) return -1;
    g_crt_program = link_program(cv, cf);
    if (!g_crt_program) return -1;

    glUseProgram(g_crt_program);
    glUniform1i(glGetUniformLocation(g_crt_program, "u_screen"), 0);
    glUseProgram(0);

    /* Empty VAO for fullscreen quad */
    glGenVertexArrays(1, &g_crt_vao);

    /* Create screen upload texture */
    glGenTextures(1, &g_screen_tex);
    glBindTexture(GL_TEXTURE_2D, g_screen_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Set initial preset */
    crt_set_preset(state, state->crt_preset);

    fprintf(stderr, "[CRT] Shader pipeline initialized (preset: %s)\n",
            crt_preset_names[state->crt_preset]);

    return 0;
}

void crt_set_preset(amos_state_t *state, crt_preset_t preset)
{
    if (preset >= CRT_PRESET_COUNT) preset = CRT_PRESET_CLEAN;
    float saved_time = state->crt.time;
    state->crt = crt_presets[preset];
    state->crt.time = saved_time;
    state->crt_preset = preset;
}

void crt_render(amos_state_t *state, uint32_t *framebuffer, int fb_w, int fb_h)
{
    int win_w = state->window_width;
    int win_h = state->window_height;
    if (win_w <= 0 || win_h <= 0) return;

    /* Ensure FBO matches window size */
    create_or_resize_fbo(win_w, win_h);

    /* Upload AMOS framebuffer to texture */
    glBindTexture(GL_TEXTURE_2D, g_screen_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fb_w, fb_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, framebuffer);

    /* ── Pass 1: Blit AMOS screen to FBO ──────────────────────────── */
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glViewport(0, 0, win_w, win_h);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_screen_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_screen_tex);

    glBindVertexArray(g_crt_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);

    /* ── Pass 2: CRT post-processing to screen ───────────────────── */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glClear(GL_COLOR_BUFFER_BIT);

    state->crt.time += 1.0f / 60.0f;

    glUseProgram(g_crt_program);

    /* Upload CRT uniforms */
    glUniform2f(glGetUniformLocation(g_crt_program, "u_resolution"),
                (float)win_w, (float)win_h);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_scanline_intensity"),
                state->crt.scanline_intensity);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_bloom_intensity"),
                state->crt.bloom_intensity);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_curvature"),
                state->crt.curvature);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_mask_intensity"),
                state->crt.mask_intensity);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_chroma_offset"),
                state->crt.chroma_offset);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_vignette_intensity"),
                state->crt.vignette_intensity);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_noise_intensity"),
                state->crt.noise_intensity);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_flicker"),
                state->crt.flicker);
    glUniform1f(glGetUniformLocation(g_crt_program, "u_time"),
                state->crt.time);
    glUniform3f(glGetUniformLocation(g_crt_program, "u_tint"),
                state->crt.tint_r, state->crt.tint_g, state->crt.tint_b);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);

    glBindVertexArray(g_crt_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

void crt_shutdown(void)
{
    if (g_screen_tex) { glDeleteTextures(1, &g_screen_tex); g_screen_tex = 0; }
    if (g_fbo_tex) { glDeleteTextures(1, &g_fbo_tex); g_fbo_tex = 0; }
    if (g_fbo) { glDeleteFramebuffers(1, &g_fbo); g_fbo = 0; }
    if (g_crt_vao) { glDeleteVertexArrays(1, &g_crt_vao); g_crt_vao = 0; }
    if (g_crt_program) { glDeleteProgram(g_crt_program); g_crt_program = 0; }
    if (g_screen_program) { glDeleteProgram(g_screen_program); g_screen_program = 0; }
}
