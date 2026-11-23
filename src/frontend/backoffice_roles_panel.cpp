#include "src/frontend/backoffice_roles_panel.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/toggle_switch.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <algorithm>
#include <array>
#include <sstream>
#include <vector>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/settings.h>
#include <wx/wrapsizer.h>

namespace app::frontend {

namespace {

const std::array<const char*, 4> kSectionOrder = {
    "Punto de Venta",
    "Inventario",
    "Caja y Reportes",
    "BackOffice"
};

std::string wrap_text_to_width(wxWindow* win, const std::string& text, int max_width_px) {
    const int min_w = win ? win->FromDIP(60) : 60;
    if (max_width_px < min_w) {
        max_width_px = min_w;
    }
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

int measure_multiline_height(wxWindow* win, const wxString& text) {
    if (!win) return 0;
    wxClientDC dc(win);
    dc.SetFont(win->GetFont());
    const wxSize ext = dc.GetMultiLineTextExtent(text.empty() ? wxString(wxT("Ág")) : text);
    return ext.GetHeight();
}

void wrap_static_text_deterministic(wxStaticText* lbl, const wxString& raw, int max_width_px) {
    if (!lbl) return;
    const std::string utf8(raw.ToUTF8());
    const std::string wrapped = wrap_text_to_width(lbl, utf8, max_width_px);
    const wxString wxs = wxString::FromUTF8(wrapped.c_str());
    lbl->SetMinSize(wxDefaultSize);
    lbl->SetLabel(wxs);
    lbl->InvalidateBestSize();
    const int mh = measure_multiline_height(lbl, wxs);
    lbl->SetMinSize(wxSize(-1, mh + lbl->FromDIP(2)));
}

wxPanel* create_separator_line(wxWindow* parent) {
    auto* sep = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxBORDER_NONE);
    sep->SetBackgroundColour(Theme::ColorBorderSubtle);
    sep->SetBackgroundStyle(wxBG_STYLE_PAINT);
    sep->SetMinSize(wxSize(-1, 1));
    sep->SetMaxSize(wxSize(-1, 1));
    sep->Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    sep->Bind(wxEVT_PAINT, [](wxPaintEvent& event) {
        auto* win = static_cast<wxWindow*>(event.GetEventObject());
        wxAutoBufferedPaintDC dc(win);
        dc.SetBackground(wxBrush(Theme::ColorBorderSubtle));
        dc.Clear();
    });
    return sep;
}

void bind_row_click_target(wxWindow* target, ToggleSwitch* toggle) {
    if (!target || !toggle) return;
    target->Bind(wxEVT_LEFT_DOWN, [target](wxMouseEvent& event) {
        target->SetClientData(reinterpret_cast<void*>(1));
        event.Skip();
    });
    target->Bind(wxEVT_LEFT_UP, [target, toggle](wxMouseEvent& event) {
        const bool pressed = (target->GetClientData() != nullptr);
        target->SetClientData(nullptr);
        if (pressed && toggle->IsEnabled() && wxRect(target->GetClientSize()).Contains(event.GetPosition())) {
            toggle->SetFocus();
            toggle->Toggle();
        }
        event.Skip();
    });
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

BackofficeRolesPanel::~BackofficeRolesPanel() {
    *alive_ = false;
}

void BackofficeRolesPanel::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    auto* root = new wxBoxSizer(wxVERTICAL);

    const int btn_h_logical = Theme::is_touch() ? 48 : 36;

    auto style_and_size_btn = [this](wxButton* btn, int h_logical, const std::string& icon_name,
                                     const wxColour& tint, bool is_primary, bool is_destructive) {
        if (is_primary) {
            Theme::style_button_primary(btn, h_logical);
        } else if (is_destructive) {
            Theme::style_button_destructive(btn, h_logical);
        } else {
            Theme::style_button_secondary(btn, h_logical);
        }
        if (!icon_name.empty()) {
            Theme::setup_button_icon(btn, icon_name, wxSize(18, 18), tint);
        }
        int tw = 0, th = 0;
        btn->GetTextExtent(btn->GetLabel(), &tw, &th);
        const int icon_w = icon_name.empty() ? 0 : btn->FromDIP(18);
        const int icon_m = icon_name.empty() ? 0 : btn->FromDIP(Theme::is_touch() ? 10 : 8) * 2;
        const int bp = btn->FromDIP(Theme::is_touch() ? 28 : 24);
        int w = tw + icon_w + icon_m + bp;
        btn->InvalidateBestSize();
        const wxSize best = btn->GetBestSize();
        if (best.GetWidth() > w) w = best.GetWidth();
        const int h_px = std::max(btn->FromDIP(h_logical), best.GetHeight());
        btn->SetMinSize(wxSize(w, h_px));
        btn->SetSize(wxSize(w, h_px));
    };

    // 1. Barra superior
    auto* top_bar = new wxBoxSizer(wxHORIZONTAL);

    auto* title = new wxStaticText(this, wxID_ANY, wxT("Roles y Permisos del Sistema"));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    top_bar->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));

