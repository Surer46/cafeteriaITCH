#include "src/frontend/refund_dialog.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ticket_print_dialog.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <wx/display.h>
#include <wx/statline.h>
#include <algorithm>
#include <chrono>

namespace app::frontend {

namespace {

wxString utf8(const std::string& s) { return wxString::FromUTF8(s.c_str()); }
wxString money_text(const contracts::Money& m) { return utf8(m.to_string()); }

// Una clave por apertura del diálogo (el contador evita repetirla si se abren dos diálogos en el mismo milisegundo).
std::string make_idempotency_key(const std::string& folio) {
    static unsigned counter = 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "refund-" + folio + "-" + std::to_string(ms) + "-" + std::to_string(++counter);
}

} // namespace

RefundDialog::RefundDialog(wxWindow* parent, services::ServiceRegistry services, const std::string& sale_folio)
    : wxDialog(parent, wxID_ANY, wxT("Reembolso de venta — Cafetería ITCH II"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE),
      services_(std::move(services)),
      folio_(sale_folio),
      idempotency_key_(make_idempotency_key(sale_folio)) {
    SetBackgroundColour(Theme::ColorBackground);
    if (!services_.refund_service) {
        build_blocked_ui("El servicio de reembolsos no está disponible.");
        return;
    }
    auto res = services_.refund_service->get_sale_for_refund(folio_);
    if (!res.is_success()) {
        build_blocked_ui(res.error_message());
        return;
    }
    detail_ = res.value();
    if (!detail_.can_refund) {
        build_blocked_ui(detail_.cannot_refund_reason);
        return;
    }
    build_ui();
}

void RefundDialog::build_blocked_ui(const std::string& message) {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* title = new wxStaticText(this, wxID_ANY, wxString::Format(wxT("Venta %s"), utf8(folio_)));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    sizer->Add(title, 0, wxALL, FromDIP(16));

    auto* text = new wxStaticText(this, wxID_ANY, utf8(message));
    text->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    text->SetForegroundColour(Theme::ColorError);
    text->Wrap(FromDIP(400));
    sizer->Add(text, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    const int h = Theme::is_touch() ? 48 : 40;
    auto* btn = new wxButton(this, wxID_CANCEL, wxT("Cerrar"));
    Theme::style_button_secondary(btn, h);
    Theme::ensure_button_min_size(btn, 120, h);
    sizer->Add(btn, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));
    sizer->SetMinSize(FromDIP(wxSize(440, -1)));
    SetSizerAndFit(sizer);
    Center();
}

void RefundDialog::build_ui() {
    const bool touch = Theme::is_touch();
    const int ctrl_h = touch ? 48 : 36;            // alto de campos y botones de cantidad (lógico)
    const int step_w = touch ? 48 : 36;            // ancho de los botones − y +
    const int btn_h = touch ? 48 : 40;

    auto* outer = new wxBoxSizer(wxHORIZONTAL);
    auto* main = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, wxString::Format(wxT("Reembolsar venta %s"), utf8(detail_.folio)));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    main->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    const char* method_name = detail_.payment_method == contracts::PaymentMethod::Efectivo ? "Efectivo"
                              : detail_.payment_method == contracts::PaymentMethod::Tarjeta ? "Tarjeta" : "Transferencia";
    auto* info = new wxStaticText(this, wxID_ANY,
        wxString::Format(wxT("%s · %s · Cobrado %s"), utf8(detail_.created_at), wxString::FromUTF8(method_name), money_text(detail_.charged_total)));
    info->SetFont(Theme::get_font_body());
    info->SetForegroundColour(Theme::ColorTextSecondary);
    main->AddSpacer(FromDIP(4));
    main->Add(info, 0, wxLEFT | wxRIGHT, FromDIP(16));

    // Tabla de líneas: solo se desplaza en vertical; las columnas se reparten en el ancho disponible
    wrap_width_ = touch ? 520 : 640;
    scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_NONE);
    scroll_->SetBackgroundColour(Theme::ColorSurface);
    scroll_->SetScrollRate(0, FromDIP(8));
    scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    auto* grid = new wxFlexGridSizer(4, FromDIP(8), FromDIP(10));
    grid->AddGrowableCol(0, 1);
    auto head = [&](const wxString& text, int align) {
        auto* h = new wxStaticText(scroll_, wxID_ANY, text);
        h->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
        h->SetForegroundColour(Theme::ColorTextSecondary);
        grid->Add(h, 0, align | wxALIGN_CENTER_VERTICAL);
    };
    head(wxT("Producto"), 0);
    head(wxT("Vendidas"), wxALIGN_RIGHT);
    head(wxT("Reembolsadas"), wxALIGN_RIGHT);
    head(wxT("Cantidad"), wxALIGN_CENTER_HORIZONTAL);

