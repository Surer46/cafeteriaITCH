#include "src/frontend/ui_helpers.hpp"
#include "src/frontend/theme.hpp"
#include <algorithm>
#include <wx/imaglist.h>
#include <wx/settings.h>

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>
#endif

namespace app::frontend::ui {

namespace {
const char* const kFieldBoxName = "itch_text_field_box";
}

int header_text_width(wxListCtrl* list, const wxString& text) {
#ifdef __WXMSW__
    HWND hdr = ListView_GetHeader(static_cast<HWND>(list->GetHWND()));
    if (hdr) {
        HFONT font = reinterpret_cast<HFONT>(::SendMessageW(hdr, WM_GETFONT, 0, 0));
        HDC dc = ::GetDC(hdr);
        if (dc) {
            HGDIOBJ old = font ? ::SelectObject(dc, font) : nullptr;
            SIZE sz{0, 0};
            ::GetTextExtentPoint32W(dc, text.wc_str(), static_cast<int>(text.length()), &sz);
            if (old) ::SelectObject(dc, old);
            ::ReleaseDC(hdr, dc);
            return sz.cx;
        }
    }
#endif
    int w = 0, h = 0;
    list->GetTextExtent(text, &w, &h);
    return w;
}

int header_height(wxListCtrl* list) {
#ifdef __WXMSW__
    HWND hdr = ListView_GetHeader(static_cast<HWND>(list->GetHWND()));
    RECT r{0, 0, 0, 0};
    if (hdr && ::GetWindowRect(hdr, &r)) {
        return static_cast<int>(r.bottom - r.top);
    }
#endif
    return list->FromDIP(24);
}

bool has_vertical_scrollbar(wxListCtrl* list) {
#ifdef __WXMSW__
    return (::GetWindowLongPtr(static_cast<HWND>(list->GetHWND()), GWL_STYLE) & WS_VSCROLL) != 0;
#else
    (void)list;
    return false;
#endif
}

void distribute_list_columns(wxListCtrl* list, const std::vector<ColumnSpec>& columns, std::size_t flexible_index) {
    if (!list || columns.empty() || flexible_index >= columns.size()) return;

    // Ancho útil: área cliente SIN la barra vertical (visible ahora o necesaria con las filas actuales).
    const int sb_w = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, list);
    const wxSize client = list->GetClientSize();
    int full_w = client.GetWidth() + (has_vertical_scrollbar(list) ? sb_w : 0);
    if (full_w <= 50) {
        full_w = list->GetSize().GetWidth();
    }
    if (full_w <= 50) return;

    int row_h = list->FromDIP(Theme::is_touch() ? 42 : 20);
    const int rows = list->GetItemCount();
    if (rows > 0) {
        wxRect item_rect;
        if (list->GetItemRect(0, item_rect) && item_rect.GetHeight() > 0) {
            row_h = item_rect.GetHeight();
        }
    }
    const int rows_h = header_height(list) + rows * row_h;
    const bool needs_vbar = client.GetHeight() > 0 && rows_h > client.GetHeight();
    const int budget = full_w - (needs_vbar ? sb_w : 0) - 2;

    const int hpad = list->FromDIP(20);
    const std::size_t n = columns.size();
    std::vector<int> want(n), floor_w(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& c = columns[i];
        const int hdr = header_text_width(list, c.header) + hpad;
        const int floor_content = c.floor_content_px >= 0 ? c.floor_content_px : c.content_px;
        want[i] = std::max(hdr, c.content_px + c.content_pad + c.extra_px);
        floor_w[i] = std::max(hdr, floor_content + c.content_pad + c.extra_px);
    }
    const int flex_min = header_text_width(list, columns[flexible_index].header) + hpad;

    std::vector<int> width(n);
    int others = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i == flexible_index) continue;
        width[i] = want[i];
        others += width[i];
    }
    // Si no queda lugar para el encabezado de la flexible, las demás ceden hasta su contenido protegido.
    int deficit = flex_min - (budget - others);
    for (std::size_t i = 0; i < n && deficit > 0; ++i) {
        if (i == flexible_index) continue;
        const int give = std::min(deficit, width[i] - floor_w[i]);
        if (give > 0) {
            width[i] -= give;
            deficit -= give;
        }
    }
    others = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i != flexible_index) others += width[i];
    }
    width[flexible_index] = std::max(flex_min, budget - others);

    for (std::size_t i = 0; i < n; ++i) {
        if (list->GetColumnWidth(static_cast<int>(i)) != width[i]) {
            list->SetColumnWidth(static_cast<int>(i), width[i]);
        }
    }
}

void apply_touch_row_height(wxListCtrl* list) {
    if (!list) return;
    list->AssignImageList(new wxImageList(1, 42), wxIMAGE_LIST_SMALL);
}

wxWindow* make_text_field(wxWindow* parent, wxTextCtrl*& out, int fixed_height, int fixed_width, const wxFont& font) {
    const wxFont use_font = font.IsOk() ? font : Theme::get_font_body();
    if (fixed_height <= 0) {
        out = new wxTextCtrl(parent, wxID_ANY);
        out->SetFont(use_font);
        return out;
    }

    auto* box = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(fixed_width, fixed_height), wxBORDER_SIMPLE);
    box->SetName(kFieldBoxName);
    box->SetBackgroundColour(Theme::ColorSurface);

    out = new wxTextCtrl(box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    out->SetFont(use_font);
    out->SetBackgroundColour(Theme::ColorSurface);

    // Alto del contenedor independiente del tema: nunca menor que el campo natural + 2.
    const int best_h = out->GetBestSize().GetHeight();
    const int box_h = std::max(fixed_height, best_h + 2);
    box->SetMinSize(wxSize(fixed_width, box_h));
    box->SetSize(wxSize(fixed_width, box_h));

    auto* sz = new wxBoxSizer(wxVERTICAL);
    sz->AddStretchSpacer(1);
    sz->Add(out, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
    sz->AddStretchSpacer(1);
    box->SetSizer(sz);

    wxTextCtrl* edit = out;
    box->Bind(wxEVT_LEFT_DOWN, [edit](wxMouseEvent&) { edit->SetFocus(); });
    return box;
}

void set_text_field_enabled(wxTextCtrl* edit, bool enabled) {
    if (!edit) return;
    edit->Enable(enabled);
    wxWindow* parent = edit->GetParent();
    if (!parent || parent->GetName() != kFieldBoxName) return;
    // El campo deshabilitado se pinta con el color de control del sistema: el contenedor lo sigue
    // para que no se vea un recuadro gris dentro de un contenedor blanco.
    const wxColour bg = enabled ? Theme::ColorSurface : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);
    edit->SetBackgroundColour(bg);
    parent->SetBackgroundColour(bg);
    edit->Refresh();
    parent->Refresh();
}

} // namespace app::frontend::ui