    btn_new_role_ = new wxButton(this, wxID_ANY, wxT("Nuevo rol"));
    style_and_size_btn(btn_new_role_, btn_h_logical, "add", Theme::ColorSurface, true, false);
    btn_new_role_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_new_role, this);
    btn_new_role_->Enable(can_manage_);
    top_bar->Add(btn_new_role_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    btn_refresh_ = new wxButton(this, wxID_ANY, wxT("Actualizar"));
    style_and_size_btn(btn_refresh_, btn_h_logical, "", wxNullColour, false, false);
    btn_refresh_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_roles(); });
    top_bar->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);

    top_bar->AddStretchSpacer(1);

    lbl_top_status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_top_status_->SetFont(Theme::get_font_small());
    lbl_top_status_->SetForegroundColour(Theme::ColorTextSecondary);
    top_bar->Add(lbl_top_status_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    root->Add(top_bar, 0, wxEXPAND);

    // 2. Banner de aviso si no tiene permiso roles.manage
    pnl_warning_ = new wxPanel(this, wxID_ANY);
    pnl_warning_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* warn_sizer = new wxBoxSizer(wxHORIZONTAL);
    warning_raw_ = wxT("Aviso: Modo de solo lectura. Su cuenta no cuenta con el permiso roles.manage para modificar roles.");
    lbl_warning_ = new wxStaticText(pnl_warning_, wxID_ANY, warning_raw_);
    lbl_warning_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_warning_->SetForegroundColour(Theme::ColorError);
    warn_sizer->Add(lbl_warning_, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(8));
    pnl_warning_->SetSizer(warn_sizer);
    root->Add(pnl_warning_, 0, wxEXPAND | wxTOP, FromDIP(4));
    pnl_warning_->Show(!can_manage_);

    // 3. Layout principal dividido en dos columnas adaptativas (Tarjetas)
    // Margen interior estándar de CardPanel = FromDIP(16), igual que BackofficeSettingsPanel
    // (src/frontend/backoffice_settings_panel.cpp:39,48,107).
    const int card_pad = FromDIP(16);
    main_sizer_ = new wxBoxSizer(wxHORIZONTAL);

    // --- Columna izquierda: Lista de Roles ---
    left_card_ = new CardPanel(this, wxID_ANY);
    left_card_->set_card_background(Theme::ColorSurface);
    auto* left_outer = new wxBoxSizer(wxVERTICAL);
    auto* left_sizer = new wxBoxSizer(wxVERTICAL);

    auto* left_title = new wxStaticText(left_card_, wxID_ANY, wxT("Roles Registrados"));
    left_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    left_title->SetForegroundColour(Theme::ColorText);
    left_sizer->Add(left_title, 0, wxBOTTOM, FromDIP(8));

    list_roles_ = new wxListCtrl(left_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_roles_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        ui::apply_touch_row_height(list_roles_);
    }
    list_roles_->InsertColumn(0, wxT("Nombre"), wxLIST_FORMAT_LEFT, FromDIP(110));
    list_roles_->InsertColumn(1, wxT("Descripción"), wxLIST_FORMAT_LEFT, FromDIP(110));
    list_roles_->InsertColumn(2, wxT("Sistema"), wxLIST_FORMAT_CENTER, FromDIP(65));
    list_roles_->InsertColumn(3, wxT("Permisos"), wxLIST_FORMAT_CENTER, FromDIP(75));

    // Si aparece o desaparece la barra vertical cambia el área cliente: recalcular anchos.
    list_roles_->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        auto_size_columns();
    });
    list_roles_->Bind(wxEVT_LIST_ITEM_SELECTED, &BackofficeRolesPanel::on_role_selected, this);
    list_roles_->Bind(wxEVT_LIST_ITEM_DESELECTED, &BackofficeRolesPanel::on_role_deselected, this);
    left_sizer->Add(list_roles_, 1, wxEXPAND | wxBOTTOM, FromDIP(8));

    lbl_roles_count_ = new wxStaticText(left_card_, wxID_ANY, wxEmptyString);
    lbl_roles_count_->SetFont(Theme::get_font_small());
    lbl_roles_count_->SetForegroundColour(Theme::ColorTextSecondary);
    left_sizer->Add(lbl_roles_count_, 0);

    left_outer->Add(left_sizer, 1, wxEXPAND | wxALL, card_pad);
    left_card_->SetSizer(left_outer);
    const int left_min_w = FromDIP(Theme::is_touch() ? 376 : 360);
    left_card_->SetMinSize(wxSize(left_min_w, -1));
    main_sizer_->Add(left_card_, 38, wxEXPAND | wxRIGHT, FromDIP(12));

    // --- Columna derecha: Detalle del Rol y Lista de Permisos ---
    right_card_ = new CardPanel(this, wxID_ANY);
    right_card_->set_card_background(Theme::ColorSurface);
    auto* right_outer = new wxBoxSizer(wxVERTICAL);
    auto* right_sizer = new wxBoxSizer(wxVERTICAL);

    rhead_container_ = new wxBoxSizer(wxVERTICAL);
    editor_title_raw_ = wxT("Detalle del Rol");
    lbl_editor_title_ = new wxStaticText(right_card_, wxID_ANY, editor_title_raw_);
    lbl_editor_title_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_editor_title_->SetForegroundColour(Theme::ColorText);

    system_notice_raw_ = wxT("Rol de sistema (solo lectura)");
    lbl_system_notice_ = new wxStaticText(right_card_, wxID_ANY, system_notice_raw_);
    lbl_system_notice_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_system_notice_->SetForegroundColour(Theme::ColorBrand);
    lbl_system_notice_->Show(false);

    right_sizer->Add(rhead_container_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    // Touch: campos de 48 px lógicos con el texto centrado (fábrica común). Estándar: campo natural.
    const int field_h_logical = Theme::is_touch() ? 48 : 0;
    const wxFont field_font = Theme::is_touch() ? Theme::get_font_body(wxFONTWEIGHT_MEDIUM) : Theme::get_font_body();
    form_container_ = new wxBoxSizer(wxVERTICAL);

    lbl_name_ = new wxStaticText(right_card_, wxID_ANY, wxT("Nombre:"));
    lbl_name_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    field_name_box_ = ui::make_text_field(right_card_, txt_name_, field_h_logical, -1, field_font);

    lbl_desc_ = new wxStaticText(right_card_, wxID_ANY, wxT("Descripción:"));
    lbl_desc_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    field_desc_box_ = ui::make_text_field(right_card_, txt_desc_, field_h_logical, -1, field_font);

    right_sizer->Add(form_container_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    // Encabezado de permisos + retroalimentación de validación/guardado (oculta si vacía; en línea cuando cabe)
    perm_header_container_ = new wxBoxSizer(wxVERTICAL);
    lbl_p_header_ = new wxStaticText(right_card_, wxID_ANY, wxT("Permisos del rol:"));
    lbl_p_header_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_p_header_->SetForegroundColour(Theme::ColorTextSecondary);

    lbl_form_feedback_ = new wxStaticText(right_card_, wxID_ANY, wxEmptyString);
    lbl_form_feedback_->SetFont(Theme::get_font_small());
    lbl_form_feedback_->Show(false);

    right_sizer->Add(perm_header_container_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    scroll_perms_ = new wxScrolledWindow(
        right_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxVSCROLL | wxTAB_TRAVERSAL | wxBORDER_NONE
    );
    scroll_perms_->SetBackgroundColour(Theme::ColorSurface);
    scroll_perms_->SetScrollRate(0, FromDIP(12));
    scroll_perms_->SetMinSize(FromDIP(wxSize(180, 90)));
    perms_sizer_ = new wxBoxSizer(wxVERTICAL);
    scroll_perms_->SetSizer(perms_sizer_);
    scroll_perms_->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        if (!in_matrix_layout_) {
            update_permission_matrix_layout();
        }
    });
    right_sizer->Add(scroll_perms_, 1, wxEXPAND | wxBOTTOM, FromDIP(8));

    // Barra de acciones inferior (siempre visible sin desplazarse)
    auto* btm_sizer = new wxWrapSizer(wxHORIZONTAL, wxWRAPSIZER_DEFAULT_FLAGS);

    btn_save_ = new wxButton(right_card_, wxID_ANY, wxT("Guardar"));
    style_and_size_btn(btn_save_, btn_h_logical, "check_circle", Theme::ColorSurface, true, false);
    btn_save_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_save_role, this);
    btm_sizer->Add(btn_save_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(4));

    lbl_perm_count_ = new wxStaticText(right_card_, wxID_ANY, wxEmptyString);
    lbl_perm_count_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_perm_count_->SetForegroundColour(Theme::ColorTextSecondary);
    btm_sizer->Add(lbl_perm_count_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_delete_ = new wxButton(right_card_, wxID_ANY, wxT("Eliminar"));
    style_and_size_btn(btn_delete_, btn_h_logical, "delete", Theme::ColorError, false, true);
    btn_delete_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_delete_role, this);
    btm_sizer->Add(btn_delete_, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, FromDIP(4));

    right_sizer->Add(btm_sizer, 0, wxEXPAND);

    right_outer->Add(right_sizer, 1, wxEXPAND | wxALL, card_pad);
    right_card_->SetSizer(right_outer);
    right_card_->SetMinSize(FromDIP(wxSize(240, -1)));
    main_sizer_->Add(right_card_, 62, wxEXPAND);

    root->AddSpacer(FromDIP(10));
    root->Add(main_sizer_, 1, wxEXPAND);
    SetSizer(root);

    update_responsive_layout();
    Bind(wxEVT_SIZE, &BackofficeRolesPanel::on_size, this);
}

