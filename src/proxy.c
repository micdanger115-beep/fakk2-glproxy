/*
 * Laden der System-opengl32.dll und Weiterleitungs-Thunks fuer alle Exporte.
 */
#include "proxy.h"
#include "log.h"

#if !defined(_M_IX86) && !defined(__i386__)
#error "fakk2-glproxy muss als 32-Bit-x86-DLL gebaut werden (F.A.K.K. 2 ist ein 32-Bit-Spiel)."
#endif

HMODULE proxy_self;
static HMODULE real_dll;
static volatile LONG proxy_ready;
static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

/* real_<name>: echte Funktion; disp_<name>: Sprungziel des Thunks. */
#define X(name, argbytes) PROC real_##name; PROC disp_##name;
#include "gl_exports.inc"
#undef X

void proxy_path_in_dll_dir(const wchar_t *file, wchar_t *out, DWORD out_len)
{
    DWORD n = GetModuleFileNameW(proxy_self, out, out_len);
    wchar_t *slash = NULL;
    for (DWORD i = 0; i < n; i++)
        if (out[i] == L'\\' || out[i] == L'/')
            slash = &out[i];
    if (slash)
        slash[1] = 0;
    else
        out[0] = 0;
    lstrcatW(out, file);
}

static void fatal(const char *what, const char *detail)
{
    char msg[512];
    wsprintfA(msg, "fakk2-glproxy: %s\n%s", what, detail ? detail : "");
    log_printf("FATAL: %s %s", what, detail ? detail : "");
    log_close();
    MessageBoxA(NULL, msg, "fakk2-glproxy", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

/* Ersatz fuer Exporte, die die System-DLL nicht hat: geben 0 zurueck und
 * entfernen wie stdcall-Funktionen ihre Argumente vom Stack. */
#if defined(_MSC_VER)
#define X(name, argbytes)                                    \
    static __declspec(naked) void __cdecl missing_##name(void) \
    {                                                        \
        __asm xor eax, eax                                   \
        __asm ret argbytes                                   \
    }
#else
#define X(name, argbytes)                                    \
    __attribute__((naked)) static void missing_##name(void)  \
    {                                                        \
        __asm__ volatile("xorl %eax, %eax\n\t"               \
                         "ret $" #argbytes);                 \
    }
#endif
#include "gl_exports.inc"
#undef X

static BOOL CALLBACK do_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once; (void)param; (void)ctx;
    wchar_t path[MAX_PATH];

    log_open();
    log_printf("fakk2-glproxy " FAKK2_GLPROXY_VERSION " geladen");

    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 16)
        fatal("GetSystemDirectory fehlgeschlagen", NULL);
    lstrcatW(path, L"\\opengl32.dll");

    /* Voller Pfad: so laedt Windows die echte DLL, nicht diese hier erneut.
     * Bei einem 32-Bit-Prozess leitet WOW64 System32 auf SysWOW64 um. */
    real_dll = LoadLibraryW(path);
    if (!real_dll)
        fatal("System-opengl32.dll konnte nicht geladen werden", NULL);
    if (real_dll == proxy_self)
        fatal("Proxy hat sich selbst geladen", NULL);
    log_printf("echte opengl32.dll: %ls", path);

    int missing = 0;
#define X(name, argbytes)                                           \
    real_##name = GetProcAddress(real_dll, #name);                  \
    if (!real_##name) {                                             \
        /* z. B. Glmf* unter Wine: Stub liefert 0 und raeumt den    \
         * Stack korrekt ab. */                                     \
        log_printf("Warnung: %s fehlt in der System-DLL, Stub aktiv", #name); \
        real_##name = (PROC)missing_##name;                         \
        missing++;                                                  \
    }                                                               \
    disp_##name = real_##name;
#include "gl_exports.inc"
#undef X
    if (missing > 50)
        fatal("Die geladene opengl32.dll sieht nicht nach OpenGL aus", NULL);

    hooks_install();
    InterlockedExchange(&proxy_ready, 1);
    return TRUE;
}

void __cdecl proxy_ensure_init(void)
{
    /* Wird vor jedem weitergeleiteten Aufruf ausgefuehrt. Darf nur eax, ecx,
     * edx und Flags veraendern (cdecl), damit die stdcall-Argumente des
     * Spiels unangetastet auf dem Stack liegen bleiben. */
    if (proxy_ready)
        return;
    InitOnceExecuteOnce(&init_once, do_init, NULL, NULL);
}

/*
 * Thunks. Exportiert als proxy_<name> und per .def-Datei auf <name> abgebildet.
 * Sie haben keinen eigenen Stackrahmen: call proxy_ensure_init, dann jmp auf
 * disp_<name>. Die Zielfunktion raeumt ihre stdcall-Argumente selbst ab.
 */
#if defined(_MSC_VER)
#define X(name, argbytes)                                    \
    __declspec(naked) void __cdecl proxy_##name(void)        \
    {                                                        \
        __asm call proxy_ensure_init                         \
        __asm jmp dword ptr [disp_##name]                    \
    }
#elif defined(__GNUC__)
#define X(name, argbytes)                                    \
    __attribute__((naked)) void proxy_##name(void)           \
    {                                                        \
        __asm__ volatile("call _proxy_ensure_init\n\t"       \
                         "jmp *_disp_" #name);               \
    }
#else
#error "Nicht unterstuetzter Compiler"
#endif
#include "gl_exports.inc"
#undef X

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        /* Kein LoadLibrary hier (Loader-Lock); die echte DLL wird beim
         * ersten GL-Aufruf geladen. */
        proxy_self = inst;
        DisableThreadLibraryCalls(inst);
        break;
    case DLL_PROCESS_DETACH:
        if (proxy_ready)
            hooks_shutdown();
        log_close();
        /* Bei Prozessende (reserved != NULL) nichts mehr freigeben. */
        if (!reserved && real_dll)
            FreeLibrary(real_dll);
        break;
    }
    return TRUE;
}
