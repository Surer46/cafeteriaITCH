#include "theme.hpp"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace app::frontend {

// Inicialización de la paleta oficial (frontend/design-system.md)
const wxColour Theme::ColorBackground    = wxColour(0xF1, 0xF5, 0xF9); // #F1F5F9 Slate 100
const wxColour Theme::ColorSurface       = wxColour(0xFF, 0xFF, 0xFF); // #FFFFFF Pure White
const wxColour Theme::ColorSurfaceSubtle = wxColour(0xF8, 0xFA, 0xFC); // #F8FAFC Slate 50
const wxColour Theme::ColorBorder        = wxColour(0xCB, 0xD5, 0xE1); // #CBD5E1 Slate 300
const wxColour Theme::ColorBorderSubtle  = wxColour(0xE2, 0xE8, 0xF0); // #E2E8F0 Slate 200
const wxColour Theme::ColorText          = wxColour(0x1F, 0x29, 0x37); // #1F2937 Gray 800
const wxColour Theme::ColorTextSecondary = wxColour(0x47, 0x55, 0x69); // #475569 Slate 600
const wxColour Theme::ColorTextMuted     = wxColour(0x94, 0xA3, 0xB8); // #94A3B8 Slate 400
const wxColour Theme::ColorBrand         = wxColour(0xB9, 0x1C, 0x1C); // #B91C1C Red 700
const wxColour Theme::ColorBrandDeep     = wxColour(0x99, 0x1B, 0x1B); // #991B1B Red 800 (Sidebar)
const wxColour Theme::ColorBrandSoft     = wxColour(0xFE, 0xF2, 0xF2); // #FEF2F2 Red 50
const wxColour Theme::ColorAccent        = wxColour(0xFA, 0xCC, 0x15); // #FACC15 Yellow 400
const wxColour Theme::ColorAccentText    = wxColour(0x78, 0x35, 0x0F); // #78350F Amber 900
const wxColour Theme::ColorSuccess       = wxColour(0x16, 0x65, 0x34); // #166534 Green 800 (Cobrar)
const wxColour Theme::ColorSuccessSoft   = wxColour(0xDC, 0xFC, 0xE7); // #DCFCE7 Green 100
const wxColour Theme::ColorError         = wxColour(0xB9, 0x1C, 0x1C); // #B91C1C Red 700
const wxColour Theme::ColorErrorSoft     = wxColour(0xFE, 0xE2, 0xE2); // #FEE2E2 Red 100
const wxColour Theme::ColorFocus         = wxColour(0x1D, 0x4E, 0xD8); // #1D4ED8 Blue 700

std::string Theme::assets_dir_ = "";
std::string Theme::data_dir_ = "";
UiProfile Theme::current_profile_ = UiProfile::Standard;
bool Theme::fonts_registered_ = false;
std::vector<std::string> Theme::registered_font_paths_;
std::map<Theme::IconKey, wxBitmapBundle> Theme::icon_cache_;

void Theme::init(const std::string& assets_dir) {
    if (!assets_dir.empty() && wxDirExists(wxString::FromUTF8(assets_dir.c_str()))) {
        assets_dir_ = assets_dir;
    } else {
        // Rutas candidatas usando wxString nativo para soportar acentos como en "Cafetería"
        std::vector<wxString> candidates;
        wxString exe_path = wxStandardPaths::Get().GetExecutablePath();
        wxFileName fn(exe_path);
        wxString exe_dir = fn.GetPath();

        candidates.push_back(exe_dir + "/assets");
        candidates.push_back(exe_dir + "/../assets");
        candidates.push_back(exe_dir + "/../../assets");
        candidates.push_back("assets");

        for (const auto& c : candidates) {
            if (wxDirExists(c)) {
                assets_dir_ = std::string(c.ToUTF8());
                break;
            }
        }
    }

#ifdef _WIN32
    if (!fonts_registered_ && !assets_dir_.empty()) {
        wxString fonts_dir = wxString::FromUTF8(assets_dir_.c_str()) + "/fonts";
        if (wxDirExists(fonts_dir)) {
            std::vector<wxString> font_files = {
                fonts_dir + "/Roboto-Regular.ttf",
                fonts_dir + "/Roboto-Medium.ttf",
                fonts_dir + "/Roboto-Bold.ttf"
            };

            for (const auto& f : font_files) {
                if (wxFileExists(f)) {
                    int added = AddFontResourceExW(f.wc_str(), FR_PRIVATE, 0);
                    if (added > 0) {
                        registered_font_paths_.push_back(std::string(f.ToUTF8()));
                    }
                }
            }
            fonts_registered_ = !registered_font_paths_.empty();
        }
    }
#endif
}

