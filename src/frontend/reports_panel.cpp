#include "src/frontend/reports_panel.hpp"

#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <wx/datetime.h>
#include <wx/filedlg.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace app::frontend {

namespace {

constexpr const char* kReportLabels[7] = {
    "Ventas por día",
    "Ventas por producto",
    "Ventas por método de pago",
    "Ventas por cajero",
    "Mermas y ajustes",
    "Movimientos de inventario",
    "Compras"
};

constexpr const char* kReportSlugs[7] = {
    "ventas-por-dia",
    "ventas-por-producto",
    "ventas-por-metodo-pago",
    "ventas-por-cajero",
    "mermas-y-ajustes",
    "movimientos-de-inventario",
    "compras"
};

contracts::Money abs_money_cents(std::int64_t cents) {
    if (cents >= 0) return contracts::Money::from_cents(cents);
    if (cents == std::numeric_limits<std::int64_t>::min()) {
        return contracts::Money::from_cents(std::numeric_limits<std::int64_t>::max());
    }
    return contracts::Money::from_cents(-cents);
}

std::string format_scaled_three_decimals_plain(std::uint64_t whole, std::uint64_t millis, const char* unit) {
    if (millis == 0) {
        return std::to_string(whole) + " " + unit;
    }
    char frac[3] = {
        static_cast<char>('0' + (millis / 100ULL) % 10ULL),
        static_cast<char>('0' + (millis / 10ULL) % 10ULL),
        static_cast<char>('0' + millis % 10ULL)
    };
    int len = 3;
    while (len > 0 && frac[len - 1] == '0') {
        --len;
    }
    return std::to_string(whole) + "." + std::string(frac, static_cast<std::size_t>(len)) + " " + unit;
}

std::string format_base_qty_readable(std::int64_t qty_base, const std::string& base_unit, bool signed_plus = false) {
    const bool neg = qty_base < 0;
    const std::uint64_t mag = neg
        ? static_cast<std::uint64_t>(-(qty_base + 1)) + 1ULL
        : static_cast<std::uint64_t>(qty_base);
    std::string prefix;
    if (neg) prefix = "-";
    else if (signed_plus && qty_base > 0) prefix = "+";

    if (base_unit == "mg") {
        if (mag == 0) return "0 g";
        if (mag < 1000000ULL) {
            return prefix + format_scaled_three_decimals_plain(mag / 1000ULL, mag % 1000ULL, "g");
        }
        std::uint64_t whole = mag / 1000000ULL;
        std::uint64_t millis = ((mag % 1000000ULL) + 500ULL) / 1000ULL;
        if (millis >= 1000ULL) {
            ++whole;
            millis = 0;
        }
        return prefix + format_scaled_three_decimals_plain(whole, millis, "kg");
    }
    if (base_unit == "ml") {
        if (mag < 1000ULL) {
            return prefix + std::to_string(mag) + " ml";
        }
        return prefix + format_scaled_three_decimals_plain(mag / 1000ULL, mag % 1000ULL, "l");
    }
    return prefix + std::to_string(mag) + (mag == 1ULL ? " pieza" : " piezas");
}

std::string payment_method_plain(const std::string& method) {
    if (method == "EFECTIVO") return "Efectivo";
    if (method == "TARJETA") return "Tarjeta";
    if (method == "TRANSFERENCIA") return "Transferencia";
    return method;
}

std::string movement_type_plain(const std::string& movement_type) {
    if (movement_type == "PURCHASE") return "Compra";
    if (movement_type == "SALE") return "Venta";
    if (movement_type == "ADJUSTMENT") return "Ajuste";
    if (movement_type == "WASTE") return "Merma";
    if (movement_type == "TRANSFER") return "Traspaso";
    return movement_type;
}

std::string iso_date(const wxDateTime& dt) {
    return std::string(dt.FormatISODate().ToUTF8());
}

void ensure_btn_fits(wxButton* btn, int min_w_logical, int min_h_logical) {
    if (!btn) return;
    const int text_w = btn->GetTextExtent(btn->GetLabel()).GetWidth();
    const int needed_w = std::max(btn->FromDIP(min_w_logical), text_w + btn->FromDIP(26));
    const int min_h = btn->FromDIP(min_h_logical);
    btn->SetMinSize(wxSize(needed_w, min_h));
    btn->SetSize(wxSize(needed_w, min_h));
}

} // namespace

