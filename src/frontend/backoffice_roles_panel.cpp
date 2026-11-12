#include "src/frontend/backoffice_roles_panel.hpp"
#include "src/frontend/theme.hpp"
#include <algorithm>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <sstream>
#include <vector>

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>
#endif

namespace app::frontend {

namespace {

std::string get_module_display_name(const std::string& mod) {
    if (mod == "pos") return "Punto de Venta";
    if (mod == "inventory") return "Inventario y Almacén";
    if (mod == "shift") return "Caja y Turnos";
    if (mod == "audit") return "Auditoría y Bitácora";
    if (mod == "reports") return "Reportes Operativos";
    if (mod == "users") return "Usuarios y Cuentas";
    if (mod == "roles") return "Roles y Permisos";
    if (mod == "config") return "Configuración del Negocio";
    if (mod == "backup") return "Respaldos";
    std::string s = mod;
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(s[0]));
    return s;
}

void apply_checkbox_top_alignment(wxCheckBox* cb) {
#ifdef __WXMSW__
    if (!cb) return;
    HWND hwnd = static_cast<HWND>(cb->GetHWND());
    if (hwnd) {
        LONG_PTR st = ::GetWindowLongPtr(hwnd, GWL_STYLE);
        st &= ~BS_VCENTER;
        st |= BS_TOP | BS_MULTILINE;
        ::SetWindowLongPtr(hwnd, GWL_STYLE, st);
        ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
#endif
}

// Ancho de un texto con la fuente REAL del encabezado de la lista (en Windows el encabezado
// nativo no hereda la fuente de la lista). Fuera de Windows se usa la fuente de la lista.
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

std::string wrap_text_to_width(wxWindow* win, const std::string& text, int max_width_px) {
    if (max_width_px <= 80) max_width_px = 260;
    wxString wx_text = wxString::FromUTF8(text.c_str());
    int total_w = 0, total_h = 0;
    if (win) {
        win->GetTextExtent(wx_text, &total_w, &total_h);
    }
    if (total_w <= max_width_px && total_w > 0) {
        return text;
    }

    std::vector<std::string> words;
    std::string word;
    std::istringstream iss(text);
    while (iss >> word) {
        words.push_back(word);
    }
    if (words.empty()) return text;

    std::string result;
    std::string current_line;
    for (const auto& w : words) {
        std::string test_line = current_line.empty() ? w : (current_line + " " + w);
        int tw = 0, th = 0;
        if (win) {
            win->GetTextExtent(wxString::FromUTF8(test_line.c_str()), &tw, &th);
        }
        if (tw > max_width_px && !current_line.empty()) {
            if (!result.empty()) result += "\n";
            result += current_line;
            current_line = w;
        } else {
            current_line = test_line;
        }
    }
    if (!current_line.empty()) {
        if (!result.empty()) result += "\n";
        result += current_line;
    }
    return result;
}

} // namespace

BackofficeRolesPanel::BackofficeRolesPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::IAccountService> account_service
) : wxPanel(parent, wxID_ANY), account_service_(std::move(account_service)) {
    can_manage_ = account_service_ ? account_service_->has_permission("roles.manage") : false;

    setup_ui();
    load_permissions();
    build_permission_matrix();
    refresh_roles();
}

