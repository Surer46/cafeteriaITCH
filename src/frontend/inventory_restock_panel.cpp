#include "src/frontend/inventory_restock_panel.hpp"

#include "app/contracts/money.hpp"
#include "src/frontend/inventory_ingredients_panel.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace app::frontend {

namespace {

std::string trim_ascii(const std::string& input) {
    std::size_t start = 0;
    while (start < input.size() &&
           (input[start] == ' ' || input[start] == '\t' || input[start] == '\r' || input[start] == '\n')) {
        ++start;
    }
    std::size_t end = input.size();
    while (end > start &&
           (input[end - 1] == ' ' || input[end - 1] == '\t' || input[end - 1] == '\r' || input[end - 1] == '\n')) {
        --end;
    }
    return input.substr(start, end - start);
}

std::string to_lower_ascii(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

void set_selector_items(wxChoice* ch, ui::TouchComboBox* cb, const std::vector<std::string>& labels, int select_idx) {
    if (ch) {
        ch->Clear();
        for (const auto& lbl : labels) {
            ch->Append(wxString::FromUTF8(lbl));
        }
        if (!labels.empty() && select_idx >= 0 && select_idx < static_cast<int>(labels.size())) {
            ch->SetSelection(select_idx);
        }
    }
    if (cb) {
        cb->Clear();
        for (const auto& lbl : labels) {
            cb->Append(wxString::FromUTF8(lbl));
        }
        if (!labels.empty() && select_idx >= 0 && select_idx < static_cast<int>(labels.size())) {
            cb->SetSelection(select_idx);
        }
    }
}

int get_selector_index(const wxChoice* ch, const ui::TouchComboBox* cb) {
    if (cb) {
        return cb->GetSelection();
    }
    if (ch) {
        return ch->GetSelection();
    }
    return wxNOT_FOUND;
}

void enable_selector(wxChoice* ch, ui::TouchComboBox* cb, bool enabled) {
    if (cb) {
        cb->Enable(enabled);
    }
    if (ch) {
        ch->Enable(enabled);
    }
}

} // namespace

std::vector<std::string> compatible_units_for_dimension(const std::string& unit_dimension) {
    if (unit_dimension == "VOLUME") {
        return {"ml", "l"};
    }
    if (unit_dimension == "COUNT") {
        return {"piezas"};
    }
    return {"g", "kg"};
}

std::string friendly_unit_label(const std::string& unit_code) {
    const std::string u = to_lower_ascii(trim_ascii(unit_code));
    if (u == "pz" || u == "pieza" || u == "piezas" || u == "u" || u == "unidades") {
        return "piezas";
    }
    if (u == "l" || u == "lt" || u == "litro" || u == "litros") {
        return "l";
    }
    if (u == "ml" || u == "g" || u == "gr" || u == "kg" || u == "mg") {
        return (u == "gr") ? "g" : u;
    }
    return unit_code;
}

std::string dimension_from_base_unit(const std::string& base_unit) {
    const std::string u = to_lower_ascii(trim_ascii(base_unit));
    if (u == "ml" || u == "l" || u == "lt" || u == "litro" || u == "litros") {
        return "VOLUME";
    }
    if (u == "pz" || u == "pieza" || u == "piezas" || u == "u" || u == "unidades") {
        return "COUNT";
    }
    return "MASS";
}

bool parse_positive_int64_input(const std::string& raw, std::int64_t& out_value, std::string& out_error) {
    const std::string clean = trim_ascii(raw);
    if (clean.empty()) {
        out_error = "Escribe una cantidad mayor que cero.";
        return false;
    }
    for (unsigned char ch : clean) {
        if (!std::isdigit(ch)) {
            out_error = "La cantidad debe ser un número entero mayor que cero.";
            return false;
        }
    }
    std::int64_t acc = 0;
    const std::int64_t max_val = std::numeric_limits<std::int64_t>::max();
    for (unsigned char ch : clean) {
        const std::int64_t digit = static_cast<std::int64_t>(ch - '0');
        if (acc > (max_val - digit) / 10) {
            out_error = "La cantidad ingresada es demasiado grande.";
            return false;
        }
        acc = acc * 10 + digit;
    }
    if (acc <= 0) {
        out_error = "La cantidad debe ser mayor que cero.";
        return false;
    }
    out_value = acc;
    return true;
}

InventoryRestockPanel::InventoryRestockPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::IInventoryService> inventory_service,
    std::shared_ptr<contracts::IAccountService> account_service)
    : wxPanel(parent, wxID_ANY),
      inventory_service_(std::move(inventory_service)),
      account_service_(std::move(account_service)) {
    SetBackgroundColour(Theme::ColorBackground);
    build_ui();
    refresh_data();
}

