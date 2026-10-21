# fakk2-glproxy

Proxy-`opengl32.dll` für **Heavy Metal: F.A.K.K. 2** (2000, 32-Bit x86, OpenGL Fixed Function).

Die DLL wird neben `fakk2.exe` gelegt. Windows lädt sie dann statt der System-`opengl32.dll`;
sie lädt ihrerseits die echte DLL aus dem Systemordner und reicht alle 368 Exporte durch.
Ausgewählte Aufrufe laufen dabei durch Hooks und werden protokolliert. Das ist das Gerüst für
Stufe 2 des Plans: erst verstehen, wie der Renderer zeichnet, dann gezielt eingreifen
(eigene GLSL-Shader, Post-Processing, bessere Texturfilter).

Dieses Repo enthält **keine Spieldateien** und soll auch keine enthalten.

## Stand

- Alle Exporte der Windows-`opengl32.dll` (GL 1.1, `wgl*`, `Glmf*`) werden weitergeleitet.
- Die echte DLL wird erst beim ersten GL-Aufruf geladen, nicht in `DllMain`.
- Fehlt ein Export in der System-DLL (z. B. `Glmf*` unter Wine), springt ein Stub ein, der 0 liefert.
- Hooks mit Logging: `glBegin/glEnd`, `glVertex2f/3f/3fv`, `glDrawElements`, `glDrawArrays`,
  `glArrayElement`, `glBindTexture`, `glTexImage2D`, `glTexSubImage2D`, `glEnable/glDisable`,
  `glBlendFunc`, `glClear`, `wglCreateContext`, `wglMakeCurrent`, `wglGetProcAddress`,
  `wglSwapBuffers` sowie `glLockArraysEXT/glUnlockArraysEXT` (über `wglGetProcAddress`).

## Benutzung

1. `opengl32.dll` und `fakk2_glproxy.ini` aus einem Build (GitHub Actions → Artifacts) in den
   Spielordner neben `fakk2.exe` kopieren.
2. Spiel starten. Im Spielordner entsteht `fakk2_glproxy.log`.
3. Zum Entfernen beide Dateien wieder löschen.

Was im Log steht:

- Treiber (`GL_VENDOR`, `GL_RENDERER`, `GL_VERSION`) und angefragte Extensions.
- Jede Textur-Hochladung mit Größe und Format.
- Alle 300 Frames eine Zeile mit Durchschnittswerten pro Frame (Primitive, Draw-Calls,
  Texturwechsel, Zustandswechsel).
- **F11** schneidet den nächsten Frame komplett mit, jeder gehookte Aufruf mit Argumenten.

Einstellungen stehen kommentiert in [`fakk2_glproxy.ini`](fakk2_glproxy.ini).

Unter Wine/Proton muss Wine angewiesen werden, die DLL aus dem Spielordner zu nehmen:
`WINEDLLOVERRIDES="opengl32=n,b"`.

## Bauen

Das Ziel ist immer **32-Bit**, sonst lädt das Spiel die DLL nicht.

Windows mit Visual Studio 2019/2022:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
build\bin\Release\export_check.exe
```

Linux mit MinGW (`gcc-mingw-w64-i686`):

```sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
WINEDLLOVERRIDES="opengl32=n,b" wine build/bin/export_check.exe
```

GitHub Actions baut beide Varianten, testet die MinGW-Variante unter Wine mit Mesa (llvmpipe)
und stellt die DLLs als Artifacts bereit.

## Aufbau

| Datei | Inhalt |
|---|---|
| `src/gl_exports.inc` | Liste aller Exporte mit stdcall-Argumentbytes (`tools/gen_exports.sh`) |
| `src/proxy.c` | Laden der echten DLL, Thunks, `DllMain` |
| `src/hooks.c` | Hooks, Zähler, Frame-Capture, INI |
| `src/log.c` | Logdatei neben der DLL |
| `tests/export_check.c` | Prüft alle Exporte und das Laden der echten DLL |
| `tests/smoke_gl.c` | Erstellt einen GL-Kontext, zeichnet 10 Frames, prüft das Log |

So funktioniert die Weiterleitung: Jeder Export ist ein Thunk ohne eigenen Stackrahmen
(`call proxy_ensure_init; jmp [disp_<name>]`). `disp_<name>` zeigt auf die echte Funktion oder
auf einen Hook. Dadurch braucht die Weiterleitung keine Signaturen, nur die Hooks.

### Einen Hook hinzufügen

In `src/hooks.c`:

```c
static void APIENTRY hook_glFogf(GLenum pname, GLfloat param)
{
    CAP("glFogf(%s, %f)", enum_name(pname), param);
    REAL(glFogf, void (APIENTRY *)(GLenum, GLfloat))(pname, param);
}
```

und in `hooks_install()` die Zeile `HOOK(glFogf);` ergänzen.

## Nächste Schritte

- Mit dem Log und REA herausfinden, welche Pfade der Renderer für Welt, Modelle, Himmel und HUD nutzt.
- In `wglMakeCurrent` einen modernen Kontext-Zustand aufbauen (GL-Funktionen über `wglGetProcAddress`).
- Am Frame-Ende (`wglSwapBuffers`) einen Post-Processing-Durchlauf mit eigenem GLSL-Shader einhängen.
