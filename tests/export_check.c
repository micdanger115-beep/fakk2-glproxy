/*
 * Laedt die Proxy-opengl32.dll aus dem eigenen Ordner und prueft:
 *  - alle 368 Exporte sind vorhanden und liegen im Proxy-Modul,
 *  - ein erster Aufruf laedt die System-opengl32.dll,
 *  - fakk2_glproxy.log wird angelegt.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *const names[] = {
#define X(name, argbytes) #name,
#include "gl_exports.inc"
#undef X
};

int main(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    *strrchr(dir, '\\') = 0;
    snprintf(path, sizeof path, "%s\\opengl32.dll", dir);

    HMODULE proxy = LoadLibraryA(path);
    if (!proxy) {
        printf("FEHLER: %s nicht ladbar (%lu)\n", path, GetLastError());
        return 1;
    }
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)proxy;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)proxy + dos->e_lfanew);
    char *lo = (char *)proxy, *hi = lo + nt->OptionalHeader.SizeOfImage;

    int count = (int)(sizeof names / sizeof names[0]), bad = 0;
    for (int i = 0; i < count; i++) {
        char *p = (char *)GetProcAddress(proxy, names[i]);
        if (!p || p < lo || p >= hi) {
            printf("FEHLER: Export %s %s\n", names[i], p ? "zeigt aus dem Modul heraus" : "fehlt");
            bad++;
        }
    }
    printf("%d/%d Exporte ok\n", count - bad, count);

    /* Erster Aufruf ohne Kontext: initialisiert den Proxy, darf nicht abstuerzen. */
    typedef unsigned (WINAPI *PFN_GETERROR)(void);
    PFN_GETERROR get_error = (PFN_GETERROR)GetProcAddress(proxy, "glGetError");
    printf("glGetError() = 0x%X\n", get_error());

    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    strcat(sys, "\\opengl32.dll");
    HMODULE real = GetModuleHandleA(sys);
    if (!real || real == proxy) {
        printf("FEHLER: System-opengl32.dll wurde nicht geladen\n");
        bad++;
    }

    FreeLibrary(proxy);

    snprintf(path, sizeof path, "%s\\fakk2_glproxy.log", dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        printf("FEHLER: %s fehlt\n", path);
        bad++;
    }
    puts(bad ? "export_check: FEHLGESCHLAGEN" : "export_check: OK");
    return bad ? 1 : 0;
}
