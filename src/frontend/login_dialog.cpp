#include "src/frontend/login_dialog.hpp"
#include "src/frontend/theme.hpp"
#include <algorithm>

namespace app::frontend {

namespace {

wxWindow* make_dialog_text_field(
    wxWindow* parent,
    wxTextCtrl*& out,
    long style,
    int min_height_px,
    int min_width_px,
    const wxFont& font,
    bool touch) {
    if (!touch) {
        out = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style);
        return out;
    }

    auto* box = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
    box->SetBackgroundColour(Theme::ColorSurface);

    out = new wxTextCtrl(box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, style | wxBORDER_NONE);
    out->SetFont(font);
    out->SetBackgroundColour(Theme::ColorSurface);

    const int best_h = out->GetBestSize().GetHeight();
    const int box_h = std::max(min_height_px, best_h + parent->FromDIP(4));
    box->SetMinSize(wxSize(min_width_px, box_h));

    auto* sz = new wxBoxSizer(wxVERTICAL);
    sz->AddStretchSpacer(1);
    sz->Add(out, 0, wxEXPAND | wxLEFT | wxRIGHT, parent->FromDIP(8));
    sz->AddStretchSpacer(1);
    box->SetSizer(sz);

    wxTextCtrl* edit = out;
    box->Bind(wxEVT_LEFT_DOWN, [edit](wxMouseEvent&) {
        if (edit) {
            edit->SetFocus();
        }
    });
    return box;
}

} // namespace

LoginDialog::LoginDialog(
    wxWindow* parent,
    std::shared_ptr<contracts::IAccountService> account_service
) : wxDialog(parent, wxID_ANY, wxT("Iniciar Sesión — Cafetería ITCH II"),
             wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
    account_service_(std::move(account_service)) {

    const bool touch = Theme::is_touch();
    const wxFont body_font = touch ? Theme::get_font_body() : GetFont();

    SetBackgroundColour(wxColour(250, 250, 252));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, wxT("Cafetería ITCH II"));
    if (touch) {
        title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    } else {
        wxFont f = title->GetFont();
        f.SetPointSize(13);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(f);
    }
    title->SetForegroundColour(wxColour(138, 3, 3));
    main_sizer->Add(title, 0, wxALL | wxALIGN_CENTER, FromDIP(14));

    auto* form_sizer = new wxFlexGridSizer(2, 2, FromDIP(touch ? 12 : 10), FromDIP(12));
    form_sizer->AddGrowableCol(1, 1);

    const int field_h = FromDIP(48);
    const int field_w = FromDIP(touch ? 240 : 210);

    auto* lbl_user = new wxStaticText(this, wxID_ANY, wxT("Usuario:"));
    if (touch) {
        lbl_user->SetFont(body_font);
    }
    form_sizer->Add(lbl_user, 0, wxALIGN_CENTER_VERTICAL);
    form_sizer->Add(
        make_dialog_text_field(this, txt_username_, 0, field_h, field_w, body_font, touch),
        1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    auto* lbl_pass = new wxStaticText(this, wxID_ANY, wxT("Contraseña:"));
    if (touch) {
        lbl_pass->SetFont(body_font);
    }
    form_sizer->Add(lbl_pass, 0, wxALIGN_CENTER_VERTICAL);
    form_sizer->Add(
        make_dialog_text_field(
            this, txt_password_, wxTE_PASSWORD | wxTE_PROCESS_ENTER, field_h, field_w, body_font, touch),
        1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    main_sizer->Add(form_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    if (touch) {
        lbl_error_->SetFont(body_font);
    }
    lbl_error_->SetForegroundColour(wxColour(200, 0, 0));
    main_sizer->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, FromDIP(8));

    auto* btn_login = new wxButton(this, wxID_ANY, wxT("Iniciar Sesión"), wxDefaultPosition, wxDefaultSize);
    btn_login->SetBackgroundColour(wxColour(180, 20, 30));
    btn_login->SetForegroundColour(wxColour(255, 255, 255));
    if (touch) {
        btn_login->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    } else {
        wxFont btn_f = btn_login->GetFont();
        btn_f.SetWeight(wxFONTWEIGHT_BOLD);
        btn_login->SetFont(btn_f);
    }
    btn_login->SetMinSize(FromDIP(wxSize(-1, touch ? 56 : 40)));

    btn_login->Bind(wxEVT_BUTTON, &LoginDialog::on_submit, this);
    txt_password_->Bind(wxEVT_TEXT_ENTER, &LoginDialog::on_submit, this);

    main_sizer->Add(btn_login, 0, wxEXPAND | wxALL, FromDIP(16));

    main_sizer->SetMinSize(FromDIP(wxSize(touch ? 440 : 380, touch ? -1 : 280)));
    SetSizerAndFit(main_sizer);
    SetMinSize(GetSize());
    Centre();
}

void LoginDialog::on_submit(wxCommandEvent&) {
    std::string username = txt_username_->GetValue().ToStdString();
    std::string password = txt_password_->GetValue().ToStdString();

    if (username.empty() || password.empty()) {
        lbl_error_->SetLabel(wxT("Ingrese usuario y contraseña."));
        lbl_error_->Wrap(std::max(FromDIP(240), GetClientSize().GetWidth() - FromDIP(32)));
        Layout();
        Fit();
        return;
    }

    auto res = account_service_->login(username, password);
    if (!res.is_success()) {
        lbl_error_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
        lbl_error_->Wrap(std::max(FromDIP(240), GetClientSize().GetWidth() - FromDIP(32)));
        Layout();
        Fit();
        return;
    }

    EndModal(wxID_OK);
}

} // namespace app::frontend
