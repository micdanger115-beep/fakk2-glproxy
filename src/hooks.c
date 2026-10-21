/*
 * Logging der Zeichenaufrufe.
 *
 * Normalbetrieb: pro Frame werden Zaehler gefuehrt und alle
 * summary_interval Frames als Durchschnitt pro Frame ins Log geschrieben.
 * Frame-Capture: Taste (Standard F11) oder capture_frame=N in der INI;
 * dann wird jeder gehookte Aufruf eines Frames mit Argumenten geloggt.
 *
 * Neue Hooks: Funktion hook_<name> schreiben (Signatur wie in gl.h) und in
 * hooks_install() mit HOOK(<name>) eintragen. Die echte Funktion ruft man
 * mit REAL(<name>, PFN-Typ)(...) auf.
 */
#include "proxy.h"
#include "log.h"

#include <GL/gl.h>
#include <string.h>

#define HOOK(name) (disp_##name = (PROC)hook_##name)
#define REAL(name, type) ((type)real_##name)

/* --- Einstellungen (fakk2_glproxy.ini, Abschnitt [log]) ------------------- */

static struct {
    int summary_interval;  /* Frames pro Zusammenfassung, 0 = aus */
    int capture_key;       /* Virtual-Key-Code, 0 = aus */
    int capture_frame;     /* Frame-Nummer fuer automatisches Capture, 0 = aus */
    int capture_vertices;  /* glVertex* im Capture mitloggen */
    int log_extensions;    /* wglGetProcAddress-Anfragen loggen */
} cfg;

static void load_config(void)
{
    wchar_t ini[MAX_PATH];
    proxy_path_in_dll_dir(L"fakk2_glproxy.ini", ini, MAX_PATH);
    cfg.summary_interval = GetPrivateProfileIntW(L"log", L"summary_interval", 300, ini);
    cfg.capture_key      = GetPrivateProfileIntW(L"log", L"capture_key", VK_F11, ini);
    cfg.capture_frame    = GetPrivateProfileIntW(L"log", L"capture_frame", 0, ini);
    cfg.capture_vertices = GetPrivateProfileIntW(L"log", L"capture_vertices", 0, ini);
    cfg.log_extensions   = GetPrivateProfileIntW(L"log", L"log_extensions", 1, ini);
    log_printf("Konfiguration: summary_interval=%d capture_key=0x%02X capture_frame=%d "
               "capture_vertices=%d log_extensions=%d",
               cfg.summary_interval, cfg.capture_key, cfg.capture_frame,
               cfg.capture_vertices, cfg.log_extensions);
}

/* --- Zaehler ------------------------------------------------------------- */

#define NUM_PRIMS 10 /* GL_POINTS (0) .. GL_POLYGON (9) */

typedef struct {
    unsigned frames;
    unsigned begin[NUM_PRIMS];
    unsigned vertices;          /* glVertex* zwischen glBegin/glEnd */
    unsigned draw_elements;
    unsigned draw_elements_idx;
    unsigned draw_arrays;
    unsigned draw_arrays_verts;
    unsigned array_element;
    unsigned lock_arrays;
    unsigned bind_texture;
    unsigned tex_image;
    unsigned tex_sub_image;
    double   tex_bytes;
    unsigned enable_disable;
    unsigned blend_func;
    unsigned clear;
} stats_t;

static stats_t stats;
static unsigned frame_no = 1;
static int capturing;
static int key_was_down;

/* GL ist auf einen Thread zur Zeit beschraenkt, die Zaehler brauchen keine Locks. */

static const char *prim_name(GLenum mode)
{
    static const char *names[NUM_PRIMS] = {
        "GL_POINTS", "GL_LINES", "GL_LINE_LOOP", "GL_LINE_STRIP", "GL_TRIANGLES",
        "GL_TRIANGLE_STRIP", "GL_TRIANGLE_FAN", "GL_QUADS", "GL_QUAD_STRIP", "GL_POLYGON"
    };
    return mode < NUM_PRIMS ? names[mode] : "?";
}

