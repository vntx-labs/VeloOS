#include <litehtml.h>
#include <velo/litehtml_bridge.h>
#include <string.h>
#include <stdlib.h>

// Globale Callback-Pointer zu C
static velo_draw_text_cb g_text_cb = nullptr;
static velo_draw_rect_cb g_rect_cb = nullptr;
static velo_on_link_cb   g_link_cb = nullptr;

class VeloCppContainer : public litehtml::document_container {
public:
    int m_offset_x = 0;
    int m_offset_y = 0;
    int m_clip_y1 = 0;
    int m_clip_y2 = 0;

    VeloCppContainer() {}

    litehtml::uint_ptr create_font(const char* faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration, litehtml::font_metrics* fm) override {
        (void)faceName; (void)weight; (void)italic; (void)decoration;
        if (fm) {
            int s = size ? size : 14;
            fm->ascent = s;
            fm->descent = 3;
            fm->height = s + 4;
            fm->x_height = s / 2;
        }
        return 1;
    }

    void delete_font(litehtml::uint_ptr hFont) override { (void)hFont; }

    int text_width(const char* text, litehtml::uint_ptr hFont) override {
        (void)hFont;
        if (!text) return 0;
        return (int)strlen(text) * 8;
    }

    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override {
        (void)hdc; (void)hFont;
        if (!text || !g_text_cb) return;

        unsigned int col = ((unsigned int)color.red << 16) | ((unsigned int)color.green << 8) | (unsigned int)color.blue;
        int x = pos.x + m_offset_x;
        int y = pos.y + m_offset_y;

        if (y >= m_clip_y1 - 16 && y <= m_clip_y2) {
            g_text_cb(text, x, y, col);
        }
    }

    int pt_to_px(int pt) const override { return (pt * 96) / 72; }
    int get_default_font_size() const override { return 14; }
    const char* get_default_font_name() const override { return "VeloSans"; }

    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override {
        (void)hdc;
        if (!g_text_cb) return;
        int x = marker.pos.x + m_offset_x;
        int y = marker.pos.y + m_offset_y;
        if (y >= m_clip_y1 && y <= m_clip_y2) {
            g_text_cb("*", x, y, 0x002563EB);
        }
    }

    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) override {
        (void)hdc; (void)root;
        if (!g_rect_cb) return;

        int x = draw_pos.x + m_offset_x;
        int y = draw_pos.y + m_offset_y;
        int w = draw_pos.width;
        int h = draw_pos.height;

        if (borders.bottom.width > 0) {
            unsigned int col = ((unsigned int)borders.bottom.color.red << 16) | ((unsigned int)borders.bottom.color.green << 8) | (unsigned int)borders.bottom.color.blue;
            g_rect_cb(x, y + h - borders.bottom.width, w, borders.bottom.width, col);
        }
    }

    void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override { (void)src; (void)baseurl; (void)redraw_on_ready; }
    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override { (void)src; (void)baseurl; sz.width = 0; sz.height = 0; }
    void draw_background(litehtml::uint_ptr hdc, const std::vector<litehtml::background_paint>& bg) override { (void)hdc; (void)bg; }

    void set_caption(const char* caption) override { (void)caption; }
    void set_base_url(const char* base_url) override { (void)base_url; }
    void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override { (void)doc; (void)el; }
    void on_anchor_click(const char* url, const litehtml::element::ptr& el) override {
        (void)el;
        if (url && g_link_cb) g_link_cb(url);
    }
    void set_cursor(const char* cursor) override { (void)cursor; }
    void transform_text(litehtml::string& text, litehtml::text_transform tt) override { (void)text; (void)tt; }
    void import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override { (void)text; (void)url; (void)baseurl; }
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override { (void)pos; (void)bdr_radius; }
    void del_clip() override {}
    void get_client_rect(litehtml::position& client) const override {
        client.x = 0;
        client.y = 0;
        client.width = 800;
        client.height = 600;
    }
    std::shared_ptr<litehtml::element> create_element(const char* tag_name, const litehtml::string_map& attributes, const std::shared_ptr<litehtml::document>& doc) override {
        (void)tag_name; (void)attributes; (void)doc;
        return nullptr;
    }
    void get_media_features(litehtml::media_features& media) const override {
        media.type = litehtml::media_type_screen;
        media.width = 800;
        media.height = 600;
        media.device_width = 800;
        media.device_height = 600;
        media.color = 8;
        media.resolution = 96;
    }
};

static VeloCppContainer g_cpp_container;

extern "C" {

void velo_litehtml_init(velo_draw_text_cb text_cb, velo_draw_rect_cb rect_cb, velo_on_link_cb link_cb) {
    g_text_cb = text_cb;
    g_rect_cb = rect_cb;
    g_link_cb = link_cb;
}

litehtml_doc_t velo_litehtml_create_document(const char *html_text) {
    if (!html_text || !html_text[0]) return nullptr;
    try {
        litehtml::document::ptr *doc = new litehtml::document::ptr();
        *doc = litehtml::document::createFromString(html_text, &g_cpp_container);
        return (litehtml_doc_t)doc;
    } catch (...) {
        return nullptr;
    }
}

void velo_litehtml_render(litehtml_doc_t doc, int max_width) {
    if (!doc) return;
    litehtml::document::ptr *d = (litehtml::document::ptr*)doc;
    if (*d) {
        (*d)->render(max_width);
    }
}

void velo_litehtml_draw(litehtml_doc_t doc, int offset_x, int offset_y, int clip_x, int clip_y, int clip_w, int clip_h) {
    if (!doc) return;
    litehtml::document::ptr *d = (litehtml::document::ptr*)doc;
    if (*d) {
        g_cpp_container.m_offset_x = offset_x;
        g_cpp_container.m_offset_y = offset_y;
        g_cpp_container.m_clip_y1 = clip_y;
        g_cpp_container.m_clip_y2 = clip_y + clip_h;

        litehtml::position clip(clip_x, clip_y, clip_w, clip_h);
        (*d)->draw((litehtml::uint_ptr)0, 0, 0, &clip);
    }
}

int velo_litehtml_get_height(litehtml_doc_t doc) {
    if (!doc) return 0;
    litehtml::document::ptr *d = (litehtml::document::ptr*)doc;
    return (*d) ? (*d)->height() : 0;
}

void velo_litehtml_destroy_document(litehtml_doc_t doc) {
    if (!doc) return;
    litehtml::document::ptr *d = (litehtml::document::ptr*)doc;
    delete d;
}

}