ReportsPanel::ReportsPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::IReportService> report_service,
    std::shared_ptr<contracts::IAccountService> account_service)
    : wxPanel(parent, wxID_ANY),
      report_service_(std::move(report_service)),
      account_service_(std::move(account_service)) {
    SetBackgroundColour(Theme::ColorBackground);
    build_ui();
    apply_preset(0); // Hoy por omisión
    refresh_report();
}

ReportsPanel::~ReportsPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &ReportsPanel::on_size, this);
    if (list_) {
        list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &ReportsPanel::on_row_selected, this);
    }
    DestroyChildren();
}

void ReportsPanel::build_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;
    const int field_h = touch ? 48 : 34;

    auto* root = new wxBoxSizer(wxVERTICAL);

    // Vista sin permiso reports.view
    pnl_no_permission_ = new CardPanel(this);
    static_cast<CardPanel*>(pnl_no_permission_)->set_card_background(Theme::ColorSurface);
    auto* np_s = new wxBoxSizer(wxVERTICAL);
    auto* np_title = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Reportes"));
    np_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    np_title->SetForegroundColour(Theme::ColorBrandDeep);
    np_s->Add(np_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* np_msg = new wxStaticText(
        pnl_no_permission_, wxID_ANY, wxT("Sin permiso para ver reportes")
    );
    np_msg->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    np_msg->SetForegroundColour(Theme::ColorError);
    np_s->Add(np_msg, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto* np_sub = new wxStaticText(
        pnl_no_permission_,
        wxID_ANY,
        wxT("Para consultar y exportar reportes pide el permiso al Dueño.")
    );
    np_sub->SetFont(Theme::get_font_body());
    np_sub->SetForegroundColour(Theme::ColorTextSecondary);
    np_s->Add(np_sub, 0, wxALL, FromDIP(20));
    pnl_no_permission_->SetSizer(np_s);
    pnl_no_permission_->Hide();
    root->Add(pnl_no_permission_, 1, wxEXPAND);

    // Vista principal autorizada
    main_card_ = new CardPanel(this);
    main_card_->set_card_background(Theme::ColorSurface);
    auto* card_s = new wxBoxSizer(wxVERTICAL);

    // Fila 1: Título + selector de reporte + acciones Consultar / Exportar a CSV
    auto* top_row = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    auto* lbl_title = new wxStaticText(main_card_, wxID_ANY, wxT("Reportes"));
    lbl_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    top_row->Add(lbl_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(8));

    wxArrayString report_choices;
    for (const char* lbl : kReportLabels) {
        report_choices.Add(wxString::FromUTF8(lbl));
    }
    if (touch) {
        cb_kind_ = new ui::TouchComboBox(
            main_card_, wxID_ANY, wxSize(FromDIP(250), FromDIP(field_h)), report_choices
        );
        cb_kind_->SetFont(Theme::get_font_body());
        cb_kind_->SetMinSize(wxSize(FromDIP(240), FromDIP(field_h)));
        cb_kind_->SetSelection(0);
        cb_kind_->Bind(wxEVT_COMBOBOX, &ReportsPanel::on_kind_changed, this);
        top_row->Add(cb_kind_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(8));
    } else {
        ch_kind_ = new wxChoice(main_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, report_choices);
        ch_kind_->SetFont(Theme::get_font_body());
        ch_kind_->SetMinSize(wxSize(FromDIP(230), FromDIP(field_h)));
        ch_kind_->SetSelection(0);
        ch_kind_->Bind(wxEVT_CHOICE, &ReportsPanel::on_kind_changed, this);
        top_row->Add(ch_kind_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(8));
    }

    btn_query_ = new wxButton(main_card_, wxID_ANY, wxT("Consultar"));
    btn_query_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_query_, btn_h);
    ensure_btn_fits(btn_query_, 110, btn_h);
    btn_query_->Bind(wxEVT_BUTTON, &ReportsPanel::on_query_clicked, this);
    top_row->Add(btn_query_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(8));

    btn_export_ = new wxButton(main_card_, wxID_ANY, wxT("Exportar a CSV"));
    btn_export_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_export_, btn_h);
    ensure_btn_fits(btn_export_, 145, btn_h);
    btn_export_->Bind(wxEVT_BUTTON, &ReportsPanel::on_export_clicked, this);
    top_row->Add(btn_export_, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, FromDIP(8));

    card_s->Add(top_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Fila 2: Presets y rango Desde / Hasta
    auto* range_row = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    auto make_preset = [&](const wxString& label, void (ReportsPanel::*handler)(wxCommandEvent&), int min_w) {
        auto* btn = new wxButton(main_card_, wxID_ANY, label);
        btn->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
        Theme::style_button_secondary(btn, btn_h);
        ensure_btn_fits(btn, min_w, btn_h);
        btn->Bind(wxEVT_BUTTON, handler, this);
        range_row->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));
        return btn;
    };
    btn_today_ = make_preset(wxT("Hoy"), &ReportsPanel::on_preset_today, 68);
    btn_yesterday_ = make_preset(wxT("Ayer"), &ReportsPanel::on_preset_yesterday, 72);
    btn_week_ = make_preset(wxT("Esta semana"), &ReportsPanel::on_preset_week, 112);
    btn_month_ = make_preset(wxT("Este mes"), &ReportsPanel::on_preset_month, 92);
    btn_last_month_ = make_preset(wxT("Mes pasado"), &ReportsPanel::on_preset_last_month, 108);

    auto* lbl_from = new wxStaticText(main_card_, wxID_ANY, wxT("Desde:"));
    lbl_from->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    range_row->Add(lbl_from, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));
    wxWindow* from_box = ui::make_text_field(
        main_card_, txt_from_, field_h, 110, Theme::get_font_body()
    );
    range_row->Add(from_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));

    auto* lbl_to = new wxStaticText(main_card_, wxID_ANY, wxT("Hasta:"));
    lbl_to->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    range_row->Add(lbl_to, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));
    wxWindow* to_box = ui::make_text_field(
        main_card_, txt_to_, field_h, 110, Theme::get_font_body()
    );
    range_row->Add(to_box, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, FromDIP(6));

    card_s->Add(range_row, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));

    // Banners de error / discrepancia / truncamiento / exportación
    auto make_banner = [&](wxPanel*& pnl, wxStaticText*& lbl, const wxColour& bg, const wxColour& fg) {
        pnl = new wxPanel(main_card_, wxID_ANY);
        pnl->SetBackgroundColour(bg);
        auto* bs = new wxBoxSizer(wxHORIZONTAL);
        lbl = new wxStaticText(pnl, wxID_ANY, wxEmptyString);
        lbl->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
        lbl->SetForegroundColour(fg);
        bs->Add(lbl, 1, wxALL, FromDIP(8));
        pnl->SetSizer(bs);
        pnl->Hide();
        card_s->Add(pnl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(6));
    };
    make_banner(pnl_error_, lbl_error_, Theme::ColorErrorSoft, Theme::ColorError);
    make_banner(pnl_mismatch_, lbl_mismatch_, Theme::ColorErrorSoft, Theme::ColorError);
    make_banner(pnl_truncated_, lbl_truncated_, wxColour(254, 243, 199), wxColour(180, 83, 9));
    make_banner(pnl_export_ok_, lbl_export_ok_, wxColour(220, 252, 231), Theme::ColorSuccess);

    // Estado vacío y tabla
    lbl_empty_ = new wxStaticText(
        main_card_, wxID_ANY, wxT("No hay datos en el rango seleccionado."),
        wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_empty_->SetFont(Theme::get_font_body());
    lbl_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_empty_->Hide();
    card_s->Add(lbl_empty_, 1, wxEXPAND | wxALL, FromDIP(16));

    list_ = new wxListView(
        main_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    list_->SetFont(Theme::get_font_body());
    list_->SetBackgroundColour(Theme::ColorSurface);
    list_->SetForegroundColour(Theme::ColorText);
    if (touch) {
        ui::apply_touch_row_height(list_);
    }
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, &ReportsPanel::on_row_selected, this);
    card_s->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    lbl_detail_ = new wxStaticText(main_card_, wxID_ANY, wxEmptyString);
    lbl_detail_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_detail_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_detail_->Hide();
    card_s->Add(lbl_detail_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(8));

    main_card_->SetSizer(card_s);
    root->Add(main_card_, 1, wxEXPAND);

    SetSizer(root);
    Bind(wxEVT_SIZE, &ReportsPanel::on_size, this);
}