void Theme::cleanup() {
#ifdef _WIN32
    for (const auto& f : registered_font_paths_) {
        wxString wf = wxString::FromUTF8(f.c_str());
        RemoveFontResourceExW(wf.wc_str(), FR_PRIVATE, 0);
    }
    registered_font_paths_.clear();
    fonts_registered_ = false;
#endif
    icon_cache_.clear();
}

std::string Theme::get_assets_dir() {
    return assets_dir_;
}

void Theme::set_data_dir(const std::string& data_dir) {
    data_dir_ = data_dir;
    load_station_profile();
}

std::string Theme::get_data_dir() {
    return data_dir_;
}


void Theme::load_station_profile() {
    if (data_dir_.empty()) return;
    wxString cfg_file = wxString::FromUTF8(data_dir_.c_str()) + "/station_profile.cfg";
    if (wxFileExists(cfg_file)) {
        wxFile f(cfg_file, wxFile::read);
        if (f.IsOpened()) {
            wxString content;
            if (f.ReadAll(&content)) {
                content = content.Trim().Trim(false).Lower();
                if (content == "touch") {
                    current_profile_ = UiProfile::Touch;
                } else if (content == "standard") {
                    current_profile_ = UiProfile::Standard;
                }
            }
        }
    }
}

void Theme::save_station_profile(UiProfile profile) {
    current_profile_ = profile;
    if (data_dir_.empty()) return;
    wxString cfg_file = wxString::FromUTF8(data_dir_.c_str()) + "/station_profile.cfg";
    wxFile f(cfg_file, wxFile::write);
    if (f.IsOpened()) {
        f.Write(profile == UiProfile::Touch ? "touch\n" : "standard\n");
    }
}

UiProfile Theme::get_profile() {
    return current_profile_;
}

void Theme::set_profile(UiProfile profile) {
    save_station_profile(profile);
}

bool Theme::is_touch() {
    return current_profile_ == UiProfile::Touch;
}

int Theme::get_spacing_xs() { return is_touch() ? 6 : 4; }
int Theme::get_spacing_sm() { return is_touch() ? 12 : 8; }
int Theme::get_spacing_md() { return is_touch() ? 16 : 12; }
int Theme::get_spacing_lg() { return is_touch() ? 20 : 16; }
int Theme::get_spacing_xl() { return is_touch() ? 28 : 24; }

int Theme::get_card_radius() { return is_touch() ? 16 : 12; }
int Theme::get_button_radius() { return is_touch() ? 12 : 8; }
int Theme::get_control_radius() { return is_touch() ? 8 : 6; }
int Theme::get_button_height() { return is_touch() ? 48 : 36; }
int Theme::get_primary_button_height() { return is_touch() ? 56 : 42; }
int Theme::get_touch_target_min() { return is_touch() ? 48 : 40; }

wxFont Theme::get_font(int size_pt, wxFontWeight weight, bool italic) {
    wxFont font(wxFontInfo(size_pt).Family(wxFONTFAMILY_SWISS).FaceName("Roboto").Weight(weight).Italic(italic));
    if (!font.IsOk()) {
        font = wxFont(wxFontInfo(size_pt).Family(wxFONTFAMILY_SWISS).Weight(weight).Italic(italic));
    }
    return font;
}

wxFont Theme::get_font_small(wxFontWeight weight) {
    return get_font(is_touch() ? 10 : 9, weight);
}

wxFont Theme::get_font_body(wxFontWeight weight) {
    return get_font(is_touch() ? 12 : 11, weight);
}

wxFont Theme::get_font_subtitle(wxFontWeight weight) {
    return get_font(is_touch() ? 14 : 13, weight);
}

wxFont Theme::get_font_title(wxFontWeight weight) {
    return get_font(is_touch() ? 18 : 16, weight);
}

wxFont Theme::get_font_hero(wxFontWeight weight) {
    return get_font(is_touch() ? 24 : 22, weight);
}

