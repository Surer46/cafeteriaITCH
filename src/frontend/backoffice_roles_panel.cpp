#include "src/frontend/backoffice_roles_panel.hpp"
#include "src/frontend/theme.hpp"
#include <algorithm>

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

std::string wrap_label(const std::string& text, std::size_t max_line_len = 40) {
    if (text.length() <= max_line_len) return text;
    std::string result;
    std::size_t start = 0;
    while (start < text.length()) {
        if (text.length() - start <= max_line_len) {
            result += text.substr(start);
            break;
        }
        std::size_t pos = text.rfind(' ', start + max_line_len);
        if (pos == std::string::npos || pos <= start) {
            pos = start + max_line_len;
        }
        result += text.substr(start, pos - start);
        result += "\n";
        start = (pos < text.length() && text[pos] == ' ') ? pos + 1 : pos;
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
    auto* left_card = new CardPanel(this, wxID_ANY);
    left_card->set_card_background(Theme::ColorSurface);
    auto* left_sizer = new wxBoxSizer(wxVERTICAL);

    auto* left_title = new wxStaticText(left_card, wxID_ANY, wxT("Roles Registrados"));
    left_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    left_title->SetForegroundColour(Theme::ColorText);
    left_sizer->Add(left_title, 0, wxALL, 10);

    list_roles_ = new wxListCtrl(left_card, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_roles_->SetFont(Theme::get_font_body());
    list_roles_->InsertColumn(0, wxT("Nombre"), wxLIST_FORMAT_LEFT, 68);
    list_roles_->InsertColumn(1, wxT("Descripción"), wxLIST_FORMAT_LEFT, 78);
    list_roles_->InsertColumn(2, wxT("Sistema"), wxLIST_FORMAT_CENTER, 38);
    list_roles_->InsertColumn(3, wxT("Permisos"), wxLIST_FORMAT_CENTER, 42);

    list_roles_->Bind(wxEVT_LIST_ITEM_SELECTED, &BackofficeRolesPanel::on_role_selected, this);
    list_roles_->Bind(wxEVT_LIST_ITEM_DESELECTED, &BackofficeRolesPanel::on_role_deselected, this);
    left_sizer->Add(list_roles_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    lbl_roles_count_ = new wxStaticText(left_card, wxID_ANY, wxEmptyString);
    lbl_roles_count_->SetFont(Theme::get_font_small());
    lbl_roles_count_->SetForegroundColour(Theme::ColorTextSecondary);
    left_sizer->Add(lbl_roles_count_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    left_card->SetSizer(left_sizer);
    left_card->SetMinSize(wxSize(230, -1));
    main_sizer->Add(left_card, 0, wxEXPAND | wxRIGHT, 10);

    // --- Columna derecha: Detalle del Rol y Matriz de Permisos ---
    auto* right_card = new CardPanel(this, wxID_ANY);
    right_card->set_card_background(Theme::ColorSurface);
    auto* right_sizer = new wxBoxSizer(wxVERTICAL);

    auto* rhead_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_editor_title_ = new wxStaticText(right_card, wxID_ANY, wxT("Detalle del Rol"));
    lbl_editor_title_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_editor_title_->SetForegroundColour(Theme::ColorText);
    rhead_sizer->Add(lbl_editor_title_, 0, wxALIGN_CENTER_VERTICAL);

    rhead_sizer->AddStretchSpacer(1);

    lbl_system_notice_ = new wxStaticText(right_card, wxID_ANY, wxT("Rol de sistema (solo lectura)"));
    lbl_system_notice_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_system_notice_->SetForegroundColour(Theme::ColorBrand);
    lbl_system_notice_->Show(false);
    rhead_sizer->Add(lbl_system_notice_, 0, wxALIGN_CENTER_VERTICAL);

    right_sizer->Add(rhead_sizer, 0, wxEXPAND | wxALL, 10);

    auto* form_grid = new wxFlexGridSizer(2, 2, 8, 10);
    form_grid->AddGrowableCol(1, 1);

    auto* lbl_n = new wxStaticText(right_card, wxID_ANY, wxT("Nombre:"));
    lbl_n->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    form_grid->Add(lbl_n, 0, wxALIGN_CENTER_VERTICAL);
    txt_name_ = new wxTextCtrl(right_card, wxID_ANY);
    txt_name_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        txt_name_->SetMinSize(wxSize(-1, 48));
    }
    form_grid->Add(txt_name_, 1, wxEXPAND);

    auto* lbl_d = new wxStaticText(right_card, wxID_ANY, wxT("Descripción:"));
    lbl_d->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    form_grid->Add(lbl_d, 0, wxALIGN_CENTER_VERTICAL);
    txt_desc_ = new wxTextCtrl(right_card, wxID_ANY);
    txt_desc_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        txt_desc_->SetMinSize(wxSize(-1, 48));
    }
    form_grid->Add(txt_desc_, 1, wxEXPAND);

    right_sizer->Add(form_grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* lbl_p_header = new wxStaticText(right_card, wxID_ANY, wxT("Matriz de Permisos por Módulo:"));
    lbl_p_header->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_p_header->SetForegroundColour(Theme::ColorTextSecondary);
    right_sizer->Add(lbl_p_header, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    scroll_perms_ = new wxScrolledWindow(
        right_card, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxVSCROLL | wxHSCROLL | wxBORDER_NONE
    );
    scroll_perms_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    scroll_perms_->SetScrollRate(10, 10);
    scroll_perms_->SetMinSize(wxSize(200, 120));
    perms_sizer_ = new wxBoxSizer(wxVERTICAL);
    scroll_perms_->SetSizer(perms_sizer_);
    right_sizer->Add(scroll_perms_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // Barra de acciones inferior
    auto* btm_sizer = new wxBoxSizer(wxHORIZONTAL);

    btn_save_ = new wxButton(right_card, wxID_ANY, wxT("Guardar"));
    style_and_size_btn(btn_save_, btn_h, "check_circle", Theme::ColorSurface, true, false);
    btn_save_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_save_role, this);
    btm_sizer->Add(btn_save_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    btn_delete_ = new wxButton(right_card, wxID_ANY, wxT("Eliminar"));
    style_and_size_btn(btn_delete_, btn_h, "delete", Theme::ColorError, false, true);
    btn_delete_->Bind(wxEVT_BUTTON, &BackofficeRolesPanel::on_delete_role, this);
    btm_sizer->Add(btn_delete_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    lbl_form_feedback_ = new wxStaticText(right_card, wxID_ANY, wxEmptyString);
    lbl_form_feedback_->SetFont(Theme::get_font_small());
    btm_sizer->Add(lbl_form_feedback_, 1, wxALIGN_CENTER_VERTICAL);

    right_sizer->Add(btm_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

    right_card->SetSizer(right_sizer);
    right_card->SetMinSize(wxSize(280, -1));
    main_sizer->Add(right_card, 1, wxEXPAND);

    root->Add(main_sizer, 1, wxEXPAND | wxALL, 12);
    SetSizer(root);
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
            std::string wrapped = wrap_label(p.description, 40);
            auto* cb = new wxCheckBox(
                scroll_perms_, wxID_ANY,
                wxString::FromUTF8(wrapped.c_str())
            );
            cb->SetFont(Theme::get_font_body(wxFONTWEIGHT_NORMAL));
            cb->SetToolTip(wxString::FromUTF8(p.id.c_str()));
            int lines = 1;
            for (char c : wrapped) {
                if (c == '\n') ++lines;
            }
            int min_h = Theme::is_touch() ? (lines > 1 ? 52 : 48) : (lines > 1 ? 38 : 26);
            cb->SetMinSize(wxSize(-1, min_h));
            cb->Enable(false); // Inicialmente deshabilitado hasta seleccionar rol
            perm_checkboxes_[p.id] = cb;
            grp_box->Add(cb, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
        }

        perms_sizer_->Add(grp_box, 0, wxEXPAND | wxBOTTOM, 8);
    }

    scroll_perms_->FitInside();
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

    bool editable = !r.is_system && can_manage_;

    if (r.is_system) {
        lbl_system_notice_->SetLabel(wxT("Rol de sistema (solo lectura: no editable ni eliminable)"));
        lbl_system_notice_->Show(true);
    } else if (!can_manage_) {
        lbl_system_notice_->SetLabel(wxT("Sin permiso roles.manage (solo lectura)"));
        lbl_system_notice_->Show(true);
    } else {
        lbl_system_notice_->Hide();
    }

    txt_name_->Enable(editable);
    txt_desc_->Enable(editable);
    btn_save_->Enable(editable);
    btn_delete_->Enable(editable);

    for (auto& [perm_id, cb] : perm_checkboxes_) {
        bool has = std::find(r.permissions.begin(), r.permissions.end(), perm_id) != r.permissions.end();
        cb->SetValue(has);
        cb->Enable(editable);
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
        if (r.is_system) {
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
        dto.name = name;
        dto.description = desc;
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