void ReportsPanel::update_permissions_ui() {
    if (!can_view_) {
        main_card_->Hide();
        pnl_no_permission_->Show();
    } else {
        pnl_no_permission_->Hide();
        main_card_->Show();
    }
}

void ReportsPanel::apply_preset(int preset_id) {
    if (!txt_from_ || !txt_to_) return;
    const wxDateTime today = wxDateTime::Today();
    wxDateTime from = today;
    wxDateTime to = today;

    if (preset_id == 1) { // Ayer
        from = today - wxDateSpan::Days(1);
        to = from;
    } else if (preset_id == 2) { // Esta semana (lunes a hoy)
        const wxDateTime::WeekDay wd = today.GetWeekDay();
        const int since_mon = (wd == wxDateTime::Sun) ? 6 : (static_cast<int>(wd) - 1);
        from = today - wxDateSpan::Days(since_mon);
    } else if (preset_id == 3) { // Este mes
        from = wxDateTime(1, today.GetMonth(), today.GetYear());
    } else if (preset_id == 4) { // Mes pasado
        const wxDateTime first_this(1, today.GetMonth(), today.GetYear());
        to = first_this - wxDateSpan::Days(1);
        from = wxDateTime(1, to.GetMonth(), to.GetYear());
    }

    txt_from_->ChangeValue(wxString::FromUTF8(iso_date(from)));
    txt_to_->ChangeValue(wxString::FromUTF8(iso_date(to)));
}

