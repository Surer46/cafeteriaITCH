#include "src/frontend/initial_owner_dialog.hpp"

namespace app::frontend {

InitialOwnerDialog::InitialOwnerDialog(
    wxWindow* parent,
    std::shared_ptr<contracts::IAccountService> account_service
)    : wxDialog(parent, wxID_ANY, wxT("Primer Inicio — Alta de Cuenta Dueño"),
             wxDefaultPosition, wxSize(460, 460), wxDEFAULT_DIALOG_STYLE),
    account_service_(std::move(account_service)) {

    SetBackgroundColour(wxColour(250, 250, 252));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Encabezado del diálogo
    auto* title = new wxStaticText(this, wxID_ANY, wxT("Bienvenido a Cafetería ITCH II"));
    wxFont f = title->GetFont();
    f.SetPointSize(12);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(f);
    title->SetForegroundColour(wxColour(138, 3, 3));
    main_sizer->Add(title, 0, wxALL | wxALIGN_CENTER, 14);

    auto* desc = new wxStaticText(this, wxID_ANY,
        wxT("Para comenzar, registre la cuenta principal con rol Dueño.\n")
        wxT("Esta cuenta administrará el negocio y no tiene contraseña predeterminada.")
    );
    main_sizer->Add(desc, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_CENTER, 10);

    // Formulario
    auto* form_sizer = new wxFlexGridSizer(5, 2, 10, 12);
    form_sizer->AddGrowableCol(1, 1);

    form_sizer->Add(new wxStaticText(this, wxID_ANY, wxT("Nombre completo:")), 0, wxALIGN_CENTER_VERTICAL);
    txt_display_name_ = new wxTextCtrl(this, wxID_ANY);
    form_sizer->Add(txt_display_name_, 1, wxEXPAND);

    form_sizer->Add(new wxStaticText(this, wxID_ANY, wxT("Usuario:")), 0, wxALIGN_CENTER_VERTICAL);
    txt_username_ = new wxTextCtrl(this, wxID_ANY);
    form_sizer->Add(txt_username_, 1, wxEXPAND);

    form_sizer->Add(new wxStaticText(this, wxID_ANY, wxT("Correo electrónico:")), 0, wxALIGN_CENTER_VERTICAL);
    txt_email_ = new wxTextCtrl(this, wxID_ANY);
    form_sizer->Add(txt_email_, 1, wxEXPAND);

    form_sizer->Add(new wxStaticText(this, wxID_ANY, wxT("Contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
    txt_password_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    form_sizer->Add(txt_password_, 1, wxEXPAND);

    form_sizer->Add(new wxStaticText(this, wxID_ANY, wxT("Confirmar contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
    txt_confirm_password_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    form_sizer->Add(txt_confirm_password_, 1, wxEXPAND);

    main_sizer->Add(form_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);

    // Mensaje de error
    lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_error_->SetForegroundColour(wxColour(200, 0, 0));
    main_sizer->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, 8);

    // Botón Crear Cuenta
    auto* btn_submit = new wxButton(this, wxID_ANY, wxT("Registrar Cuenta Dueño"), wxDefaultPosition, wxSize(-1, 42));
    btn_submit->SetBackgroundColour(wxColour(180, 20, 30));
    btn_submit->SetForegroundColour(wxColour(255, 255, 255));
    wxFont btn_f = btn_submit->GetFont();
    btn_f.SetWeight(wxFONTWEIGHT_BOLD);
    btn_submit->SetFont(btn_f);

    btn_submit->Bind(wxEVT_BUTTON, &InitialOwnerDialog::on_submit, this);
    main_sizer->Add(btn_submit, 0, wxEXPAND | wxALL, 16);

    SetSizer(main_sizer);
    Centre();
}

void InitialOwnerDialog::on_submit(wxCommandEvent&) {
    std::string display_name = txt_display_name_->GetValue().ToStdString();
    std::string username = txt_username_->GetValue().ToStdString();
    std::string email = txt_email_->GetValue().ToStdString();
    std::string password = txt_password_->GetValue().ToStdString();
    std::string confirm = txt_confirm_password_->GetValue().ToStdString();

    if (display_name.empty() || username.empty() || email.empty() || password.empty()) {
        lbl_error_->SetLabel(wxT("Todos los campos son obligatorios."));
        Layout();
        return;
    }

    if (password.length() < 6) {
        lbl_error_->SetLabel(wxT("La contraseña debe tener al menos 6 caracteres."));
        Layout();
        return;
    }

    if (password != confirm) {
        lbl_error_->SetLabel(wxT("Las contraseñas no coinciden."));
        Layout();
        return;
    }

    auto res = account_service_->create_initial_owner(username, display_name, password, email);
    if (!res.is_success()) {
        lbl_error_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
        Layout();
        return;
    }

    EndModal(wxID_OK);
}

} // namespace app::frontend