wxBitmapBundle Theme::get_icon(const std::string& icon_name, const wxSize& size, const wxColour& tint_color) {
    unsigned long color_val = tint_color.IsOk() ? tint_color.GetRGB() : 0xFFFFFFFF;
    IconKey key{icon_name, size.GetWidth(), size.GetHeight(), color_val};

    auto it = icon_cache_.find(key);
    if (it != icon_cache_.end()) {
        return it->second;
    }

    std::string filename = icon_name;
    if (filename.find(".svg") == std::string::npos) {
        filename += ".svg";
    }

    wxString full_path = wxString::FromUTF8(assets_dir_.c_str()) + "/icons/" + wxString::FromUTF8(filename.c_str());
    if (!wxFileExists(full_path)) {
        return wxBitmapBundle();
    }

    wxFile file(full_path, wxFile::read);
    if (!file.IsOpened()) {
        return wxBitmapBundle();
    }

    wxString svg_wx_str;
    if (!file.ReadAll(&svg_wx_str)) {
        return wxBitmapBundle();
    }
    std::string svg_data = std::string(svg_wx_str.ToUTF8());

    if (tint_color.IsOk()) {
        char hex[16];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                 tint_color.Red(), tint_color.Green(), tint_color.Blue());
        std::string fill_attr = std::string("fill=\"") + hex + "\" ";
        
        // Insertar fill en etiqueta <path
        size_t path_pos = svg_data.find("<path ");
        if (path_pos != std::string::npos) {
            svg_data.insert(path_pos + 6, fill_attr);
        } else {
            size_t svg_pos = svg_data.find("<svg ");
            if (svg_pos != std::string::npos) {
                svg_data.insert(svg_pos + 5, fill_attr);
            }
        }
    }

    wxBitmapBundle bundle = wxBitmapBundle::FromSVG(svg_data.c_str(), size);
    if (bundle.IsOk()) {
        icon_cache_[key] = bundle;
    }
    return bundle;
}

void Theme::style_button_primary(wxButton* btn, int min_height) {
    if (!btn) return;
    int h = min_height > 0 ? min_height : get_button_height();
    btn->SetBackgroundColour(ColorBrand);
    btn->SetForegroundColour(ColorSurface);
    btn->SetFont(get_font_body(wxFONTWEIGHT_BOLD));
    btn->SetMinSize(wxSize(btn->GetMinSize().GetWidth() > 0 ? btn->GetMinSize().GetWidth() : -1, h));
}

void Theme::style_button_success(wxButton* btn, int min_height) {
    if (!btn) return;
    int h = min_height > 0 ? min_height : get_primary_button_height();
    btn->SetBackgroundColour(ColorSuccess);
    btn->SetForegroundColour(ColorSurface);
    btn->SetFont(get_font_subtitle(wxFONTWEIGHT_BOLD));
    btn->SetMinSize(wxSize(btn->GetMinSize().GetWidth() > 0 ? btn->GetMinSize().GetWidth() : -1, h));
}

void Theme::style_button_secondary(wxButton* btn, int min_height) {
    if (!btn) return;
    int h = min_height > 0 ? min_height : get_button_height();
    btn->SetBackgroundColour(ColorSurface);
    btn->SetForegroundColour(ColorText);
    btn->SetFont(get_font_body(wxFONTWEIGHT_MEDIUM));
    btn->SetMinSize(wxSize(btn->GetMinSize().GetWidth() > 0 ? btn->GetMinSize().GetWidth() : -1, h));
}

void Theme::style_button_destructive(wxButton* btn, int min_height) {
    if (!btn) return;
    int h = min_height > 0 ? min_height : get_button_height();
    btn->SetBackgroundColour(ColorErrorSoft);
    btn->SetForegroundColour(ColorError);
    btn->SetFont(get_font_body(wxFONTWEIGHT_BOLD));
    btn->SetMinSize(wxSize(btn->GetMinSize().GetWidth() > 0 ? btn->GetMinSize().GetWidth() : -1, h));
}

void Theme::style_button_brand(wxButton* btn, int min_height) {
    if (!btn) return;
    int h = min_height > 0 ? min_height : get_button_height();
    btn->SetBackgroundColour(ColorBrandSoft);
    btn->SetForegroundColour(ColorBrand);
    btn->SetFont(get_font_body(wxFONTWEIGHT_BOLD));
    btn->SetMinSize(wxSize(btn->GetMinSize().GetWidth() > 0 ? btn->GetMinSize().GetWidth() : -1, h));
}

