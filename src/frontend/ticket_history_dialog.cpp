#include "src/frontend/ticket_history_dialog.hpp"
#include "src/frontend/ticket_print_dialog.hpp"
#include "src/frontend/refund_dialog.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <wx/statline.h>

namespace app::frontend {

TicketHistoryDialog::TicketHistoryDialog(
    wxWindow* parent,
    services::ServiceRegistry services
) : wxDialog(parent, wxID_ANY, wxT("Historial de Tickets y Reimpresión"),
             wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    services_(services) {
    setup_ui();
    load_tickets();
}

void TicketHistoryDialog::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_title = new wxStaticText(this, wxID_ANY, wxT("Historial de Ventas y Tickets"));
    lbl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    main_sizer->Add(lbl_title, 0, wxALL, FromDIP(16));

    // Barra de búsqueda por Folio
    auto* search_row = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_prompt = new wxStaticText(this, wxID_ANY, wxT("Buscar por Folio:"));
    lbl_prompt->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_prompt->SetForegroundColour(Theme::ColorTextSecondary);

    const int search_h_logical = Theme::is_touch() ? 48 : 34;

    auto* search_field = ui::make_text_field(
        this, txt_search_folio_, search_h_logical, 220, Theme::get_font_body(), wxTE_PROCESS_ENTER);

    btn_search_ = new wxButton(this, wxID_ANY, wxT("Buscar"));
    Theme::style_button_secondary(btn_search_, search_h_logical);
    Theme::setup_button_icon(btn_search_, "search", wxSize(16, 16), Theme::ColorTextSecondary);
    Theme::ensure_button_min_size(btn_search_, 100, search_h_logical);

    search_row->Add(lbl_prompt, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    search_row->Add(search_field, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    search_row->Add(btn_search_, 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(search_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));

    // Tabla de Tickets
    list_tickets_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_tickets_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        ui::apply_touch_row_height(list_tickets_);
    }
    // T17: columna «Reembolsado» después del total (vacía si no hay reembolsos); los anchos suman menos que el diálogo
    list_tickets_->InsertColumn(0, wxT("Folio"), wxLIST_FORMAT_LEFT, FromDIP(150));
    list_tickets_->InsertColumn(1, wxT("Fecha y Hora"), wxLIST_FORMAT_LEFT, FromDIP(150));
    list_tickets_->InsertColumn(2, wxT("Total"), wxLIST_FORMAT_RIGHT, FromDIP(90));
    list_tickets_->InsertColumn(3, wxT("Reembolsado"), wxLIST_FORMAT_RIGHT, FromDIP(110));
    list_tickets_->InsertColumn(4, wxT("Artículos"), wxLIST_FORMAT_RIGHT, FromDIP(80));
    list_tickets_->InsertColumn(5, wxT("Cajero"), wxLIST_FORMAT_LEFT, FromDIP(120));
    list_tickets_->InsertColumn(6, wxT("Método de Pago"), wxLIST_FORMAT_LEFT, FromDIP(110));

    main_sizer->Add(list_tickets_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));

    // Botones de acción
    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    const int action_h_logical = Theme::is_touch() ? 48 : 38;
    btn_reprint_ = new wxButton(this, wxID_ANY, wxT("Ver / Reimprimir Ticket"));
    Theme::style_button_primary(btn_reprint_, action_h_logical);
    Theme::setup_button_icon(btn_reprint_, "print", wxSize(18, 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_reprint_, 210, action_h_logical);

    btn_close_ = new wxButton(this, wxID_CANCEL, wxT("Cerrar"));
    Theme::style_button_secondary(btn_close_, action_h_logical);
    Theme::ensure_button_min_size(btn_close_, 110, action_h_logical);

    btn_row->Add(btn_reprint_, 0, wxRIGHT, FromDIP(10));
    // T17: «Reembolsar» solo si la sesión tiene pos.refund; habilitado con un ticket elegido
    if (services_.account_service && services_.account_service->has_permission("pos.refund")) {
        btn_refund_ = new wxButton(this, wxID_ANY, wxT("Reembolsar"));
        Theme::style_button_secondary(btn_refund_, action_h_logical);
        Theme::ensure_button_min_size(btn_refund_, 140, action_h_logical);
        btn_row->Add(btn_refund_, 0, wxRIGHT, FromDIP(10));
    }
    btn_row->AddStretchSpacer(1);
    btn_row->Add(btn_close_, 0);

    main_sizer->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    SetSizer(main_sizer);
    SetSize(FromDIP(wxSize(900, 520)));
    Center();

    btn_search_->Bind(wxEVT_BUTTON, &TicketHistoryDialog::on_search, this);
    txt_search_folio_->Bind(wxEVT_TEXT_ENTER, &TicketHistoryDialog::on_search, this);
    btn_reprint_->Bind(wxEVT_BUTTON, &TicketHistoryDialog::on_view_reprint, this);
    list_tickets_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &TicketHistoryDialog::on_item_activated, this);
    if (btn_refund_) {
        btn_refund_->Bind(wxEVT_BUTTON, &TicketHistoryDialog::on_refund, this);
        list_tickets_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& ev) { ev.Skip(); update_buttons(); });
        list_tickets_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent& ev) { ev.Skip(); update_buttons(); });
        update_buttons();
    }
}

