#include <litehtml.h>
#include <velo/litehtml_bridge.h>
#include <string.h>
#include <stdlib.h>
#include <functional>

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

    litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override {
        (void)descr; (void)doc;
        if (fm) {
            int s = (int)(float)descr.size;
            if (s <= 0) s = 14;
            fm->ascent = s;
            fm->descent = 3;
            fm->height = s + 4;
            fm->x_height = s / 2;
        }
        return 1;
    }

    void delete_font(litehtml::uint_ptr hFont) override { (void)hFont; }

    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override {
        (void)hFont;
        if (!text) return 0;
        return (int)strlen(text) * 8;
    }

    void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override {
        (void)hdc; (void)hFont;
        if (!text || !g_text_cb) return;

        unsigned int col = ((unsigned int)color.red << 16) | ((unsigned int)color.green << 8) | (unsigned int)color.blue;
        int x = (int)(float)pos.x + m_offset_x;
        int y = (int)(float)pos.y + m_offset_y;

        if (y >= m_clip_y1 - 16 && y <= m_clip_y2) {
            g_text_cb(text, x, y, col);
        }
    }

    litehtml::pixel_t pt_to_px(float pt) const override { return (pt * 96.0f) / 72.0f; }
    litehtml::pixel_t get_default_font_size() const override { return 14; }
    const char* get_default_font_name() const override { return "VeloSans"; }

    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker& marker) override {
        (void)hdc;
        if (!g_text_cb) return;
        int x = (int)(float)marker.pos.x + m_offset_x;
        int y = (int)(float)marker.pos.y + m_offset_y;
        if (y >= m_clip_y1 && y <= m_clip_y2) {
            g_text_cb("*", x, y, 0x002563EB);
        }
    }

    void load_image(const char* src, const char* baseurl, bool redraw_on_ready) override {
        (void)src; (void)baseurl; (void)redraw_on_ready;
    }

    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override {
        (void)src; (void)baseurl;
        sz.width = 0;
        sz.height = 0;
    }

    void draw_image(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const std::string& url, const std::string& baseurl) override {
        (void)hdc; (void)layer; (void)url; (void)baseurl;
    }

    void draw_solid_fill(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::web_color& color) override {
        (void)hdc;
        if (!g_rect_cb) return;
        int x = (int)(float)layer.border_box.x + m_offset_x;
        int y = (int)(float)layer.border_box.y + m_offset_y;
        int w = (int)(float)layer.border_box.width;
        int h = (int)(float)layer.border_box.height;
        unsigned int col = ((unsigned int)color.red << 16) | ((unsigned int)color.green << 8) | (unsigned int)color.blue;
        g_rect_cb(x, y, w, h, col);
    }

    void draw_linear_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::linear_gradient& grad) override {
        (void)hdc; (void)layer; (void)grad;
    }

    void draw_radial_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::radial_gradient& grad) override {
        (void)hdc; (void)layer; (void)grad;
    }

    void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer, const litehtml::background_layer::conic_gradient& grad) override {
        (void)hdc; (void)layer; (void)grad;
    }

    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders& borders, const litehtml::position& draw_pos, bool root) override {
        (void)hdc; (void)root;
        if (!g_rect_cb) return;

        int x = (int)(float)draw_pos.x + m_offset_x;
        int y = (int)(float)draw_pos.y + m_offset_y;
        int w = (int)(float)draw_pos.width;
        int h = (int)(float)draw_pos.height;

        int bw = (int)(float)borders.bottom.width;
        if (bw > 0) {
            unsigned int col = ((unsigned int)borders.bottom.color.red << 16) | ((unsigned int)borders.bottom.color.green << 8) | (unsigned int)borders.bottom.color.blue;
            g_rect_cb(x, y + h - bw, w, bw, col);
        }
    }

    void set_caption(const char* caption) override { (void)caption; }
    void set_base_url(const char* base_url) override { (void)base_url; }
    void link(const std::shared_ptr<litehtml::document>& doc, const litehtml::element::ptr& el) override { (void)doc; (void)el; }

    void on_anchor_click(const char* url, const litehtml::element::ptr& el) override {
        (void)el;
        if (url && g_link_cb) g_link_cb(url);
    }

    void on_mouse_event(const litehtml::element::ptr& el, litehtml::mouse_event event) override {
        (void)el; (void)event;
    }

    void set_cursor(const char* cursor) override { (void)cursor; }
    void transform_text(std::string& text, litehtml::text_transform tt) override { (void)text; (void)tt; }
    void import_css(std::string& text, const std::string& url, std::string& baseurl) override { (void)text; (void)url; (void)baseurl; }
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& bdr_radius) override { (void)pos; (void)bdr_radius; }
    void del_clip() override {}

    void get_viewport(litehtml::position& viewport) const override {
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = 800;
        viewport.height = 600;
    }

    litehtml::element::ptr create_element(const char* tag_name, const litehtml::string_map& attributes, const std::shared_ptr<litehtml::document>& doc) override {
        (void)tag_name; (void)attributes; (void)doc;
        return nullptr;
    }

    void get_language(std::string& language, std::string& culture) const override {
        language = "de";
        culture = "DE";
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
    if (!*d) return 0;
    return (int)(float)(*d)->height();
}

int velo_litehtml_mouse_click(litehtml_doc_t doc, int doc_x, int doc_y, int client_x, int client_y) {
    if (!doc) return 0;
    litehtml::document::ptr *d = (litehtml::document::ptr*)doc;
    if (*d) {
        auto noop = [](const litehtml::position&) {};
        (*d)->on_lbutton_down(doc_x, doc_y, client_x, client_y, noop);
        return (*d)->on_lbutton_up(doc_x, doc_y, client_x, client_y, noop) ? 1 : 0;
    }
    return 0;
}

void velo_litehtml_destroy_document(litehtml_doc_t doc) {
    if (!doc) return;
    litehtml::document::ptr *d = (litehtml::document::ptr*)doc;
    delete d;
}

}