void BackofficeRolesPanel::load_permissions() {
    all_permissions_.clear();
    if (!account_service_) return;

    auto res = account_service_->list_permissions();
    if (!res.is_success()) {
        warning_raw_ = wxString::Format(
            wxT("Aviso: %s. Modo de solo lectura."),
            wxString::FromUTF8(res.error_message().c_str())
        );
        lbl_warning_->SetLabel(warning_raw_);
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

    std::stable_sort(
        all_permissions_.begin(),
        all_permissions_.end(),
        [](const contracts::PermissionDto& a, const contracts::PermissionDto& b) {
            return a.sort_order < b.sort_order;
        }
    );
}

void BackofficeRolesPanel::build_permission_matrix() {
    perms_sizer_->Clear(true);
    perm_groups_.clear();
    perm_rows_.clear();
    perm_toggles_.clear();
    perm_separators_.clear();
    matrix_sizers_built_ = false;
    matrix_two_cols_ = false;
    last_matrix_avail_w_ = -1;

    if (all_permissions_.empty()) {
        auto* empty_lbl = new wxStaticText(
            scroll_perms_, wxID_ANY,
            wxT("No hay permisos disponibles para mostrar.")
        );
        empty_lbl->SetFont(Theme::get_font_body());
        perms_sizer_->Add(empty_lbl, 0, wxALL, FromDIP(12));
        update_permission_count();
        scroll_perms_->FitInside();
        return;
    }

    // Agrupar por PermissionDto.section en el orden exacto de la barra lateral.
    std::vector<std::string> section_order;
    for (const char* sec : kSectionOrder) {
        section_order.emplace_back(sec);
    }
    std::map<std::string, std::vector<contracts::PermissionDto>> groups;
    for (const auto& p : all_permissions_) {
        const std::string sec_key = p.section;
        if (std::find(section_order.begin(), section_order.end(), sec_key) == section_order.end()) {
            section_order.push_back(sec_key);
        }
        groups[sec_key].push_back(p);
    }

    const int pad_x = FromDIP(10);
    const int pad_y = FromDIP(8);
    const int title_desc_gap = FromDIP(4);
    const int text_switch_gap = FromDIP(12);

    for (const auto& sec : section_order) {
        auto it = groups.find(sec);
        if (it == groups.end() || it->second.empty()) {
            continue;
        }

        PermissionGroup grp;
        grp.header = new wxStaticText(
            scroll_perms_, wxID_ANY,
            wxString::FromUTF8(sec.c_str())
        );
        grp.header->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
        grp.header->SetForegroundColour(Theme::ColorBrandDeep);

        const auto& items = it->second;
        for (const auto& p : items) {
            const std::string title_text = p.title.empty() ? p.description : p.title;
            const std::string desc_text = p.description;

            auto* row_panel = new wxPanel(
                scroll_perms_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE
            );
            row_panel->SetBackgroundColour(Theme::ColorSurface);

            auto* lbl_title = new wxStaticText(
                row_panel, wxID_ANY,
                wxString::FromUTF8(title_text.c_str())
            );
            lbl_title->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
            lbl_title->SetForegroundColour(Theme::ColorText);

            auto* lbl_desc = new wxStaticText(
                row_panel, wxID_ANY,
                wxString::FromUTF8(desc_text.c_str())
            );
            lbl_desc->SetFont(Theme::get_font_body(wxFONTWEIGHT_NORMAL));
            lbl_desc->SetForegroundColour(Theme::ColorTextSecondary);

            auto* toggle = new ToggleSwitch(
                row_panel, wxID_ANY, false,
                wxString::FromUTF8(title_text.c_str())
            );
            toggle->Enable(false);
            toggle->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
                update_permission_count();
            });

            bind_row_click_target(row_panel, toggle);
            bind_row_click_target(lbl_title, toggle);
            bind_row_click_target(lbl_desc, toggle);

            auto* row_sizer = new wxBoxSizer(wxHORIZONTAL);
            row_sizer->AddSpacer(pad_x);

            auto* text_col = new wxBoxSizer(wxVERTICAL);
            text_col->AddSpacer(pad_y);
            text_col->Add(lbl_title, 0, wxEXPAND);
            text_col->AddSpacer(title_desc_gap);
            text_col->Add(lbl_desc, 0, wxEXPAND);
            text_col->AddSpacer(pad_y);

            row_sizer->Add(text_col, 1, wxALIGN_CENTER_VERTICAL);
            row_sizer->AddSpacer(text_switch_gap);
            row_sizer->Add(toggle, 0, wxALIGN_CENTER_VERTICAL);
            row_sizer->AddSpacer(pad_x);
            row_panel->SetSizer(row_sizer);

            PermissionRow row;
            row.perm = p;
            row.perm.title = title_text;
            row.row_panel = row_panel;
            row.lbl_title = lbl_title;
            row.lbl_desc = lbl_desc;
            row.toggle = toggle;

            const size_t row_idx = perm_rows_.size();
            perm_rows_.push_back(row);
            perm_toggles_[p.id] = toggle;
            grp.row_indices.push_back(row_idx);
        }

        perm_groups_.push_back(std::move(grp));
    }

    update_permission_count();
    update_permission_matrix_layout();
}