void Theme::setup_button_icon(wxButton* btn, const std::string& icon_name, const wxSize& size, const wxColour& tint) {
    if (!btn) return;
    wxBitmapBundle ico = get_icon(icon_name, size, tint);
    if (ico.IsOk()) {
        btn->SetBitmap(ico);
        btn->SetBitmapPosition(wxLEFT);
        btn->SetBitmapMargins(is_touch() ? 10 : 8, 0);
    }
}

// =============================================================================
// CardPanel
// =============================================================================

CardPanel::CardPanel(wxWindow* parent, wxWindowID id,
                     const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(bg_colour_);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &CardPanel::on_paint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

void CardPanel::set_corner_radius(int radius) {
    corner_radius_ = radius;
    Refresh();
}

void CardPanel::set_card_background(const wxColour& bg) {
    bg_colour_ = bg;
    SetBackgroundColour(bg);
    Refresh();
}

void CardPanel::set_card_border(const wxColour& border_col, int border_width) {
    border_colour_ = border_col;
    border_width_ = border_width;
    Refresh();
}

void CardPanel::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    
    wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::ColorBackground;
    dc.SetBackground(wxBrush(parent_bg));
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) return;

    wxSize sz = GetClientSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) {
        delete gc;
        return;
    }

    gc->SetBrush(wxBrush(bg_colour_));
    if (border_width_ > 0) {
        gc->SetPen(wxPen(border_colour_, border_width_));
    } else {
        gc->SetPen(*wxTRANSPARENT_PEN);
    }

    double offset = (border_width_ > 0) ? (border_width_ / 2.0) : 0.0;
    double w = sz.GetWidth() - (border_width_ > 0 ? border_width_ : 0);
    double h = sz.GetHeight() - (border_width_ > 0 ? border_width_ : 0);

    gc->DrawRoundedRectangle(offset, offset, w, h, corner_radius_);
    delete gc;
}

// =============================================================================
// StatusBadge
// =============================================================================

StatusBadge::StatusBadge(wxWindow* parent, const wxString& text, bool is_open)
    : wxPanel(parent, wxID_ANY), text_(text), is_open_(is_open)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(DoGetBestSize());
    Bind(wxEVT_PAINT, &StatusBadge::on_paint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

void StatusBadge::set_status(const wxString& text, bool is_open) {
    text_ = text;
    is_open_ = is_open;
    InvalidateBestSize();
    SetMinSize(DoGetBestSize());
    Refresh();
}

wxSize StatusBadge::DoGetBestSize() const {
    wxClientDC dc(const_cast<StatusBadge*>(this));
    dc.SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    wxSize textSize = dc.GetTextExtent(text_);
    int w = textSize.GetWidth() + 38;
    int min_h = Theme::is_touch() ? 34 : 28;
    int h = textSize.GetHeight() + 10;
    if (h < min_h) h = min_h;
    return wxSize(w, h);
}

void StatusBadge::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);

    wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::ColorBackground;
    dc.SetBackground(wxBrush(parent_bg));
    dc.Clear();

    wxSize sz = GetClientSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) {
        return;
    }

    wxColour bg = is_open_ ? Theme::ColorSuccessSoft : Theme::ColorBorderSubtle;
    wxColour border = is_open_ ? Theme::ColorSuccess : Theme::ColorBorder;
    wxColour text_col = is_open_ ? Theme::ColorSuccess : Theme::ColorTextSecondary;

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (gc) {
        gc->SetBrush(wxBrush(bg));
        gc->SetPen(wxPen(border, 1));

        double radius = sz.GetHeight() / 2.0;
        gc->DrawRoundedRectangle(0.5, 0.5, sz.GetWidth() - 1, sz.GetHeight() - 1, radius);

        // Indicador circular (dot)
        double dot_radius = 4.0;
        double dot_x = 12.0;
        double dot_y = sz.GetHeight() / 2.0;
        gc->SetBrush(wxBrush(text_col));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->DrawEllipse(dot_x - dot_radius, dot_y - dot_radius, dot_radius * 2, dot_radius * 2);

        delete gc;
    }

    // Renderizado de texto vía GDI DC nativo (100% confiable en Windows con fuentes privadas y PrintWindow)
    dc.SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    dc.SetTextForeground(text_col);
    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);

    wxSize textSize = dc.GetTextExtent(text_);
    int tx = 22;
    int ty = (sz.GetHeight() - textSize.GetHeight()) / 2;
    dc.DrawText(text_, tx, ty);
}

} // namespace app::frontend
