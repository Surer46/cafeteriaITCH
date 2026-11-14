#include "src/frontend/audit_log_panel.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <algorithm>
#include <set>

namespace app::frontend {

namespace {

class AuditComboBox : public wxOwnerDrawnComboBox {
public:
    AuditComboBox(wxWindow* parent, wxWindowID id, const wxSize& size)
        : wxOwnerDrawnComboBox(parent, id, wxEmptyString, wxDefaultPosition, size, 0, nullptr, wxCB_READONLY) {}

protected:
    wxCoord OnMeasureItem(size_t) const override {
        return Theme::is_touch() ? FromDIP(44) : -1;
    }

    void OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const override {
        if (item == wxNOT_FOUND) return;
        const wxString text = GetString(static_cast<unsigned int>(item));
        wxCoord tw = 0;
        wxCoord th = 0;
        dc.GetTextExtent(text.empty() ? wxString(wxT("Ag")) : text, &tw, &th);
        const int y = rect.y + std::max(0, (rect.height - th) / 2);
        const int x = rect.x + ((flags & wxODCB_PAINTING_CONTROL) ? FromDIP(8) : FromDIP(10));
        dc.DrawText(text, x, y);
    }
};

} // namespace

AuditLogPanel::AuditLogPanel(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service)
    : wxPanel(parent, wxID_ANY),
      account_service_(std::move(account_service)) {
    setup_ui();
    load_events();
}

void AuditLogPanel::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    int btn_h = FromDIP(Theme::is_touch() ? 48 : 34);
    int ctrl_h = FromDIP(Theme::is_touch() ? 48 : 34);