void BackofficeRolesPanel::rebuild_permission_sizers(bool two_cols) {
    if (!perms_sizer_ || !scroll_perms_) return;

    // Desacoplar encabezados y paneles de fila sin destruirlos (preserva controles y estado de toggles).
    for (auto& grp : perm_groups_) {
        if (grp.header && grp.header->GetContainingSizer()) {
            grp.header->GetContainingSizer()->Detach(grp.header);
        }
    }
    for (auto& row : perm_rows_) {
        if (row.row_panel && row.row_panel->GetContainingSizer()) {
            row.row_panel->GetContainingSizer()->Detach(row.row_panel);
        }
    }
    for (auto* sep : perm_separators_) {
        if (sep) {
            if (sep->GetContainingSizer()) {
                sep->GetContainingSizer()->Detach(sep);
            }
            sep->Destroy();
        }
    }
    perm_separators_.clear();
    perms_sizer_->Clear(false);

    const int pad_x = FromDIP(10);
    const int hdr_top = FromDIP(12);
    const int hdr_bottom = FromDIP(6);
    const int col_gap = FromDIP(12);
    const int group_gap = FromDIP(two_cols ? 8 : 10);

    bool first_group = true;
    for (auto& grp : perm_groups_) {
        if (!grp.header || grp.row_indices.empty()) continue;

        if (!first_group) {
            perms_sizer_->AddSpacer(group_gap);
        }
        first_group = false;

        auto* grp_box = new wxBoxSizer(wxVERTICAL);
        grp_box->AddSpacer(hdr_top);
        grp_box->Add(grp.header, 0, wxLEFT | wxRIGHT, pad_x);
        grp_box->AddSpacer(hdr_bottom);

        if (!two_cols) {
            for (size_t i = 0; i < grp.row_indices.size(); ++i) {
                if (i > 0) {
                    wxPanel* sep = create_separator_line(scroll_perms_);
                    perm_separators_.push_back(sep);
                    grp_box->Add(sep, 0, wxEXPAND);
                }
                grp_box->Add(perm_rows_[grp.row_indices[i]].row_panel, 0, wxEXPAND);
            }
        } else {
            for (size_t j = 0; j < grp.row_indices.size(); j += 2) {
                if (j > 0) {
                    wxPanel* sep = create_separator_line(scroll_perms_);
                    perm_separators_.push_back(sep);
                    grp_box->Add(sep, 0, wxEXPAND);
                }
                auto* pair_sizer = new wxBoxSizer(wxHORIZONTAL);
                pair_sizer->Add(perm_rows_[grp.row_indices[j]].row_panel, 0, wxALIGN_CENTER_VERTICAL);
                pair_sizer->AddSpacer(col_gap);
                if (j + 1 < grp.row_indices.size()) {
                    pair_sizer->Add(perm_rows_[grp.row_indices[j + 1]].row_panel, 0, wxALIGN_CENTER_VERTICAL);
                } else {
                    pair_sizer->AddStretchSpacer(1);
                }
                grp_box->Add(pair_sizer, 0, wxEXPAND);
            }
        }

        perms_sizer_->Add(grp_box, 0, wxEXPAND);
    }

    matrix_two_cols_ = two_cols;
    matrix_sizers_built_ = true;
}

