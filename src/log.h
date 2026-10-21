#ifndef FAKK2_GLPROXY_LOG_H
#define FAKK2_GLPROXY_LOG_H

#ifndef FAKK2_GLPROXY_VERSION
#define FAKK2_GLPROXY_VERSION "0.1.0"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Oeffnet fakk2_glproxy.log im Ordner der DLL (ueberschreibt eine alte Datei). */
void log_open(void);
void log_close(void);

/* Eine Zeile mit Zeitstempel (Millisekunden seit log_open). Threadsicher. */
void log_printf(const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#ifdef __cplusplus
}
#endif

#endif
