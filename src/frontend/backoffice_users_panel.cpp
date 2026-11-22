#include "src/frontend/backoffice_users_panel.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <algorithm>
#include <array>
#include <wx/filedlg.h>
#include <wx/mstream.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <fstream>

namespace app::frontend {

namespace {

class CreateUserDialog : public wxDialog {
public:
    CreateUserDialog(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service)
        : wxDialog(parent, wxID_ANY, wxT("Nuevo Usuario de Sistema"),
                   wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
          account_service_(std::move(account_service)) {

        SetBackgroundColour(Theme::ColorBackground);
        const bool touch = Theme::is_touch();
        const int ctrl_h_logical = touch ? 48 : 34;
        const int field_h_logical = touch ? 48 : 0;
        const int btn_h_logical = touch ? 48 : 36;

        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, wxT("Registrar Cuenta de Usuario"));
        title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
        title->SetForegroundColour(Theme::ColorBrandDeep);
        root->Add(title, 0, wxALL | wxALIGN_CENTER, FromDIP(12));

        auto* form = new wxFlexGridSizer(6, 2, FromDIP(12), FromDIP(12));
        form->AddGrowableCol(1, 1);

        auto add_label = [&](const wxString& text) {
            auto* lbl = new wxStaticText(this, wxID_ANY, text);
            lbl->SetFont(Theme::get_font_body());
            lbl->SetForegroundColour(Theme::ColorText);
            form->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);
        };

        add_label(wxT("Nombre completo:"));
        form->Add(ui::make_text_field(this, txt_display_name_, field_h_logical, -1, Theme::get_font_body()), 1, wxEXPAND);

        add_label(wxT("Usuario:"));
        form->Add(ui::make_text_field(this, txt_username_, field_h_logical, -1, Theme::get_font_body()), 1, wxEXPAND);

        add_label(wxT("Correo electrónico:"));
        form->Add(ui::make_text_field(this, txt_email_, field_h_logical, -1, Theme::get_font_body()), 1, wxEXPAND);

        add_label(wxT("Contraseña:"));
        form->Add(ui::make_text_field(this, txt_password_, field_h_logical, -1, Theme::get_font_body(), wxTE_PASSWORD), 1, wxEXPAND);

        add_label(wxT("Rol:"));
        choice_role_ = new ui::TouchComboBox(this, wxID_ANY, FromDIP(wxSize(-1, ctrl_h_logical)));
        choice_role_->SetFont(Theme::get_font_body());
        choice_role_->SetMinSize(FromDIP(wxSize(-1, ctrl_h_logical)));
        populate_roles();
        form->Add(choice_role_, 1, wxEXPAND);

