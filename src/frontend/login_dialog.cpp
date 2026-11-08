#include "src/frontend/login_dialog.hpp"

namespace app::frontend {

LoginDialog::LoginDialog(
    wxWindow* parent,
    std::shared_ptr<contracts::IAccountService> account_service
) : wxDialog(parent, wxID_ANY, wxT("Iniciar Sesión — Cafetería ITCH II"),
             wxDefaultPosition, wxSize(400, 320), wxDEFAULT_DIALOG_STYLE),
    account_service_(std::move(account_service)) {

    SetBackgroundColour(wxColour(250, 250, 252));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, wxT("Cafetería ITCH II"));
    wxFont f = title->GetFont();
    f.SetPointSize(13);
    f.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(f);
    title->SetForegroundColour(wxColour(138, 3, 3));
    main_sizer->Add(title, 0, wxALL | wxALIGN_CENTER, 14);

    auto* form_sizer = new wxFlexGridSizer(2, 2, 10, 12);
    form_sizer->AddGrowableCol(1, 1);

    form_sizer->Add(new wxStaticText(this, wxID_ANY, wxT("Usuario:")), 0, wxALIGN_CENTER_VERTICAL);
    txt_username_ = new wxTextCtrl(this, wxID_ANY);
    form_sizer->Add(txt_username_, 1, wxEXPAND);

    form_sizer->Add(new wxStaticText(this, wxID_ANY, wxT("Contraseña:")), 0, wxALIGN_CENTER_VERTICAL);
    txt_password_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD | wxTE_PROCESS_ENTER);
    form_sizer->Add(txt_password_, 1, wxEXPAND);

    main_sizer->Add(form_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);

    lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_error_->SetForegroundColour(wxColour(200, 0, 0));
    main_sizer->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, 8);

    auto* btn_login = new wxButton(this, wxID_ANY, wxT("Iniciar Sesión"), wxDefaultPosition, wxSize(-1, 40));
    btn_login->SetBackgroundColour(wxColour(180, 20, 30));
    btn_login->SetForegroundColour(wxColour(255, 255, 255));
    wxFont btn_f = btn_login->GetFont();
    btn_f.SetWeight(wxFONTWEIGHT_BOLD);
    btn_login->SetFont(btn_f);

    btn_login->Bind(wxEVT_BUTTON, &LoginDialog::on_submit, this);
    txt_password_->Bind(wxEVT_TEXT_ENTER, &LoginDialog::on_submit, this);

    main_sizer->Add(btn_login, 0, wxEXPAND | wxALL, 16);

    SetSizer(main_sizer);
    Centre();
}

void LoginDialog::on_submit(wxCommandEvent&) {
    std::string username = txt_username_->GetValue().ToStdString();
    std::string password = txt_password_->GetValue().ToStdString();

    if (username.empty() || password.empty()) {
        lbl_error_->SetLabel(wxT("Ingrese usuario y contraseña."));
        Layout();
        return;
    }

    auto res = account_service_->login(username, password);
    if (!res.is_success()) {
        lbl_error_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
        Layout();
        return;
    }

    EndModal(wxID_OK);
}

} // namespace app::frontend