    // 1. Barra superior
    auto* top_bar = new wxBoxSizer(wxHORIZONTAL);
    auto* title = new wxStaticText(this, wxID_ANY, wxT("Bitácora de Auditoría y Eventos"));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    top_bar->Add(title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    auto* subtitle = new wxStaticText(this, wxID_ANY, wxT("Registro cronológico inmutable de operaciones del sistema"));
    subtitle->SetFont(Theme::get_font_small());
    subtitle->SetForegroundColour(Theme::ColorTextSecondary);
    top_bar->Add(subtitle, 0, wxALIGN_CENTER_VERTICAL);

    top_bar->AddStretchSpacer(1);
    root_sizer->Add(top_bar, 0, wxEXPAND | wxALL, 12);

    // 2. Banner de aviso/error
    pnl_warning_ = new wxPanel(this, wxID_ANY);
    pnl_warning_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* warn_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_warning_ = new wxStaticText(pnl_warning_, wxID_ANY, wxEmptyString);
    lbl_warning_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_warning_->SetForegroundColour(Theme::ColorError);
    warn_sizer->Add(lbl_warning_, 1, wxALL, 10);
    pnl_warning_->SetSizer(warn_sizer);
    pnl_warning_->Show(false);
    root_sizer->Add(pnl_warning_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // 3. Tarjeta contenedora principal
    main_card_ = new CardPanel(this, wxID_ANY);
    main_card_->set_card_background(Theme::ColorSurface);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    // 3.1 Sección de Filtros
    auto* filters_box = new wxBoxSizer(wxVERTICAL);

    // Fila 1 de filtros: Evento, Usuario, Botón Buscar
    auto* row1_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* lbl_ev = new wxStaticText(main_card_, wxID_ANY, wxT("Tipo de evento:"));
    lbl_ev->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_ev->SetForegroundColour(Theme::ColorText);
    row1_sizer->Add(lbl_ev, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    choice_event_type_ = new AuditComboBox(main_card_, wxID_ANY, wxSize(FromDIP(160), ctrl_h));
    choice_event_type_->SetFont(Theme::get_font_body());
    choice_event_type_->Append(wxT("Todos"));
    choice_event_type_->SetSelection(0);
    choice_event_type_->SetMinSize(wxSize(FromDIP(160), ctrl_h));
    row1_sizer->Add(choice_event_type_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

    auto* lbl_u = new wxStaticText(main_card_, wxID_ANY, wxT("Usuario:"));
    lbl_u->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_u->SetForegroundColour(Theme::ColorText);
    row1_sizer->Add(lbl_u, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    auto* user_field = ui::make_text_field(main_card_, txt_user_, ctrl_h, 140, Theme::get_font_body());
    txt_user_->SetHint(wxT("Username o ID"));
    row1_sizer->Add(user_field, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

    btn_search_ = new wxButton(main_card_, wxID_ANY, wxT("Buscar"));
    Theme::style_button_primary(btn_search_, btn_h);
    Theme::setup_button_icon(btn_search_, "search", wxSize(18, 18), Theme::ColorSurface);
    btn_search_->SetMinSize(wxSize(110, btn_h));
    btn_search_->Bind(wxEVT_BUTTON, &AuditLogPanel::on_search_clicked, this);
    row1_sizer->Add(btn_search_, 0, wxALIGN_CENTER_VERTICAL);

    filters_box->Add(row1_sizer, 0, wxEXPAND | wxBOTTOM, 8);

    // Fila 2 de filtros: Fecha desde, Fecha hasta
    auto* row2_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto* lbl_from = new wxStaticText(main_card_, wxID_ANY, wxT("Fecha desde:"));
    lbl_from->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_from->SetForegroundColour(Theme::ColorText);
    row2_sizer->Add(lbl_from, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    auto* from_field = ui::make_text_field(main_card_, txt_date_from_, ctrl_h, 120, Theme::get_font_body());
    txt_date_from_->SetHint(wxT("YYYY-MM-DD"));
    row2_sizer->Add(from_field, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

    auto* lbl_to = new wxStaticText(main_card_, wxID_ANY, wxT("Fecha hasta:"));
    lbl_to->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_to->SetForegroundColour(Theme::ColorText);
    row2_sizer->Add(lbl_to, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    auto* to_field = ui::make_text_field(main_card_, txt_date_to_, ctrl_h, 120, Theme::get_font_body());
    txt_date_to_->SetHint(wxT("YYYY-MM-DD"));
    row2_sizer->Add(to_field, 0, wxALIGN_CENTER_VERTICAL);

    filters_box->Add(row2_sizer, 0, wxEXPAND | wxBOTTOM, 12);
    card_sizer->Add(filters_box, 0, wxEXPAND | wxALL, 12);

    // 3.2 Lista de eventos
    list_events_ = new wxListView(
        main_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE
    );
    list_events_->SetFont(Theme::get_font_body());
    if (Theme::is_touch()) {
        ui::apply_touch_row_height(list_events_);
    }
    list_events_->InsertColumn(0, wxT("Fecha y hora"), wxLIST_FORMAT_LEFT, 160);
    list_events_->InsertColumn(1, wxT("Usuario"), wxLIST_FORMAT_LEFT, 110);
    list_events_->InsertColumn(2, wxT("Evento"), wxLIST_FORMAT_LEFT, 130);
    list_events_->InsertColumn(3, wxT("Detalle"), wxLIST_FORMAT_LEFT, 260);

    // Recalcular al cambiar el tamaño de la lista o al aparecer/desaparecer la barra vertical.
    list_events_->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        auto_size_columns();
    });
    list_events_->Bind(wxEVT_LIST_ITEM_SELECTED, &AuditLogPanel::on_item_selected, this);
    list_events_->Bind(wxEVT_LIST_ITEM_DESELECTED, &AuditLogPanel::on_item_deselected, this);
    card_sizer->Add(list_events_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // 3.3 Visualizador de detalle completo
    auto* lbl_detail_hdr = new wxStaticText(main_card_, wxID_ANY, wxT("Detalle completo del evento seleccionado:"));
    lbl_detail_hdr->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_detail_hdr->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_detail_hdr, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

    int detail_h = Theme::is_touch() ? 64 : 48;
    txt_detail_ = new wxTextCtrl(
        main_card_, wxID_ANY,
        wxT("Seleccione un evento de la lista para ver el detalle completo."),
        wxDefaultPosition, wxSize(-1, detail_h),
        wxTE_MULTILINE | wxTE_READONLY | wxBORDER_SIMPLE
    );
    txt_detail_->SetFont(Theme::get_font_body());
    txt_detail_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    card_sizer->Add(txt_detail_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // 3.4 Barra de paginación inferior
    auto* page_sizer = new wxBoxSizer(wxHORIZONTAL);

    btn_prev_ = new wxButton(main_card_, wxID_ANY, wxT("Anterior"));
    Theme::style_button_secondary(btn_prev_, btn_h);
    btn_prev_->SetMinSize(wxSize(100, btn_h));
    btn_prev_->Bind(wxEVT_BUTTON, &AuditLogPanel::on_prev_page, this);
    page_sizer->Add(btn_prev_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    lbl_page_ = new wxStaticText(main_card_, wxID_ANY, wxT("Página 1"));
    lbl_page_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_page_->SetForegroundColour(Theme::ColorText);
    page_sizer->Add(lbl_page_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    btn_next_ = new wxButton(main_card_, wxID_ANY, wxT("Siguiente"));
    Theme::style_button_secondary(btn_next_, btn_h);
    btn_next_->SetMinSize(wxSize(100, btn_h));
    btn_next_->Bind(wxEVT_BUTTON, &AuditLogPanel::on_next_page, this);
    page_sizer->Add(btn_next_, 0, wxALIGN_CENTER_VERTICAL);

    page_sizer->AddStretchSpacer(1);

    lbl_count_ = new wxStaticText(main_card_, wxID_ANY, wxT("Total: 0 eventos"));
    lbl_count_->SetFont(Theme::get_font_small());
    lbl_count_->SetForegroundColour(Theme::ColorTextSecondary);
    page_sizer->Add(lbl_count_, 0, wxALIGN_CENTER_VERTICAL);

    card_sizer->Add(page_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    main_card_->SetSizer(card_sizer);
    root_sizer->Add(main_card_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizer(root_sizer);
    Bind(wxEVT_SIZE, &AuditLogPanel::on_size, this);
}

void AuditLogPanel::load_events() {
    if (!account_service_) {
        pnl_warning_->Show(true);
        lbl_warning_->SetLabel(wxT("Servicio de cuentas no disponible."));
        Layout();
        return;
    }

    contracts::AuditFilterDto filter;
    filter.limit = kPageSize;
    filter.offset = (current_page_ - 1) * kPageSize;

    // 1. Filtro Tipo de evento
    if (choice_event_type_ && choice_event_type_->GetSelection() > 0) {
        std::string sel = std::string(
            choice_event_type_->GetString(static_cast<unsigned int>(choice_event_type_->GetSelection())).ToUTF8()
        );
        if (sel != "Todos" && !sel.empty()) {
            filter.event_type = sel;
        }
    }

    // 2. Filtro Usuario (username o ID)
    if (txt_user_) {
        std::string raw_u = std::string(txt_user_->GetValue().Trim().ToUTF8());
        if (!raw_u.empty()) {
            std::string matched_id = raw_u;
            auto users_res = account_service_->list_users();
            if (users_res.is_success()) {
                for (const auto& u : users_res.value()) {
                    if (u.username == raw_u || u.id == raw_u) {
                        matched_id = u.id;
                        break;
                    }
                }
            }
            filter.user_id = matched_id;
        }
    }

    // 3. Filtros de Fecha
    if (txt_date_from_) {
        std::string f_str = std::string(txt_date_from_->GetValue().Trim().ToUTF8());
        if (!f_str.empty()) {
            if (f_str.length() == 10) {
                f_str += " 00:00:00";
            }
            filter.start_date = f_str;
        }
    }
    if (txt_date_to_) {
        std::string t_str = std::string(txt_date_to_->GetValue().Trim().ToUTF8());
        if (!t_str.empty()) {
            if (t_str.length() == 10) {
                t_str += " 23:59:59";
            }
            filter.end_date = t_str;
        }
    }

    auto res = account_service_->list_audit_events(filter);
    if (!res.is_success()) {
        lbl_warning_->SetLabel(wxString::Format(
            wxT("Error: %s"),
            wxString::FromUTF8(res.error_message().c_str())
        ));
        pnl_warning_->Show(true);

        list_events_->DeleteAllItems();
        current_events_.clear();
        btn_prev_->Enable(false);
        btn_next_->Enable(false);
        lbl_page_->SetLabel(wxT("Página 1"));
        lbl_count_->SetLabel(wxT("Total: 0 eventos"));
        txt_detail_->SetValue(wxT("Acceso restringido o servicio no disponible."));
        Layout();
        return;
    }

    pnl_warning_->Show(false);
    current_events_ = res.value();

    list_events_->DeleteAllItems();
    for (std::size_t i = 0; i < current_events_.size(); ++i) {
        const auto& ev = current_events_[i];
        long idx = list_events_->InsertItem(static_cast<long>(i), wxString::FromUTF8(ev.created_at.c_str()));
        std::string u_disp = ev.username.empty() ? ev.user_id : ev.username;
        list_events_->SetItem(idx, 1, wxString::FromUTF8(u_disp.c_str()));
        list_events_->SetItem(idx, 2, wxString::FromUTF8(ev.event_type.c_str()));
        list_events_->SetItem(idx, 3, wxString::FromUTF8(ev.details.c_str()));
    }

    lbl_page_->SetLabel(wxString::Format(wxT("Página %zu"), current_page_));
    lbl_count_->SetLabel(wxString::Format(wxT("Mostrando %zu eventos"), current_events_.size()));

    btn_prev_->Enable(current_page_ > 1);
    btn_next_->Enable(current_events_.size() == kPageSize);

    // Actualizar choice_event_type_ con tipos presentes en la página actual más "Todos"
    if (choice_event_type_) {
        const int cur_idx = choice_event_type_->GetSelection();
        std::string current_sel = (cur_idx != wxNOT_FOUND)
            ? std::string(choice_event_type_->GetString(static_cast<unsigned int>(cur_idx)).ToUTF8())
            : std::string();
        std::set<std::string> types_on_page;
        for (const auto& ev : current_events_) {
            if (!ev.event_type.empty()) {
                types_on_page.insert(ev.event_type);
            }
        }
        choice_event_type_->Clear();
        choice_event_type_->Append(wxT("Todos"));
        for (const auto& t : types_on_page) {
            choice_event_type_->Append(wxString::FromUTF8(t.c_str()));
        }
        if (current_sel != "Todos" && types_on_page.count(current_sel)) {
            const int found_idx = choice_event_type_->FindString(wxString::FromUTF8(current_sel.c_str()));
            choice_event_type_->SetSelection(found_idx != wxNOT_FOUND ? found_idx : 0);
        } else {
            choice_event_type_->SetSelection(0);
        }
    }

    txt_detail_->SetValue(wxT("Seleccione un evento de la lista para ver el detalle completo."));

    auto_size_columns();
    Layout();
    // La barra vertical puede aparecer tras insertar filas: recalcular al asentarse el layout.
    CallAfter([this]() { auto_size_columns(); });
}

void AuditLogPanel::auto_size_columns() {
    if (!list_events_) return;

    int tw = 0, th = 0;
    int max_w0 = 0, max_w1 = 0, max_w2 = 0;
    for (const auto& evt : current_events_) {
        list_events_->GetTextExtent(wxString::FromUTF8(evt.created_at.c_str()), &tw, &th);
        max_w0 = std::max(max_w0, tw);

        std::string user_disp = evt.username.empty() ? evt.user_id : evt.username;
        list_events_->GetTextExtent(wxString::FromUTF8(user_disp.c_str()), &tw, &th);
        max_w1 = std::max(max_w1, tw);

        list_events_->GetTextExtent(wxString::FromUTF8(evt.event_type.c_str()), &tw, &th);
        max_w2 = std::max(max_w2, tw);
    }

    std::vector<ui::ColumnSpec> cols(4);
    cols[0] = {wxT("Fecha y hora"), max_w0, -1, 16, 0};
    cols[1] = {wxT("Usuario"), max_w1, -1, 16, 0};
    cols[2] = {wxT("Evento"), max_w2, -1, 16, 0};
    cols[3] = {wxT("Detalle"), 0, 0, 16, 0};
    ui::distribute_list_columns(list_events_, cols, 3);  // Detalle toma todo el espacio restante
}

void AuditLogPanel::on_search_clicked(wxCommandEvent&) {
    current_page_ = 1;
    load_events();
}

void AuditLogPanel::on_prev_page(wxCommandEvent&) {
    if (current_page_ > 1) {
        --current_page_;
        load_events();
    }
}

void AuditLogPanel::on_next_page(wxCommandEvent&) {
    if (current_events_.size() == kPageSize) {
        ++current_page_;
        load_events();
    }
}

void AuditLogPanel::on_item_selected(wxListEvent& event) {
    long idx = event.GetIndex();
    if (idx >= 0 && idx < static_cast<long>(current_events_.size())) {
        txt_detail_->SetValue(wxString::FromUTF8(current_events_[idx].details.c_str()));
    }
}

void AuditLogPanel::on_item_deselected(wxListEvent&) {
    txt_detail_->SetValue(wxT("Seleccione un evento de la lista para ver el detalle completo."));
}

void AuditLogPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    if (in_size_event_) return;
    in_size_event_ = true;

    auto_size_columns();
    Layout();

    in_size_event_ = false;
}

} // namespace app::frontend