    bool any_prepared_parts = false;
    rows_.reserve(detail_.lines.size());
    for (std::size_t i = 0; i < detail_.lines.size(); ++i) {
        const auto& l = detail_.lines[i];
        LineRow row;
        row.sale_item_id = l.sale_item_id;
        row.refundable = l.quantity_refundable;
        row.has_prepared_parts = l.has_prepared_parts;
        any_prepared_parts = any_prepared_parts || l.has_prepared_parts;

        auto* name = new wxStaticText(scroll_, wxID_ANY, utf8(l.product_name), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        name->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        name->SetMinSize(wxSize(FromDIP(110), -1));
        // La casilla «Ya se preparó» va bajo el nombre del producto (misma fila): la tabla no crece en alto ni en ancho
        auto* name_box = new wxBoxSizer(wxVERTICAL);
        name_box->Add(name, 0, wxEXPAND);
        grid->Add(name_box, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
        auto* sold = new wxStaticText(scroll_, wxID_ANY, wxString::Format(wxT("%lld"), static_cast<long long>(l.quantity_sold)));
        sold->SetFont(Theme::get_font_body());
        grid->Add(sold, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
        auto* done = new wxStaticText(scroll_, wxID_ANY, wxString::Format(wxT("%lld"), static_cast<long long>(l.quantity_refunded)));
        done->SetFont(Theme::get_font_body());
        grid->Add(done, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);

        auto* qty_row = new wxBoxSizer(wxHORIZONTAL);
        row.minus = new wxButton(scroll_, wxID_ANY, wxT("−"));
        Theme::style_button_secondary(row.minus, ctrl_h);
        row.minus->SetMinSize(FromDIP(wxSize(step_w, ctrl_h)));
        row.qty = new wxTextCtrl(scroll_, wxID_ANY, wxT("0"), wxDefaultPosition, FromDIP(wxSize(56, ctrl_h)), wxTE_CENTER,
                                 wxTextValidator(wxFILTER_DIGITS));
        row.qty->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        row.qty->SetMinSize(FromDIP(wxSize(56, ctrl_h)));
        row.plus = new wxButton(scroll_, wxID_ANY, wxT("+"));
        Theme::style_button_secondary(row.plus, ctrl_h);
        row.plus->SetMinSize(FromDIP(wxSize(step_w, ctrl_h)));
        qty_row->Add(row.minus, 0, wxALIGN_CENTER_VERTICAL);
        qty_row->Add(row.qty, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(4));
        qty_row->Add(row.plus, 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(qty_row, 0, wxALIGN_CENTER_VERTICAL);

        if (l.has_prepared_parts) {
            // La casilla va en su propia fila, bajo el producto, para que la tabla nunca pida más ancho
            row.prepared = new wxCheckBox(scroll_, wxID_ANY, wxT("Ya se preparó"));
            row.prepared->SetFont(Theme::get_font_body());
            row.prepared->SetValue(true);   // por omisión: si se desmarca por error, el inventario subiría con ingredientes que ya no existen
            row.prepared->SetMinSize(wxSize(-1, FromDIP(ctrl_h)));
            name_box->Add(row.prepared, 0, wxTOP, FromDIP(2));
        }
        rows_.push_back(row);
    }
    auto* scroll_sizer = new wxBoxSizer(wxVERTICAL);
    scroll_sizer->Add(grid, 1, wxEXPAND | wxALL, FromDIP(16));
    scroll_->SetSizer(scroll_sizer);
    // Filas visibles sin desplazar: pocas, para que el diálogo quepa en 960x600 (más líneas se desplazan en vertical)
    int content_h = 0;
    for (const auto& r : rows_) content_h += std::max(ctrl_h, r.has_prepared_parts ? 24 + ctrl_h : 0) + 8;
    const int budget_h = touch ? 120 : 190;   // alto lógico visible de las filas; el resto se desplaza
    const int visible_h = std::max(ctrl_h + 8, std::min(content_h, budget_h));
    scroll_->SetMinSize(FromDIP(wxSize(wrap_width_, 24 + 16 + visible_h + 16)));
    main->AddSpacer(FromDIP(8));
    main->Add(scroll_, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    if (any_prepared_parts) {
        auto* help = new wxStaticText(this, wxID_ANY,
            wxT("Marcada: queda como merma. Desmarcada: los ingredientes regresan al inventario."));
        help->SetFont(Theme::get_font_small());
        help->SetForegroundColour(Theme::ColorTextSecondary);
        help->Wrap(FromDIP(wrap_width_));
        main->AddSpacer(FromDIP(8));
        main->Add(help, 0, wxLEFT | wxRIGHT, FromDIP(16));
    }

    auto* lbl_reason = new wxStaticText(this, wxID_ANY, wxT("Motivo del reembolso (de 3 a 200 caracteres)"));
    lbl_reason->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_reason->SetForegroundColour(Theme::ColorTextSecondary);
    main->AddSpacer(FromDIP(8));
    main->Add(lbl_reason, 0, wxLEFT | wxRIGHT, FromDIP(16));
    auto* reason_field = ui::make_text_field(this, txt_reason_, touch ? 48 : 36, -1, Theme::get_font_body());
    main->AddSpacer(FromDIP(4));
    main->Add(reason_field, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    // Resumen: el importe lo calcula el servicio
    auto* summary = new CardPanel(this);
    summary->set_card_background(Theme::ColorSurface);
    auto* sum_sizer = new wxBoxSizer(wxVERTICAL);
    lbl_amount_ = new wxStaticText(summary, wxID_ANY, wxT("Importe a devolver: $0.00"));
    lbl_amount_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_amount_->SetForegroundColour(Theme::ColorSuccess);
    sum_sizer->Add(lbl_amount_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
    lbl_method_ = new wxStaticText(summary, wxID_ANY, wxEmptyString);
    lbl_method_->SetFont(Theme::get_font_body());
    lbl_method_->SetForegroundColour(Theme::ColorText);
    sum_sizer->AddSpacer(FromDIP(6));
    sum_sizer->Add(lbl_method_, 0, wxLEFT | wxRIGHT, FromDIP(16));
    lbl_surcharge_note_ = new wxStaticText(summary, wxID_ANY, wxEmptyString);
    lbl_surcharge_note_->SetFont(Theme::get_font_body());
    lbl_surcharge_note_->SetForegroundColour(Theme::ColorTextSecondary);
    sum_sizer->AddSpacer(FromDIP(6));
    sum_sizer->Add(lbl_surcharge_note_, 0, wxLEFT | wxRIGHT, FromDIP(16));
    lbl_error_ = new wxStaticText(summary, wxID_ANY, wxEmptyString);
    lbl_error_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_error_->SetForegroundColour(Theme::ColorError);
    sum_sizer->AddSpacer(FromDIP(6));
    sum_sizer->Add(lbl_error_, 0, wxLEFT | wxRIGHT, FromDIP(16));
    sum_sizer->AddSpacer(FromDIP(16));
    summary->SetSizer(sum_sizer);
    main->AddSpacer(FromDIP(8));
    main->Add(summary, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_cancel = new wxButton(this, wxID_CANCEL, wxT("Cancelar"));
    Theme::style_button_secondary(btn_cancel, btn_h);
    Theme::ensure_button_min_size(btn_cancel, 110, btn_h);
    btn_confirm_ = new wxButton(this, wxID_ANY, wxT("Reembolsar"));
    Theme::style_button_primary(btn_confirm_, btn_h);
    Theme::ensure_button_min_size(btn_confirm_, 170, btn_h);
    btn_row->Add(btn_cancel, 0, wxRIGHT, FromDIP(12));
    btn_row->Add(btn_confirm_, 1);
    main->Add(btn_row, 0, wxEXPAND | wxALL, FromDIP(16));

    if (touch) {
        outer->Add(main, 1, wxEXPAND);
        keypad_ = new ui::NumericKeypad(this, false, true);
        outer->Add(keypad_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(16));
    } else {
        outer->Add(main, 1, wxEXPAND);
    }
    SetSizerAndFit(outer);

    // Nunca más grande que la pantalla: lo que no cabe se desplaza en vertical dentro de la tabla
    const int disp = wxDisplay::GetFromWindow(this);
    wxRect area = wxDisplay(disp == wxNOT_FOUND ? 0 : disp).GetClientArea();
    wxSize sz = GetSize();
    const int max_w = area.width - FromDIP(24), max_h = area.height - FromDIP(24);
    if (sz.x > max_w || sz.y > max_h) {
        SetSize(wxSize(std::min(sz.x, max_w), std::min(sz.y, max_h)));
        Layout();
    }
    Center();

    // Eventos
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        auto& r = rows_[i];
        r.minus->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { set_qty(i, qty_of(i) - 1); });
        r.plus->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { set_qty(i, qty_of(i) + 1); });
        r.qty->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update_preview(); });
        if (r.prepared) r.prepared->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { update_preview(); });
        if (keypad_) {
            r.qty->Bind(wxEVT_SET_FOCUS, [this, i](wxFocusEvent& ev) {
                keypad_->set_target_field(rows_[i].qty, false);
                ev.Skip();
            });
        }
    }
    txt_reason_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update_preview(); });
    btn_confirm_->Bind(wxEVT_BUTTON, &RefundDialog::on_confirm, this);
    if (keypad_ && !rows_.empty()) keypad_->set_target_field(rows_[0].qty, false);
    update_preview();
}