void BackofficeRolesPanel::update_responsive_layout() {
    if (!main_sizer_ || !left_card_ || !right_card_) return;

    const int panel_w = GetClientSize().GetWidth();

    // Regla A: reparto 40:60 en pantalla completa (>= 1100 lógicos) para dar más ancho a las 2 columnas
    // de permisos sin reducir los mínimos táctiles; 38:62 en ventana normal (< 1100 lógicos).
    const bool wide = (panel_w >= FromDIP(1100));
    if (!split_initialized_ || wide != wide_split_) {
        if (auto* li = main_sizer_->GetItem(left_card_)) {
            li->SetProportion(wide ? 40 : 38);
        }
        if (auto* ri = main_sizer_->GetItem(right_card_)) {
            ri->SetProportion(wide ? 60 : 62);
        }
        wide_split_ = wide;
        split_initialized_ = true;
    }

    // Banner de solo lectura (si visible)
    if (pnl_warning_ && pnl_warning_->IsShown() && lbl_warning_ && panel_w > FromDIP(80)) {
        const int warn_w = std::max(FromDIP(120), panel_w - FromDIP(40));
        if (warn_w != last_warning_wrap_w_) {
            wrap_static_text_deterministic(lbl_warning_, warning_raw_, warn_w);
            last_warning_wrap_w_ = warn_w;
        }
    }

    // Ancho útil interno de right_card_ (descontando margen interior FromDIP(16) a cada lado)
    int right_card_w = right_card_->GetClientSize().GetWidth();
    if (right_card_w <= FromDIP(80) && panel_w > FromDIP(160)) {
        const int main_w = std::max(FromDIP(200), panel_w - FromDIP(24));
        const int left_min = left_card_->GetMinSize().GetWidth();
        const int left_est = std::max(left_min, (main_w * (wide ? 40 : 38)) / 100);
        right_card_w = std::max(FromDIP(200), main_w - left_est);
    }
    const int right_inner_w = std::max(FromDIP(120), right_card_w - FromDIP(32));

    // Regla B: encabezado derecho en 1 fila o 2 filas (umbral calculado desde los textos reales; ref plan: ~520 lógicos)
    if (rhead_container_ && lbl_editor_title_ && lbl_system_notice_) {
        int title_w = 0, title_h = 0;
        lbl_editor_title_->GetTextExtent(editor_title_raw_, &title_w, &title_h);
        int notice_w = 0, notice_h = 0;
        const bool notice_shown = lbl_system_notice_->IsShown() && !system_notice_raw_.empty();
        if (notice_shown) {
            lbl_system_notice_->GetTextExtent(system_notice_raw_, &notice_w, &notice_h);
        }
        const int need_rhead_one_row = title_w + FromDIP(12) + notice_w + FromDIP(8);
        const bool stack_rhead = notice_shown && (right_card_w > FromDIP(80)) && (right_inner_w < need_rhead_one_row);

        if (!rhead_initialized_ || stack_rhead != rhead_stacked_) {
            if (rhead_active_) {
                rhead_active_->Detach(lbl_editor_title_);
                rhead_active_->Detach(lbl_system_notice_);
            }
            rhead_active_ = nullptr;
            rhead_container_->Clear(false);

            if (!stack_rhead) {
                rhead_active_ = new wxBoxSizer(wxHORIZONTAL);
                rhead_active_->Add(lbl_editor_title_, 0, wxALIGN_CENTER_VERTICAL);
                rhead_active_->AddStretchSpacer(1);
                rhead_active_->Add(lbl_system_notice_, 0, wxALIGN_CENTER_VERTICAL);
                rhead_container_->Add(rhead_active_, 0, wxEXPAND);
            } else {
                rhead_active_ = new wxBoxSizer(wxVERTICAL);
                rhead_active_->Add(lbl_editor_title_, 0, wxEXPAND);
                rhead_active_->Add(lbl_system_notice_, 0, wxEXPAND | wxTOP, FromDIP(2));
                rhead_container_->Add(rhead_active_, 0, wxEXPAND);
            }
            rhead_stacked_ = stack_rhead;
            rhead_initialized_ = true;
            last_rhead_wrap_w_ = -1;
        }

        const int wrap_w = std::max(FromDIP(80), right_inner_w - FromDIP(4));
        if (stack_rhead) {
            if (wrap_w != last_rhead_wrap_w_) {
                wrap_static_text_deterministic(lbl_editor_title_, editor_title_raw_, wrap_w);
                wrap_static_text_deterministic(lbl_system_notice_, system_notice_raw_, wrap_w);
                last_rhead_wrap_w_ = wrap_w;
            }
        } else {
            if (title_w > wrap_w && right_card_w > FromDIP(80)) {
                wrap_static_text_deterministic(lbl_editor_title_, editor_title_raw_, wrap_w);
            } else {
                lbl_editor_title_->SetLabel(editor_title_raw_);
                lbl_editor_title_->SetMinSize(wxDefaultSize);
                lbl_editor_title_->InvalidateBestSize();
            }
            if (notice_shown) {
                lbl_system_notice_->SetLabel(system_notice_raw_);
                lbl_system_notice_->SetMinSize(wxDefaultSize);
                lbl_system_notice_->InvalidateBestSize();
            }
            last_rhead_wrap_w_ = -1;
        }
    }

    // Regla C: formulario Nombre + Descripción en 1 fila o 2 filas (umbral calculado desde etiquetas y fuentes reales; ref plan: ~480 lógicos)
    if (form_container_ && lbl_name_ && lbl_desc_ && field_name_box_ && field_desc_box_ && txt_name_ && txt_desc_) {
        int ln_w = 0, ln_h = 0, ld_w = 0, ld_h = 0;
        lbl_name_->GetTextExtent(lbl_name_->GetLabel(), &ln_w, &ln_h);
        lbl_desc_->GetTextExtent(lbl_desc_->GetLabel(), &ld_w, &ld_h);
        int sample_name_w = 0, sample_name_h = 0, sample_desc_w = 0, sample_desc_h = 0;
        txt_name_->GetTextExtent(wxT("Supervisor Táctil"), &sample_name_w, &sample_name_h);
        txt_desc_->GetTextExtent(wxT("Prueba de centrado"), &sample_desc_w, &sample_desc_h);
        // Incluir relleno/borde interno de ui::make_text_field (14 lógicos) + margen de lectura (20 lógicos)
        const int min_name_field_w = sample_name_w + FromDIP(34);
        const int min_desc_field_w = sample_desc_w + FromDIP(34);
        const int need_form_one_row = ln_w + FromDIP(6) + min_name_field_w
                                    + FromDIP(10) + ld_w + FromDIP(6) + min_desc_field_w;
        const bool single_row = (right_inner_w >= need_form_one_row);

        if (!form_initialized_ || single_row != form_single_row_) {
            if (form_active_) {
                form_active_->Detach(lbl_name_);
                form_active_->Detach(field_name_box_);
                form_active_->Detach(lbl_desc_);
                form_active_->Detach(field_desc_box_);
            }
            form_active_ = nullptr;
            form_container_->Clear(false);

            if (single_row) {
                auto* row = new wxBoxSizer(wxHORIZONTAL);
                row->Add(lbl_name_, 0, wxALIGN_CENTER_VERTICAL);
                row->Add(field_name_box_, 15, wxEXPAND | wxLEFT, FromDIP(6));
                row->Add(lbl_desc_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
                row->Add(field_desc_box_, 17, wxEXPAND | wxLEFT, FromDIP(6));
                form_active_ = row;
            } else {
                auto* grid = new wxFlexGridSizer(2, 2, FromDIP(6), FromDIP(8));
                grid->AddGrowableCol(1, 1);
                grid->Add(lbl_name_, 0, wxALIGN_CENTER_VERTICAL);
                grid->Add(field_name_box_, 1, wxEXPAND);
                grid->Add(lbl_desc_, 0, wxALIGN_CENTER_VERTICAL);
                grid->Add(field_desc_box_, 1, wxEXPAND);
                form_active_ = grid;
            }
            form_container_->Add(form_active_, 0, wxEXPAND);
            form_single_row_ = single_row;
            form_initialized_ = true;
        }
    }

    // Encabezado de permisos + lbl_form_feedback_ (oculto si vacío; en línea si cabe para no restar alto vertical)
    if (perm_header_container_ && lbl_p_header_ && lbl_form_feedback_) {
        const bool fb_shown = lbl_form_feedback_->IsShown() && !form_feedback_raw_.empty();
        int ph_w = 0, ph_h = 0, fb_w = 0, fb_h = 0;
        lbl_p_header_->GetTextExtent(lbl_p_header_->GetLabel(), &ph_w, &ph_h);
        if (fb_shown) {
            lbl_form_feedback_->GetTextExtent(form_feedback_raw_, &fb_w, &fb_h);
        }
        const int need_phead_one_row = ph_w + FromDIP(10) + fb_w + FromDIP(4);
        const bool stack_phead = fb_shown && (right_card_w > FromDIP(80)) && (right_inner_w < need_phead_one_row);

        if (!phead_initialized_ || stack_phead != phead_stacked_) {
            if (perm_header_active_) {
                perm_header_active_->Detach(lbl_p_header_);
                perm_header_active_->Detach(lbl_form_feedback_);
            }
            perm_header_active_ = nullptr;
            perm_header_container_->Clear(false);

            if (!stack_phead) {
                perm_header_active_ = new wxBoxSizer(wxHORIZONTAL);
                perm_header_active_->Add(lbl_p_header_, 0, wxALIGN_CENTER_VERTICAL);
                perm_header_active_->AddStretchSpacer(1);
                perm_header_active_->Add(lbl_form_feedback_, 0, wxALIGN_CENTER_VERTICAL);
                perm_header_container_->Add(perm_header_active_, 0, wxEXPAND);
            } else {
                perm_header_active_ = new wxBoxSizer(wxVERTICAL);
                perm_header_active_->Add(lbl_p_header_, 0, wxEXPAND);
                perm_header_active_->Add(lbl_form_feedback_, 0, wxEXPAND | wxTOP, FromDIP(2));
                perm_header_container_->Add(perm_header_active_, 0, wxEXPAND);
            }
            phead_stacked_ = stack_phead;
            phead_initialized_ = true;
            last_phead_wrap_w_ = -1;
        }

        if (fb_shown) {
            if (stack_phead) {
                const int wrap_w = std::max(FromDIP(80), right_inner_w - FromDIP(4));
                if (wrap_w != last_phead_wrap_w_) {
                    wrap_static_text_deterministic(lbl_form_feedback_, form_feedback_raw_, wrap_w);
                    last_phead_wrap_w_ = wrap_w;
                }
            } else {
                lbl_form_feedback_->SetLabel(form_feedback_raw_);
                lbl_form_feedback_->SetMinSize(wxDefaultSize);
                lbl_form_feedback_->InvalidateBestSize();
                last_phead_wrap_w_ = -1;
            }
        }
    }
}

void BackofficeRolesPanel::set_form_feedback(const wxString& message, const wxColour& colour) {
    if (!lbl_form_feedback_) return;
    form_feedback_raw_ = message;
    if (message.empty()) {
        lbl_form_feedback_->SetLabel(wxEmptyString);
        lbl_form_feedback_->SetMinSize(wxDefaultSize);
        lbl_form_feedback_->Show(false);
    } else {
        lbl_form_feedback_->SetForegroundColour(colour);
        lbl_form_feedback_->SetLabel(message);
        lbl_form_feedback_->Show(true);
    }
    last_phead_wrap_w_ = -1;
    update_responsive_layout();
    if (right_card_) {
        right_card_->Layout();
    }
    Layout();
}

void BackofficeRolesPanel::update_permission_matrix_layout() {
    if (!scroll_perms_ || perm_rows_.empty() || in_matrix_layout_) return;
    in_matrix_layout_ = true;

    int client_w = scroll_perms_->GetClientSize().GetWidth();
    const int win_w = scroll_perms_->GetSize().GetWidth();
    if (client_w <= FromDIP(60)) {
        const int panel_w = GetClientSize().GetWidth();
        int left_w = left_card_ ? left_card_->GetSize().GetWidth() : FromDIP(Theme::is_touch() ? 376 : 360);
        if (left_w <= FromDIP(100)) left_w = FromDIP(Theme::is_touch() ? 376 : 360);
        const int right_card_w = std::max(FromDIP(200), panel_w - left_w - FromDIP(24));
        client_w = std::max(FromDIP(160), right_card_w - FromDIP(32));
    }
    if (win_w <= 0 || client_w >= win_w - 2) {
        int sb_w = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, scroll_perms_);
        if (sb_w <= 0) sb_w = FromDIP(18);
        client_w = std::max(FromDIP(140), client_w - sb_w);
    }

    const int avail_w = std::max(FromDIP(140), client_w);

    const int pad_x = FromDIP(10);
    const int pad_y = FromDIP(8);
    const int title_desc_gap = FromDIP(4);
    const int text_switch_gap = FromDIP(12);
    const int col_gap = FromDIP(12);
    const int min_row_h = FromDIP(Theme::is_touch() ? 68 : 52);

    // Regla D: umbral de 2 columnas calculado en tiempo de ejecución desde los controles reales
    // (títulos, descripciones, interruptor y márgenes; referencia del plan: ~620 lógicos).
    std::vector<int> title_ws;
    std::vector<int> desc_ws;
    title_ws.reserve(perm_rows_.size());
    desc_ws.reserve(perm_rows_.size());
    int sw_w = FromDIP(Theme::is_touch() ? 60 : 44);
    for (const auto& row : perm_rows_) {
        if (row.toggle) {
            sw_w = std::max(sw_w, row.toggle->GetBestSize().GetWidth());
        }
        if (row.lbl_title) {
            int tw = 0, th = 0;
            row.lbl_title->GetTextExtent(wxString::FromUTF8(row.perm.title.c_str()), &tw, &th);
            title_ws.push_back(tw);
        }
        if (row.lbl_desc) {
            int dw = 0, dh = 0;
            row.lbl_desc->GetTextExtent(wxString::FromUTF8(row.perm.description.c_str()), &dw, &dh);
            desc_ws.push_back(dw);
        }
    }
    std::sort(title_ws.begin(), title_ws.end());
    std::sort(desc_ws.begin(), desc_ws.end());
    const int p85_title_w = title_ws.empty() ? FromDIP(210) : title_ws[(title_ws.size() * 85) / 100];
    const int p50_desc_w = desc_ws.empty() ? FromDIP(380) : desc_ws[desc_ws.size() / 2];
    const int min_text_w = std::max(p85_title_w, (p50_desc_w + 1) / 2 + FromDIP(22));
    const int min_cell_w = pad_x + min_text_w + text_switch_gap + sw_w + pad_x + FromDIP(4);
    const int need_two_cols = 2 * min_cell_w + col_gap;
    const bool want_two_cols = (avail_w >= need_two_cols);

    if (matrix_sizers_built_ && want_two_cols == matrix_two_cols_ && avail_w == last_matrix_avail_w_) {
        in_matrix_layout_ = false;
        return;
    }

    if (!matrix_sizers_built_ || want_two_cols != matrix_two_cols_) {
        rebuild_permission_sizers(want_two_cols);
    }
    last_matrix_avail_w_ = avail_w;

    for (auto* sep : perm_separators_) {
        if (sep) {
            sep->SetMinSize(wxSize(avail_w, 1));
            sep->SetMaxSize(wxSize(avail_w, 1));
        }
    }

    auto measure_and_wrap_row = [&](PermissionRow& row, int cell_w) -> int {
        if (!row.row_panel || !row.lbl_title || !row.lbl_desc || !row.toggle) return min_row_h;
        const wxSize sw_sz = row.toggle->GetBestSize();
        const int avail_text_w = std::max(
            FromDIP(60),
            cell_w - pad_x - text_switch_gap - sw_sz.GetWidth() - pad_x - FromDIP(4)
        );

        const std::string wrapped_title = wrap_text_to_width(row.lbl_title, row.perm.title, avail_text_w);
        const std::string wrapped_desc = wrap_text_to_width(row.lbl_desc, row.perm.description, avail_text_w);

        const wxString wt = wxString::FromUTF8(wrapped_title.c_str());
        const wxString wd = wxString::FromUTF8(wrapped_desc.c_str());

        row.lbl_title->SetMinSize(wxDefaultSize);
        row.lbl_title->SetLabel(wt);
        row.lbl_title->InvalidateBestSize();
        const int title_h = std::max(
            row.lbl_title->GetBestSize().GetHeight(),
            measure_multiline_height(row.lbl_title, wt)
        );
        row.lbl_title->SetMinSize(wxSize(-1, title_h));

        row.lbl_desc->SetMinSize(wxDefaultSize);
        row.lbl_desc->SetLabel(wd);
        row.lbl_desc->InvalidateBestSize();
        const int desc_h = std::max(
            row.lbl_desc->GetBestSize().GetHeight(),
            measure_multiline_height(row.lbl_desc, wd)
        );
        row.lbl_desc->SetMinSize(wxSize(-1, desc_h));

        const int text_block_h = pad_y + title_h + title_desc_gap + desc_h + pad_y;
        return std::max({min_row_h, text_block_h, sw_sz.GetHeight() + 2 * pad_y});
    };

    if (!want_two_cols) {
        for (auto& row : perm_rows_) {
            const int row_h = measure_and_wrap_row(row, avail_w);
            row.row_panel->SetMinSize(wxSize(avail_w, row_h));
            row.row_panel->SetMaxSize(wxSize(avail_w, row_h));
            row.row_panel->SetSize(wxSize(avail_w, row_h));
            row.row_panel->Layout();
        }
    } else {
        const int col_w = std::max(FromDIP(120), (avail_w - col_gap) / 2);
        for (auto& grp : perm_groups_) {
            for (size_t j = 0; j < grp.row_indices.size(); j += 2) {
                auto& left_row = perm_rows_[grp.row_indices[j]];
                const int h_left = measure_and_wrap_row(left_row, col_w);
                if (j + 1 < grp.row_indices.size()) {
                    auto& right_row = perm_rows_[grp.row_indices[j + 1]];
                    const int h_right = measure_and_wrap_row(right_row, col_w);
                    const int pair_h = std::max(h_left, h_right);
                    left_row.row_panel->SetMinSize(wxSize(col_w, pair_h));
                    left_row.row_panel->SetMaxSize(wxSize(col_w, pair_h));
                    left_row.row_panel->SetSize(wxSize(col_w, pair_h));
                    left_row.row_panel->Layout();
                    right_row.row_panel->SetMinSize(wxSize(col_w, pair_h));
                    right_row.row_panel->SetMaxSize(wxSize(col_w, pair_h));
                    right_row.row_panel->SetSize(wxSize(col_w, pair_h));
                    right_row.row_panel->Layout();
                } else {
                    left_row.row_panel->SetMinSize(wxSize(col_w, h_left));
                    left_row.row_panel->SetMaxSize(wxSize(col_w, h_left));
                    left_row.row_panel->SetSize(wxSize(col_w, h_left));
                    left_row.row_panel->Layout();
                }
            }
        }
    }

    perms_sizer_->SetMinSize(wxSize(avail_w, -1));
    scroll_perms_->FitInside();
    scroll_perms_->Layout();

    in_matrix_layout_ = false;
}