InventoryRestockPanel::~InventoryRestockPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &InventoryRestockPanel::on_size, this);
    if (keypad_) {
        keypad_->hide_keypad(false);
    }
    DestroyChildren();
}

void InventoryRestockPanel::build_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h_logical = touch ? 48 : 36;
    const int field_h_logical = touch ? 48 : 36;

    auto* root = new wxBoxSizer(wxVERTICAL);

    // Vista cuando no tiene permiso ni para registrar ni para ver compras
    pnl_no_permission_ = new CardPanel(this);
    static_cast<CardPanel*>(pnl_no_permission_)->set_card_background(Theme::ColorSurface);
    auto* np_sizer = new wxBoxSizer(wxVERTICAL);
    auto* np_title = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Reabastecimiento"));
    np_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    np_title->SetForegroundColour(Theme::ColorBrandDeep);
    np_sizer->Add(np_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* np_msg = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Sin permiso para registrar compras"));
    np_msg->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    np_msg->SetForegroundColour(Theme::ColorError);
    np_sizer->Add(np_msg, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto* np_sub = new wxStaticText(
        pnl_no_permission_,
        wxID_ANY,
        wxT("Para registrar compras de inventario pide el permiso al Dueño.")
    );
    np_sub->SetFont(Theme::get_font_body());
    np_sub->SetForegroundColour(Theme::ColorTextSecondary);
    np_sizer->Add(np_sub, 0, wxALL, FromDIP(20));
    pnl_no_permission_->SetSizer(np_sizer);
    pnl_no_permission_->Hide();
    root->Add(pnl_no_permission_, 1, wxEXPAND);

    // Cuerpo principal: formulario a la izquierda, compras recientes a la derecha
    main_body_ = new wxBoxSizer(wxHORIZONTAL);

    form_card_ = new CardPanel(this);
    form_card_->SetMinSize(FromDIP(wxSize(touch ? 285 : 275, -1)));
    auto* form_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_form_title = new wxStaticText(form_card_, wxID_ANY, wxT("Registrar compra"));
    lbl_form_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_form_title->SetForegroundColour(Theme::ColorBrandDeep);
    form_sizer->Add(lbl_form_title, 0, wxEXPAND | wxBOTTOM, FromDIP(touch ? 6 : 10));

    auto* lbl_ing = new wxStaticText(form_card_, wxID_ANY, wxT("Ingrediente:"));
    lbl_ing->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_ing->SetForegroundColour(Theme::ColorText);
    form_sizer->Add(lbl_ing, 0, wxEXPAND);

    if (touch) {
        cb_ingredient_ = new ui::TouchComboBox(
            form_card_, wxID_ANY, wxSize(-1, FromDIP(field_h_logical)), wxArrayString()
        );
        cb_ingredient_->SetFont(Theme::get_font_body());
        cb_ingredient_->SetMinSize(wxSize(-1, FromDIP(field_h_logical)));
        cb_ingredient_->Bind(wxEVT_COMBOBOX, &InventoryRestockPanel::on_ingredient_changed, this);
        form_sizer->Add(cb_ingredient_, 0, wxEXPAND | wxTOP, FromDIP(6));
    } else {
        ch_ingredient_ = new wxChoice(form_card_, wxID_ANY);
        ch_ingredient_->SetFont(Theme::get_font_body());
        ch_ingredient_->SetMinSize(wxSize(-1, FromDIP(field_h_logical)));
        ch_ingredient_->Bind(wxEVT_CHOICE, &InventoryRestockPanel::on_ingredient_changed, this);
        form_sizer->Add(ch_ingredient_, 0, wxEXPAND | wxTOP, FromDIP(6));
    }

    auto* lbl_qty = new wxStaticText(form_card_, wxID_ANY, wxT("Cantidad y unidad compradas:"));
    lbl_qty->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_qty->SetForegroundColour(Theme::ColorText);
    form_sizer->Add(lbl_qty, 0, wxEXPAND | wxTOP, FromDIP(touch ? 8 : 10));

    auto* qty_row = new wxBoxSizer(wxHORIZONTAL);
    wxWindow* qty_box = ui::make_text_field(
        form_card_, txt_qty_, field_h_logical, 80, Theme::get_font_body()
    );
    qty_row->Add(qty_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    if (touch) {
        cb_unit_ = new ui::TouchComboBox(
            form_card_, wxID_ANY, wxSize(FromDIP(108), FromDIP(field_h_logical)), wxArrayString()
        );
        cb_unit_->SetFont(Theme::get_font_body());
        cb_unit_->SetMinSize(wxSize(FromDIP(108), FromDIP(field_h_logical)));
        qty_row->Add(cb_unit_, 0, wxALIGN_CENTER_VERTICAL);
    } else {
        ch_unit_ = new wxChoice(form_card_, wxID_ANY);
        ch_unit_->SetFont(Theme::get_font_body());
        ch_unit_->SetMinSize(wxSize(FromDIP(96), FromDIP(field_h_logical)));
        qty_row->Add(ch_unit_, 0, wxALIGN_CENTER_VERTICAL);
    }
    form_sizer->Add(qty_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    auto* lbl_cost = new wxStaticText(form_card_, wxID_ANY, wxT("Costo total pagado ($):"));
    lbl_cost->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_cost->SetForegroundColour(Theme::ColorText);
    form_sizer->Add(lbl_cost, 0, wxEXPAND | wxTOP, FromDIP(touch ? 8 : 10));

    wxWindow* cost_box = ui::make_text_field(
        form_card_, txt_cost_, field_h_logical, 100, Theme::get_font_body()
    );
    form_sizer->Add(cost_box, 0, wxEXPAND | wxTOP, FromDIP(6));

    auto* lbl_cost_hint = new wxStaticText(form_card_, wxID_ANY, wxT("Ejemplo: 1 kg por 50.00"));
    lbl_cost_hint->SetFont(Theme::get_font_small());
    lbl_cost_hint->SetForegroundColour(Theme::ColorTextSecondary);
    form_sizer->Add(lbl_cost_hint, 0, wxEXPAND | wxTOP, FromDIP(6));

    btn_register_ = new wxButton(form_card_, wxID_ANY, wxT("Registrar compra"));
    btn_register_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_register_, btn_h_logical);
    Theme::ensure_button_min_size(btn_register_, 180, btn_h_logical);
    btn_register_->Bind(wxEVT_BUTTON, &InventoryRestockPanel::on_register_purchase, this);
    form_sizer->Add(btn_register_, 0, wxALIGN_LEFT | wxTOP, FromDIP(touch ? 8 : 12));

    lbl_feedback_ = new wxStaticText(form_card_, wxID_ANY, wxEmptyString);
    lbl_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_feedback_->SetForegroundColour(Theme::ColorText);
    form_sizer->Add(lbl_feedback_, 0, wxEXPAND | wxTOP, FromDIP(6));

    form_sizer->AddStretchSpacer(1);

    auto* form_outer = new wxBoxSizer(wxVERTICAL);
    form_outer->Add(form_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    form_card_->SetSizer(form_outer);
    main_body_->Add(form_card_, 0, wxEXPAND | wxRIGHT, FromDIP(8));

    // Derecha: Lista de compras recientes
    list_card_ = new CardPanel(this);
    auto* list_outer = new wxBoxSizer(wxVERTICAL);
    auto* list_sizer = new wxBoxSizer(wxVERTICAL);

    pnl_readonly_notice_ = new wxPanel(list_card_, wxID_ANY);
    pnl_readonly_notice_->SetBackgroundColour(Theme::ColorBrandSoft);
    auto* ro_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_ro = new wxStaticText(pnl_readonly_notice_, wxID_ANY, wxT("Sin permiso para registrar compras"));
    lbl_ro->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_ro->SetForegroundColour(Theme::ColorError);
    ro_sizer->Add(lbl_ro, 1, wxALL, FromDIP(8));
    pnl_readonly_notice_->SetSizer(ro_sizer);
    pnl_readonly_notice_->Hide();
    list_sizer->Add(pnl_readonly_notice_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* list_header = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_list_title = new wxStaticText(list_card_, wxID_ANY, wxT("Compras recientes"));
    lbl_list_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_list_title->SetForegroundColour(Theme::ColorText);
    list_header->Add(lbl_list_title, 1, wxALIGN_CENTER_VERTICAL);

    btn_refresh_ = new wxButton(list_card_, wxID_ANY, wxT("Actualizar"));
    btn_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_refresh_, btn_h_logical);
    Theme::ensure_button_min_size(btn_refresh_, 115, btn_h_logical);
    btn_refresh_->Bind(wxEVT_BUTTON, &InventoryRestockPanel::on_refresh, this);
    list_header->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);
    list_sizer->Add(list_header, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    lbl_empty_state_ = new wxStaticText(
        list_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_empty_state_->SetFont(Theme::get_font_body());
    lbl_empty_state_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_empty_state_->Hide();
    list_sizer->Add(lbl_empty_state_, 1, wxEXPAND | wxTOP, FromDIP(8));

    list_ = new wxListView(list_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->SetFont(Theme::get_font_body());
    list_->SetBackgroundColour(Theme::ColorSurface);
    list_->SetForegroundColour(Theme::ColorText);
    list_->InsertColumn(0, wxString::FromUTF8("Fecha"));
    list_->InsertColumn(1, wxString::FromUTF8("Ingrediente"));
    list_->InsertColumn(2, wxString::FromUTF8("Cantidad"));
    list_->InsertColumn(3, wxString::FromUTF8("Costo"));
    list_->InsertColumn(4, wxString::FromUTF8("Registró"));
    if (touch) {
        ui::apply_touch_row_height(list_);
    }
    list_sizer->Add(list_, 1, wxEXPAND);

    list_outer->Add(list_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    list_card_->SetSizer(list_outer);
    main_body_->Add(list_card_, 1, wxEXPAND);

    root->Add(main_body_, 1, wxEXPAND);
    SetSizer(root);

    if (touch) {
        keypad_ = new ui::NumericKeypad(this);
        auto protected_fn = [this]() {
            std::vector<wxRect> rects;
            auto add_rect = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_rect(form_card_);
            add_rect(btn_register_);
            return rects;
        };
        keypad_->bind_field(txt_qty_, qty_box, false, form_card_, protected_fn);
        keypad_->bind_field(txt_cost_, cost_box, true, form_card_, protected_fn);
    }

    Bind(wxEVT_SIZE, &InventoryRestockPanel::on_size, this);
}

void InventoryRestockPanel::refresh_data() {
    if (destroying_ || IsBeingDeleted()) {
        return;
    }
    const std::string keep_id = selected_ingredient_id();

    can_restock_ = account_service_ && account_service_->has_permission("inventory.restock");
    can_view_purchases_ = can_restock_ ||
        (account_service_ && account_service_->has_permission("inventory.manage_products"));

    ingredients_.clear();
    entries_.clear();
    list_load_failed_ = false;
    last_list_error_.clear();
    list_->DeleteAllItems();

    if (!can_restock_ && !can_view_purchases_) {
        update_permissions_ui();
        Layout();
        return;
    }

    if (!inventory_service_) {
        list_load_failed_ = true;
        last_list_error_ = "Servicio de inventario no disponible.";
        update_permissions_ui();
        update_empty_state();
        Layout();
        return;
    }

    if (can_restock_) {
        auto ing_res = inventory_service_->list_ingredients();
        if (ing_res.is_success()) {
            ingredients_ = ing_res.value();
        } else {
            set_feedback(ing_res.error_message(), true);
        }
        populate_ingredient_selector(keep_id);
        update_unit_choices_for_selected();
    }

    contracts::PurchaseEntryFilterDto filter;
    filter.limit = 100;
    auto list_res = inventory_service_->list_purchase_entries(filter);
    if (!list_res.is_success()) {
        list_load_failed_ = true;
        last_list_error_ = list_res.error_message();
    } else {
        entries_ = list_res.value();
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& e = entries_[i];
            const std::string date_short = e.created_at.size() >= 16 ? e.created_at.substr(0, 16) : e.created_at;
            const long idx = list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(date_short));
            list_->SetItem(idx, 1, wxString::FromUTF8(e.ingredient_name));
            const std::string qty_str = std::to_string(e.quantity_purchased) + " " + friendly_unit_label(e.purchase_unit);
            list_->SetItem(idx, 2, wxString::FromUTF8(qty_str));
            list_->SetItem(idx, 3, wxString::FromUTF8(e.total_cost.to_string()));
            const std::string actor = e.actor_name.empty() ? "—" : e.actor_name;
            list_->SetItem(idx, 4, wxString::FromUTF8(actor));
        }
    }

    update_permissions_ui();
    update_empty_state();
    Layout();
    update_list_column_widths();
}

void InventoryRestockPanel::update_permissions_ui() {
    if (!can_restock_ && !can_view_purchases_) {
        if (keypad_) {
            keypad_->hide_keypad(false);
        }
        form_card_->Hide();
        list_card_->Hide();
        pnl_no_permission_->Show();
        return;
    }

    pnl_no_permission_->Hide();
    list_card_->Show();

    if (can_restock_) {
        form_card_->Show();
        pnl_readonly_notice_->Hide();
        const bool has_ing = !ingredients_.empty();
        enable_selector(ch_ingredient_, cb_ingredient_, has_ing);
        enable_selector(ch_unit_, cb_unit_, has_ing);
        ui::set_text_field_enabled(txt_qty_, has_ing);
        ui::set_text_field_enabled(txt_cost_, has_ing);
        btn_register_->Enable(has_ing);
        if (!has_ing && last_feedback_.empty()) {
            set_feedback("Primero registra ingredientes en la pestaña Ingredientes.", false);
        }
    } else {
        if (keypad_) {
            keypad_->hide_keypad(false);
        }
        form_card_->Hide();
        pnl_readonly_notice_->Show();
    }
}

void InventoryRestockPanel::populate_ingredient_selector(const std::string& keep_id) {
    std::vector<std::string> labels;
    labels.reserve(ingredients_.size());
    int select_idx = ingredients_.empty() ? wxNOT_FOUND : 0;
    for (std::size_t i = 0; i < ingredients_.size(); ++i) {
        labels.push_back(ingredients_[i].name);
        if (!keep_id.empty() && ingredients_[i].id == keep_id) {
            select_idx = static_cast<int>(i);
        }
    }
    set_selector_items(ch_ingredient_, cb_ingredient_, labels, select_idx);
}

void InventoryRestockPanel::update_unit_choices_for_selected() {
    const std::string dim = selected_ingredient_dimension();
    current_units_ = compatible_units_for_dimension(dim);
    const int select_idx = current_units_.empty() ? wxNOT_FOUND : 0;
    set_selector_items(ch_unit_, cb_unit_, current_units_, select_idx);
}

void InventoryRestockPanel::update_empty_state() {
    if (!list_card_->IsShown()) {
        return;
    }
    if (list_load_failed_) {
        list_->Hide();
        lbl_empty_state_->SetLabel(wxString::FromUTF8(last_list_error_));
        lbl_empty_state_->SetForegroundColour(Theme::ColorError);
        lbl_empty_state_->Show();
        const int wrap_w = std::max(FromDIP(180), list_card_->GetClientSize().GetWidth() - FromDIP(40));
        lbl_empty_state_->Wrap(wrap_w);
    } else if (entries_.empty()) {
        list_->Hide();
        lbl_empty_state_->SetLabel(wxT("Todavía no hay compras registradas."));
        lbl_empty_state_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_empty_state_->Show();
        const int wrap_w = std::max(FromDIP(180), list_card_->GetClientSize().GetWidth() - FromDIP(40));
        lbl_empty_state_->Wrap(wrap_w);
    } else {
        lbl_empty_state_->Hide();
        list_->Show();
    }
}

void InventoryRestockPanel::update_list_column_widths() {
    if (destroying_ || sizing_columns_ || list_ == nullptr || !list_->IsShown()) {
        return;
    }
    if (list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;

    wxClientDC dc(list_);
    dc.SetFont(list_->GetFont());

    int max_date = dc.GetTextExtent(wxT("2026-11-16 20:30")).GetWidth();
    int max_ing = dc.GetTextExtent(wxT("Ingrediente")).GetWidth();
    int max_qty = dc.GetTextExtent(wxT("Cantidad")).GetWidth();
    int max_cost = dc.GetTextExtent(wxT("Costo")).GetWidth();
    int max_actor = dc.GetTextExtent(wxT("Registró")).GetWidth();

    for (const auto& e : entries_) {
        const std::string date_short = e.created_at.size() >= 16 ? e.created_at.substr(0, 16) : e.created_at;
        max_date = std::max(max_date, dc.GetTextExtent(wxString::FromUTF8(date_short)).GetWidth());
        max_ing = std::max(max_ing, dc.GetTextExtent(wxString::FromUTF8(e.ingredient_name)).GetWidth());
        const std::string qty_str = std::to_string(e.quantity_purchased) + " " + friendly_unit_label(e.purchase_unit);
        max_qty = std::max(max_qty, dc.GetTextExtent(wxString::FromUTF8(qty_str)).GetWidth());
        max_cost = std::max(max_cost, dc.GetTextExtent(wxString::FromUTF8(e.total_cost.to_string())).GetWidth());
        const std::string actor = e.actor_name.empty() ? "—" : e.actor_name;
        max_actor = std::max(max_actor, dc.GetTextExtent(wxString::FromUTF8(actor)).GetWidth());
    }

    const int pad = 18;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Fecha"), max_date, 0, pad, 0});
    specs.push_back({wxT("Ingrediente"), max_ing, 0, pad, 0});
    specs.push_back({wxT("Cantidad"), max_qty, 0, pad, 0});
    specs.push_back({wxT("Costo"), max_cost, 0, pad, 0});
    specs.push_back({wxT("Registró"), max_actor, 0, pad, 0});

    ui::distribute_list_columns(list_, specs, 1);
    sizing_columns_ = false;
}

void InventoryRestockPanel::set_feedback(const std::string& message, bool is_error) {
    last_feedback_ = message;
    last_feedback_is_error_ = is_error;
    if (!lbl_feedback_) {
        return;
    }
    lbl_feedback_->SetForegroundColour(is_error ? Theme::ColorError : Theme::ColorSuccess);
    lbl_feedback_->SetLabel(wxString::FromUTF8(message));
    if (form_card_) {
        const int wrap_w = std::max(FromDIP(180), form_card_->GetClientSize().GetWidth() - FromDIP(28));
        lbl_feedback_->Wrap(wrap_w);
        form_card_->Layout();
    }
}

void InventoryRestockPanel::on_refresh(wxCommandEvent&) {
    if (destroying_) return;
    refresh_data();
}

void InventoryRestockPanel::on_ingredient_changed(wxCommandEvent&) {
    if (destroying_) return;
    update_unit_choices_for_selected();
    if (form_card_) {
        form_card_->Layout();
    }
}

void InventoryRestockPanel::on_register_purchase(wxCommandEvent&) {
    if (destroying_ || !inventory_service_) {
        return;
    }
    if (keypad_) {
        keypad_->hide_keypad(false);
    }

    const std::string ing_id = selected_ingredient_id();
    if (ing_id.empty()) {
        set_feedback("Elige un ingrediente.", true);
        return;
    }

    std::int64_t qty = 0;
    std::string qty_err;
    if (!parse_positive_int64_input(std::string(txt_qty_->GetValue().ToUTF8().data()), qty, qty_err)) {
        set_feedback(qty_err, true);
        return;
    }

    const std::string raw_cost = trim_ascii(std::string(txt_cost_->GetValue().ToUTF8().data()));
    if (raw_cost.empty()) {
        set_feedback("Escribe el costo total en pesos.", true);
        return;
    }
    auto money_res = contracts::Money::try_parse(raw_cost, false);
    if (!money_res.is_success()) {
        set_feedback(money_res.error_message(), true);
        return;
    }

    const std::string unit = selected_unit_code();
    const auto res = inventory_service_->register_purchase_entry(ing_id, qty, unit, money_res.value());
    if (!res.is_success()) {
        set_feedback(res.error_message(), true);
        return;
    }

    txt_qty_->ChangeValue(wxEmptyString);
    txt_cost_->ChangeValue(wxEmptyString);
    refresh_data();
    set_feedback("Compra registrada y sumada al inventario.", false);
}

void InventoryRestockPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted()) {
            return;
        }
        update_empty_state();
        update_list_column_widths();
        if (lbl_feedback_ && form_card_) {
            const int wrap_w = std::max(FromDIP(180), form_card_->GetClientSize().GetWidth() - FromDIP(28));
            lbl_feedback_->SetLabel(wxString::FromUTF8(last_feedback_));
            lbl_feedback_->Wrap(wrap_w);
        }
        Layout();
    });
}

std::string InventoryRestockPanel::selected_ingredient_id() const {
    const int idx = get_selector_index(ch_ingredient_, cb_ingredient_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ingredients_.size()) {
        return ingredients_[static_cast<std::size_t>(idx)].id;
    }
    return "";
}

std::string InventoryRestockPanel::selected_ingredient_dimension() const {
    const int idx = get_selector_index(ch_ingredient_, cb_ingredient_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ingredients_.size()) {
        return ingredients_[static_cast<std::size_t>(idx)].unit_dimension;
    }
    return "MASS";
}

std::string InventoryRestockPanel::selected_unit_code() const {
    const int idx = get_selector_index(ch_unit_, cb_unit_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < current_units_.size()) {
        return current_units_[static_cast<std::size_t>(idx)];
    }
    return current_units_.empty() ? "g" : current_units_.front();
}

} // namespace app::frontend