contracts::ReportKind ReportsPanel::current_kind() const {
    int idx = 0;
    if (cb_kind_) idx = cb_kind_->GetSelection();
    else if (ch_kind_) idx = ch_kind_->GetSelection();
    switch (idx) {
        case 1: return contracts::ReportKind::SalesByProduct;
        case 2: return contracts::ReportKind::SalesByPaymentMethod;
        case 3: return contracts::ReportKind::SalesByCashier;
        case 4: return contracts::ReportKind::InventoryLosses;
        case 5: return contracts::ReportKind::InventoryMovements;
        case 6: return contracts::ReportKind::PurchasesSummary;
        case 0:
        default:
            return contracts::ReportKind::SalesByDay;
    }
}

std::string ReportsPanel::suggested_csv_filename(const std::string& from, const std::string& to) const {
    int idx = 0;
    if (cb_kind_) idx = cb_kind_->GetSelection();
    else if (ch_kind_) idx = ch_kind_->GetSelection();
    if (idx < 0 || idx > 6) idx = 0;
    return std::string(kReportSlugs[idx]) + "_" + from + "_" + to + ".csv";
}

void ReportsPanel::clear_banners() {
    pnl_error_->Hide();
    pnl_mismatch_->Hide();
    pnl_truncated_->Hide();
    pnl_export_ok_->Hide();
    lbl_detail_->Hide();
    row_summaries_.clear();
}

void ReportsPanel::show_error(const std::string& msg) {
    lbl_error_->SetLabel(wxString::FromUTF8(msg));
    lbl_error_->Wrap(std::max(FromDIP(260), main_card_->GetClientSize().GetWidth() - FromDIP(40)));
    pnl_error_->Show();
}

void ReportsPanel::show_mismatch(std::int64_t mismatch_cents) {
    if (mismatch_cents == 0) return;
    const std::string msg =
        "Aviso: las ventas no cuadran con los pagos registrados por " +
        abs_money_cents(mismatch_cents).to_string() + ".";
    lbl_mismatch_->SetLabel(wxString::FromUTF8(msg));
    lbl_mismatch_->Wrap(std::max(FromDIP(260), main_card_->GetClientSize().GetWidth() - FromDIP(40)));
    pnl_mismatch_->Show();
}