void BackofficeRolesPanel::update_permission_count() {
    if (!lbl_perm_count_) return;
    size_t enabled_count = 0;
    for (const auto& row : perm_rows_) {
        if (row.toggle && row.toggle->GetValue()) {
            ++enabled_count;
        }
    }
    lbl_perm_count_->SetLabel(wxString::Format(
        wxT("%zu de %zu permisos"),
        enabled_count,
        perm_rows_.size()
    ));
    if (right_card_) {
        right_card_->Layout();
    }
}

void BackofficeRolesPanel::set_permissions_editable(bool editable) {
    const wxCursor row_cursor = editable ? wxCursor(wxCURSOR_HAND) : wxNullCursor;
    for (auto& row : perm_rows_) {
        if (row.toggle) {
            row.toggle->Enable(editable);
        }
        if (row.row_panel) {
            row.row_panel->SetCursor(row_cursor);
        }
        if (row.lbl_title) {
            row.lbl_title->SetCursor(row_cursor);
        }
        if (row.lbl_desc) {
            row.lbl_desc->SetCursor(row_cursor);
        }
    }
}

void BackofficeRolesPanel::auto_size_columns() {
    if (!list_roles_) return;

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
    std::vector<ui::ColumnSpec> cols(4);
    cols[0] = {wxT("Nombre"), content0, sys_content0, 16, img_pad};
    cols[1] = {wxT("Descripción"), 0, 0, 16, 0};
    cols[2] = {wxT("Sistema"), content2, -1, 18, 0};
    cols[3] = {wxT("Permisos"), content3, -1, 18, 0};
    ui::distribute_list_columns(list_roles_, cols, 1);  // Descripción toma el espacio restante
}

void BackofficeRolesPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    if (in_size_event_) return;
    in_size_event_ = true;

    update_responsive_layout();
    Layout();
    // Tras aplicar el reparto 44:56 / 38:62, ajustar encabezados/formulario al ancho real de right_card_.
    update_responsive_layout();
    Layout();
    auto_size_columns();
    update_permission_matrix_layout();

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
    auto alive = alive_;
    CallAfter([this, alive]() {
        if (!*alive) return;
        auto_size_columns();
        update_responsive_layout();
        Layout();
        update_permission_matrix_layout();
    });
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

    editor_title_raw_ = wxString::Format(wxT("Rol: %s"), wxString::FromUTF8(r.name.c_str()));
    lbl_editor_title_->SetLabel(editor_title_raw_);
    txt_name_->SetValue(wxString::FromUTF8(r.name.c_str()));
    txt_desc_->SetValue(wxString::FromUTF8(r.description.c_str()));
    set_form_feedback(wxEmptyString, Theme::ColorTextSecondary);

    bool is_cashier = (r.id == "role-cashier");

    bool can_edit_fields = !r.is_system && can_manage_;
    bool can_edit_perms = (!r.is_system || is_cashier) && can_manage_;
    bool can_save = (!r.is_system || is_cashier) && can_manage_;
    bool can_delete = !r.is_system && can_manage_;

    if (!can_manage_) {
        system_notice_raw_ = wxT("Sin permiso roles.manage (solo lectura)");
        lbl_system_notice_->SetLabel(system_notice_raw_);
        lbl_system_notice_->Show(true);
    } else if (is_cashier) {
        system_notice_raw_ = wxT("Rol de sistema: solo se pueden cambiar sus permisos.");
        lbl_system_notice_->SetLabel(system_notice_raw_);
        lbl_system_notice_->Show(true);
    } else if (r.is_system) {
        system_notice_raw_ = wxT("Rol de sistema (solo lectura: no editable ni eliminable)");
        lbl_system_notice_->SetLabel(system_notice_raw_);
        lbl_system_notice_->Show(true);
    } else {
        system_notice_raw_.clear();
        lbl_system_notice_->Hide();
    }
    last_rhead_wrap_w_ = -1;

    ui::set_text_field_enabled(txt_name_, can_edit_fields);
    ui::set_text_field_enabled(txt_desc_, can_edit_fields);
    btn_save_->Enable(can_save);
    btn_delete_->Enable(can_delete);

    for (auto& [perm_id, toggle] : perm_toggles_) {
        bool has = std::find(r.permissions.begin(), r.permissions.end(), perm_id) != r.permissions.end();
        toggle->SetValue(has);
    }
    set_permissions_editable(can_edit_perms);
    update_permission_count();

    update_responsive_layout();
    if (right_card_) right_card_->Layout();
    Layout();
    update_permission_matrix_layout();
}

