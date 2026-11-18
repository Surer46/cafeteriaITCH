#include "src/frontend/inventory_stock_panel.hpp"

#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <utility>

namespace app::frontend {

namespace {

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

std::string format_stock_ingredient_readable(std::int64_t amount_base, const std::string& unit_dimension) {
    const bool negative = amount_base < 0;
    const std::uint64_t mag = negative
        ? static_cast<std::uint64_t>(-(amount_base + 1)) + 1ULL
        : static_cast<std::uint64_t>(amount_base);
    const std::string prefix = negative ? "-" : "";

    if (unit_dimension == "MASS") {
        if (mag == 0) {
            return "0 g";
        }
        if (mag < 1000000ULL) {
            const std::uint64_t whole = mag / 1000ULL;
            const std::uint64_t millis = mag % 1000ULL;
            return prefix + format_scaled_three_decimals_plain(whole, millis, "g");
        }
        std::uint64_t whole = mag / 1000000ULL;
        const std::uint64_t rem = mag % 1000000ULL;
        std::uint64_t millis = (rem + 500ULL) / 1000ULL;
        if (millis >= 1000ULL) {
            ++whole;
            millis = 0;
        }
        return prefix + format_scaled_three_decimals_plain(whole, millis, "kg");
    }

    if (unit_dimension == "VOLUME") {
        if (mag < 1000ULL) {
            return prefix + std::to_string(mag) + " ml";
        }
        const std::uint64_t whole = mag / 1000ULL;
        const std::uint64_t millis = mag % 1000ULL;
        return prefix + format_scaled_three_decimals_plain(whole, millis, "l");
    }

    return prefix + std::to_string(mag) + (mag == 1ULL ? " pieza" : " piezas");
}

std::string format_portions_readable(std::int64_t units) {
    return std::to_string(units) + (units == 1 ? " porción" : " porciones");
}

std::string format_pieces_readable(std::int64_t units) {
    return std::to_string(units) + (units == 1 ? " pieza" : " piezas");
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

InventoryStockPanel::InventoryStockPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::IInventoryService> inventory_service,
    std::shared_ptr<contracts::ICatalogService> catalog_service,
    std::shared_ptr<contracts::IAccountService> account_service)
    : wxPanel(parent, wxID_ANY),
      inventory_service_(std::move(inventory_service)),
      catalog_service_(std::move(catalog_service)),
      account_service_(std::move(account_service)) {
    SetBackgroundColour(Theme::ColorBackground);
    build_ui();
    refresh_data();
}

InventoryStockPanel::~InventoryStockPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &InventoryStockPanel::on_size, this);
    if (list_) {
        list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryStockPanel::on_row_selected, this);
        list_->Unbind(wxEVT_LIST_ITEM_DESELECTED, &InventoryStockPanel::on_row_deselected, this);
    }
    DestroyChildren();
}