void ReportsPanel::show_truncated(std::size_t shown_count) {
    const std::string msg =
        "Se muestran los primeros " + std::to_string(shown_count) +
        " movimientos; exporta a CSV para verlos todos";
    lbl_truncated_->SetLabel(wxString::FromUTF8(msg));
    lbl_truncated_->Wrap(std::max(FromDIP(260), main_card_->GetClientSize().GetWidth() - FromDIP(40)));
    pnl_truncated_->Show();
}

void ReportsPanel::show_export_success(const contracts::ReportExportResultDto& res) {
    const std::string msg =
        "Reporte guardado: " + res.file_name + " (" + std::to_string(res.rows_written) + " filas)";
    lbl_export_ok_->SetLabel(wxString::FromUTF8(msg));
    lbl_export_ok_->Wrap(std::max(FromDIP(260), main_card_->GetClientSize().GetWidth() - FromDIP(40)));
    pnl_export_ok_->Show();
}

void ReportsPanel::setup_columns(const std::vector<std::string>& headers, std::vector<int> weights) {
    list_->ClearAll();
    col_weights_ = std::move(weights);
    for (std::size_t i = 0; i < headers.size(); ++i) {
        list_->InsertColumn(static_cast<long>(i), wxString::FromUTF8(headers[i]));
    }
}

void ReportsPanel::append_row(const std::vector<std::string>& cells, bool is_total_row) {
    if (cells.empty()) return;
    const long row_idx = list_->GetItemCount();
    const long idx = list_->InsertItem(row_idx, wxString::FromUTF8(cells[0]));
    std::string summary = cells[0];
    for (std::size_t c = 1; c < cells.size(); ++c) {
        list_->SetItem(idx, static_cast<int>(c), wxString::FromUTF8(cells[c]));
        if (!cells[c].empty()) {
            summary += " · " + cells[c];
        }
    }
    if (is_total_row) {
        wxFont f = list_->GetFont();
        f.SetWeight(wxFONTWEIGHT_BOLD);
        list_->SetItemFont(idx, f);
        list_->SetItemBackgroundColour(idx, Theme::ColorSurfaceSubtle);
        list_->SetItemTextColour(idx, Theme::ColorBrandDeep);
    } else {
        list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
        list_->SetItemTextColour(idx, Theme::ColorText);
    }
    row_summaries_.push_back(std::move(summary));
}

void ReportsPanel::fit_columns() {
    if (destroying_ || sizing_columns_ || !list_ || !list_->IsShown() || list_->GetColumnCount() <= 0) {
        return;
    }
    if (list_->GetClientSize().GetWidth() <= 0) return;
    sizing_columns_ = true;

    wxClientDC dc(list_);
    dc.SetFont(list_->GetFont());
    const int ncols = list_->GetColumnCount();
    std::size_t flex = 0;
    if (!col_weights_.empty()) {
        auto it = std::max_element(col_weights_.begin(), col_weights_.end());
        flex = static_cast<std::size_t>(std::distance(col_weights_.begin(), it));
        if (flex >= static_cast<std::size_t>(ncols)) flex = 0;
    }

    std::vector<ui::ColumnSpec> specs;
    specs.reserve(static_cast<std::size_t>(ncols));
    for (int c = 0; c < ncols; ++c) {
        wxListItem col_info;
        col_info.SetMask(wxLIST_MASK_TEXT);
        wxString hdr;
        if (list_->GetColumn(c, col_info)) {
            hdr = col_info.GetText();
        }
        int max_px = dc.GetTextExtent(hdr).GetWidth();
        const int rows = std::min(list_->GetItemCount(), 40);
        for (int r = 0; r < rows; ++r) {
            max_px = std::max(max_px, dc.GetTextExtent(list_->GetItemText(r, c)).GetWidth());
        }
        const int floor_px = 0;
        specs.push_back({hdr, max_px, floor_px, 16, 0});
    }
    ui::distribute_list_columns(list_, specs, flex);
    sizing_columns_ = false;
}