void BackofficeRolesPanel::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    auto* root = new wxBoxSizer(wxVERTICAL);

    int btn_h = Theme::is_touch() ? 48 : 36;

    auto style_and_size_btn = [&](wxButton* btn, int h, const std::string& icon_name,
                                  const wxColour& tint, bool is_primary, bool is_destructive) {
        if (is_primary) {
            Theme::style_button_primary(btn, h);
        } else if (is_destructive) {
            Theme::style_button_destructive(btn, h);
        } else {
            Theme::style_button_secondary(btn, h);
        }
        if (!icon_name.empty()) {
            Theme::setup_button_icon(btn, icon_name, wxSize(18, 18), tint);
        }
        int tw = 0, th = 0;
        btn->GetTextExtent(btn->GetLabel(), &tw, &th);
        int icon_w = icon_name.empty() ? 0 : 18;
        int icon_m = icon_name.empty() ? 0 : (Theme::is_touch() ? 10 : 8) * 2;
        int bp = Theme::is_touch() ? 28 : 24;
        int w = tw + icon_w + icon_m + bp;
        wxSize best = btn->GetBestSize();
        if (best.GetWidth() > w) w = best.GetWidth();
        btn->SetMinSize(wxSize(w, h));
        btn->SetSize(wxSize(w, h));
    };

    // 1. Barra superior
    auto* top_bar = new wxBoxSizer(wxHORIZONTAL);

    auto* title = new wxStaticText(this, wxID_ANY, wxT("Roles y Permisos del Sistema"));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    top_bar->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    btn_new_role_ = new wxButton(this, wxID_ANY, wxT("Nuevo rol"));
    style_and_size_btn(btn_new_role_, btn_h, "add", Theme::ColorSurface, true, false);
    btn_new_role_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_new_role, this);
    btn_new_role_->Enable(can_manage_);
    top_bar->Add(btn_new_role_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    btn_refresh_ = new wxButton(this, wxID_ANY, wxT("Actualizar"));
    style_and_size_btn(btn_refresh_, btn_h, "", wxNullColour, false, false);
    btn_refresh_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_roles(); });
    top_bar->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);

    top_bar->AddStretchSpacer(1);

    lbl_top_status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_top_status_->SetFont(Theme::get_font_small());
    lbl_top_status_->SetForegroundColour(Theme::ColorTextSecondary);
    top_bar->Add(lbl_top_status_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    root->Add(top_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);

    // 2. Banner de aviso si no tiene permiso roles.manage
    pnl_warning_ = new wxPanel(this, wxID_ANY);
    pnl_warning_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* warn_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_warning_ = new wxStaticText(
        pnl_warning_, wxID_ANY,
        wxT("Aviso: Modo de solo lectura. Su cuenta no cuenta con el permiso roles.manage para modificar roles.")
    );
    lbl_warning_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_warning_->SetForegroundColour(Theme::ColorError);
    warn_sizer->Add(lbl_warning_, 1, wxALL | wxALIGN_CENTER_VERTICAL, 8);
    pnl_warning_->SetSizer(warn_sizer);
    root->Add(pnl_warning_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 12);
    pnl_warning_->Show(!can_manage_);

    // 3. Layout principal dividido en dos columnas (Tarjetas)
    auto* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    // --- Columna izquierda: Lista de Roles ---
    left_card_ = new CardPanel(this, wxID_ANY);
    left_card_->set_card_background(Theme::ColorSurface);
    auto* left_sizer = new wxBoxSizer(wxVERTICAL);

    auto* left_title = new wxStaticText(left_card_, wxID_ANY, wxT("Roles Registrados"));
    left_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    left_title->SetForegroundColour(Theme::ColorText);
    left_sizer->Add(left_title, 0, wxALL, 10);

    list_roles_ = new wxListCtrl(left_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_roles_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        // Filas de al menos 40 px en Touch: la altura de fila de SysListView32 sigue a la lista de imágenes
        // (misma técnica que pos_panel.cpp).
        list_roles_->AssignImageList(new wxImageList(1, 42), wxIMAGE_LIST_SMALL);
    }
    list_roles_->InsertColumn(0, wxT("Nombre"), wxLIST_FORMAT_LEFT, 110);
    list_roles_->InsertColumn(1, wxT("Descripción"), wxLIST_FORMAT_LEFT, 110);
    list_roles_->InsertColumn(2, wxT("Sistema"), wxLIST_FORMAT_CENTER, 65);
    list_roles_->InsertColumn(3, wxT("Permisos"), wxLIST_FORMAT_CENTER, 75);

    // Si aparece o desaparece la barra vertical cambia el área cliente: recalcular anchos.
    list_roles_->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        if (in_col_resize_) return;
        auto_size_columns();
    });
    list_roles_->Bind(wxEVT_LIST_ITEM_SELECTED, &BackofficeRolesPanel::on_role_selected, this);
    list_roles_->Bind(wxEVT_LIST_ITEM_DESELECTED, &BackofficeRolesPanel::on_role_deselected, this);
    left_sizer->Add(list_roles_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    lbl_roles_count_ = new wxStaticText(left_card_, wxID_ANY, wxEmptyString);
    lbl_roles_count_->SetFont(Theme::get_font_small());
    lbl_roles_count_->SetForegroundColour(Theme::ColorTextSecondary);
    left_sizer->Add(lbl_roles_count_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    left_card_->SetSizer(left_sizer);
    int left_w = Theme::is_touch() ? 390 : 360;
    left_card_->SetMinSize(wxSize(left_w, -1));
    main_sizer->Add(left_card_, 0, wxEXPAND | wxRIGHT, 10);

    // --- Columna derecha: Detalle del Rol y Matriz de Permisos ---
    right_card_ = new CardPanel(this, wxID_ANY);
    right_card_->set_card_background(Theme::ColorSurface);
    auto* right_sizer = new wxBoxSizer(wxVERTICAL);

    auto* rhead_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_editor_title_ = new wxStaticText(right_card_, wxID_ANY, wxT("Detalle del Rol"));
    lbl_editor_title_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_editor_title_->SetForegroundColour(Theme::ColorText);
    rhead_sizer->Add(lbl_editor_title_, 0, wxALIGN_CENTER_VERTICAL);

    rhead_sizer->AddStretchSpacer(1);

    lbl_system_notice_ = new wxStaticText(right_card_, wxID_ANY, wxT("Rol de sistema (solo lectura)"));
    lbl_system_notice_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_system_notice_->SetForegroundColour(Theme::ColorBrand);
    lbl_system_notice_->Show(false);
    rhead_sizer->Add(lbl_system_notice_, 0, wxALIGN_CENTER_VERTICAL);

    right_sizer->Add(rhead_sizer, 0, wxEXPAND | wxALL, 10);

    // En Touch cada campo va en un contenedor de 48 px con el texto centrado verticalmente
    // (misma técnica de contenedor y espaciadores que la búsqueda de pos_panel.cpp).
    // El wxTextCtrl se devuelve por referencia; Enable()/Disable() siguen actuando sobre él.
    auto make_text_field = [](wxWindow* parent, wxTextCtrl*& out) -> wxWindow* {
        if (!Theme::is_touch()) {
            out = new wxTextCtrl(parent, wxID_ANY);
            out->SetFont(Theme::get_font_body());
            return out;
        }
        auto* box = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 48), wxBORDER_SIMPLE);
        box->SetBackgroundColour(Theme::ColorSurface);
        box->SetMinSize(wxSize(-1, 48));
        out = new wxTextCtrl(box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        out->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        out->SetBackgroundColour(Theme::ColorSurface);
        auto* sz = new wxBoxSizer(wxVERTICAL);
        sz->AddStretchSpacer(1);
        sz->Add(out, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
        sz->AddStretchSpacer(1);
        box->SetSizer(sz);
        wxTextCtrl* edit = out;
        box->Bind(wxEVT_LEFT_DOWN, [edit](wxMouseEvent&) { edit->SetFocus(); });
        return box;
    };

    auto* form_grid = new wxFlexGridSizer(2, 2, 8, 10);
    form_grid->AddGrowableCol(1, 1);

    auto* lbl_n = new wxStaticText(right_card_, wxID_ANY, wxT("Nombre:"));
    lbl_n->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    form_grid->Add(lbl_n, 0, wxALIGN_CENTER_VERTICAL);
    form_grid->Add(make_text_field(right_card_, txt_name_), 1, wxEXPAND);

    auto* lbl_d = new wxStaticText(right_card_, wxID_ANY, wxT("Descripción:"));
    lbl_d->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    form_grid->Add(lbl_d, 0, wxALIGN_CENTER_VERTICAL);
    form_grid->Add(make_text_field(right_card_, txt_desc_), 1, wxEXPAND);

    right_sizer->Add(form_grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* lbl_p_header = new wxStaticText(right_card_, wxID_ANY, wxT("Matriz de Permisos por Módulo:"));
    lbl_p_header->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_p_header->SetForegroundColour(Theme::ColorTextSecondary);
    right_sizer->Add(lbl_p_header, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    scroll_perms_ = new wxScrolledWindow(
        right_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxVSCROLL | wxBORDER_NONE
    );
    scroll_perms_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    scroll_perms_->SetScrollRate(0, 10);
    scroll_perms_->SetMinSize(wxSize(200, 120));
    perms_sizer_ = new wxBoxSizer(wxVERTICAL);
    scroll_perms_->SetSizer(perms_sizer_);
    right_sizer->Add(scroll_perms_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // Barra de acciones inferior
    auto* btm_sizer = new wxBoxSizer(wxHORIZONTAL);

    btn_save_ = new wxButton(right_card_, wxID_ANY, wxT("Guardar"));
    style_and_size_btn(btn_save_, btn_h, "check_circle", Theme::ColorSurface, true, false);
    btn_save_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_save_role, this);
    btm_sizer->Add(btn_save_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    btn_delete_ = new wxButton(right_card_, wxID_ANY, wxT("Eliminar"));
    style_and_size_btn(btn_delete_, btn_h, "delete", Theme::ColorError, false, true);
    btn_delete_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_delete_role, this);
    btm_sizer->Add(btn_delete_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    lbl_form_feedback_ = new wxStaticText(right_card_, wxID_ANY, wxEmptyString);
    lbl_form_feedback_->SetFont(Theme::get_font_small());
    btm_sizer->Add(lbl_form_feedback_, 1, wxALIGN_CENTER_VERTICAL);

    right_sizer->Add(btm_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    right_card_->SetSizer(right_sizer);
    right_card_->SetMinSize(wxSize(280, -1));
    main_sizer->Add(right_card_, 1, wxEXPAND);

    root->Add(main_sizer, 1, wxEXPAND | wxALL, 12);
    SetSizer(root);

    Bind(wxEVT_SIZE, &BackofficeRolesPanel::on_size, this);
}

void BackofficeRolesPanel::load_permissions() {
    all_permissions_.clear();
    if (!account_service_) return;

    auto res = account_service_->list_permissions();
    if (!res.is_success()) {
        lbl_warning_->SetLabel(wxString::Format(
            wxT("Aviso: %s. Modo de solo lectura."),
            wxString::FromUTF8(res.error_message().c_str())
        ));
        pnl_warning_->Show(true);
        can_manage_ = false;
        btn_new_role_->Enable(false);
        return;
    }

    for (const auto& p : res.value()) {
        // REGLA: backup.dev_config no debe aparecer en ningún control
        if (p.id == "backup.dev_config") {
            continue;
        }
        all_permissions_.push_back(p);
    }
}

void BackofficeRolesPanel::build_permission_matrix() {
    perms_sizer_->Clear(true);
    perm_checkboxes_.clear();

    if (all_permissions_.empty()) {
        auto* empty_lbl = new wxStaticText(
            scroll_perms_, wxID_ANY,
            wxT("No hay permisos disponibles para mostrar.")
        );
        empty_lbl->SetFont(Theme::get_font_body());
        perms_sizer_->Add(empty_lbl, 0, wxALL, 12);
        scroll_perms_->FitInside();
        return;
    }

    // Agrupar por módulo conservando orden
    std::vector<std::string> module_order;
    std::map<std::string, std::vector<contracts::PermissionDto>> groups;
    for (const auto& p : all_permissions_) {
        if (groups.find(p.module) == groups.end()) {
            module_order.push_back(p.module);
        }
        groups[p.module].push_back(p);
    }

    for (const auto& mod : module_order) {
        auto* grp_box = new wxBoxSizer(wxVERTICAL);

        auto* mod_title = new wxStaticText(
            scroll_perms_, wxID_ANY,
            wxString::FromUTF8(get_module_display_name(mod).c_str())
        );
        mod_title->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        mod_title->SetForegroundColour(Theme::ColorBrandDeep);
        grp_box->Add(mod_title, 0, wxLEFT | wxRIGHT | wxTOP, 8);

        for (const auto& p : groups[mod]) {
            auto* cb = new wxCheckBox(
                scroll_perms_, wxID_ANY,
                wxString::FromUTF8(p.description.c_str()),
                wxDefaultPosition, wxDefaultSize,
                wxALIGN_TOP
            );
            cb->SetFont(Theme::get_font_body(wxFONTWEIGHT_NORMAL));
            cb->SetToolTip(wxString::FromUTF8(p.id.c_str()));
            cb->Enable(false);
            apply_checkbox_top_alignment(cb);
            perm_checkboxes_[p.id] = cb;
            grp_box->Add(cb, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
        }

        perms_sizer_->Add(grp_box, 0, wxEXPAND | wxBOTTOM, 8);
    }

    update_permission_matrix_layout();
}

void BackofficeRolesPanel::update_permission_matrix_layout() {
    if (!scroll_perms_ || perm_checkboxes_.empty()) return;

    int panel_w = GetClientSize().GetWidth();
    int left_w = left_card_ ? left_card_->GetSize().GetWidth() : (Theme::is_touch() ? 390 : 360);
    if (left_w <= 100) left_w = (Theme::is_touch() ? 390 : 360);

    // Calcular espacio disponible dinámico para tarjeta derecha y scroll sin depender de dimensiones previas
    int right_card_w = std::max(200, panel_w - left_w - 24);
    int target_scroll_w = std::max(180, right_card_w - 20);

    int target_cb_w = std::max(160, target_scroll_w - 24);
    int avail_text_w = std::max(140, target_cb_w - 32);

    int line_h = Theme::is_touch() ? 24 : 18;
    int pad_h = Theme::is_touch() ? 16 : 8;

    for (const auto& p : all_permissions_) {
        auto it = perm_checkboxes_.find(p.id);
        if (it == perm_checkboxes_.end() || !it->second) continue;
        auto* cb = it->second;

        std::string wrapped = wrap_text_to_width(cb, p.description, avail_text_w);
        cb->SetLabel(wxString::FromUTF8(wrapped.c_str()));

        int lines = 1;
        for (char c : wrapped) {
            if (c == '\n') ++lines;
        }

        int needed_h = lines * line_h + pad_h;
        int min_h = Theme::is_touch() ? std::max(48, needed_h) : std::max(26, needed_h);
        cb->SetMinSize(wxSize(-1, min_h));
        cb->SetMaxSize(wxSize(target_cb_w, -1));
        cb->SetSize(wxSize(target_cb_w, min_h));

        apply_checkbox_top_alignment(cb);
    }

    scroll_perms_->SetVirtualSize(target_scroll_w, -1);
    scroll_perms_->FitInside();
    scroll_perms_->Layout();
}

void BackofficeRolesPanel::auto_size_columns() {
    if (!list_roles_) return;
    in_col_resize_ = true;

    // Ancho útil: área cliente SIN la barra vertical, tanto si ya está visible como si va a aparecer
    // con las filas actuales (así nunca surge barra horizontal).
    const int sb_w = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, list_roles_);
    const wxSize client = list_roles_->GetClientSize();
    int full_w = client.GetWidth() + (has_vertical_scrollbar(list_roles_) ? sb_w : 0);
    if (full_w <= 50) {
        full_w = list_roles_->GetSize().GetWidth();
    }
    if (full_w <= 50) {
        in_col_resize_ = false;
        return;
    }
    int row_h = list_roles_->FromDIP(Theme::is_touch() ? 42 : 20);
    if (list_roles_->GetItemCount() > 0) {
        wxRect item_rect;
        if (list_roles_->GetItemRect(0, item_rect) && item_rect.GetHeight() > 0) {
            row_h = item_rect.GetHeight();
        }
    }
    const int rows_h = header_height(list_roles_) + static_cast<int>(roles_.size()) * row_h;
    const bool needs_vbar = client.GetHeight() > 0 && rows_h > client.GetHeight();
    const int list_w = full_w - (needs_vbar ? sb_w : 0);

    // Encabezados: fuente real del encabezado. Contenido: fuente de la lista.
    const int hpad = list_roles_->FromDIP(20);
    const int h0 = header_text_width(list_roles_, wxT("Nombre"));
    const int h1 = header_text_width(list_roles_, wxT("Descripción"));
    const int h2 = header_text_width(list_roles_, wxT("Sistema"));
    const int h3 = header_text_width(list_roles_, wxT("Permisos"));

    int th = 0, tw = 0;
    int content0 = 0, sys_content0 = 0, content2 = 0, content3 = 0;
    list_roles_->GetTextExtent(wxT("Sí"), &tw, &th);
    content2 = tw;
    list_roles_->GetTextExtent(wxT("No"), &tw, &th);
    content2 = std::max(content2, tw);
    for (const auto& r : roles_) {
        list_roles_->GetTextExtent(wxString::FromUTF8(r.name.c_str()), &tw, &th);
        content0 = std::max(content0, tw);
        if (r.is_system) sys_content0 = std::max(sys_content0, tw);
        list_roles_->GetTextExtent(wxString::Format(wxT("%zu"), r.permissions.size()), &tw, &th);
        content3 = std::max(content3, tw);
    }

    // Columna 0 con imagen de 1 px y reserva nativa de icono en Touch (como pos_panel).
    const int img_pad = Theme::is_touch() ? 1 + 6 : 0;
    const int want0 = std::max(h0 + hpad, content0 + 16 + img_pad);
    const int floor0 = std::max(h0 + hpad, sys_content0 + 16 + img_pad);
    const int col2 = std::max(h2 + hpad, content2 + 18);
    const int col3 = std::max(h3 + hpad, content3 + 18);
    const int min1 = h1 + hpad;

    const int budget = list_w - 2;
    int col0 = want0;
    // Un nombre muy largo no puede quitarle a Descripción el espacio de su encabezado.
    if (budget - col0 - col2 - col3 < min1) {
        col0 = std::max(floor0, budget - col2 - col3 - min1);
    }
    // Descripción toma todo el espacio restante (el contenido puede ir con puntos suspensivos).
    const int col1 = std::max(min1, budget - col0 - col2 - col3);

    list_roles_->SetColumnWidth(0, col0);
    list_roles_->SetColumnWidth(1, col1);
    list_roles_->SetColumnWidth(2, col2);
    list_roles_->SetColumnWidth(3, col3);
    in_col_resize_ = false;
}

void BackofficeRolesPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    if (in_size_event_) return;
    in_size_event_ = true;

    auto_size_columns();
    update_permission_matrix_layout();
    Layout();

    in_size_event_ = false;
}

void BackofficeRolesPanel::refresh_roles(const std::string& select_role_id) {
    list_roles_->DeleteAllItems();
    roles_.clear();
    selected_index_ = -1;

    if (!account_service_) return;

    auto res = account_service_->list_roles();
    if (!res.is_success()) {
        lbl_top_status_->SetLabel(wxString::Format(
            wxT("Error: %s"),
            wxString::FromUTF8(res.error_message().c_str())
        ));
        return;
    }

    long target_idx = -1;
    for (const auto& r : res.value()) {
        // REGLA: role-dev no debe mostrarse
        if (r.id == "role-dev") {
            continue;
        }
        long idx = static_cast<long>(roles_.size());
        roles_.push_back(r);

        long item_idx = list_roles_->InsertItem(idx, wxString::FromUTF8(r.name.c_str()));
        list_roles_->SetItem(item_idx, 1, wxString::FromUTF8(r.description.c_str()));
        list_roles_->SetItem(item_idx, 2, r.is_system ? wxT("Sí") : wxT("No"));
        list_roles_->SetItem(item_idx, 3, wxString::Format(wxT("%zu"), r.permissions.size()));

        if (!select_role_id.empty() && r.id == select_role_id) {
            target_idx = item_idx;
        }
    }

    lbl_roles_count_->SetLabel(wxString::Format(wxT("Total: %zu roles"), roles_.size()));

    auto_size_columns();
    // La barra vertical puede aparecer o desaparecer tras insertar filas: recalcular al asentarse el layout.
    CallAfter([this]() { auto_size_columns(); });
    update_permission_matrix_layout();

    if (target_idx >= 0) {
        list_roles_->SetItemState(target_idx, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        list_roles_->EnsureVisible(target_idx);
        populate_role_details(target_idx);
    } else if (!roles_.empty()) {
        list_roles_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        populate_role_details(0);
    } else {
        reset_form_to_empty();
    }
}

void BackofficeRolesPanel::populate_role_details(long index) {
    if (index < 0 || index >= static_cast<long>(roles_.size())) {
        return;
    }

    selected_index_ = index;
    is_creating_new_ = false;
    const auto& r = roles_[index];

    lbl_editor_title_->SetLabel(wxString::Format(wxT("Rol: %s"), wxString::FromUTF8(r.name.c_str())));
    txt_name_->SetValue(wxString::FromUTF8(r.name.c_str()));
    txt_desc_->SetValue(wxString::FromUTF8(r.description.c_str()));
    lbl_form_feedback_->SetLabel(wxEmptyString);

    bool is_cashier = (r.id == "role-cashier");

    bool can_edit_fields = !r.is_system && can_manage_;
    bool can_edit_perms = (!r.is_system || is_cashier) && can_manage_;
    bool can_save = (!r.is_system || is_cashier) && can_manage_;
    bool can_delete = !r.is_system && can_manage_;

    if (!can_manage_) {
        lbl_system_notice_->SetLabel(wxT("Sin permiso roles.manage (solo lectura)"));
        lbl_system_notice_->Show(true);
    } else if (is_cashier) {
        lbl_system_notice_->SetLabel(wxT("Rol de sistema: solo se pueden cambiar sus permisos."));
        lbl_system_notice_->Show(true);
    } else if (r.is_system) {
        lbl_system_notice_->SetLabel(wxT("Rol de sistema (solo lectura: no editable ni eliminable)"));
        lbl_system_notice_->Show(true);
    } else {
        lbl_system_notice_->Hide();
    }

    txt_name_->Enable(can_edit_fields);
    txt_desc_->Enable(can_edit_fields);
    btn_save_->Enable(can_save);
    btn_delete_->Enable(can_delete);

    for (auto& [perm_id, cb] : perm_checkboxes_) {
        bool has = std::find(r.permissions.begin(), r.permissions.end(), perm_id) != r.permissions.end();
        cb->SetValue(has);
        cb->Enable(can_edit_perms);
    }

    Layout();
}

void BackofficeRolesPanel::reset_form_to_empty() {
    selected_index_ = -1;
    is_creating_new_ = false;
    lbl_editor_title_->SetLabel(wxT("Detalle del Rol"));
    lbl_system_notice_->Hide();
    lbl_form_feedback_->SetLabel(wxEmptyString);
    txt_name_->SetValue(wxEmptyString);
    txt_desc_->SetValue(wxEmptyString);
    txt_name_->Enable(false);
    txt_desc_->Enable(false);
    btn_save_->Enable(false);
    btn_delete_->Enable(false);

    for (auto& [perm_id, cb] : perm_checkboxes_) {
        cb->SetValue(false);
        cb->Enable(false);
    }

    Layout();
}

void BackofficeRolesPanel::on_role_selected(wxListEvent& event) {
    populate_role_details(event.GetIndex());
}

void BackofficeRolesPanel::on_role_deselected(wxListEvent&) {
    // Si no estamos creando uno nuevo, no vaciar si simplemente cambió de selección
}

void BackofficeRolesPanel::on_new_role(wxCommandEvent&) {
    if (!can_manage_) return;

    is_creating_new_ = true;
    selected_index_ = -1;

    for (long i = 0; i < list_roles_->GetItemCount(); ++i) {
        list_roles_->SetItemState(i, 0, wxLIST_STATE_SELECTED);
    }

    lbl_editor_title_->SetLabel(wxT("Nuevo Rol Personalizado"));
    lbl_system_notice_->Hide();
    lbl_form_feedback_->SetLabel(wxEmptyString);

    txt_name_->SetValue(wxEmptyString);
    txt_desc_->SetValue(wxEmptyString);
    txt_name_->Enable(true);
    txt_desc_->Enable(true);

    for (auto& [perm_id, cb] : perm_checkboxes_) {
        cb->SetValue(false);
        cb->Enable(true);
    }

    btn_save_->Enable(true);
    btn_delete_->Enable(false); // No se puede eliminar antes de crearlo

    txt_name_->SetFocus();
    Layout();
}

void BackofficeRolesPanel::on_save_role(wxCommandEvent&) {
    std::string name = std::string(txt_name_->GetValue().Trim().ToUTF8());
    std::string desc = std::string(txt_desc_->GetValue().Trim().ToUTF8());

    if (name.empty()) {
        lbl_form_feedback_->SetForegroundColour(Theme::ColorError);
        lbl_form_feedback_->SetLabel(wxT("El nombre del rol es requerido."));
        Layout();
        return;
    }

    std::vector<std::string> perms;
    for (const auto& p : all_permissions_) {
        auto it = perm_checkboxes_.find(p.id);
        if (it != perm_checkboxes_.end() && it->second->GetValue()) {
            perms.push_back(p.id);
        }
    }

    if (is_creating_new_) {
        contracts::CreateRoleDto dto;
        dto.name = name;
        dto.description = desc;
        dto.permissions = perms;

        auto res = account_service_->create_role(dto);
        if (!res.is_success()) {
            lbl_form_feedback_->SetForegroundColour(Theme::ColorError);
            lbl_form_feedback_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
            wxMessageBox(
                wxString::FromUTF8(res.error_message().c_str()),
                wxT("Error al Crear Rol"),
                wxOK | wxICON_ERROR,
                this
            );
            Layout();
            return;
        }

        lbl_form_feedback_->SetForegroundColour(Theme::ColorSuccess);
        lbl_form_feedback_->SetLabel(wxT("Rol creado exitosamente."));
        refresh_roles(res.value().id);
    } else {
        if (selected_index_ < 0 || selected_index_ >= static_cast<long>(roles_.size())) return;
        const auto& r = roles_[selected_index_];
        if (r.is_system && r.id != "role-cashier") {
            wxMessageBox(
                wxT("Los roles protegidos de sistema no pueden ser modificados."),
                wxT("Operación Denegada"),
                wxOK | wxICON_WARNING,
                this
            );
            return;
        }

        contracts::UpdateRoleDto dto;
        dto.role_id = r.id;
        if (r.id == "role-cashier") {
            dto.name = r.name;
            dto.description = r.description;
        } else {
            dto.name = name;
            dto.description = desc;
        }
        dto.permissions = perms;

        auto res = account_service_->update_role(dto);
        if (!res.is_success()) {
            lbl_form_feedback_->SetForegroundColour(Theme::ColorError);
            lbl_form_feedback_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
            wxMessageBox(
                wxString::FromUTF8(res.error_message().c_str()),
                wxT("Error al Modificar Rol"),
                wxOK | wxICON_ERROR,
                this
            );
            Layout();
            return;
        }

        lbl_form_feedback_->SetForegroundColour(Theme::ColorSuccess);
        lbl_form_feedback_->SetLabel(wxT("Rol actualizado exitosamente."));
        refresh_roles(r.id);
    }
}

void BackofficeRolesPanel::on_delete_role(wxCommandEvent&) {
    if (selected_index_ < 0 || selected_index_ >= static_cast<long>(roles_.size())) return;
    const auto& r = roles_[selected_index_];

    if (r.is_system) {
        wxMessageBox(
            wxT("No se pueden eliminar roles protegidos del sistema."),
            wxT("Operación Denegada"),
            wxOK | wxICON_WARNING,
            this
        );
        return;
    }

    int ans = wxMessageBox(
        wxString::Format(
            wxT("¿Está seguro de eliminar el rol '%s'? Esta acción no se puede deshacer."),
            wxString::FromUTF8(r.name.c_str())
        ),
        wxT("Confirmar Eliminación"),
        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
        this
    );
    if (ans != wxYES) {
        return;
    }

    auto res = account_service_->delete_role(r.id);
    if (!res.is_success()) {
        lbl_form_feedback_->SetForegroundColour(Theme::ColorError);
        lbl_form_feedback_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
        wxMessageBox(
            wxString::FromUTF8(res.error_message().c_str()),
            wxT("Error al Eliminar Rol"),
            wxOK | wxICON_ERROR,
            this
        );
        Layout();
        return;
    }

    lbl_form_feedback_->SetForegroundColour(Theme::ColorSuccess);
    lbl_form_feedback_->SetLabel(wxT("Rol eliminado exitosamente."));
    refresh_roles("");
}

} // namespace app::frontend
