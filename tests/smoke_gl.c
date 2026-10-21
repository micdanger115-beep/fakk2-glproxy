/*
 * Rauchtest wie im Spiel: Proxy per LoadLibrary laden (wie qgl in
 * Quake-3-Engines), Kontext erstellen, ein paar Frames zeichnen, mit
 * GDI-SwapBuffers praesentieren und das Log pruefen.
 *
 * Ohne OpenGL-Kontext (z. B. Dienst ohne Desktop) endet er mit "SKIP" und
 * Exitcode 0, ausser --require-gl wird uebergeben.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 10

static HMODULE gl;
#define LOAD(ret, name, args) ret (WINAPI *q##name) args = (ret (WINAPI *) args)(void *)GetProcAddress(gl, #name)

static int skip(int require, const char *why)
{
    printf("%s: %s (%lu)\n", require ? "FEHLER" : "SKIP", why, GetLastError());
    return require ? 1 : 0;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    buf[fread(buf, 1, n, f)] = 0;
    fclose(f);
    return buf;
}

int main(int argc, char **argv)
{
    int require = argc > 1 && !strcmp(argv[1], "--require-gl");
    char dir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    *strrchr(dir, '\\') = 0;

    /* Capture in Frame 3, Zusammenfassung alle 5 Frames, keine Taste. */
    snprintf(path, sizeof path, "%s\\fakk2_glproxy.ini", dir);
    FILE *ini = fopen(path, "w");
    fputs("[log]\nsummary_interval=5\ncapture_key=0\ncapture_frame=3\n", ini);
    fclose(ini);

    gl = LoadLibraryA("opengl32.dll");
    if (!gl)
        return skip(1, "opengl32.dll nicht ladbar");

    LOAD(HGLRC, wglCreateContext, (HDC));
    LOAD(BOOL, wglMakeCurrent, (HDC, HGLRC));
    LOAD(BOOL, wglDeleteContext, (HGLRC));
    LOAD(PROC, wglGetProcAddress, (LPCSTR));
    LOAD(void, glClear, (GLbitfield));
    LOAD(void, glBegin, (GLenum));
    LOAD(void, glEnd, (void));
    LOAD(void, glVertex3f, (GLfloat, GLfloat, GLfloat));
    LOAD(void, glEnableClientState, (GLenum));
    LOAD(void, glVertexPointer, (GLint, GLenum, GLsizei, const GLvoid *));
    LOAD(void, glDrawArrays, (GLenum, GLint, GLsizei));
    LOAD(void, glBindTexture, (GLenum, GLuint));
    LOAD(void, glFinish, (void));

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "fakk2_glproxy_smoke";
    wc.style = CS_OWNDC;
    RegisterClassA(&wc);
    HWND wnd = CreateWindowA(wc.lpszClassName, "smoke", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                             NULL, NULL, wc.hInstance, NULL);
    if (!wnd)
        return skip(require, "CreateWindow fehlgeschlagen");
    HDC dc = GetDC(wnd);

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd))
        return skip(require, "kein Pixelformat");
    HGLRC rc = qwglCreateContext(dc);
    if (!rc || !qwglMakeCurrent(dc, rc))
        return skip(require, "kein GL-Kontext");

    qwglGetProcAddress("glLockArraysEXT");

    static const GLfloat tri[] = { -1, -1, 0,  1, -1, 0,  0, 1, 0 };
    qglEnableClientState(GL_VERTEX_ARRAY);
    qglVertexPointer(3, GL_FLOAT, 0, tri);

    for (int i = 0; i < FRAMES; i++) {
        qglClear(GL_COLOR_BUFFER_BIT);
        qglBindTexture(GL_TEXTURE_2D, 0);
        qglBegin(GL_TRIANGLES);
        qglVertex3f(-1, -1, 0);
        qglVertex3f(1, -1, 0);
        qglVertex3f(0, 1, 0);
        qglEnd();
        qglDrawArrays(GL_TRIANGLES, 0, 3);
        qglFinish();
        SwapBuffers(dc); /* GDI, so wie Quake-3-Engines praesentieren */
    }

    qwglMakeCurrent(NULL, NULL);
    qwglDeleteContext(rc);
    ReleaseDC(wnd, dc);
    DestroyWindow(wnd);
    /* Kein FreeLibrary-Test: GDI haelt selbst eine Referenz auf opengl32.dll,
     * die Abschlusszeile "Beendet nach ..." erscheint erst bei Prozessende. */

    snprintf(path, sizeof path, "%s\\fakk2_glproxy.log", dir);
    char *log = read_file(path);
    if (!log)
        return skip(1, "Log fehlt");

    static const char *const expect[] = {
        "Hooks installiert",
        "wglCreateContext(",
        "GL_RENDERER=",
        "wglGetProcAddress(\"glLockArraysEXT\")",
        "=== Capture Start Frame 3 ===",
        "  glBegin(GL_TRIANGLES)",
        "  glDrawArrays(GL_TRIANGLES, 0, 3)",
        "=== Capture Ende Frame 3 ===",
        "Frames 1-5 (5): pro Frame 1 glBegin [TRI 1,",
        "Frames 6-10 (5)",
    };
    int bad = 0;
    for (size_t i = 0; i < sizeof expect / sizeof expect[0]; i++) {
        if (!strstr(log, expect[i])) {
            printf("FEHLER: im Log fehlt \"%s\"\n", expect[i]);
            bad++;
        }
    }
    /* Capture darf nur einen Frame umfassen. */
    const char *first = strstr(log, "  glBegin(");
    if (first && strstr(first + 1, "  glBegin(") != NULL) {
        printf("FEHLER: Capture hat mehr als einen Frame erfasst\n");
        bad++;
    }
    if (bad)
        printf("--- Log ---\n%s\n", log);
    puts(bad ? "smoke_gl: FEHLGESCHLAGEN" : "smoke_gl: OK");
    return bad ? 1 : 0;
}