void ReportsPanel::refresh_report() {
    if (destroying_ || IsBeingDeleted()) return;
    can_view_ = account_service_ && account_service_->has_permission("reports.view");
    update_permissions_ui();
    if (!can_view_) {
        Layout();
        return;
    }

    clear_banners();
    list_->ClearAll();

    if (!report_service_) {
        list_->Hide();
        lbl_empty_->SetLabel(wxT("Servicio de reportes no disponible."));
        lbl_empty_->Show();
        main_card_->Layout();
        Layout();
        return;
    }

    const std::string from = txt_from_
        ? std::string(txt_from_->GetValue().Trim().Trim(false).ToUTF8())
        : "";
    const std::string to = txt_to_
        ? std::string(txt_to_->GetValue().Trim().Trim(false).ToUTF8())
        : "";

    bool empty_data = true;
    const auto kind = current_kind();

    auto finish_empty_or_table = [&](bool has_rows) {
        empty_data = !has_rows;
        if (empty_data) {
            list_->Hide();
            lbl_empty_->SetLabel(wxT("No hay datos en el rango seleccionado."));
            lbl_empty_->Show();
        } else {
            lbl_empty_->Hide();
            list_->Show();
        }
    };

    switch (kind) {
        case contracts::ReportKind::SalesByDay: {
            auto res = report_service_->sales_by_day(from, to);
            if (!res.is_success()) {
                list_->Hide();
                lbl_empty_->Hide();
                show_error(res.error_message());
                break;
            }
            const auto& dto = res.value();
            show_mismatch(dto.totals.payments_mismatch_cents);
            if (!dto.rows.empty()) {
                setup_columns(
                    {"Fecha", "Ventas", "Bruto", "Descuento", "Neto", "Efectivo", "Tarjeta", "Transferencia"},
                    {2, 1, 2, 2, 2, 2, 2, 2}
                );
                for (const auto& r : dto.rows) {
                    append_row({
                        r.date,
                        std::to_string(r.sales_count),
                        r.gross.to_string(),
                        r.discount.to_string(),
                        r.net.to_string(),
                        r.cash.to_string(),
                        r.card.to_string(),
                        r.transfer.to_string()
                    });
                }
                append_row({
                    "Total",
                    std::to_string(dto.totals.sales_count),
                    dto.totals.gross.to_string(),
                    dto.totals.discount.to_string(),
                    dto.totals.net.to_string(),
                    dto.totals.cash.to_string(),
                    dto.totals.card.to_string(),
                    dto.totals.transfer.to_string()
                }, true);
            }
            finish_empty_or_table(!dto.rows.empty());
            break;
        }
        case contracts::ReportKind::SalesByProduct: {
            auto res = report_service_->sales_by_product(from, to);
            if (!res.is_success()) {
                list_->Hide();
                lbl_empty_->Hide();
                show_error(res.error_message());
                break;
            }
            const auto& dto = res.value();
            show_mismatch(dto.totals.payments_mismatch_cents);
            if (!dto.rows.empty()) {
                setup_columns({"Producto", "Piezas", "Bruto", "Descuento", "Neto"}, {4, 1, 2, 2, 2});
                for (const auto& r : dto.rows) {
                    append_row({
                        r.product_name,
                        std::to_string(r.pieces),
                        r.gross.to_string(),
                        r.discount.to_string(),
                        r.net.to_string()
                    });
                }
                append_row({
                    "Total",
                    std::to_string(dto.totals.pieces),
                    dto.totals.gross.to_string(),
                    dto.totals.discount.to_string(),
                    dto.totals.net.to_string()
                }, true);
            }
            finish_empty_or_table(!dto.rows.empty());
            break;
        }
        case contracts::ReportKind::SalesByPaymentMethod: {
            auto res = report_service_->sales_by_payment_method(from, to);
            if (!res.is_success()) {
                list_->Hide();
                lbl_empty_->Hide();
                show_error(res.error_message());
                break;
            }
            const auto& dto = res.value();
            show_mismatch(dto.totals.payments_mismatch_cents);
            if (!dto.rows.empty()) {
                setup_columns({"Método", "Pagos", "Importe"}, {3, 2, 3});
                for (const auto& r : dto.rows) {
                    append_row({
                        payment_method_plain(r.method),
                        std::to_string(r.payments_count),
                        r.amount.to_string()
                    });
                }
                append_row({
                    "Total",
                    std::to_string(dto.totals.sales_count) + " ventas",
                    dto.totals.payments_total.to_string()
                }, true);
            }
            finish_empty_or_table(!dto.rows.empty());
            break;
        }
        case contracts::ReportKind::SalesByCashier: {
            auto res = report_service_->sales_by_cashier(from, to);
            if (!res.is_success()) {
                list_->Hide();
                lbl_empty_->Hide();
                show_error(res.error_message());
                break;
            }
            const auto& dto = res.value();
            show_mismatch(dto.totals.payments_mismatch_cents);
            if (!dto.rows.empty()) {
                setup_columns({"Cajero", "Ventas", "Bruto", "Descuento", "Neto"}, {4, 1, 2, 2, 2});
                for (const auto& r : dto.rows) {
                    append_row({
                        r.cashier_name,
                        std::to_string(r.sales_count),
                        r.gross.to_string(),
                        r.discount.to_string(),
                        r.net.to_string()
                    });
                }
                append_row({
                    "Total",
                    std::to_string(dto.totals.sales_count),
                    dto.totals.gross.to_string(),
                    dto.totals.discount.to_string(),
                    dto.totals.net.to_string()
                }, true);
            }
            finish_empty_or_table(!dto.rows.empty());
            break;
        }
        case contracts::ReportKind::InventoryLosses: {
            auto res = report_service_->inventory_losses(from, to);
            if (!res.is_success()) {
                list_->Hide();
                lbl_empty_->Hide();
                show_error(res.error_message());
                break;
            }
            const auto& dto = res.value();
            if (!dto.rows.empty()) {
                setup_columns(
                    {"Ingrediente", "Merma", "Costo merma", "Ajuste +", "Costo +", "Ajuste -", "Costo -"},
                    {3, 2, 2, 2, 2, 2, 2}
                );
                for (const auto& r : dto.rows) {
                    const std::string unk = "Sin costo conocido";
                    append_row({
                        r.ingredient_name,
                        format_base_qty_readable(r.waste_quantity_base, r.base_unit),
                        r.has_known_cost ? r.waste_estimated_cost.to_string() : unk,
                        format_base_qty_readable(r.adjustment_in_quantity_base, r.base_unit),
                        r.has_known_cost ? r.adjustment_in_estimated_cost.to_string() : unk,
                        format_base_qty_readable(r.adjustment_out_quantity_base, r.base_unit),
                        r.has_known_cost ? r.adjustment_out_estimated_cost.to_string() : unk
                    });
                }
                append_row({
                    "Total",
                    "",
                    dto.total_waste_estimated_cost.to_string(),
                    "",
                    dto.total_adjustment_in_estimated_cost.to_string(),
                    "",
                    dto.total_adjustment_out_estimated_cost.to_string()
                }, true);
            }
            finish_empty_or_table(!dto.rows.empty());
            break;
        }
        case contracts::ReportKind::InventoryMovements: {
            auto res = report_service_->inventory_movements(from, to, 1000);
            if (!res.is_success()) {
                list_->Hide();
                lbl_empty_->Hide();
                show_error(res.error_message());
                break;
            }
            const auto& dto = res.value();
            if (dto.truncated) {
                show_truncated(dto.rows.size());
            }
            if (!dto.rows.empty()) {
                setup_columns(
                    {"Fecha", "Ingrediente", "Tipo", "Cantidad", "Motivo", "Usuario"},
                    {2, 3, 1, 2, 3, 2}
                );
                for (const auto& r : dto.rows) {
                    append_row({
                        r.created_at,
                        r.ingredient_name,
                        movement_type_plain(r.movement_type),
                        format_base_qty_readable(r.quantity_base, r.base_unit, true),
                        r.reason.empty() ? "—" : r.reason,
                        r.actor_name.empty() ? "—" : r.actor_name
                    });
                }
            }
            finish_empty_or_table(!dto.rows.empty());
            break;
        }
        case contracts::ReportKind::PurchasesSummary: {
            auto res = report_service_->purchases_summary(from, to);
            if (!res.is_success()) {
                list_->Hide();
                lbl_empty_->Hide();
                show_error(res.error_message());
                break;
            }
            const auto& dto = res.value();
            if (!dto.rows.empty()) {
                setup_columns({"Ingrediente", "Compras", "Cantidad", "Costo total"}, {4, 1, 3, 2});
                for (const auto& r : dto.rows) {
                    append_row({
                        r.ingredient_name,
                        std::to_string(r.purchases_count),
                        format_base_qty_readable(r.base_units_purchased, r.base_unit),
                        r.total_cost.to_string()
                    });
                }
                append_row({"Total", "", "", dto.total_cost.to_string()}, true);
            }
            finish_empty_or_table(!dto.rows.empty());
            break;
        }
    }

    main_card_->Layout();
    Layout();
    fit_columns();
}