std::int64_t RefundDialog::qty_of(std::size_t index) const {
    long long v = 0;
    wxString s = rows_[index].qty->GetValue().Trim().Trim(false);
    if (s.empty() || !s.ToLongLong(&v) || v < 0) return 0;
    return static_cast<std::int64_t>(v);
}

void RefundDialog::set_qty(std::size_t index, std::int64_t value) {
    const std::int64_t clamped = std::max<std::int64_t>(0, std::min(value, rows_[index].refundable));
    rows_[index].qty->ChangeValue(wxString::Format(wxT("%lld"), static_cast<long long>(clamped)));
    update_preview();
}

contracts::RefundRequestDto RefundDialog::build_request() const {
    contracts::RefundRequestDto req;
    req.folio = folio_;
    req.reason = std::string(txt_reason_->GetValue().ToUTF8());
    req.idempotency_key = idempotency_key_;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto q = qty_of(i);
        if (q <= 0) continue;
        contracts::RefundLineRequestDto l;
        l.sale_item_id = rows_[i].sale_item_id;
        l.quantity = q;
        l.already_prepared = rows_[i].prepared ? rows_[i].prepared->GetValue() : false;
        req.lines.push_back(l);
    }
    return req;
}

void RefundDialog::show_error(const wxString& text) {
    lbl_error_->SetLabel(text);
    lbl_error_->Wrap(FromDIP(wrap_width_ - 40));
    Layout();
}