static const char *enum_name(GLenum e)
{
    static char buf[4][16];
    static int slot;
    switch (e) {
#define E(x) case x: return #x;
    E(GL_TEXTURE_2D) E(GL_BLEND) E(GL_DEPTH_TEST) E(GL_CULL_FACE) E(GL_ALPHA_TEST)
    E(GL_FOG) E(GL_LIGHTING) E(GL_POLYGON_OFFSET_FILL) E(GL_SCISSOR_TEST)
    E(GL_STENCIL_TEST) E(GL_CLIP_PLANE0) E(GL_TEXTURE_GEN_S) E(GL_TEXTURE_GEN_T)
    E(GL_ZERO) E(GL_ONE) E(GL_SRC_COLOR) E(GL_ONE_MINUS_SRC_COLOR) E(GL_SRC_ALPHA)
    E(GL_ONE_MINUS_SRC_ALPHA) E(GL_DST_ALPHA) E(GL_ONE_MINUS_DST_ALPHA)
    E(GL_DST_COLOR) E(GL_ONE_MINUS_DST_COLOR) E(GL_SRC_ALPHA_SATURATE)
    E(GL_RGB) E(GL_RGBA) E(GL_RGB8) E(GL_RGBA8) E(GL_LUMINANCE) E(GL_ALPHA)
    E(GL_LUMINANCE_ALPHA) E(GL_UNSIGNED_BYTE) E(GL_UNSIGNED_SHORT) E(GL_UNSIGNED_INT)
    E(GL_FLOAT)
#undef E
    }
    /* Kleiner Ringpuffer, damit mehrere Aufrufe in einem log_printf gehen. */
    slot = (slot + 1) & 3;
    wsprintfA(buf[slot], "0x%04X", (unsigned)e);
    return buf[slot];
}

static unsigned bytes_per_pixel(GLenum format, GLenum type)
{
    unsigned comps;
    switch (format) {
    case GL_RGBA: case GL_BGRA_EXT: comps = 4; break;
    case GL_RGB: case GL_BGR_EXT:   comps = 3; break;
    case GL_LUMINANCE_ALPHA:        comps = 2; break;
    default:                        comps = 1; break;
    }
    switch (type) {
    case GL_UNSIGNED_SHORT: case GL_SHORT:            return comps * 2;
    case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: return comps * 4;
    default:                                          return comps;
    }
}

#define CAP(...) do { if (capturing) log_printf("  " __VA_ARGS__); } while (0)

/* --- Hooks: Immediate Mode ----------------------------------------------- */

static void APIENTRY hook_glBegin(GLenum mode)
{
    if (mode < NUM_PRIMS)
        stats.begin[mode]++;
    CAP("glBegin(%s)", prim_name(mode));
    REAL(glBegin, void (APIENTRY *)(GLenum))(mode);
}

static void APIENTRY hook_glEnd(void)
{
    CAP("glEnd()");
    REAL(glEnd, void (APIENTRY *)(void))();
}

static void APIENTRY hook_glVertex2f(GLfloat x, GLfloat y)
{
    stats.vertices++;
    if (cfg.capture_vertices)
        CAP("glVertex2f(%.3f, %.3f)", x, y);
    REAL(glVertex2f, void (APIENTRY *)(GLfloat, GLfloat))(x, y);
}

static void APIENTRY hook_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    stats.vertices++;
    if (cfg.capture_vertices)
        CAP("glVertex3f(%.3f, %.3f, %.3f)", x, y, z);
    REAL(glVertex3f, void (APIENTRY *)(GLfloat, GLfloat, GLfloat))(x, y, z);
}

static void APIENTRY hook_glVertex3fv(const GLfloat *v)
{
    stats.vertices++;
    if (cfg.capture_vertices)
        CAP("glVertex3fv(%.3f, %.3f, %.3f)", v[0], v[1], v[2]);
    REAL(glVertex3fv, void (APIENTRY *)(const GLfloat *))(v);
}

/* --- Hooks: Vertex Arrays (Q3-artige Renderer nutzen vor allem diese) ---- */

static void APIENTRY hook_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    stats.draw_elements++;
    stats.draw_elements_idx += (unsigned)count;
    CAP("glDrawElements(%s, %d, %s, %p)", prim_name(mode), (int)count, enum_name(type), indices);
    REAL(glDrawElements, void (APIENTRY *)(GLenum, GLsizei, GLenum, const GLvoid *))(mode, count, type, indices);
}

static void APIENTRY hook_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    stats.draw_arrays++;
    stats.draw_arrays_verts += (unsigned)count;
    CAP("glDrawArrays(%s, %d, %d)", prim_name(mode), (int)first, (int)count);
    REAL(glDrawArrays, void (APIENTRY *)(GLenum, GLint, GLsizei))(mode, first, count);
}

static void APIENTRY hook_glArrayElement(GLint i)
{
    stats.array_element++;
    if (cfg.capture_vertices)
        CAP("glArrayElement(%d)", (int)i);
    REAL(glArrayElement, void (APIENTRY *)(GLint))(i);
}

/* --- Hooks: Texturen und Zustand ----------------------------------------- */

