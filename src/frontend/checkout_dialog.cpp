#include "src/frontend/checkout_dialog.hpp"
#include "src/frontend/theme.hpp"
#include <wx/statline.h>
#include <cctype>

namespace app::frontend {

CheckoutDialog::CheckoutDialog(
    wxWindow* parent,
    contracts::Money total
) : wxDialog(parent, wxID_ANY, wxT("Cobro de Venta — Cafetería ITCH II"), wxDefaultPosition, wxSize(440, 520), wxDEFAULT_DIALOG_STYLE),
    total_(total),
    cash_received_(total) {
    SetBackgroundColour(Theme::ColorBackground);

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Encabezado
    auto* lbl_title = new wxStaticText(this, wxID_ANY, wxT("Confirmación de Cobro"));
    lbl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    main_sizer->Add(lbl_title, 0, wxALL, 16);

    // Tarjeta de Total a Cobrar
    auto* total_card = new CardPanel(this);
    total_card->set_card_background(Theme::ColorSurface);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_total_caption = new wxStaticText(total_card, wxID_ANY, wxT("TOTAL A COBRAR"));
    lbl_total_caption->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_total_caption->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_total_caption, 0, wxLEFT | wxRIGHT | wxTOP, 14);

    lbl_total_val_ = new wxStaticText(total_card, wxID_ANY, wxString::FromUTF8(total_.to_string().c_str()));
    lbl_total_val_->SetFont(Theme::get_font_hero(wxFONTWEIGHT_BOLD));
    lbl_total_val_->SetForegroundColour(Theme::ColorSuccess);
    card_sizer->Add(lbl_total_val_, 0, wxALL, 14);

