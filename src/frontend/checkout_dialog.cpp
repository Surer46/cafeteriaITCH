#include "src/frontend/checkout_dialog.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <wx/statline.h>
#include <cctype>
#include <cstdio>

namespace app::frontend {

namespace {

// 406 -> "4.06" (centésimas de punto porcentual, solo enteros)
wxString percent_text(std::int64_t hundredths) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld.%02lld", static_cast<long long>(hundredths / 100), static_cast<long long>(hundredths % 100));
    return wxString::FromUTF8(buf);
}

wxString money_text(const contracts::Money& m) { return wxString::FromUTF8(m.to_string().c_str()); }

} // namespace

CheckoutDialog::CheckoutDialog(
    wxWindow* parent,
    contracts::Money total,
    contracts::Money discount,
    std::shared_ptr<contracts::ISaleService> sale_service
) : wxDialog(parent, wxID_ANY, wxT("Cobro de Venta — Cafetería ITCH II"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
    total_(total),
    discount_(discount),
    sale_service_(std::move(sale_service)),
    cash_received_(total) {
    SetBackgroundColour(Theme::ColorBackground);
    const bool touch = Theme::is_touch();

    auto* outer_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Encabezado
    auto* lbl_title = new wxStaticText(this, wxID_ANY, wxT("Confirmación de Cobro"));
    lbl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    main_sizer->Add(lbl_title, 0, wxALL, FromDIP(16));

    // Tarjeta de Total a Cobrar
    auto* total_card = new CardPanel(this);
    total_card->set_card_background(Theme::ColorSurface);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_total_caption = new wxStaticText(total_card, wxID_ANY, wxT("TOTAL A COBRAR"));
    lbl_total_caption->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_total_caption->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_total_caption, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    lbl_total_val_ = new wxStaticText(total_card, wxID_ANY, money_text(total_));
    lbl_total_val_->SetFont(Theme::get_font_hero(wxFONTWEIGHT_BOLD));
    lbl_total_val_->SetForegroundColour(Theme::ColorSuccess);
    card_sizer->Add(lbl_total_val_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    // Desglose: productos, descuento y cargo por tarjeta (los dos últimos solo si son mayores que 0)
    breakdown_sizer_ = new wxFlexGridSizer(2, FromDIP(4), FromDIP(12));
    breakdown_sizer_->AddGrowableCol(0, 1);
    auto add_row = [&](wxStaticText*& caption, wxStaticText*& value, const wxString& text, const wxColour& color) {
        caption = new wxStaticText(total_card, wxID_ANY, text);
        caption->SetFont(Theme::get_font_body());
        caption->SetForegroundColour(color);
        value = new wxStaticText(total_card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
        value->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        value->SetForegroundColour(color);
        breakdown_sizer_->Add(caption, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
        breakdown_sizer_->Add(value, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
    };
    wxStaticText* lbl_products_caption = nullptr;
    add_row(lbl_products_caption, lbl_products_val_, wxT("Productos"), Theme::ColorTextSecondary);
    add_row(lbl_discount_caption_, lbl_discount_val_, wxT("Descuento aplicado"), Theme::ColorSuccess);
    add_row(lbl_surcharge_caption_, lbl_surcharge_val_, wxT("Cargo por pago con tarjeta"), Theme::ColorTextSecondary);
    card_sizer->Add(breakdown_sizer_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    lbl_preview_error_ = new wxStaticText(total_card, wxID_ANY, wxEmptyString);
    lbl_preview_error_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_preview_error_->SetForegroundColour(Theme::ColorError);
    card_sizer->Add(lbl_preview_error_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    card_sizer->AddSpacer(FromDIP(16));

    total_card->SetSizer(card_sizer);
    main_sizer->Add(total_card, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    main_sizer->AddSpacer(FromDIP(14));

    // Tarjeta de Método y Detalle de Pago
    auto* payment_card = new CardPanel(this);
    payment_card->set_card_background(Theme::ColorSurface);
    auto* pay_card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_method = new wxStaticText(payment_card, wxID_ANY, wxT("Método de pago:"));
    lbl_method->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_method->SetForegroundColour(Theme::ColorTextSecondary);
    pay_card_sizer->Add(lbl_method, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(6));

    wxArrayString methods;
    methods.Add(wxT("Efectivo"));
    methods.Add(wxT("Tarjeta"));
    methods.Add(wxT("Transferencia"));
    const int combo_h_logical = touch ? 48 : 34;
    choice_method_ = new ui::TouchComboBox(payment_card, wxID_ANY, FromDIP(wxSize(-1, combo_h_logical)), methods);
    choice_method_->SetFont(Theme::get_font_body());
    choice_method_->SetMinSize(FromDIP(wxSize(-1, combo_h_logical)));
    choice_method_->SetSelection(0);
    pay_card_sizer->Add(choice_method_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    // Sección de Efectivo (Recibido y Cambio)
    cash_details_sizer_ = new wxBoxSizer(wxVERTICAL);

    auto* lbl_received = new wxStaticText(payment_card, wxID_ANY, wxT("Monto en efectivo recibido:"));
    lbl_received->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_received->SetForegroundColour(Theme::ColorTextSecondary);
    cash_details_sizer_->Add(lbl_received, 0, wxBOTTOM, FromDIP(4));

    auto* cash_field = ui::make_text_field(
        payment_card, txt_cash_received_, touch ? 48 : 36, -1, Theme::get_font_body());
    txt_cash_received_->ChangeValue(wxString::FromUTF8(total_.to_string().c_str()));
    cash_details_sizer_->Add(cash_field, 0, wxEXPAND | wxBOTTOM, FromDIP(12));

    auto* change_row = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_change_caption = new wxStaticText(payment_card, wxID_ANY, wxT("Cambio a devolver:"));
    lbl_change_caption->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_change_caption->SetForegroundColour(Theme::ColorText);

    lbl_change_val_ = new wxStaticText(payment_card, wxID_ANY, wxT("$0.00"));
    lbl_change_val_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_change_val_->SetForegroundColour(Theme::ColorSuccess);

    change_row->Add(lbl_change_caption, 1, wxALIGN_CENTER_VERTICAL);
    change_row->Add(lbl_change_val_, 0, wxALIGN_CENTER_VERTICAL);
    cash_details_sizer_->Add(change_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    pay_card_sizer->Add(cash_details_sizer_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    payment_card->SetSizer(pay_card_sizer);

    main_sizer->Add(payment_card, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    main_sizer->AddStretchSpacer(1);

    // Botones de acción
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    const int btn_h_logical = touch ? 48 : 40;
    auto* btn_cancel = new wxButton(this, wxID_CANCEL, wxT("Cancelar"));
    Theme::style_button_secondary(btn_cancel, btn_h_logical);
    Theme::ensure_button_min_size(btn_cancel, 110, btn_h_logical);

    btn_confirm_ = new wxButton(this, wxID_ANY, wxT("Confirmar Venta"));
    Theme::style_button_success(btn_confirm_, btn_h_logical);
    Theme::setup_button_icon(btn_confirm_, "check_circle", wxSize(20, 20), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_confirm_, 170, btn_h_logical);

    btn_sizer->Add(btn_cancel, 0, wxRIGHT, FromDIP(12));
    btn_sizer->Add(btn_confirm_, 1);
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    if (touch) {
        main_sizer->SetMinSize(FromDIP(wxSize(436, 540)));
        outer_sizer->Add(main_sizer, 1, wxEXPAND);

        keypad_ = new ui::NumericKeypad(this, true, false);
        keypad_->set_target_field(txt_cash_received_, false);
        outer_sizer->Add(
            keypad_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP | wxBOTTOM | wxRESERVE_SPACE_EVEN_IF_HIDDEN, FromDIP(16));
        SetSizerAndFit(outer_sizer);
    } else {
        main_sizer->SetMinSize(FromDIP(wxSize(440, 520)));
        outer_sizer->Add(main_sizer, 1, wxEXPAND);
        SetSizerAndFit(outer_sizer);
    }
    Center();

    // Eventos
    choice_method_->Bind(wxEVT_COMBOBOX, &CheckoutDialog::on_payment_method_changed, this);
    txt_cash_received_->Bind(wxEVT_TEXT, &CheckoutDialog::on_cash_received_text_changed, this);
    btn_confirm_->Bind(wxEVT_BUTTON, &CheckoutDialog::on_confirm, this);

    refresh_preview();
}

// Total a cobrar con el método elegido: lo calcula el servicio (la interfaz no calcula dinero).
void CheckoutDialog::refresh_preview() {
    preview_ok_ = false;
    std::string error;
    if (!sale_service_) {
        error = "No se puede cobrar: el servicio de ventas no está disponible.";
    } else {
        auto r = sale_service_->preview_checkout(selected_method_);
        if (r.is_success()) {
            preview_ = r.value();
            preview_ok_ = true;
        } else {
            error = r.error_message();
        }
    }
    if (preview_ok_) {
        total_ = preview_.charged_total;
        discount_ = preview_.discount;
        lbl_total_val_->SetLabel(money_text(total_));
        lbl_products_val_->SetLabel(money_text(preview_.products_total));
        lbl_discount_val_->SetLabel(wxT("-") + money_text(preview_.discount));
        lbl_surcharge_caption_->SetLabel(wxString::Format(wxT("Cargo por pago con tarjeta (%s %% + %s)"),
                                                          percent_text(preview_.percent_hundredths), money_text(preview_.fixed)));
        lbl_surcharge_val_->SetLabel(money_text(preview_.card_surcharge));
        lbl_preview_error_->SetLabel(wxEmptyString);
    } else {
        lbl_preview_error_->SetLabel(wxString::FromUTF8(error.c_str()));
    }
    const bool show_discount = preview_ok_ && preview_.discount.cents() > 0;
    const bool show_surcharge = preview_ok_ && preview_.card_surcharge.cents() > 0;
    lbl_discount_caption_->Show(show_discount);
    lbl_discount_val_->Show(show_discount);
    lbl_surcharge_caption_->Show(show_surcharge);
    lbl_surcharge_val_->Show(show_surcharge);
    lbl_preview_error_->Show(!preview_ok_);
    // Un texto largo se parte en varias líneas dentro de la tarjeta (nada se corta)
    const int wrap_w = FromDIP(touch_wrap_width_);
    lbl_preview_error_->Wrap(wrap_w);
    lbl_surcharge_caption_->Wrap(wrap_w - FromDIP(110));
    update_cash_calculations();
    update_confirm_label();
    relayout();
}

void CheckoutDialog::update_confirm_label() {
    if (!btn_confirm_) return;
    if (preview_ok_ && selected_method_ == contracts::PaymentMethod::Tarjeta) {
        btn_confirm_->SetLabel(wxString::Format(wxT("Confirmar Venta — %s"), money_text(total_)));
    } else {
        btn_confirm_->SetLabel(wxT("Confirmar Venta"));
    }
    if (!preview_ok_) btn_confirm_->Enable(false);
}

void CheckoutDialog::relayout() {
    if (!GetSizer()) return;
    Layout();
    GetSizer()->SetSizeHints(this);
    Fit();
    Layout();
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
    if (keypad_) {
        keypad_->Show(sel == 0);
    }
    if (sel == 0 && preview_ok_) txt_cash_received_->ChangeValue(money_text(preview_.products_total));
    refresh_preview();
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
            btn_confirm_->Enable(preview_ok_);
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
            btn_confirm_->Enable(preview_ok_);
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
    if (!preview_ok_) return;   // sin vista previa del servicio no se cobra
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