static void APIENTRY hook_glBindTexture(GLenum target, GLuint texture)
{
    stats.bind_texture++;
    CAP("glBindTexture(%s, %u)", enum_name(target), (unsigned)texture);
    REAL(glBindTexture, void (APIENTRY *)(GLenum, GLuint))(target, texture);
}

static void APIENTRY hook_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                                       GLsizei width, GLsizei height, GLint border,
                                       GLenum format, GLenum type, const GLvoid *pixels)
{
    stats.tex_image++;
    stats.tex_bytes += (double)width * height * bytes_per_pixel(format, type);
    /* Uploads sind selten und fuer das Texturen-Inventar interessant: immer loggen. */
    log_printf("  glTexImage2D(%s, level %d, intern %s, %dx%d, %s/%s)",
               enum_name(target), (int)level, enum_name((GLenum)internalformat),
               (int)width, (int)height, enum_name(format), enum_name(type));
    (void)border;
    REAL(glTexImage2D, void (APIENTRY *)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *))
        (target, level, internalformat, width, height, border, format, type, pixels);
}

static void APIENTRY hook_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                          GLsizei width, GLsizei height, GLenum format, GLenum type,
                                          const GLvoid *pixels)
{
    stats.tex_sub_image++;
    stats.tex_bytes += (double)width * height * bytes_per_pixel(format, type);
    CAP("glTexSubImage2D(%s, level %d, %d,%d %dx%d)", enum_name(target), (int)level,
        (int)xoffset, (int)yoffset, (int)width, (int)height);
    REAL(glTexSubImage2D, void (APIENTRY *)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid *))
        (target, level, xoffset, yoffset, width, height, format, type, pixels);
}

static void APIENTRY hook_glEnable(GLenum cap)
{
    stats.enable_disable++;
    CAP("glEnable(%s)", enum_name(cap));
    REAL(glEnable, void (APIENTRY *)(GLenum))(cap);
}

static void APIENTRY hook_glDisable(GLenum cap)
{
    stats.enable_disable++;
    CAP("glDisable(%s)", enum_name(cap));
    REAL(glDisable, void (APIENTRY *)(GLenum))(cap);
}

static void APIENTRY hook_glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    stats.blend_func++;
    CAP("glBlendFunc(%s, %s)", enum_name(sfactor), enum_name(dfactor));
    REAL(glBlendFunc, void (APIENTRY *)(GLenum, GLenum))(sfactor, dfactor);
}

static void APIENTRY hook_glClear(GLbitfield mask)
{
    stats.clear++;
    CAP("glClear(0x%X)", (unsigned)mask);
    REAL(glClear, void (APIENTRY *)(GLbitfield))(mask);
}

/* --- Extensions ueber wglGetProcAddress ---------------------------------- */

typedef void (APIENTRY *PFN_LOCKARRAYS)(GLint, GLsizei);
typedef void (APIENTRY *PFN_UNLOCKARRAYS)(void);
static PFN_LOCKARRAYS real_lock_arrays;
static PFN_UNLOCKARRAYS real_unlock_arrays;

static void APIENTRY hook_glLockArraysEXT(GLint first, GLsizei count)
{
    stats.lock_arrays++;
    CAP("glLockArraysEXT(%d, %d)", (int)first, (int)count);
    real_lock_arrays(first, count);
}

static void APIENTRY hook_glUnlockArraysEXT(void)
{
    CAP("glUnlockArraysEXT()");
    real_unlock_arrays();
}

static PROC WINAPI hook_wglGetProcAddress(LPCSTR name)
{
    PROC p = REAL(wglGetProcAddress, PROC (WINAPI *)(LPCSTR))(name);
    if (cfg.log_extensions)
        log_printf("wglGetProcAddress(\"%s\") = %p", name, (void *)p);
    if (p && !strcmp(name, "glLockArraysEXT")) {
        real_lock_arrays = (PFN_LOCKARRAYS)p;
        return (PROC)hook_glLockArraysEXT;
    }
    if (p && !strcmp(name, "glUnlockArraysEXT")) {
        real_unlock_arrays = (PFN_UNLOCKARRAYS)p;
        return (PROC)hook_glUnlockArraysEXT;
    }
    return p;
}

/* --- Kontext und Frame-Ende ---------------------------------------------- */

static HGLRC WINAPI hook_wglCreateContext(HDC dc)
{
    HGLRC rc = REAL(wglCreateContext, HGLRC (WINAPI *)(HDC))(dc);
    log_printf("wglCreateContext(%p) = %p", (void *)dc, (void *)rc);
    return rc;
}

