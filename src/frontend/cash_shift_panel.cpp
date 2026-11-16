#include "src/frontend/cash_shift_panel.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <wx/statline.h>

namespace app::frontend {

CashShiftPanel::CashShiftPanel(
    wxWindow* parent,
    services::ServiceRegistry services,
    std::function<void()> on_shift_changed
) : wxPanel(parent, wxID_ANY),
    services_(std::move(services)),
    on_shift_changed_(std::move(on_shift_changed)) {
    setup_ui();
    refresh_status();
}

void CashShiftPanel::setup_ui() {
    main_sizer_ = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(Theme::ColorBackground);

    // Título del módulo
    auto* lbl_title = new wxStaticText(this, wxID_ANY, wxT("Control de Turno de Caja"));
    lbl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    main_sizer_->Add(lbl_title, 0, wxALL, FromDIP(16));

    // Tarjeta de Estado
    status_card_ = new CardPanel(this);
    status_card_->set_card_background(Theme::ColorSurface);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    shift_badge_ = new StatusBadge(status_card_, wxT("Turno Cerrado"), false);
    card_sizer->Add(shift_badge_, 0, wxALL, FromDIP(16));

    lbl_details_ = new wxStaticText(status_card_, wxID_ANY, wxEmptyString);
    lbl_details_->SetFont(Theme::get_font_body());
    lbl_details_->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_details_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    card_sizer->Add(new wxStaticLine(status_card_, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
    card_sizer->AddSpacer(FromDIP(14));

    // ---------------------------------------------------------
    // Controles para ABRIR turno (visible cuando el turno está cerrado)
    // ---------------------------------------------------------
    open_box_ = new wxBoxSizer(wxVERTICAL);
    auto* lbl_open_prompt = new wxStaticText(status_card_, wxID_ANY, wxT("Ingrese el fondo inicial en efectivo para iniciar el turno:"));
    lbl_open_prompt->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_open_prompt->SetForegroundColour(Theme::ColorText);
    open_box_->Add(lbl_open_prompt, 0, wxBOTTOM, FromDIP(6));

    const bool touch = Theme::is_touch();
    const int field_h_logical = touch ? 48 : 36;
    const int field_w_logical = 140;
    const int btn_h_logical = touch ? 48 : 38;

    auto* open_row = new wxBoxSizer(touch ? wxVERTICAL : wxHORIZONTAL);
    auto* initial_field = ui::make_text_field(status_card_, txt_initial_cash_, field_h_logical, field_w_logical, Theme::get_font_body());
    txt_initial_cash_->SetValue(wxT("300.00"));

    btn_open_shift_ = new wxButton(status_card_, wxID_ANY, wxT("Abrir Turno de Caja"));
    Theme::style_button_success(btn_open_shift_, btn_h_logical);
    Theme::setup_button_icon(btn_open_shift_, "lock_open", wxSize(18, 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_open_shift_, 200, btn_h_logical);

    if (touch) {
        open_row->Add(initial_field, 0, wxBOTTOM | wxALIGN_LEFT, FromDIP(10));
        open_row->Add(btn_open_shift_, 0, wxALIGN_LEFT);
    } else {
        open_row->Add(initial_field, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
        open_row->Add(btn_open_shift_, 0, wxALIGN_CENTER_VERTICAL);
    }
    open_box_->Add(open_row, 0, wxEXPAND | wxBOTTOM, FromDIP(16));

    card_sizer->Add(open_box_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    // ---------------------------------------------------------
    // Controles para CERRAR turno (visible cuando el turno está abierto)
    // ---------------------------------------------------------
    close_box_ = new wxBoxSizer(wxVERTICAL);
    auto* lbl_close_prompt = new wxStaticText(status_card_, wxID_ANY, wxT("Ingrese el efectivo final contado en caja (Arqueo de cierre):"));
    lbl_close_prompt->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_close_prompt->SetForegroundColour(Theme::ColorText);
    close_box_->Add(lbl_close_prompt, 0, wxBOTTOM, FromDIP(6));

    auto* close_row = new wxBoxSizer(touch ? wxVERTICAL : wxHORIZONTAL);
    auto* final_field = ui::make_text_field(status_card_, txt_final_cash_, field_h_logical, field_w_logical, Theme::get_font_body());
    txt_final_cash_->SetValue(wxT("0.00"));

    btn_close_shift_ = new wxButton(status_card_, wxID_ANY, wxT("Cerrar Turno de Caja"));
    Theme::style_button_destructive(btn_close_shift_, btn_h_logical);
    Theme::setup_button_icon(btn_close_shift_, "lock", wxSize(18, 18), Theme::ColorError);
    Theme::ensure_button_min_size(btn_close_shift_, 200, btn_h_logical);

    if (touch) {
        close_row->Add(final_field, 0, wxBOTTOM | wxALIGN_LEFT, FromDIP(10));
        close_row->Add(btn_close_shift_, 0, wxALIGN_LEFT);
    } else {
        close_row->Add(final_field, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
        close_row->Add(btn_close_shift_, 0, wxALIGN_CENTER_VERTICAL);
    }
    close_box_->Add(close_row, 0, wxEXPAND | wxBOTTOM, FromDIP(16));

    card_sizer->Add(close_box_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    status_card_->SetSizer(card_sizer);
    main_sizer_->Add(status_card_, 0, wxEXPAND | wxALL, FromDIP(16));

    SetSizer(main_sizer_);

    btn_open_shift_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_open_shift_clicked, this);
    btn_close_shift_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_close_shift_clicked, this);

    if (touch) {
        keypad_ = new ui::NumericKeypad(this);
        auto make_protected = [this, initial_field, final_field]() {
            std::vector<wxRect> rects;
            auto add_rect = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_rect(shift_badge_);
            add_rect(lbl_details_);
            add_rect(initial_field);
            add_rect(btn_open_shift_);
            add_rect(final_field);
            add_rect(btn_close_shift_);
            return rects;
        };
        keypad_->bind_field(txt_initial_cash_, initial_field, true, btn_open_shift_, make_protected);
        keypad_->bind_field(txt_final_cash_, final_field, true, btn_close_shift_, make_protected);
    }
}

void CashShiftPanel::refresh_status() {
    if (!services_.shift_service) return;

    auto shift_res = services_.shift_service->get_current_shift();
    if (shift_res.is_success()) {
        const auto& s = shift_res.value();
        shift_badge_->set_status(wxT("Turno Activo"), true);

        wxString details = wxString::Format(
            wxT("Caja: %s (%s)\nCajero en turno: %s\nHora de apertura: %s\nFondo inicial en efectivo: %s"),
            wxString::FromUTF8(s.register_name.c_str()),
            wxString::FromUTF8(s.register_id.c_str()),
            wxString::FromUTF8(s.cashier_name.c_str()),
            wxString::FromUTF8(s.opened_at.c_str()),
            wxString::FromUTF8(s.initial_cash.to_string().c_str())
        );
        lbl_details_->SetLabel(details);

        open_box_->ShowItems(false);
        close_box_->ShowItems(true);
    } else {
        shift_badge_->set_status(wxT("Turno Cerrado"), false);

        lbl_details_->SetLabel(
            wxT("No hay ningún turno abierto en la caja actual. ")
            wxT("Debe abrir un turno con un fondo inicial para registrar cobros en el punto de venta.")
        );

        open_box_->ShowItems(true);
        close_box_->ShowItems(false);
    }

    lbl_details_->Wrap(FromDIP(Theme::is_touch() ? 290 : 660));
    status_card_->Layout();
    Layout();
}

void CashShiftPanel::on_open_shift_clicked(wxCommandEvent&) {
    wxString str = txt_initial_cash_->GetValue().Trim().Trim(false);
    std::string s = std::string(str.ToUTF8());
    if (s.empty()) {
        wxMessageBox(wxT("Debe ingresar un monto válido para el fondo inicial."), wxT("Monto Vacío"), wxOK | wxICON_WARNING, this);
        txt_initial_cash_->SetFocus();
        return;
    }

    auto parse_res = contracts::Money::try_parse(s, false);
    if (!parse_res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Monto de fondo inicial inválido: %s"), wxString::FromUTF8(parse_res.error_message().c_str())),
            wxT("Monto Inválido"),
            wxOK | wxICON_WARNING,
            this
        );
        txt_initial_cash_->SetFocus();
        return;
    }

    contracts::Money initial_cash = parse_res.value();
    auto res = services_.shift_service->open_shift("reg-01", initial_cash);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al abrir turno: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error de Apertura"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    wxMessageBox(
        wxString::Format(wxT("¡Turno abierto exitosamente!\nFondo inicial: %s"), wxString::FromUTF8(initial_cash.to_string().c_str())),
        wxT("Turno Abierto"),
        wxOK | wxICON_INFORMATION,
        this
    );

    refresh_status();
    if (on_shift_changed_) {
        on_shift_changed_();
    }
}

void CashShiftPanel::on_close_shift_clicked(wxCommandEvent&) {
    wxString str = txt_final_cash_->GetValue().Trim().Trim(false);
    std::string s = std::string(str.ToUTF8());
    if (s.empty()) {
        wxMessageBox(wxT("Debe ingresar el importe contado en el arqueo de cierre."), wxT("Monto Vacío"), wxOK | wxICON_WARNING, this);
        txt_final_cash_->SetFocus();
        return;
    }

    auto parse_res = contracts::Money::try_parse(s, false);
    if (!parse_res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Monto de arqueo final inválido: %s"), wxString::FromUTF8(parse_res.error_message().c_str())),
            wxT("Monto Inválido"),
            wxOK | wxICON_WARNING,
            this
        );
        txt_final_cash_->SetFocus();
        return;
    }

    contracts::Money final_cash = parse_res.value();
    int confirm = wxMessageBox(
        wxString::Format(wxT("¿Está seguro de cerrar el turno de caja con arqueo de %s?"), wxString::FromUTF8(final_cash.to_string().c_str())),
        wxT("Confirmar Cierre de Turno"),
        wxYES_NO | wxICON_QUESTION,
        this
    );
    if (confirm != wxYES) return;

    auto res = services_.shift_service->close_shift(final_cash);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al cerrar turno: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error de Cierre"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    wxString closed_msg = wxT("¡Turno de caja cerrado exitosamente!");
    if (services_.backup_service) {
        const auto auto_res = services_.backup_service->get_auto_backup_status();
        if (auto_res.is_success() && auto_res.value().is_enabled) {
            closed_msg += wxT("\n\nSe está haciendo un respaldo automático; puedes seguir trabajando.");
        }
    }

    wxMessageBox(
        closed_msg,
        wxT("Turno Cerrado"),
        wxOK | wxICON_INFORMATION,
        this
    );

    refresh_status();
    if (on_shift_changed_) {
        on_shift_changed_();
    }
}

} // namespace app::frontend
