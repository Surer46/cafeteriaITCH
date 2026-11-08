#include "src/frontend/backoffice_users_panel.hpp"
#include "src/frontend/theme.hpp"
#include <wx/filepicker.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <fstream>

namespace app::frontend {

namespace {

class CreateUserDialog : public wxDialog {
public:
    CreateUserDialog(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service)
        : wxDialog(parent, wxID_ANY, wxT("Nuevo Usuario de Sistema"),
                   wxDefaultPosition, wxSize(480, 520), wxDEFAULT_DIALOG_STYLE),
          account_service_(std::move(account_service)) {

        SetBackgroundColour(wxColour(250, 250, 252));
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, wxT("Registrar Cuenta de Usuario"));
        wxFont tf = title->GetFont();
        tf.SetPointSize(11);
        tf.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(tf);
        title->SetForegroundColour(wxColour(138, 3, 3));
        root->Add(title, 0, wxALL | wxALIGN_CENTER, 12);

        auto* form = new wxFlexGridSizer(6, 2, 10, 12);
        form->AddGrowableCol(1, 1);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Nombre completo:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_display_name_ = new wxTextCtrl(this, wxID_ANY);
        form->Add(txt_display_name_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Usuario:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_username_ = new wxTextCtrl(this, wxID_ANY);
        form->Add(txt_username_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Correo electrónico:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_email_ = new wxTextCtrl(this, wxID_ANY);
        form->Add(txt_email_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_password_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        form->Add(txt_password_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Rol:")), 0, wxALIGN_CENTER_VERTICAL);
        choice_role_ = new wxChoice(this, wxID_ANY);
        populate_roles();
        form->Add(choice_role_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Avatar (opcional):")), 0, wxALIGN_CENTER_VERTICAL);
        file_picker_avatar_ = new wxFilePickerCtrl(
            this, wxID_ANY, wxEmptyString, wxT("Seleccionar imagen de avatar"),
            wxT("Imágenes (*.png;*.jpg;*.jpeg;*.webp)|*.png;*.jpg;*.jpeg;*.webp"),
            wxDefaultPosition, wxDefaultSize, wxFLP_OPEN | wxFLP_FILE_MUST_EXIST
        );
        form->Add(file_picker_avatar_, 1, wxEXPAND);

        root->Add(form, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);

        lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
        lbl_error_->SetForegroundColour(wxColour(200, 0, 0));
        root->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, 8);

        auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* btn_cancel = new wxButton(this, wxID_CANCEL, wxT("Cancelar"));
        auto* btn_save = new wxButton(this, wxID_OK, wxT("Guardar Usuario"));
        btn_save->SetBackgroundColour(wxColour(180, 20, 30));
        btn_save->SetForegroundColour(wxColour(255, 255, 255));

        btn_save->Bind(wxEVT_BUTTON, &CreateUserDialog::on_save, this);

        btn_sizer->Add(btn_cancel, 0, wxRIGHT, 10);
        btn_sizer->Add(btn_save, 0);
        root->Add(btn_sizer, 0, wxALIGN_RIGHT | wxALL, 16);

        SetSizer(root);
        Centre();
    }

private:
    void populate_roles() {
        if (!account_service_) return;
        auto roles_res = account_service_->list_roles();
        if (roles_res.is_success()) {
            for (const auto& r : roles_res.value()) {
                // El rol Desarrollador no puede asignarse por UI regular
                if (r.id == "role-dev") continue;
                int idx = choice_role_->Append(wxString::FromUTF8(r.name.c_str()), new wxStringClientData(wxString::FromUTF8(r.id.c_str())));
                if (r.id == "role-cashier") {
                    choice_role_->SetSelection(idx);
                }
            }
            if (choice_role_->GetSelection() == wxNOT_FOUND && choice_role_->GetCount() > 0) {
                choice_role_->SetSelection(0);
            }
        }
    }

    void on_save(wxCommandEvent&) {
        std::string display_name = txt_display_name_->GetValue().ToStdString();
        std::string username = txt_username_->GetValue().ToStdString();
        std::string email = txt_email_->GetValue().ToStdString();
        std::string password = txt_password_->GetValue().ToStdString();

        if (display_name.empty() || username.empty() || email.empty() || password.empty()) {
            lbl_error_->SetLabel(wxT("Nombre, usuario, correo y contraseña son obligatorios."));
            Layout();
            return;
        }

        int sel = choice_role_->GetSelection();
        if (sel == wxNOT_FOUND) {
            lbl_error_->SetLabel(wxT("Debe seleccionar un rol para el usuario."));
            Layout();
            return;
        }

        auto* cd = static_cast<wxStringClientData*>(choice_role_->GetClientObject(sel));
        std::string role_id = cd ? cd->GetData().ToStdString() : "";

        contracts::CreateUserDto dto;
        dto.display_name = display_name;
        dto.username = username;
        dto.email = email;
        dto.raw_password = password;
        dto.role_id = role_id;

        wxString avatar_path = file_picker_avatar_->GetPath();
        if (!avatar_path.empty()) {
            std::ifstream file(avatar_path.ToStdString(), std::ios::binary);
            if (file.is_open()) {
                std::vector<std::uint8_t> bytes(
                    (std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>()
                );
                dto.avatar_bytes = std::move(bytes);
            }
        }

        auto res = account_service_->create_user(dto);
        if (!res.is_success()) {
            lbl_error_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
            Layout();
            return;
        }

        EndModal(wxID_OK);
    }

    std::shared_ptr<contracts::IAccountService> account_service_;
    wxTextCtrl* txt_display_name_{nullptr};
    wxTextCtrl* txt_username_{nullptr};
    wxTextCtrl* txt_email_{nullptr};
    wxTextCtrl* txt_password_{nullptr};
    wxChoice* choice_role_{nullptr};
    wxFilePickerCtrl* file_picker_avatar_{nullptr};
    wxStaticText* lbl_error_{nullptr};
};

class ChangePasswordDialog : public wxDialog {
public:
    ChangePasswordDialog(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service, const std::string& user_id, const std::string& username)
        : wxDialog(parent, wxID_ANY, wxT("Cambiar Contraseña"),
                   wxDefaultPosition, wxSize(400, 260), wxDEFAULT_DIALOG_STYLE),
          account_service_(std::move(account_service)), user_id_(user_id), username_(username) {

        SetBackgroundColour(wxColour(250, 250, 252));
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, wxString::Format(wxT("Nueva contraseña para '%s'"), wxString::FromUTF8(username_.c_str())));
        wxFont tf = title->GetFont();
        tf.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(tf);
        root->Add(title, 0, wxALL | wxALIGN_CENTER, 12);

        auto* form = new wxFlexGridSizer(2, 2, 10, 12);
        form->AddGrowableCol(1, 1);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Nueva contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_new_pwd_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        form->Add(txt_new_pwd_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Confirmar contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_confirm_pwd_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        form->Add(txt_confirm_pwd_, 1, wxEXPAND);

        root->Add(form, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

        lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
        lbl_error_->SetForegroundColour(wxColour(200, 0, 0));
        root->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, 8);

        auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* btn_cancel = new wxButton(this, wxID_CANCEL, wxT("Cancelar"));
        auto* btn_save = new wxButton(this, wxID_OK, wxT("Actualizar"));
        btn_save->Bind(wxEVT_BUTTON, &ChangePasswordDialog::on_save, this);

        btn_sizer->Add(btn_cancel, 0, wxRIGHT, 10);
        btn_sizer->Add(btn_save, 0);
        root->Add(btn_sizer, 0, wxALIGN_RIGHT | wxALL, 14);

        SetSizer(root);
        Centre();
    }

private:
    void on_save(wxCommandEvent&) {
        std::string p1 = txt_new_pwd_->GetValue().ToStdString();
        std::string p2 = txt_confirm_pwd_->GetValue().ToStdString();

        if (p1.empty() || p2.empty()) {
            lbl_error_->SetLabel(wxT("Ambos campos son obligatorios."));
            Layout();
            return;
        }
        if (p1 != p2) {
            lbl_error_->SetLabel(wxT("Las contraseñas no coinciden."));
            Layout();
            return;
        }

        auto res = account_service_->change_password(user_id_, p1);
        if (!res.is_success()) {
            lbl_error_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
            Layout();
            return;
        }

        EndModal(wxID_OK);
    }

    std::shared_ptr<contracts::IAccountService> account_service_;
    std::string user_id_;
    std::string username_;
    wxTextCtrl* txt_new_pwd_{nullptr};
    wxTextCtrl* txt_confirm_pwd_{nullptr};
    wxStaticText* lbl_error_{nullptr};
};

} // namespace

BackofficeUsersPanel::BackofficeUsersPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::IAccountService> account_service
) : wxPanel(parent, wxID_ANY), account_service_(std::move(account_service)) {
    setup_ui();
    refresh_users();
}

void BackofficeUsersPanel::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    auto* root = new wxBoxSizer(wxVERTICAL);

    // Barra superior de acciones
    auto* top_bar = new wxBoxSizer(wxHORIZONTAL);

    auto* title = new wxStaticText(this, wxID_ANY, wxT("Usuarios y Cuentas de Acceso"));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    top_bar->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    int btn_h = Theme::is_touch() ? 48 : 36;

    btn_new_user_ = new wxButton(this, wxID_ANY, wxT("Nuevo Usuario"), wxDefaultPosition, wxSize(160, btn_h));
    Theme::style_button_primary(btn_new_user_, btn_h);
    Theme::setup_button_icon(btn_new_user_, "add", wxSize(18, 18), Theme::ColorSurface);
    btn_new_user_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_new_user, this);
    top_bar->Add(btn_new_user_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    btn_refresh_ = new wxButton(this, wxID_ANY, wxT("Actualizar"), wxDefaultPosition, wxSize(110, btn_h));
    Theme::style_button_secondary(btn_refresh_, btn_h);
    btn_refresh_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_users(); });
    top_bar->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);