void BackofficeRolesPanel::reset_form_to_empty() {
    selected_index_ = -1;
    is_creating_new_ = false;
    editor_title_raw_ = wxT("Detalle del Rol");
    lbl_editor_title_->SetLabel(editor_title_raw_);
    system_notice_raw_.clear();
    lbl_system_notice_->Hide();
    last_rhead_wrap_w_ = -1;
    set_form_feedback(wxEmptyString, Theme::ColorTextSecondary);
    txt_name_->SetValue(wxEmptyString);
    txt_desc_->SetValue(wxEmptyString);
    ui::set_text_field_enabled(txt_name_, false);
    ui::set_text_field_enabled(txt_desc_, false);
    btn_save_->Enable(false);
    btn_delete_->Enable(false);

    for (auto& [perm_id, toggle] : perm_toggles_) {
        toggle->SetValue(false);
    }
    set_permissions_editable(false);
    update_permission_count();

    update_responsive_layout();
    if (right_card_) right_card_->Layout();
    Layout();
    update_permission_matrix_layout();
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

    editor_title_raw_ = wxT("Nuevo Rol Personalizado");
    lbl_editor_title_->SetLabel(editor_title_raw_);
    system_notice_raw_.clear();
    lbl_system_notice_->Hide();
    last_rhead_wrap_w_ = -1;
    set_form_feedback(wxEmptyString, Theme::ColorTextSecondary);

    txt_name_->SetValue(wxEmptyString);
    txt_desc_->SetValue(wxEmptyString);
    ui::set_text_field_enabled(txt_name_, true);
    ui::set_text_field_enabled(txt_desc_, true);

    for (auto& [perm_id, toggle] : perm_toggles_) {
        toggle->SetValue(false);
    }
    set_permissions_editable(true);
    update_permission_count();

    btn_save_->Enable(true);
    btn_delete_->Enable(false); // No se puede eliminar antes de crearlo

    txt_name_->SetFocus();
    update_responsive_layout();
    if (right_card_) right_card_->Layout();
    Layout();
    update_permission_matrix_layout();
}

void BackofficeRolesPanel::on_save_role(wxCommandEvent&) {
    std::string name = std::string(txt_name_->GetValue().Trim().ToUTF8());
    std::string desc = std::string(txt_desc_->GetValue().Trim().ToUTF8());

    if (name.empty()) {
        set_form_feedback(wxT("El nombre del rol es requerido."), Theme::ColorError);
        return;
    }

    std::vector<std::string> perms;
    for (const auto& p : all_permissions_) {
        auto it = perm_toggles_.find(p.id);
        if (it != perm_toggles_.end() && it->second && it->second->GetValue()) {
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
            set_form_feedback(wxString::FromUTF8(res.error_message().c_str()), Theme::ColorError);
            wxMessageBox(
                wxString::FromUTF8(res.error_message().c_str()),
                wxT("Error al Crear Rol"),
                wxOK | wxICON_ERROR,
                this
            );
            return;
        }

        set_form_feedback(wxT("Rol creado exitosamente."), Theme::ColorSuccess);
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
            set_form_feedback(wxString::FromUTF8(res.error_message().c_str()), Theme::ColorError);
            wxMessageBox(
                wxString::FromUTF8(res.error_message().c_str()),
                wxT("Error al Modificar Rol"),
                wxOK | wxICON_ERROR,
                this
            );
            return;
        }

        set_form_feedback(wxT("Rol actualizado exitosamente."), Theme::ColorSuccess);
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
        set_form_feedback(wxString::FromUTF8(res.error_message().c_str()), Theme::ColorError);
        wxMessageBox(
            wxString::FromUTF8(res.error_message().c_str()),
            wxT("Error al Eliminar Rol"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    set_form_feedback(wxT("Rol eliminado exitosamente."), Theme::ColorSuccess);
    refresh_roles("");
}

} // namespace app::frontend