void RefundDialog::update_preview() {
    if (!lbl_amount_) return;
    const bool cash = detail_.payment_method == contracts::PaymentMethod::Efectivo;
    lbl_method_->SetLabel(cash ? wxT("Se devuelve en efectivo de la caja.")
                               : wxT("Devolver por Mercado Pago; aquí solo se registra."));
    if (detail_.payment_method == contracts::PaymentMethod::Tarjeta && detail_.card_surcharge.cents() > 0) {
        lbl_surcharge_note_->SetLabel(wxString::Format(wxT("El cargo por tarjeta (%s) no se devuelve."), money_text(detail_.card_surcharge)));
    } else {
        lbl_surcharge_note_->SetLabel(wxEmptyString);
    }
    lbl_surcharge_note_->Show(!lbl_surcharge_note_->GetLabel().empty());

    auto req = build_request();
    preview_ok_ = false;
    preview_reason_error_.clear();
    if (req.lines.empty()) {
        lbl_amount_->SetLabel(wxT("Importe a devolver: $0.00"));
        show_error(wxT("Elige cuántas piezas reembolsar."));
        lbl_error_->SetForegroundColour(Theme::ColorTextSecondary);
    } else {
        auto pv = services_.refund_service->preview_refund(req);
        if (pv.is_success()) {
            preview_ok_ = true;
            preview_reason_error_ = pv.value().reason_error;
            lbl_amount_->SetLabel(wxString::Format(wxT("Importe a devolver: %s"), money_text(pv.value().amount)));
            lbl_error_->SetForegroundColour(Theme::ColorError);
            show_error(reason_attempted_ && !preview_reason_error_.empty() ? utf8(preview_reason_error_) : wxString());
        } else {
            lbl_amount_->SetLabel(wxT("Importe a devolver: $0.00"));
            lbl_error_->SetForegroundColour(Theme::ColorError);
            show_error(utf8(pv.error_message()));
        }
    }
    lbl_error_->Show(!lbl_error_->GetLabel().empty());
    btn_confirm_->Enable(preview_ok_ && !refunding_);
    GetSizer()->Layout();
}

