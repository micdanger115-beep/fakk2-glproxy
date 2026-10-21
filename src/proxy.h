/*
 * fakk2-glproxy: Proxy-opengl32.dll fuer Heavy Metal: F.A.K.K. 2 (x86).
 *
 * Jeder der 368 Exporte ist ein kleiner Thunk, der ueber den Zeiger
 * disp_<name> springt. Nach dem Laden zeigt disp_<name> auf die echte
 * Funktion der System-opengl32.dll (real_<name>). Hooks ersetzen einzelne
 * disp_-Zeiger durch eigene Funktionen und rufen selbst real_<name> auf.
 */
#ifndef FAKK2_GLPROXY_PROXY_H
#define FAKK2_GLPROXY_PROXY_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define X(name, argbytes) extern PROC real_##name; extern PROC disp_##name;
#include "gl_exports.inc"
#undef X

/* Modul-Handle dieser Proxy-DLL (fuer Pfade von Log- und INI-Datei). */
extern HMODULE proxy_self;

/* Laedt beim ersten Aufruf die echte opengl32.dll und loest alle Exporte auf.
 * Wird von jedem Thunk vor dem Sprung aufgerufen; danach nur noch ein Flag-Test. */
void __cdecl proxy_ensure_init(void);

/* Pfad im Ordner dieser DLL, z. B. proxy_path_in_dll_dir(L"x.log", buf, n). */
void proxy_path_in_dll_dir(const wchar_t *file, wchar_t *out, DWORD out_len);

/* hooks.c */
void hooks_install(void);
void hooks_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