    total_card->SetSizer(card_sizer);
    main_sizer->Add(total_card, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

    main_sizer->AddSpacer(14);

    // Tarjeta de Método y Detalle de Pago
    auto* payment_card = new CardPanel(this);
    payment_card->set_card_background(Theme::ColorSurface);
    auto* pay_card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_method = new wxStaticText(payment_card, wxID_ANY, wxT("Método de pago:"));
    lbl_method->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_method->SetForegroundColour(Theme::ColorTextSecondary);
    pay_card_sizer->Add(lbl_method, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 6);

    wxArrayString methods;
    methods.Add(wxT("Efectivo"));
    methods.Add(wxT("Tarjeta"));
    methods.Add(wxT("Transferencia"));
    choice_method_ = new wxChoice(payment_card, wxID_ANY, wxDefaultPosition, wxDefaultSize, methods);
    choice_method_->SetFont(Theme::get_font_body());
    choice_method_->SetSelection(0);
    pay_card_sizer->Add(choice_method_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // Sección de Efectivo (Recibido y Cambio)
    cash_details_sizer_ = new wxBoxSizer(wxVERTICAL);

    auto* lbl_received = new wxStaticText(payment_card, wxID_ANY, wxT("Monto en efectivo recibido:"));
    lbl_received->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_received->SetForegroundColour(Theme::ColorTextSecondary);
    cash_details_sizer_->Add(lbl_received, 0, wxBOTTOM, 4);

    txt_cash_received_ = new wxTextCtrl(payment_card, wxID_ANY, wxString::FromUTF8(total_.to_string().c_str()),
                                        wxDefaultPosition, wxSize(-1, Theme::is_touch() ? 44 : 36));
    txt_cash_received_->SetFont(Theme::get_font_body());
    cash_details_sizer_->Add(txt_cash_received_, 0, wxEXPAND | wxBOTTOM, 12);

    auto* change_row = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_change_caption = new wxStaticText(payment_card, wxID_ANY, wxT("Cambio a devolver:"));
    lbl_change_caption->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_change_caption->SetForegroundColour(Theme::ColorText);

    lbl_change_val_ = new wxStaticText(payment_card, wxID_ANY, wxT("$0.00"));
    lbl_change_val_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_change_val_->SetForegroundColour(Theme::ColorSuccess);

    change_row->Add(lbl_change_caption, 1, wxALIGN_CENTER_VERTICAL);
    change_row->Add(lbl_change_val_, 0, wxALIGN_CENTER_VERTICAL);
    cash_details_sizer_->Add(change_row, 0, wxEXPAND | wxBOTTOM, 8);

    pay_card_sizer->Add(cash_details_sizer_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    payment_card->SetSizer(pay_card_sizer);

    main_sizer->Add(payment_card, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);

    main_sizer->AddStretchSpacer(1);

    // Botones de acción
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_cancel = new wxButton(this, wxID_CANCEL, wxT("Cancelar"), wxDefaultPosition, wxSize(110, Theme::is_touch() ? 48 : 40));
    Theme::style_button_secondary(btn_cancel, Theme::is_touch() ? 48 : 40);

    btn_confirm_ = new wxButton(this, wxID_ANY, wxT("Confirmar Venta"), wxDefaultPosition, wxSize(170, Theme::is_touch() ? 48 : 40));
    Theme::style_button_success(btn_confirm_, Theme::is_touch() ? 48 : 40);
    Theme::setup_button_icon(btn_confirm_, "check_circle", wxSize(20, 20), Theme::ColorSurface);

    btn_sizer->Add(btn_cancel, 0, wxRIGHT, 12);
    btn_sizer->Add(btn_confirm_, 1);
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    SetSizer(main_sizer);
    Center();

    // Eventos
    choice_method_->Bind(wxEVT_CHOICE, &CheckoutDialog::on_payment_method_changed, this);
    txt_cash_received_->Bind(wxEVT_TEXT, &CheckoutDialog::on_cash_received_text_changed, this);
    btn_confirm_->Bind(wxEVT_BUTTON, &CheckoutDialog::on_confirm, this);

    update_cash_calculations();
}

void CheckoutDialog::on_payment_method_changed(wxCommandEvent&) {
    int sel = choice_method_->GetSelection();
    if (sel == 0) {
        selected_method_ = contracts::PaymentMethod::Efectivo;
        cash_details_sizer_->ShowItems(true);
    } else if (sel == 1) {
        selected_method_ = contracts::PaymentMethod::Tarjeta;
        cash_details_sizer_->ShowItems(false);
    } else {
        selected_method_ = contracts::PaymentMethod::Transferencia;
        cash_details_sizer_->ShowItems(false);
    }
    Layout();
    update_cash_calculations();
}

void CheckoutDialog::on_cash_received_text_changed(wxCommandEvent&) {
    update_cash_calculations();
}

void CheckoutDialog::update_cash_calculations() {
    if (selected_method_ != contracts::PaymentMethod::Efectivo) {
        cash_received_ = total_;
        change_ = contracts::Money::zero();
        cash_is_valid_ = true;
        if (btn_confirm_) {
            btn_confirm_->Enable(true);
        }
        return;
    }

    wxString str = txt_cash_received_->GetValue().Trim().Trim(false);
    std::string s = std::string(str.ToUTF8());
    if (s.empty()) {
        cash_received_ = contracts::Money::zero();
        change_ = contracts::Money::zero();
        cash_is_valid_ = false;
        lbl_change_val_->SetLabel(wxT("$0.00"));
        lbl_change_val_->SetForegroundColour(Theme::ColorError);
        if (btn_confirm_) {
            btn_confirm_->Enable(false);
        }
        return;
    }

    auto parse_res = contracts::Money::try_parse(s, false);
    if (!parse_res.is_success()) {
        cash_received_ = contracts::Money::zero();
        change_ = contracts::Money::zero();
        cash_is_valid_ = false;
        lbl_change_val_->SetLabel(wxString::FromUTF8(parse_res.error_message().c_str()));
        lbl_change_val_->SetForegroundColour(Theme::ColorError);
        if (btn_confirm_) {
            btn_confirm_->Enable(false);
        }
        return;
    }

    cash_received_ = parse_res.value();
    if (cash_received_ >= total_) {
        cash_is_valid_ = true;
        change_ = cash_received_ - total_;
        lbl_change_val_->SetLabel(wxString::FromUTF8(change_.to_string().c_str()));
        lbl_change_val_->SetForegroundColour(Theme::ColorSuccess);
        if (btn_confirm_) {
            btn_confirm_->Enable(true);
        }
    } else {
        cash_is_valid_ = false;
        change_ = contracts::Money::zero();
        lbl_change_val_->SetLabel(wxT("Importe insuficiente"));
        lbl_change_val_->SetForegroundColour(Theme::ColorError);
        if (btn_confirm_) {
            btn_confirm_->Enable(false);
        }
    }
}

void CheckoutDialog::on_confirm(wxCommandEvent&) {
    if (selected_method_ == contracts::PaymentMethod::Efectivo) {
        update_cash_calculations();
        if (!cash_is_valid_ || cash_received_ < total_) {
            wxMessageBox(
                wxT("El monto en efectivo recibido es inválido o menor al total de la cuenta."),
                wxT("Importe Inválido o Insuficiente"),
                wxOK | wxICON_WARNING,
                this
            );
            txt_cash_received_->SetFocus();
            return;
        }
    }
    EndModal(wxID_OK);
}

} // namespace app::frontend