void RefundDialog::on_confirm(wxCommandEvent&) {
    if (refunding_ || !preview_ok_) return;   // un doble clic no abre dos veces la confirmación ni envía dos veces
    if (!preview_reason_error_.empty()) {
        reason_attempted_ = true;
        update_preview();
        txt_reason_->SetFocus();
        return;
    }
    auto req = build_request();
    auto pv = services_.refund_service->preview_refund(req);
    if (!pv.is_success()) {
        show_error(utf8(pv.error_message()));
        return;
    }
    const int answer = wxMessageBox(
        wxString::Format(wxT("¿Reembolsar %s de la venta %s?\n\n%s"), money_text(pv.value().amount), utf8(detail_.folio),
                         detail_.payment_method == contracts::PaymentMethod::Efectivo
                             ? wxT("El efectivo sale de la caja del turno abierto.")
                             : wxT("La devolución real se hace por Mercado Pago; aquí solo se registra.")),
        wxT("Confirmar reembolso"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (answer != wxYES) return;

    refunding_ = true;
    btn_confirm_->Enable(false);
    auto res = services_.refund_service->refund_sale(req);
    if (!res.is_success()) {
        refunding_ = false;
        show_error(utf8(res.error_message()));
        lbl_error_->Show(true);
        update_preview();
        show_error(utf8(res.error_message()));
        return;
    }
    refunded_ = true;
    refund_folio_ = res.value().refund_folio;
    wxMessageBox(
        wxString::Format(wxT("Reembolso %s registrado por %s."), utf8(res.value().refund_folio), money_text(res.value().amount)),
        wxT("Reembolso registrado"), wxOK | wxICON_INFORMATION, this);

    contracts::PrinterStationConfig print_cfg;
    if (services_.print_config_service) {
        auto cfg_res = services_.print_config_service->get_station_config();
        if (cfg_res.is_success()) print_cfg = cfg_res.value();
    }
    TicketPrintDialog print_dlg(this, res.value().to_ticket(), print_cfg);
    print_dlg.ShowModal();
    EndModal(wxID_OK);
}

} // namespace app::frontend
