#ifndef _VELO_LITEHTML_BRIDGE_H
#define _VELO_LITEHTML_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* litehtml_doc_t;

typedef void (*velo_draw_text_cb)(const char *text, int x, int y, unsigned int color);
typedef void (*velo_draw_rect_cb)(int x, int y, int w, int h, unsigned int color);
typedef void (*velo_on_link_cb)(const char *url);

void            velo_litehtml_init(velo_draw_text_cb text_cb, velo_draw_rect_cb rect_cb, velo_on_link_cb link_cb);
litehtml_doc_t  velo_litehtml_create_document(const char *html_text);
void            velo_litehtml_render(litehtml_doc_t doc, int max_width);
void            velo_litehtml_draw(litehtml_doc_t doc, int offset_x, int offset_y, int clip_x, int clip_y, int clip_w, int clip_h);
void            velo_litehtml_destroy_document(litehtml_doc_t doc);
int             velo_litehtml_get_height(litehtml_doc_t doc);
int             velo_litehtml_mouse_click(litehtml_doc_t doc, int doc_x, int doc_y, int client_x, int client_y);

#ifdef __cplusplus
}
#endif

#endif