void ReportsPanel::on_kind_changed(wxCommandEvent&) { refresh_report(); }

void ReportsPanel::on_preset_today(wxCommandEvent&) {
    apply_preset(0);
    refresh_report();
}

void ReportsPanel::on_preset_yesterday(wxCommandEvent&) {
    apply_preset(1);
    refresh_report();
}

void ReportsPanel::on_preset_week(wxCommandEvent&) {
    apply_preset(2);
    refresh_report();
}

void ReportsPanel::on_preset_month(wxCommandEvent&) {
    apply_preset(3);
    refresh_report();
}

void ReportsPanel::on_preset_last_month(wxCommandEvent&) {
    apply_preset(4);
    refresh_report();
}

void ReportsPanel::on_query_clicked(wxCommandEvent&) {
    refresh_report();
}

void ReportsPanel::on_export_clicked(wxCommandEvent&) {
    if (destroying_ || !report_service_) return;
    if (!can_view_) return;

    const std::string from = txt_from_
        ? std::string(txt_from_->GetValue().Trim().Trim(false).ToUTF8())
        : "";
    const std::string to = txt_to_
        ? std::string(txt_to_->GetValue().Trim().Trim(false).ToUTF8())
        : "";
    const std::string suggested = suggested_csv_filename(from, to);

    // No usar wxFD_OVERWRITE_PROMPT: el contrato detecta Conflict y aquí preguntamos Reemplazar / Cancelar.
    wxFileDialog dlg(
        this,
        wxT("Exportar reporte a CSV"),
        wxEmptyString,
        wxString::FromUTF8(suggested),
        wxT("Archivo CSV (*.csv)|*.csv"),
        wxFD_SAVE
    );
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }

    const std::string path_utf8 = std::string(dlg.GetPath().ToUTF8());
    const auto kind = current_kind();

    pnl_error_->Hide();
    pnl_export_ok_->Hide();

    auto res = report_service_->export_report_csv(kind, from, to, path_utf8, false);
    if (!res.is_success() && res.error_code() == contracts::ErrorCode::Conflict) {
        wxMessageDialog ask(
            this,
            wxString::FromUTF8("El archivo ya existe:\n" + path_utf8 + "\n\n¿Deseas reemplazarlo?"),
            wxT("Archivo existente"),
            wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION
        );
        ask.SetYesNoLabels(wxT("Reemplazar"), wxT("Cancelar"));
        if (ask.ShowModal() != wxID_YES) {
            return;
        }
        res = report_service_->export_report_csv(kind, from, to, path_utf8, true);
    }

    if (!res.is_success()) {
        show_error(res.error_message());
        main_card_->Layout();
        Layout();
        return;
    }

    show_export_success(res.value());
    main_card_->Layout();
    Layout();
}

void ReportsPanel::on_row_selected(wxListEvent& evt) {
    if (destroying_) return;
    const long row = evt.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= row_summaries_.size()) {
        lbl_detail_->Hide();
    } else {
        lbl_detail_->SetLabel(wxString::FromUTF8(row_summaries_[static_cast<std::size_t>(row)]));
        lbl_detail_->Wrap(std::max(FromDIP(260), main_card_->GetClientSize().GetWidth() - FromDIP(32)));
        lbl_detail_->Show();
    }
    main_card_->Layout();
}

void ReportsPanel::on_size(wxSizeEvent& evt) {
    evt.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted() || sizing_columns_) return;
        fit_columns();
    });
}

} // namespace app::frontend