void TicketHistoryDialog::update_buttons() {
    if (!btn_refund_) return;
    btn_refund_->Enable(list_tickets_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) >= 0);
}

void TicketHistoryDialog::on_refund(wxCommandEvent&) {
    long sel = list_tickets_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(tickets_.size())) return;
    const std::string folio = tickets_[sel].folio;
    RefundDialog dlg(this, services_, folio);
    dlg.ShowModal();
    if (dlg.was_refunded()) {
        load_tickets();
        for (std::size_t i = 0; i < tickets_.size(); ++i) {
            if (tickets_[i].folio == folio) {
                list_tickets_->SetItemState(static_cast<long>(i), wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                break;
            }
        }
        update_buttons();
    }
}

void TicketHistoryDialog::load_tickets() {
    if (!services_.sale_service) return;

    auto res = services_.sale_service->get_recent_tickets(50);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al consultar historial de tickets: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    tickets_ = res.value();
    list_tickets_->DeleteAllItems();

    long idx = 0;
    for (const auto& t : tickets_) {
        long row = list_tickets_->InsertItem(idx, wxString::FromUTF8(t.folio.c_str()));
        list_tickets_->SetItem(row, 1, wxString::FromUTF8(t.created_at.c_str()));
        list_tickets_->SetItem(row, 2, wxString::FromUTF8(t.total.to_string().c_str()));
        list_tickets_->SetItem(row, 3, t.refunded_total.cents() > 0 ? wxString::FromUTF8(t.refunded_total.to_string().c_str()) : wxString());
        list_tickets_->SetItem(row, 4, wxString::Format(wxT("%lld"), t.items_count));
        list_tickets_->SetItem(row, 5, wxString::FromUTF8(t.cashier_name.c_str()));
        list_tickets_->SetItem(row, 6, wxString::FromUTF8(contracts::to_string(t.payment_method)));
        idx++;
    }
}

void TicketHistoryDialog::on_search(wxCommandEvent&) {
    wxString folio_query = txt_search_folio_->GetValue().Trim().Trim(false);
    if (folio_query.empty()) {
        load_tickets();
        return;
    }

    std::string folio = std::string(folio_query.ToUTF8());
    auto ticket_res = services_.sale_service->get_ticket(folio);
    if (!ticket_res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("No se encontró ninguna venta registrada con el folio '%s'."), wxString::FromUTF8(folio.c_str())),
            wxT("Ticket No Encontrado"),
            wxOK | wxICON_INFORMATION,
            this
        );
        return;
    }

    contracts::PrinterStationConfig print_cfg;
    if (services_.print_config_service) {
        auto cfg_res = services_.print_config_service->get_station_config();
        if (cfg_res.is_success()) {
            print_cfg = cfg_res.value();
        }
    }

    TicketPrintDialog print_dlg(this, ticket_res.value(), print_cfg);
    print_dlg.ShowModal();
}

void TicketHistoryDialog::on_view_reprint(wxCommandEvent&) {
    long sel = list_tickets_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(tickets_.size())) {
        wxMessageBox(wxT("Seleccione un ticket de la lista para ver o reimprimir."), wxT("Aviso"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto& item = tickets_[sel];
    auto ticket_res = services_.sale_service->get_ticket(item.folio);
    if (!ticket_res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al recuperar el ticket: %s"), wxString::FromUTF8(ticket_res.error_message().c_str())),
            wxT("Error"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    contracts::PrinterStationConfig print_cfg;
    if (services_.print_config_service) {
        auto cfg_res = services_.print_config_service->get_station_config();
        if (cfg_res.is_success()) {
            print_cfg = cfg_res.value();
        }
    }

    TicketPrintDialog print_dlg(this, ticket_res.value(), print_cfg);
    print_dlg.ShowModal();
}

void TicketHistoryDialog::on_item_activated(wxListEvent&) {
    wxCommandEvent dummy;
    on_view_reprint(dummy);
}

} // namespace app::frontend