    top_bar->AddStretchSpacer(1);

    lbl_status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_status_->SetFont(Theme::get_font_small());
    lbl_status_->SetForegroundColour(Theme::ColorTextSecondary);
    top_bar->Add(lbl_status_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    root->Add(top_bar, 0, wxEXPAND | wxALL, 14);

    // Tabla de usuarios
    list_ctrl_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_ctrl_->SetFont(Theme::get_font_body());
    list_ctrl_->InsertColumn(0, wxT("Nombre Completo"), wxLIST_FORMAT_LEFT, 200);
    list_ctrl_->InsertColumn(1, wxT("Usuario"), wxLIST_FORMAT_LEFT, 130);
    list_ctrl_->InsertColumn(2, wxT("Correo Electrónico"), wxLIST_FORMAT_LEFT, 220);
    list_ctrl_->InsertColumn(3, wxT("Rol"), wxLIST_FORMAT_LEFT, 120);
    list_ctrl_->InsertColumn(4, wxT("Avatar"), wxLIST_FORMAT_CENTER, 70);
    list_ctrl_->InsertColumn(5, wxT("Estado"), wxLIST_FORMAT_CENTER, 90);

    list_ctrl_->Bind(wxEVT_LIST_ITEM_SELECTED, &BackofficeUsersPanel::on_item_selected, this);
    list_ctrl_->Bind(wxEVT_LIST_ITEM_DESELECTED, &BackofficeUsersPanel::on_item_deselected, this);

    root->Add(list_ctrl_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

    // Barra inferior de operaciones sobre la fila seleccionada
    auto* bottom_bar = new wxBoxSizer(wxHORIZONTAL);

    btn_change_pwd_ = new wxButton(this, wxID_ANY, wxT("Cambiar Contraseña"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_secondary(btn_change_pwd_, btn_h);
    wxSize lock_icon_sz(16, 16);
    Theme::setup_button_icon(btn_change_pwd_, "lock", lock_icon_sz, Theme::ColorTextSecondary);
    int pwd_tw = 0, pwd_th = 0;
    btn_change_pwd_->GetTextExtent(btn_change_pwd_->GetLabel(), &pwd_tw, &pwd_th);
    int lock_im = (Theme::is_touch() ? 10 : 8) * 2;
    int pwd_bp = Theme::is_touch() ? 28 : 24;
    int pwd_w = pwd_tw + lock_icon_sz.GetWidth() + lock_im + pwd_bp;
    wxSize pwd_best = btn_change_pwd_->GetBestSize();
    if (pwd_best.GetWidth() > pwd_w) {
        pwd_w = pwd_best.GetWidth();
    }
    btn_change_pwd_->SetMinSize(wxSize(pwd_w, btn_h));
    btn_change_pwd_->SetSize(wxSize(pwd_w, btn_h));
    btn_change_pwd_->Enable(false);
    btn_change_pwd_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_change_password, this);
    bottom_bar->Add(btn_change_pwd_, 0, wxRIGHT, 8);

    btn_toggle_active_ = new wxButton(this, wxID_ANY, wxT("Desactivar Cuenta"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_destructive(btn_toggle_active_, btn_h);
    int act_tw = 0, act_th = 0;
    btn_toggle_active_->GetTextExtent(btn_toggle_active_->GetLabel(), &act_tw, &act_th);
    int act_w = act_tw + (Theme::is_touch() ? 32 : 28);
    wxSize act_best = btn_toggle_active_->GetBestSize();
    if (act_best.GetWidth() > act_w) act_w = act_best.GetWidth();
    btn_toggle_active_->SetMinSize(wxSize(act_w, btn_h));
    btn_toggle_active_->SetSize(wxSize(act_w, btn_h));
    btn_toggle_active_->Enable(false);
    btn_toggle_active_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_toggle_active, this);
    bottom_bar->Add(btn_toggle_active_, 0, wxRIGHT, 8);

    btn_view_avatar_ = new wxButton(this, wxID_ANY, wxT("Ver Avatar"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_secondary(btn_view_avatar_, btn_h);
    wxSize avatar_icon_sz(16, 16);
    Theme::setup_button_icon(btn_view_avatar_, "person", avatar_icon_sz, Theme::ColorTextSecondary);
    int av_tw = 0, av_th = 0;
    btn_view_avatar_->GetTextExtent(btn_view_avatar_->GetLabel(), &av_tw, &av_th);
    int av_w = av_tw + avatar_icon_sz.GetWidth() + lock_im + pwd_bp;
    wxSize av_best = btn_view_avatar_->GetBestSize();
    if (av_best.GetWidth() > av_w) av_w = av_best.GetWidth();
    btn_view_avatar_->SetMinSize(wxSize(av_w, btn_h));
    btn_view_avatar_->SetSize(wxSize(av_w, btn_h));
    btn_view_avatar_->Enable(false);
    btn_view_avatar_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_view_avatar, this);
    bottom_bar->Add(btn_view_avatar_, 0);

    root->Add(bottom_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

    SetSizer(root);
}

void BackofficeUsersPanel::refresh_users() {
    list_ctrl_->DeleteAllItems();
    users_.clear();
    selected_index_ = -1;
    btn_change_pwd_->Enable(false);
    btn_toggle_active_->Enable(false);
    btn_view_avatar_->Enable(false);

    if (!account_service_) return;

    auto res = account_service_->list_users();
    if (!res.is_success()) {
        lbl_status_->SetLabel(wxString::Format(wxT("Error: %s"), wxString::FromUTF8(res.error_message().c_str())));
        return;
    }

    users_ = res.value();
    for (std::size_t i = 0; i < users_.size(); ++i) {
        const auto& u = users_[i];
        long idx = list_ctrl_->InsertItem(static_cast<long>(i), wxString::FromUTF8(u.display_name.c_str()));
        list_ctrl_->SetItem(idx, 1, wxString::FromUTF8(u.username.c_str()));
        list_ctrl_->SetItem(idx, 2, wxString::FromUTF8(u.email.c_str()));
        list_ctrl_->SetItem(idx, 3, wxString::FromUTF8(u.role_name.c_str()));
        list_ctrl_->SetItem(idx, 4, u.has_avatar ? wxT("Sí") : wxT("No"));
        list_ctrl_->SetItem(idx, 5, u.is_active ? wxT("Activo") : wxT("Inactivo"));

        if (!u.is_active) {
            list_ctrl_->SetItemTextColour(idx, wxColour(150, 150, 150));
        }
    }

    lbl_status_->SetLabel(wxString::Format(wxT("Total: %zu cuentas"), users_.size()));
}

void BackofficeUsersPanel::on_item_selected(wxListEvent& event) {
    selected_index_ = event.GetIndex();
    if (selected_index_ >= 0 && selected_index_ < static_cast<long>(users_.size())) {
        const auto& u = users_[selected_index_];
        btn_change_pwd_->Enable(true);
        btn_toggle_active_->Enable(true);
        btn_toggle_active_->SetLabel(u.is_active ? wxT("Desactivar Cuenta") : wxT("Reactivar Cuenta"));
        btn_view_avatar_->Enable(u.has_avatar);
    }
}

void BackofficeUsersPanel::on_item_deselected(wxListEvent&) {
    selected_index_ = -1;
    btn_change_pwd_->Enable(false);
    btn_toggle_active_->Enable(false);
    btn_view_avatar_->Enable(false);
}

void BackofficeUsersPanel::on_new_user(wxCommandEvent&) {
    CreateUserDialog dlg(this, account_service_);
    if (dlg.ShowModal() == wxID_OK) {
        refresh_users();
    }
}

void BackofficeUsersPanel::on_change_password(wxCommandEvent&) {
    if (selected_index_ < 0 || selected_index_ >= static_cast<long>(users_.size())) return;
    const auto& u = users_[selected_index_];

    ChangePasswordDialog dlg(this, account_service_, u.id, u.username);
    if (dlg.ShowModal() == wxID_OK) {
        wxMessageBox(wxT("Contraseña actualizada exitosamente."), wxT("Éxito"), wxOK | wxICON_INFORMATION, this);
    }
}

void BackofficeUsersPanel::on_toggle_active(wxCommandEvent&) {
    if (selected_index_ < 0 || selected_index_ >= static_cast<long>(users_.size())) return;
    const auto& u = users_[selected_index_];

    if (u.is_active) {
        // Comprobar si puede desactivarse
        auto can_res = account_service_->can_deactivate_user(u.id);
        if (can_res.is_success() && !can_res.value()) {
            wxMessageBox(wxT("No se puede desactivar a la última cuenta Dueño activa del sistema."),
                         wxT("Operación Denegada"), wxOK | wxICON_WARNING, this);
            return;
        }

        int answer = wxMessageBox(
            wxString::Format(wxT("¿Está seguro de que desea desactivar al usuario '%s'?"), wxString::FromUTF8(u.username.c_str())),
            wxT("Confirmar Desactivación"), wxYES_NO | wxICON_QUESTION, this
        );
        if (answer != wxYES) return;

        contracts::UpdateUserDto upd;
        upd.user_id = u.id;
        upd.is_active = false;
        auto res = account_service_->update_user(upd);
        if (!res.is_success()) {
            wxMessageBox(wxString::FromUTF8(res.error_message().c_str()), wxT("Error"), wxOK | wxICON_ERROR, this);
        }
    } else {
        contracts::UpdateUserDto upd;
        upd.user_id = u.id;
        upd.is_active = true;
        auto res = account_service_->update_user(upd);
        if (!res.is_success()) {
            wxMessageBox(wxString::FromUTF8(res.error_message().c_str()), wxT("Error"), wxOK | wxICON_ERROR, this);
        }
    }

    refresh_users();
}

void BackofficeUsersPanel::on_view_avatar(wxCommandEvent&) {
    if (selected_index_ < 0 || selected_index_ >= static_cast<long>(users_.size())) return;
    const auto& u = users_[selected_index_];

    auto av_res = account_service_->get_user_avatar(u.id);
    if (!av_res.is_success()) {
        wxMessageBox(wxString::FromUTF8(av_res.error_message().c_str()), wxT("Avatar"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto& bytes = av_res.value();
    wxMemoryInputStream mis(bytes.data(), bytes.size());
    wxImage img;
    if (!img.LoadFile(mis)) {
        wxMessageBox(wxT("No se pudo decodificar el formato de imagen del avatar."), wxT("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    // Escalar proporcionalmente a max 160x160 si es necesario
    if (img.GetWidth() > 160 || img.GetHeight() > 160) {
        img.Rescale(160, 160, wxIMAGE_QUALITY_HIGH);
    }
    wxBitmap bmp(img);

    wxDialog dlg(this, wxID_ANY, wxString::Format(wxT("Avatar de %s"), wxString::FromUTF8(u.display_name.c_str())),
                 wxDefaultPosition, wxSize(220, 260), wxDEFAULT_DIALOG_STYLE);
    auto* s = new wxBoxSizer(wxVERTICAL);
    auto* sbmp = new wxStaticBitmap(&dlg, wxID_ANY, bmp);
    s->Add(sbmp, 1, wxALL | wxALIGN_CENTER, 14);
    auto* btn_close = new wxButton(&dlg, wxID_OK, wxT("Cerrar"));
    s->Add(btn_close, 0, wxALIGN_CENTER | wxBOTTOM, 12);
    dlg.SetSizer(s);
    dlg.Centre();
    dlg.ShowModal();
}

} // namespace app::frontend
