#ifndef VIPER_H
#define VIPER_H

#include <stdint.h>
#include <velo/syscall.h>

#define MAX_GUI_MAP_CMDS   1024
#define MAX_CONTAINERS     16
#define MAX_CLASSES        16
#define MAX_METHODS_PER_CL 100 // Anti-Java-Garantie: Max. 100 Methoden pro Klasse

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int active;
} VirtualContainer;

typedef struct {
    char name[48];
    void (*func_ptr)(void);
} ViperMethod;

typedef struct {
    char name[48];
    int method_count;
    ViperMethod methods[MAX_METHODS_PER_CL];
} ViperClass;

/* GUI-Map Funktionen für Ring 3 */
void viper_roadmap_reset(void);
void viper_roadmap_rect(int x, int y, int w, int h, uint32_t color);
void viper_roadmap_text(int x, int y, const char *text, uint32_t color);
void viper_roadmap_icon(int x, int y, int icon_type, int size);
void viper_roadmap_flush(int win_id);

/* Container-Offset Steuerung */
void viper_push_container(int x, int y, int w, int h);
void viper_pop_container(void);

/* Script-Engine & Lua Bridge */
void viper_init_runtime(void);
int  viper_execute_script(const char *source);

#endif /* VIPER_H */