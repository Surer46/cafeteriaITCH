#include "src/frontend/initial_owner_dialog.hpp"
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

InitialOwnerDialog::InitialOwnerDialog(
    wxWindow* parent,
    std::shared_ptr<contracts::IAccountService> account_service
)    : wxDialog(parent, wxID_ANY, wxT("Primer Inicio — Alta de Cuenta Dueño"),
             wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
    account_service_(std::move(account_service)) {

    const bool touch = Theme::is_touch();
    const wxFont body_font = touch ? Theme::get_font_body() : GetFont();

    SetBackgroundColour(wxColour(250, 250, 252));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Encabezado del diálogo
    auto* title = new wxStaticText(this, wxID_ANY, wxT("Bienvenido a Cafetería ITCH II"));
    if (touch) {
        title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    } else {
        wxFont f = title->GetFont();
        f.SetPointSize(12);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        title->SetFont(f);
    }
    title->SetForegroundColour(wxColour(138, 3, 3));
    main_sizer->Add(title, 0, touch ? (wxTOP | wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_CENTER) : (wxALL | wxALIGN_CENTER),
                    FromDIP(touch ? 8 : 14));

    auto* desc = new wxStaticText(this, wxID_ANY,
        wxT("Para comenzar, registre la cuenta principal con rol Dueño.\n")
        wxT("Esta cuenta administrará el negocio y no tiene contraseña predeterminada.")
    );
    if (touch) {
        desc->SetFont(body_font);
    }
    main_sizer->Add(desc, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_CENTER, FromDIP(touch ? 8 : 10));

    // Formulario
    auto* form_sizer = new wxFlexGridSizer(5, 2, FromDIP(touch ? 6 : 10), FromDIP(12));
    form_sizer->AddGrowableCol(1, 1);

    const int field_h = FromDIP(48);
    const int field_w = FromDIP(touch ? 240 : 210);

    auto add_field_row = [this, form_sizer, touch, &body_font, field_h, field_w](
                             const wxString& label_text, wxTextCtrl*& out, long style = 0) {
        auto* lbl = new wxStaticText(this, wxID_ANY, label_text);
        if (touch) {
            lbl->SetFont(body_font);
        }
        form_sizer->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);
        form_sizer->Add(
            make_dialog_text_field(this, out, style, field_h, field_w, body_font, touch),
            1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    };

    add_field_row(wxT("Nombre completo:"), txt_display_name_);
    add_field_row(wxT("Usuario:"), txt_username_);
    add_field_row(wxT("Correo electrónico:"), txt_email_);
    add_field_row(wxT("Contraseña:"), txt_password_, wxTE_PASSWORD);
    add_field_row(wxT("Confirmar contraseña:"), txt_confirm_password_, wxTE_PASSWORD);

    main_sizer->Add(form_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(touch ? 12 : 16));

    // Mensaje de error
    lbl_error_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    if (touch) {
        lbl_error_->SetFont(body_font);
    }
    lbl_error_->SetForegroundColour(wxColour(200, 0, 0));
    main_sizer->Add(lbl_error_, 0, wxALL | wxALIGN_CENTER, FromDIP(touch ? 4 : 8));

    // Botón Crear Cuenta
    auto* btn_submit = new wxButton(this, wxID_ANY, wxT("Registrar Cuenta Dueño"), wxDefaultPosition, wxDefaultSize);
    btn_submit->SetBackgroundColour(wxColour(180, 20, 30));
    btn_submit->SetForegroundColour(wxColour(255, 255, 255));
    if (touch) {
        btn_submit->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    } else {
        wxFont btn_f = btn_submit->GetFont();
        btn_f.SetWeight(wxFONTWEIGHT_BOLD);
        btn_submit->SetFont(btn_f);
    }
    btn_submit->SetMinSize(FromDIP(wxSize(-1, touch ? 56 : 42)));

    btn_submit->Bind(wxEVT_BUTTON, &InitialOwnerDialog::on_submit, this);
    main_sizer->Add(btn_submit, 0, wxEXPAND | wxALL, FromDIP(touch ? 10 : 16));

    main_sizer->SetMinSize(FromDIP(wxSize(touch ? 520 : 440, touch ? -1 : 400)));
    SetSizerAndFit(main_sizer);
    SetMinSize(GetSize());
    Centre();
}

void InitialOwnerDialog::on_submit(wxCommandEvent&) {
    std::string display_name = txt_display_name_->GetValue().ToStdString();
    std::string username = txt_username_->GetValue().ToStdString();
    std::string email = txt_email_->GetValue().ToStdString();
    std::string password = txt_password_->GetValue().ToStdString();
    std::string confirm = txt_confirm_password_->GetValue().ToStdString();

    auto show_error = [this](const wxString& msg) {
        lbl_error_->SetLabel(msg);
        lbl_error_->Wrap(std::max(FromDIP(260), GetClientSize().GetWidth() - FromDIP(32)));
        Layout();
        Fit();
    };

    if (display_name.empty() || username.empty() || email.empty() || password.empty()) {
        show_error(wxT("Todos los campos son obligatorios."));
        return;
    }

    if (password.length() < 6) {
        show_error(wxT("La contraseña debe tener al menos 6 caracteres."));
        return;
    }

    if (password != confirm) {
        show_error(wxT("Las contraseñas no coinciden."));
        return;
    }

    auto res = account_service_->create_initial_owner(username, display_name, password, email);
    if (!res.is_success()) {
        show_error(wxString::FromUTF8(res.error_message().c_str()));
        return;
    }

    EndModal(wxID_OK);
}

} // namespace app::frontend
