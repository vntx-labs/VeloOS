#ifndef VIPER_H
#define VIPER_H

#include <stdint.h>
#include <velo/syscall.h>

#define MAX_ROADMAP_CMDS   1024
#define MAX_CONTAINERS     16
#define MAX_VIPER_METHODS  100 // Anti-Java-Garantie: Max 100 Funktionen pro Klasse
#define MAX_FILE_BUFFER    65536

#define WIDGET_NONE   0
#define WIDGET_WINDOW 1
#define WIDGET_BUTTON 2

typedef struct {
    int x, y, w, h;
    int active;
} VirtualContainer;

typedef struct {
    int  type;
    int  x, y, w, h;
    char text[64];
    char click_handler[64];
    int  is_pressed;
} ViperWidget;

typedef struct {
    char name[64];
    char code_block[8192];
} ViperMethodDef;

typedef struct {
    char           name[64];
    char           win_title[64];
    int            win_w, win_h;
    int            method_count;
    ViperMethodDef methods[MAX_VIPER_METHODS];
    ViperWidget    widgets[16];
    int            widget_count;
} ViperClassDef;

/* GUI-Map API */
void viper_roadmap_reset(void);
void viper_roadmap_rect(int x, int y, int w, int h, uint32_t color);
void viper_roadmap_text(int x, int y, const char *text, uint32_t color);
void viper_roadmap_flush(int win_id);

/* Virtuelle Koordinaten-Umleitung */
void viper_push_container(int x, int y, int w, int h);
void viper_pop_container(void);

/* Modul- & Ausfuehrungssystem */
int  viper_run_file(const char *filepath, int is_container, int cx, int cy);
void viper_trigger_method(const char *method_name);

#endif /* VIPER_H */