void InventoryStockPanel::build_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h_logical = touch ? 48 : 34;
    const int field_h_logical = touch ? 48 : 34;

    auto* root = new wxBoxSizer(wxVERTICAL);

    // Vista sin permiso inventory.view
    pnl_no_permission_ = new CardPanel(this);
    static_cast<CardPanel*>(pnl_no_permission_)->set_card_background(Theme::ColorSurface);
    auto* np_sizer = new wxBoxSizer(wxVERTICAL);
    auto* np_title = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Existencias"));
    np_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    np_title->SetForegroundColour(Theme::ColorBrandDeep);
    np_sizer->Add(np_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* np_msg = new wxStaticText(
        pnl_no_permission_, wxID_ANY, wxT("Sin permiso para ver existencias")
    );
    np_msg->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    np_msg->SetForegroundColour(Theme::ColorError);
    np_sizer->Add(np_msg, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto* np_sub = new wxStaticText(
        pnl_no_permission_,
        wxID_ANY,
        wxT("Para consultar las existencias actuales pide el permiso al Dueño.")
    );
    np_sub->SetFont(Theme::get_font_body());
    np_sub->SetForegroundColour(Theme::ColorTextSecondary);
    np_sizer->Add(np_sub, 0, wxALL, FromDIP(20));
    pnl_no_permission_->SetSizer(np_sizer);
    pnl_no_permission_->Hide();
    root->Add(pnl_no_permission_, 1, wxEXPAND);

    // Tarjeta principal
    main_card_ = new CardPanel(this);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* top_row = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_title = new wxStaticText(main_card_, wxID_ANY, wxT("Existencias"));
    lbl_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    top_row->Add(lbl_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    lbl_count_ = new wxStaticText(main_card_, wxID_ANY, wxEmptyString);
    lbl_count_->SetFont(Theme::get_font_small());
    lbl_count_->SetForegroundColour(Theme::ColorTextSecondary);
    top_row->Add(lbl_count_, 0, wxALIGN_CENTER_VERTICAL);

    top_row->AddStretchSpacer(1);

    btn_refresh_ = new wxButton(main_card_, wxID_ANY, wxT("Actualizar"));
    btn_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_refresh_, btn_h_logical);
    ensure_btn_fits(btn_refresh_, 105, btn_h_logical);
    btn_refresh_->Bind(wxEVT_BUTTON, &InventoryStockPanel::on_refresh, this);
    top_row->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);
    card_sizer->Add(top_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Fila de búsqueda y filtros
    auto* filter_row = new wxBoxSizer(wxHORIZONTAL);
    wxWindow* search_box = ui::make_text_field(
        main_card_, txt_search_, field_h_logical, 140, Theme::get_font_body()
    );
    txt_search_->SetHint(wxT("Buscar por nombre..."));
    txt_search_->Bind(wxEVT_TEXT, &InventoryStockPanel::on_search_changed, this);
    filter_row->Add(search_box, 2, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    wxArrayString type_choices;
    type_choices.Add(wxT("Todos los tipos"));
    type_choices.Add(wxT("Ingrediente"));
    type_choices.Add(wxT("Directo"));
    type_choices.Add(wxT("Preparado"));
    type_choices.Add(wxT("Combo"));

    if (touch) {
        cb_type_ = new ui::TouchComboBox(
            main_card_, wxID_ANY, wxSize(FromDIP(170), FromDIP(field_h_logical)), type_choices
        );
        cb_type_->SetFont(Theme::get_font_body());
        cb_type_->SetMinSize(wxSize(FromDIP(165), FromDIP(field_h_logical)));
        cb_type_->SetSelection(0);
        cb_type_->Bind(wxEVT_COMBOBOX, &InventoryStockPanel::on_type_filter_changed, this);
        filter_row->Add(cb_type_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    } else {
        ch_type_ = new wxChoice(main_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, type_choices);
        ch_type_->SetFont(Theme::get_font_body());
        ch_type_->SetMinSize(wxSize(FromDIP(155), FromDIP(field_h_logical)));
        ch_type_->SetSelection(0);
        ch_type_->Bind(wxEVT_CHOICE, &InventoryStockPanel::on_type_filter_changed, this);
        filter_row->Add(ch_type_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    }

    btn_low_only_ = new wxButton(main_card_, wxID_ANY, wxT("Solo con poco o agotado"));
    btn_low_only_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_low_only_, btn_h_logical);
    ensure_btn_fits(btn_low_only_, 180, btn_h_logical);
    btn_low_only_->Bind(wxEVT_BUTTON, &InventoryStockPanel::on_toggle_low_only, this);
    filter_row->Add(btn_low_only_, 0, wxALIGN_CENTER_VERTICAL);
    card_sizer->Add(filter_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    lbl_empty_ = new wxStaticText(
        main_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_empty_->SetFont(Theme::get_font_body());
    lbl_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_empty_->Hide();
    card_sizer->Add(lbl_empty_, 1, wxEXPAND | wxALL, FromDIP(16));

    list_ = new wxListView(
        main_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    list_->SetFont(Theme::get_font_body());
    list_->SetBackgroundColour(Theme::ColorSurface);
    list_->SetForegroundColour(Theme::ColorText);
    list_->InsertColumn(0, wxT("Nombre"));
    list_->InsertColumn(1, wxT("Tipo"));
    list_->InsertColumn(2, wxT("Existencia"));
    list_->InsertColumn(3, wxT("Estado"));
    list_->InsertColumn(4, wxT("Último movimiento"));
    if (touch) {
        ui::apply_touch_row_height(list_);
    }
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryStockPanel::on_row_selected, this);
    list_->Bind(wxEVT_LIST_ITEM_DESELECTED, &InventoryStockPanel::on_row_deselected, this);
    card_sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    lbl_detail_ = new wxStaticText(main_card_, wxID_ANY, wxEmptyString);
    lbl_detail_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_detail_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_detail_->Hide();
    card_sizer->Add(lbl_detail_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(8));

    main_card_->SetSizer(card_sizer);
    root->Add(main_card_, 1, wxEXPAND);

    SetSizer(root);
    Bind(wxEVT_SIZE, &InventoryStockPanel::on_size, this);
}

void InventoryStockPanel::refresh_data() {
    if (destroying_ || IsBeingDeleted()) return;
    can_view_ = account_service_ && account_service_->has_permission("inventory.view");
    update_permissions_ui();
    if (!can_view_) {
        Layout();
        return;
    }

    all_rows_.clear();
    load_failed_ = false;
    last_error_.clear();

    if (!inventory_service_ || !catalog_service_) {
        load_failed_ = true;
        last_error_ = "Servicios de inventario o catálogo no disponibles.";
        apply_filters();
        return;
    }

    auto ing_res = inventory_service_->list_ingredients();
    if (!ing_res.is_success()) {
        load_failed_ = true;
        last_error_ = ing_res.error_message();
        apply_filters();
        return;
    }
    for (const auto& ing : ing_res.value()) {
        StockEntryRow row;
        row.name = ing.name;
        row.type_label = "Ingrediente";
        row.stock_label = format_stock_ingredient_readable(ing.current_stock_base, ing.unit_dimension);
        if (ing.stock_status == "OUT") {
            row.status_label = "Agotado";
            row.is_out = true;
            row.is_low_or_out = true;
        } else if (ing.stock_status == "LOW") {
            row.status_label = "Poco";
            row.is_low = true;
            row.is_low_or_out = true;
        } else {
            row.status_label = "Suficiente";
        }
        row.last_movement = ing.last_movement_at.empty() ? "Sin movimientos" : ing.last_movement_at;
        all_rows_.push_back(std::move(row));
    }

    auto prod_res = catalog_service_->search_products("", std::nullopt, 0, 500, false);
    if (!prod_res.is_success()) {
        load_failed_ = true;
        last_error_ = prod_res.error_message();
        apply_filters();
        return;
    }
    for (const auto& p : prod_res.value()) {
        StockEntryRow row;
        row.name = p.name;
        row.last_movement = "—";
        if (p.is_combo) {
            row.type_label = "Combo";
            row.stock_label = format_portions_readable(p.stock_units);
            row.is_out = (p.stock_units <= 0);
            row.is_low_or_out = row.is_out;
            row.status_label = row.is_out ? "Agotado" : "Suficiente";
        } else if (p.is_prepared) {
            row.type_label = "Preparado";
            if (!p.stock_tracked) {
                row.stock_label = "Sin control de existencia";
                row.status_label = "Sin control";
            } else {
                row.stock_label = format_portions_readable(p.stock_units);
                row.is_out = (p.stock_units <= 0);
                row.is_low_or_out = row.is_out;
                row.status_label = row.is_out ? "Agotado" : "Suficiente";
            }
        } else {
            row.type_label = "Directo";
            if (!p.stock_tracked) {
                row.stock_label = "Sin control de existencia";
                row.status_label = "Sin control";
            } else {
                row.stock_label = format_pieces_readable(p.stock_units);
                row.is_out = (p.stock_units <= 0);
                row.is_low_or_out = row.is_out;
                row.status_label = row.is_out ? "Agotado" : "Suficiente";
            }
        }
        all_rows_.push_back(std::move(row));
    }

    std::sort(all_rows_.begin(), all_rows_.end(), [](const StockEntryRow& a, const StockEntryRow& b) {
        const wxString wa = wxString::FromUTF8(a.name).Lower();
        const wxString wb = wxString::FromUTF8(b.name).Lower();
        if (wa != wb) return wa < wb;
        return a.type_label < b.type_label;
    });

    apply_filters();
}

void InventoryStockPanel::update_permissions_ui() {
    if (!can_view_) {
        main_card_->Hide();
        pnl_no_permission_->Show();
        return;
    }
    pnl_no_permission_->Hide();
    main_card_->Show();
}

void InventoryStockPanel::apply_filters() {
    if (destroying_ || !list_) return;

    filtered_rows_.clear();
    list_->DeleteAllItems();

    if (!load_failed_) {
        const wxString query = txt_search_
            ? txt_search_->GetValue().Trim().Trim(false).Lower()
            : wxString();
        const std::string type_filter = selected_type_filter();

        for (const auto& row : all_rows_) {
            if (!type_filter.empty() && row.type_label != type_filter) {
                continue;
            }
            if (only_low_or_out_ && !row.is_low_or_out) {
                continue;
            }
            if (!query.empty()) {
                const wxString name_low = wxString::FromUTF8(row.name).Lower();
                if (!name_low.Contains(query)) {
                    continue;
                }
            }
            const long idx = list_->InsertItem(
                static_cast<long>(filtered_rows_.size()), wxString::FromUTF8(row.name)
            );
            list_->SetItem(idx, 1, wxString::FromUTF8(row.type_label));
            list_->SetItem(idx, 2, wxString::FromUTF8(row.stock_label));
            list_->SetItem(idx, 3, wxString::FromUTF8(row.status_label));
            list_->SetItem(idx, 4, wxString::FromUTF8(row.last_movement));

            if (row.is_out) {
                list_->SetItemTextColour(idx, Theme::ColorError);
                list_->SetItemBackgroundColour(idx, Theme::ColorErrorSoft);
            } else if (row.is_low) {
                list_->SetItemTextColour(idx, wxColour(180, 83, 9));
                list_->SetItemBackgroundColour(idx, wxColour(254, 243, 199));
            } else {
                list_->SetItemTextColour(idx, Theme::ColorText);
                list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
            }
            filtered_rows_.push_back(row);
        }
    }

    if (lbl_count_) {
        lbl_count_->SetLabel(wxString::Format(wxT("(%zu elementos)"), filtered_rows_.size()));
    }

    last_detail_.clear();
    update_detail_label();
    update_empty_state();
    update_list_column_widths();
    main_card_->Layout();
    Layout();
}

void InventoryStockPanel::update_empty_state() {
    if (!main_card_ || !main_card_->IsShown()) return;
    if (load_failed_) {
        list_->Hide();
        lbl_empty_->SetLabel(wxString::FromUTF8(last_error_));
        lbl_empty_->SetForegroundColour(Theme::ColorError);
        lbl_empty_->Show();
    } else if (filtered_rows_.empty()) {
        list_->Hide();
        lbl_empty_->SetLabel(wxT("No hay existencias que coincidan con el filtro."));
        lbl_empty_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_empty_->Show();
    } else {
        lbl_empty_->Hide();
        list_->Show();
    }
    if (lbl_empty_->IsShown()) {
        lbl_empty_->Wrap(std::max(FromDIP(200), main_card_->GetClientSize().GetWidth() - FromDIP(32)));
    }
}

void InventoryStockPanel::update_list_column_widths() {
    if (destroying_ || sizing_columns_ || !list_ || !list_->IsShown() || list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(list_);
    dc.SetFont(list_->GetFont());

    int max_name = dc.GetTextExtent(wxT("Nombre")).GetWidth();
    int max_type = dc.GetTextExtent(wxT("Ingrediente")).GetWidth();
    int max_stock = dc.GetTextExtent(wxT("Existencia")).GetWidth();
    int max_status = dc.GetTextExtent(wxT("Suficiente")).GetWidth();
    int max_mov = dc.GetTextExtent(wxT("Último movimiento")).GetWidth();

    for (const auto& r : filtered_rows_) {
        max_name = std::max(max_name, dc.GetTextExtent(wxString::FromUTF8(r.name)).GetWidth());
        max_type = std::max(max_type, dc.GetTextExtent(wxString::FromUTF8(r.type_label)).GetWidth());
        max_stock = std::max(max_stock, dc.GetTextExtent(wxString::FromUTF8(r.stock_label)).GetWidth());
        max_status = std::max(max_status, dc.GetTextExtent(wxString::FromUTF8(r.status_label)).GetWidth());
        max_mov = std::max(max_mov, dc.GetTextExtent(wxString::FromUTF8(r.last_movement)).GetWidth());
    }

    const int pad = 16;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Nombre"), max_name, 0, pad, 0});
    specs.push_back({wxT("Tipo"), max_type, max_type, pad, 0});
    specs.push_back({wxT("Existencia"), max_stock, max_stock, pad, 0});
    specs.push_back({wxT("Estado"), max_status, max_status, pad, 0});
    specs.push_back({wxT("Último movimiento"), max_mov, max_mov, pad, 0});
    ui::distribute_list_columns(list_, specs, 0);
    sizing_columns_ = false;
}

void InventoryStockPanel::update_detail_label() {
    if (!lbl_detail_) return;
    if (last_detail_.empty()) {
        lbl_detail_->Hide();
        return;
    }
    lbl_detail_->SetLabel(wxString::FromUTF8(last_detail_));
    const int wrap_w = std::max(FromDIP(200), main_card_->GetClientSize().GetWidth() - FromDIP(24));
    lbl_detail_->Wrap(wrap_w);
    lbl_detail_->Show();
}

void InventoryStockPanel::on_refresh(wxCommandEvent&) {
    if (destroying_) return;
    refresh_data();
}

void InventoryStockPanel::on_search_changed(wxCommandEvent&) {
    if (destroying_) return;
    apply_filters();
}

void InventoryStockPanel::on_type_filter_changed(wxCommandEvent&) {
    if (destroying_) return;
    apply_filters();
}

void InventoryStockPanel::on_toggle_low_only(wxCommandEvent&) {
    if (destroying_) return;
    only_low_or_out_ = !only_low_or_out_;
    const int btn_h_logical = Theme::is_touch() ? 48 : 34;
    if (only_low_or_out_) {
        Theme::style_button_primary(btn_low_only_, btn_h_logical);
    } else {
        Theme::style_button_secondary(btn_low_only_, btn_h_logical);
    }
    ensure_btn_fits(btn_low_only_, 180, btn_h_logical);
    apply_filters();
}

void InventoryStockPanel::on_row_selected(wxListEvent& event) {
    if (destroying_) return;
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= filtered_rows_.size()) {
        last_detail_.clear();
    } else {
        const auto& r = filtered_rows_[static_cast<std::size_t>(row)];
        last_detail_ = r.name + " [" + r.type_label + "] — " + r.stock_label + " · Estado: " + r.status_label;
        if (!r.last_movement.empty() && r.last_movement != "—") {
            last_detail_ += " · Último movimiento: " + r.last_movement;
        }
    }
    update_detail_label();
    main_card_->Layout();
}

void InventoryStockPanel::on_row_deselected(wxListEvent&) {
    if (destroying_) return;
    last_detail_.clear();
    update_detail_label();
    main_card_->Layout();
}

void InventoryStockPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted()) return;
        update_empty_state();
        update_list_column_widths();
        update_detail_label();
        Layout();
    });
}

std::string InventoryStockPanel::selected_type_filter() const {
    const int idx = cb_type_ ? cb_type_->GetSelection() : (ch_type_ ? ch_type_->GetSelection() : 0);
    switch (idx) {
        case 1: return "Ingrediente";
        case 2: return "Directo";
        case 3: return "Preparado";
        case 4: return "Combo";
        default: return "";
    }
}

} // namespace app::frontend
