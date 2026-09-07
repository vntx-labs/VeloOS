#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("\033[1;36mVeloOS GNU/Linux Utilities (/bin & C:/BIN):\033[0m\n");
    printf("  \033[1;32mls [pfad]\033[0m            - Dateien & Ordner auflisten\n");
    printf("  \033[1;32mcat <datei>\033[0m          - Dateiinhalt ausgeben\n");
    printf("  \033[1;32mtouch <datei>\033[0m        - Leere Datei anlegen\n");
    printf("  \033[1;32mmkdir <ordner>\033[0m       - Verzeichnis erstellen\n");
    printf("  \033[1;32mrm <datei>\033[0m           - Datei loeschen\n");
    printf("  \033[1;32mcp <src> <dst>\033[0m       - Datei kopieren\n");
    printf("  \033[1;32mmv <src> <dst>\033[0m       - Datei verschieben/umbenennen\n");
    printf("  \033[1;32mecho [text]\033[0m          - Text ausgeben\n");
    printf("  \033[1;32mgrep <muster> <dat>\033[0m  - Zeilen durchsuchen\n");
    printf("  \033[1;32mhead [-n N] <dat>\033[0m    - Erste N Zeilen ausgeben\n");
    printf("  \033[1;32mtail [-n N] <dat>\033[0m    - Letzte N Zeilen ausgeben\n");
    printf("  \033[1;32mwc <datei>\033[0m           - Zeilen, Woerter, Bytes zaehlen\n");
    printf("  \033[1;32mdf\033[0m                   - Festplattenplatz anzeigen\n");
    printf("  \033[1;32mfree\033[0m                 - RAM-Belegung anzeigen\n");
    printf("  \033[1;32mwhoami / hostname\033[0m    - Benutzer / PC-Name anzeigen\n");
    printf("  \033[1;32muname [-a]\033[0m           - Betriebssystem-Info anzeigen\n");
    printf("  \033[1;32mps / kill <pid>\033[0m      - Prozesse anzeigen / beenden\n");
    printf("  \033[1;32mreboot / shutdown\033[0m    - System neu starten oder herunterfahren\n");
    printf("  \033[1;32mcd / pwd / exit\033[0m      - Shell-Primitive\n");
    return 0;
}