static BOOL WINAPI hook_wglMakeCurrent(HDC dc, HGLRC rc)
{
    BOOL ok = REAL(wglMakeCurrent, BOOL (WINAPI *)(HDC, HGLRC))(dc, rc);
    log_printf("wglMakeCurrent(%p, %p) = %d", (void *)dc, (void *)rc, ok);
    if (ok && rc) {
        typedef const GLubyte *(APIENTRY *PFN_GETSTRING)(GLenum);
        PFN_GETSTRING gs = REAL(glGetString, PFN_GETSTRING);
        const char *vendor = (const char *)gs(GL_VENDOR);
        const char *renderer = (const char *)gs(GL_RENDERER);
        const char *version = (const char *)gs(GL_VERSION);
        log_printf("GL_VENDOR=%s | GL_RENDERER=%s | GL_VERSION=%s",
                   vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
    }
    return ok;
}

static void log_summary(void)
{
    double f = stats.frames ? (double)stats.frames : 1.0;
    unsigned begins = 0;
    for (int i = 0; i < NUM_PRIMS; i++)
        begins += stats.begin[i];

    log_printf("Frames %u-%u (%u): pro Frame %.0f glBegin [TRI %.0f, STRIP %.0f, FAN %.0f, QUADS %.0f, POLY %.0f], "
               "%.0f glVertex, %.0f glDrawElements (%.0f Indizes), %.0f glDrawArrays (%.0f Vertices), "
               "%.0f glArrayElement, %.0f LockArrays, %.0f BindTexture, %.0f Enable/Disable, "
               "%.0f BlendFunc, %.0f Clear; Uploads gesamt %u+%u (%.1f MB)",
               frame_no - stats.frames + 1, frame_no, stats.frames,
               begins / f, stats.begin[GL_TRIANGLES] / f, stats.begin[GL_TRIANGLE_STRIP] / f,
               stats.begin[GL_TRIANGLE_FAN] / f, stats.begin[GL_QUADS] / f, stats.begin[GL_POLYGON] / f,
               stats.vertices / f, stats.draw_elements / f, stats.draw_elements_idx / f,
               stats.draw_arrays / f, stats.draw_arrays_verts / f, stats.array_element / f,
               stats.lock_arrays / f, stats.bind_texture / f, stats.enable_disable / f,
               stats.blend_func / f, stats.clear / f,
               stats.tex_image, stats.tex_sub_image, stats.tex_bytes / (1024.0 * 1024.0));
    memset(&stats, 0, sizeof stats);
}

static void end_frame(void)
{
    if (capturing) {
        log_printf("=== Capture Ende Frame %u ===", frame_no);
        capturing = 0;
    }

    stats.frames++;
    if (cfg.summary_interval > 0 && stats.frames >= (unsigned)cfg.summary_interval)
        log_summary();
    frame_no++;

    int start = cfg.capture_frame > 0 && frame_no == (unsigned)cfg.capture_frame;
    if (cfg.capture_key) {
        int down = (GetAsyncKeyState(cfg.capture_key) & 0x8000) != 0;
        if (down && !key_was_down)
            start = 1;
        key_was_down = down;
    }
    if (start) {
        capturing = 1;
        log_printf("=== Capture Start Frame %u ===", frame_no);
    }
}

static BOOL WINAPI hook_wglSwapBuffers(HDC dc)
{
    CAP("wglSwapBuffers(%p)", (void *)dc);
    BOOL ok = REAL(wglSwapBuffers, BOOL (WINAPI *)(HDC))(dc);
    end_frame();
    return ok;
}

void hooks_install(void)
{
    load_config();

    HOOK(glBegin);
    HOOK(glEnd);
    HOOK(glVertex2f);
    HOOK(glVertex3f);
    HOOK(glVertex3fv);
    HOOK(glDrawElements);
    HOOK(glDrawArrays);
    HOOK(glArrayElement);
    HOOK(glBindTexture);
    HOOK(glTexImage2D);
    HOOK(glTexSubImage2D);
    HOOK(glEnable);
    HOOK(glDisable);
    HOOK(glBlendFunc);
    HOOK(glClear);
    HOOK(wglGetProcAddress);
    HOOK(wglCreateContext);
    HOOK(wglMakeCurrent);
    HOOK(wglSwapBuffers);

    if (cfg.capture_frame == 1) {
        capturing = 1;
        log_printf("=== Capture Start Frame 1 ===");
    }
    log_printf("Hooks installiert");
}

void hooks_shutdown(void)
{
    if (stats.frames) {
        frame_no--; /* log_summary erwartet die Nummer des letzten fertigen Frames */
        log_summary();
        frame_no++;
    }
    log_printf("Beendet nach %u Frames", frame_no - 1);
}