        add_label(wxT("Foto de perfil (opcional):"));
        auto* avatar_row = new wxBoxSizer(wxHORIZONTAL);
        auto* btn_choose_avatar = new wxButton(this, wxID_ANY, wxT("Elegir foto..."));
        Theme::style_button_secondary(btn_choose_avatar, btn_h_logical);
        Theme::ensure_button_min_size(btn_choose_avatar, 140, btn_h_logical);
        lbl_avatar_file_ = new wxStaticText(this, wxID_ANY, wxT("Ningún archivo elegido"),
                                            wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        lbl_avatar_file_->SetFont(Theme::get_font_small());
        lbl_avatar_file_->SetForegroundColour(Theme::ColorTextSecondary);
        btn_choose_avatar->Bind(wxEVT_BUTTON, &CreateUserDialog::on_choose_avatar, this);
        avatar_row->Add(btn_choose_avatar, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        avatar_row->Add(lbl_avatar_file_, 1, wxALIGN_CENTER_VERTICAL);
        form->Add(avatar_row, 1, wxEXPAND);

        root->Add(form, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

        lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
        lbl_error_->SetFont(Theme::get_font_small());
        lbl_error_->SetForegroundColour(Theme::ColorError);
        root->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, FromDIP(8));

        auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* btn_cancel = new wxButton(this, wxID_CANCEL, wxT("Cancelar"));
        Theme::style_button_secondary(btn_cancel, btn_h_logical);
        Theme::ensure_button_min_size(btn_cancel, 110, btn_h_logical);

        auto* btn_save = new wxButton(this, wxID_OK, wxT("Guardar Usuario"));
        Theme::style_button_primary(btn_save, btn_h_logical);
        Theme::ensure_button_min_size(btn_save, 160, btn_h_logical);

        btn_save->Bind(wxEVT_BUTTON, &CreateUserDialog::on_save, this);

        btn_sizer->Add(btn_cancel, 0, wxRIGHT, FromDIP(10));
        btn_sizer->Add(btn_save, 0);
        root->Add(btn_sizer, 0, wxALIGN_RIGHT | wxALL, FromDIP(16));

        root->SetMinSize(FromDIP(wxSize(touch ? 560 : 480, -1)));
        SetSizerAndFit(root);
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

        wxString avatar_path = avatar_path_;
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

    void on_choose_avatar(wxCommandEvent&) {
        wxFileDialog dlg(
            this, wxT("Seleccionar foto de perfil"), wxEmptyString, wxEmptyString,
            wxT("Imágenes (*.png;*.jpg;*.jpeg;*.webp)|*.png;*.jpg;*.jpeg;*.webp"),
            wxFD_OPEN | wxFD_FILE_MUST_EXIST
        );
        if (dlg.ShowModal() == wxID_OK) {
            avatar_path_ = dlg.GetPath();
            lbl_avatar_file_->SetLabel(dlg.GetFilename());
            Layout();
        }
    }

    std::shared_ptr<contracts::IAccountService> account_service_;
    wxTextCtrl* txt_display_name_{nullptr};
    wxTextCtrl* txt_username_{nullptr};
    wxTextCtrl* txt_email_{nullptr};
    wxTextCtrl* txt_password_{nullptr};
    ui::TouchComboBox* choice_role_{nullptr};
    wxStaticText* lbl_avatar_file_{nullptr};
    wxString avatar_path_;
    wxStaticText* lbl_error_{nullptr};
};

class ChangePasswordDialog : public wxDialog {
public:
    ChangePasswordDialog(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service, const std::string& user_id, const std::string& username)
        : wxDialog(parent, wxID_ANY, wxT("Cambiar Contraseña"),
                   wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
          account_service_(std::move(account_service)), user_id_(user_id), username_(username) {

        SetBackgroundColour(wxColour(250, 250, 252));
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* title = new wxStaticText(this, wxID_ANY, wxString::Format(wxT("Nueva contraseña para '%s'"), wxString::FromUTF8(username_.c_str())));
        wxFont tf = title->GetFont();
        tf.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(tf);
        root->Add(title, 0, wxALL | wxALIGN_CENTER, FromDIP(12));

        auto* form = new wxFlexGridSizer(2, 2, FromDIP(10), FromDIP(12));
        form->AddGrowableCol(1, 1);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Nueva contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_new_pwd_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        form->Add(txt_new_pwd_, 1, wxEXPAND);

        form->Add(new wxStaticText(this, wxID_ANY, wxT("Confirmar contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
        txt_confirm_pwd_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
        form->Add(txt_confirm_pwd_, 1, wxEXPAND);

        root->Add(form, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

        lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
        lbl_error_->SetForegroundColour(wxColour(200, 0, 0));
        root->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, FromDIP(8));

        auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto* btn_cancel = new wxButton(this, wxID_CANCEL, wxT("Cancelar"));
        auto* btn_save = new wxButton(this, wxID_OK, wxT("Actualizar"));
        btn_save->Bind(wxEVT_BUTTON, &ChangePasswordDialog::on_save, this);

        btn_sizer->Add(btn_cancel, 0, wxRIGHT, FromDIP(10));
        btn_sizer->Add(btn_save, 0);
        root->Add(btn_sizer, 0, wxALIGN_RIGHT | wxALL, FromDIP(14));

        SetSizer(root);
        SetSize(FromDIP(wxSize(400, 260)));
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

BackofficeUsersPanel::~BackofficeUsersPanel() {
    *alive_ = false;
}

void BackofficeUsersPanel::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    auto* root = new wxBoxSizer(wxVERTICAL);

    // Barra superior de acciones (1 o 2 filas según ancho real medido de los controles)
    top_bar_sizer_ = new wxBoxSizer(wxVERTICAL);

    lbl_title_ = new wxStaticText(this, wxID_ANY, wxT("Usuarios y Cuentas de Acceso"));
    lbl_title_->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title_->SetForegroundColour(Theme::ColorBrandDeep);

    const int btn_h_logical = Theme::is_touch() ? 48 : 36;
    const int btn_h_px = FromDIP(btn_h_logical);

    btn_new_user_ = new wxButton(this, wxID_ANY, wxT("Nuevo Usuario"));
    Theme::style_button_primary(btn_new_user_, btn_h_logical);
    Theme::setup_button_icon(btn_new_user_, "add", wxSize(18, 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_new_user_, 160, btn_h_logical);
    btn_new_user_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_new_user, this);

    btn_refresh_ = new wxButton(this, wxID_ANY, wxT("Actualizar"));
    Theme::style_button_secondary(btn_refresh_, btn_h_logical);
    Theme::ensure_button_min_size(btn_refresh_, 110, btn_h_logical);
    btn_refresh_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_users(); });

    lbl_status_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_status_->SetFont(Theme::get_font_small());
    lbl_status_->SetForegroundColour(Theme::ColorTextSecondary);

    update_top_bar_layout();
    root->Add(top_bar_sizer_, 0, wxEXPAND | wxALL, FromDIP(14));

    // Tabla de usuarios
    list_ctrl_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_ctrl_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        ui::apply_touch_row_height(list_ctrl_);
    }
    list_ctrl_->InsertColumn(0, wxT("Nombre Completo"), wxLIST_FORMAT_LEFT, FromDIP(200));
    list_ctrl_->InsertColumn(1, wxT("Usuario"), wxLIST_FORMAT_LEFT, FromDIP(130));
    list_ctrl_->InsertColumn(2, wxT("Correo Electrónico"), wxLIST_FORMAT_LEFT, FromDIP(220));
    list_ctrl_->InsertColumn(3, wxT("Rol"), wxLIST_FORMAT_LEFT, FromDIP(120));
    list_ctrl_->InsertColumn(4, wxT("Avatar"), wxLIST_FORMAT_CENTER, FromDIP(70));
    list_ctrl_->InsertColumn(5, wxT("Estado"), wxLIST_FORMAT_CENTER, FromDIP(90));

    list_ctrl_->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        auto_size_columns();
    });
    list_ctrl_->Bind(wxEVT_LIST_ITEM_SELECTED, &BackofficeUsersPanel::on_item_selected, this);
    list_ctrl_->Bind(wxEVT_LIST_ITEM_DESELECTED, &BackofficeUsersPanel::on_item_deselected, this);

    root->Add(list_ctrl_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));

    // Barra inferior de operaciones sobre la fila seleccionada
    auto* bottom_bar = new wxBoxSizer(wxHORIZONTAL);

    btn_change_pwd_ = new wxButton(this, wxID_ANY, wxT("Cambiar Contraseña"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_secondary(btn_change_pwd_, btn_h_logical);
    const wxSize lock_icon_sz(16, 16);
    Theme::setup_button_icon(btn_change_pwd_, "lock", lock_icon_sz, Theme::ColorTextSecondary);
    int pwd_tw = 0, pwd_th = 0;
    btn_change_pwd_->GetTextExtent(btn_change_pwd_->GetLabel(), &pwd_tw, &pwd_th);
    const int lock_im = btn_change_pwd_->FromDIP(Theme::is_touch() ? 10 : 8) * 2;
    const int pwd_bp = btn_change_pwd_->FromDIP(Theme::is_touch() ? 28 : 24);
    int pwd_w = pwd_tw + btn_change_pwd_->FromDIP(lock_icon_sz.GetWidth()) + lock_im + pwd_bp;
    btn_change_pwd_->InvalidateBestSize();
    wxSize pwd_best = btn_change_pwd_->GetBestSize();
    if (pwd_best.GetWidth() > pwd_w) {
        pwd_w = pwd_best.GetWidth();
    }
    const int pwd_h = std::max(btn_h_px, pwd_best.GetHeight());
    btn_change_pwd_->SetMinSize(wxSize(pwd_w, pwd_h));
    btn_change_pwd_->SetSize(wxSize(pwd_w, pwd_h));
    btn_change_pwd_->Enable(false);
    btn_change_pwd_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_change_password, this);
    bottom_bar->Add(btn_change_pwd_, 0, wxRIGHT, FromDIP(8));

    btn_toggle_active_ = new wxButton(this, wxID_ANY, wxT("Desactivar Cuenta"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_destructive(btn_toggle_active_, btn_h_logical);
    int act_tw = 0, act_th = 0;
    btn_toggle_active_->GetTextExtent(btn_toggle_active_->GetLabel(), &act_tw, &act_th);
    int act_w = act_tw + btn_toggle_active_->FromDIP(Theme::is_touch() ? 32 : 28);
    btn_toggle_active_->InvalidateBestSize();
    wxSize act_best = btn_toggle_active_->GetBestSize();
    if (act_best.GetWidth() > act_w) act_w = act_best.GetWidth();
    const int act_h = std::max(btn_h_px, act_best.GetHeight());
    btn_toggle_active_->SetMinSize(wxSize(act_w, act_h));
    btn_toggle_active_->SetSize(wxSize(act_w, act_h));
    btn_toggle_active_->Enable(false);
    btn_toggle_active_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_toggle_active, this);
    bottom_bar->Add(btn_toggle_active_, 0, wxRIGHT, FromDIP(8));

    btn_view_avatar_ = new wxButton(this, wxID_ANY, wxT("Ver Avatar"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_secondary(btn_view_avatar_, btn_h_logical);
    const wxSize avatar_icon_sz(16, 16);
    Theme::setup_button_icon(btn_view_avatar_, "person", avatar_icon_sz, Theme::ColorTextSecondary);
    int av_tw = 0, av_th = 0;
    btn_view_avatar_->GetTextExtent(btn_view_avatar_->GetLabel(), &av_tw, &av_th);
    int av_w = av_tw + btn_view_avatar_->FromDIP(avatar_icon_sz.GetWidth()) + lock_im + pwd_bp;
    btn_view_avatar_->InvalidateBestSize();
    wxSize av_best = btn_view_avatar_->GetBestSize();
    if (av_best.GetWidth() > av_w) av_w = av_best.GetWidth();
    const int av_h = std::max(btn_h_px, av_best.GetHeight());
    btn_view_avatar_->SetMinSize(wxSize(av_w, av_h));
    btn_view_avatar_->SetSize(wxSize(av_w, av_h));
    btn_view_avatar_->Enable(false);
    btn_view_avatar_->Bind(wxEVT_BUTTON, &BackofficeUsersPanel::on_view_avatar, this);
    bottom_bar->Add(btn_view_avatar_, 0);

    root->Add(bottom_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));

    SetSizer(root);
    Bind(wxEVT_SIZE, &BackofficeUsersPanel::on_size, this);
}

void BackofficeUsersPanel::update_top_bar_layout() {
    if (!top_bar_sizer_ || !lbl_title_ || !btn_new_user_ || !btn_refresh_ || !lbl_status_) return;

    int title_tw = 0, title_th = 0;
    lbl_title_->GetTextExtent(lbl_title_->GetLabel(), &title_tw, &title_th);
    const int title_w = std::max(title_tw, lbl_title_->GetBestSize().GetWidth());

    const int btn_new_w = std::max(btn_new_user_->GetMinSize().GetWidth(), btn_new_user_->GetBestSize().GetWidth());
    const int btn_ref_w = std::max(btn_refresh_->GetMinSize().GetWidth(), btn_refresh_->GetBestSize().GetWidth());

    int status_tw = 0, status_th = 0;
    const wxString status_txt = lbl_status_->GetLabel();
    if (!status_txt.empty()) {
        lbl_status_->GetTextExtent(status_txt, &status_tw, &status_th);
    }
    const int status_w = std::max(status_tw, lbl_status_->GetBestSize().GetWidth());

    // Ancho requerido en 1 fila calculado en tiempo de ejecución desde los controles reales
    // (referencia del plan: ~780 lógicos en Táctil).
    const int need_one_row = title_w + FromDIP(16) + btn_new_w + FromDIP(10) + btn_ref_w
                           + FromDIP(12) + status_w + FromDIP(8) + FromDIP(28) + FromDIP(8);
    const int panel_w = GetClientSize().GetWidth();
    const bool two_rows = (panel_w > 0 && panel_w < need_one_row);

    if (top_bar_initialized_ && two_rows == top_bar_two_rows_) {
        return;
    }

    if (top_bar_row1_) {
        top_bar_row1_->Detach(lbl_title_);
        top_bar_row1_->Detach(btn_new_user_);
        top_bar_row1_->Detach(btn_refresh_);
        top_bar_row1_->Detach(lbl_status_);
    }
    if (top_bar_row2_) {
        top_bar_row2_->Detach(btn_new_user_);
        top_bar_row2_->Detach(btn_refresh_);
    }
    top_bar_row1_ = nullptr;
    top_bar_row2_ = nullptr;
    top_bar_sizer_->Clear(false);

    if (!two_rows) {
        top_bar_row1_ = new wxBoxSizer(wxHORIZONTAL);
        top_bar_row1_->Add(lbl_title_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));
        top_bar_row1_->Add(btn_new_user_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
        top_bar_row1_->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);
        top_bar_row1_->AddStretchSpacer(1);
        top_bar_row1_->Add(lbl_status_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        top_bar_sizer_->Add(top_bar_row1_, 0, wxEXPAND);
    } else {
        top_bar_row1_ = new wxBoxSizer(wxHORIZONTAL);
        top_bar_row1_->Add(lbl_title_, 0, wxALIGN_CENTER_VERTICAL);
        top_bar_row1_->AddStretchSpacer(1);
        top_bar_row1_->Add(lbl_status_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        top_bar_sizer_->Add(top_bar_row1_, 0, wxEXPAND);

        top_bar_row2_ = new wxBoxSizer(wxHORIZONTAL);
        top_bar_row2_->Add(btn_new_user_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
        top_bar_row2_->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);
        top_bar_sizer_->Add(top_bar_row2_, 0, wxEXPAND | wxTOP, FromDIP(8));
    }

    top_bar_initialized_ = true;
    top_bar_two_rows_ = two_rows;
}

void BackofficeUsersPanel::auto_size_columns() {
    if (!list_ctrl_) return;

    const int sb_w = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, list_ctrl_);
    const wxSize client = list_ctrl_->GetClientSize();
    int full_w = client.GetWidth() + (ui::has_vertical_scrollbar(list_ctrl_) ? sb_w : 0);
    if (full_w <= 50) {
        full_w = list_ctrl_->GetSize().GetWidth();
    }
    if (full_w <= 50) return;

    int row_h = list_ctrl_->FromDIP(Theme::is_touch() ? 42 : 20);
    const int rows = list_ctrl_->GetItemCount();
    if (rows > 0) {
        wxRect item_rect;
        if (list_ctrl_->GetItemRect(0, item_rect) && item_rect.GetHeight() > 0) {
            row_h = item_rect.GetHeight();
        }
    }
    const int rows_h = ui::header_height(list_ctrl_) + rows * row_h;
    const bool needs_vbar = client.GetHeight() > 0 && rows_h > client.GetHeight();
    const int budget = full_w - (needs_vbar ? sb_w : 0) - std::max(2, list_ctrl_->FromDIP(2));
    if (budget <= 50) return;

    const std::array<wxString, 6> headers = {
        wxT("Nombre Completo"),
        wxT("Usuario"),
        wxT("Correo Electrónico"),
        wxT("Rol"),
        wxT("Avatar"),
        wxT("Estado")
    };

    std::array<int, 6> content_max = {0, 0, 0, 0, 0, 0};
    auto measure_cell = [this, &content_max](std::size_t col, const wxString& text) {
        int tw = 0, th = 0;
        list_ctrl_->GetTextExtent(text, &tw, &th);
        if (tw > content_max[col]) {
            content_max[col] = tw;
        }
    };

    measure_cell(4, wxT("Sí"));
    measure_cell(4, wxT("No"));
    measure_cell(5, wxT("Activo"));
    measure_cell(5, wxT("Inactivo"));
    for (const auto& u : users_) {
        measure_cell(0, wxString::FromUTF8(u.display_name.c_str()));
        measure_cell(1, wxString::FromUTF8(u.username.c_str()));
        measure_cell(2, wxString::FromUTF8(u.email.c_str()));
        measure_cell(3, wxString::FromUTF8(u.role_name.c_str()));
    }

    const int hpad = list_ctrl_->FromDIP(16);
    const int cpad = list_ctrl_->FromDIP(18);
    const int img_extra0 = Theme::is_touch() ? list_ctrl_->FromDIP(7) : 0;

    std::array<int, 6> hdr_w{};
    std::array<int, 6> want{};
    int sum_want = 0;
    for (std::size_t i = 0; i < 6; ++i) {
        hdr_w[i] = ui::header_text_width(list_ctrl_, headers[i]) + hpad;
        const int extra = (i == 0) ? img_extra0 : 0;
        want[i] = std::max(hdr_w[i], content_max[i] + cpad + extra);
        sum_want += want[i];
    }

    std::array<int, 6> width = want;
    if (budget >= sum_want) {
        // Excedente repartido 45 % Nombre Completo (col 0) y 55 % Correo Electrónico (col 2).
        const int surplus = budget - sum_want;
        const int add0 = (surplus * 45) / 100;
        const int add2 = surplus - add0;
        width[0] += add0;
        width[2] += add2;
    } else {
        // Presupuesto ajustado: ceden primero Correo Electrónico y Nombre Completo hasta su encabezado,
        // manteniendo protegidos Rol, Usuario, Avatar y Estado para evitar WS_HSCROLL.
        int deficit = sum_want - budget;
        const int slack0 = std::max(0, width[0] - hdr_w[0]);
        const int slack2 = std::max(0, width[2] - hdr_w[2]);
        const int total_elastic = slack0 + slack2;
        if (total_elastic > 0) {
            const int take = std::min(deficit, total_elastic);
            const int take2 = (take * slack2 + total_elastic / 2) / total_elastic;
            const int take0 = std::min(slack0, take - take2);
            const int actual2 = std::min(slack2, take - take0);
            width[0] -= take0;
            width[2] -= actual2;
            deficit -= (take0 + actual2);
        }
        for (std::size_t idx : {std::size_t{1}, std::size_t{3}, std::size_t{5}, std::size_t{4}}) {
            if (deficit <= 0) break;
            const int give = std::min(deficit, std::max(0, width[idx] - hdr_w[idx]));
            width[idx] -= give;
            deficit -= give;
        }
    }

    for (std::size_t i = 0; i < 6; ++i) {
        if (list_ctrl_->GetColumnWidth(static_cast<int>(i)) != width[i]) {
            list_ctrl_->SetColumnWidth(static_cast<int>(i), width[i]);
        }
    }
}

void BackofficeUsersPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    if (in_size_event_) return;
    in_size_event_ = true;

    update_top_bar_layout();
    Layout();
    auto_size_columns();

    in_size_event_ = false;
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
        lbl_status_->SetMinSize(wxDefaultSize);
        lbl_status_->InvalidateBestSize();
        update_top_bar_layout();
        Layout();
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
    lbl_status_->SetMinSize(wxDefaultSize);
    lbl_status_->InvalidateBestSize();
    update_top_bar_layout();
    Layout();
    auto_size_columns();
    auto alive = alive_;
    CallAfter([this, alive]() {
        if (!*alive) return;
        auto_size_columns();
    });
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

    // Escalar proporcionalmente a max 160x160 (lógicos) si es necesario
    const int max_av = FromDIP(160);
    if (img.GetWidth() > max_av || img.GetHeight() > max_av) {
        img.Rescale(max_av, max_av, wxIMAGE_QUALITY_HIGH);
    }
    wxBitmap bmp(img);

    wxDialog dlg(this, wxID_ANY, wxString::Format(wxT("Avatar de %s"), wxString::FromUTF8(u.display_name.c_str())),
                 wxDefaultPosition, FromDIP(wxSize(220, 260)), wxDEFAULT_DIALOG_STYLE);
    auto* s = new wxBoxSizer(wxVERTICAL);
    auto* sbmp = new wxStaticBitmap(&dlg, wxID_ANY, bmp);
    s->Add(sbmp, 1, wxALL | wxALIGN_CENTER, FromDIP(14));
    auto* btn_close = new wxButton(&dlg, wxID_OK, wxT("Cerrar"));
    s->Add(btn_close, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(12));
    dlg.SetSizer(s);
    dlg.Centre();
    dlg.ShowModal();
}

} // namespace app::frontend
