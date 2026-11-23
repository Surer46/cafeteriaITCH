#include "src/frontend/inventory_bar_panel.hpp"

#include "src/frontend/inventory_ingredients_panel.hpp"
#include "src/frontend/inventory_restock_panel.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <algorithm>
#include <cctype>

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

void set_selector_index(wxChoice* ch, ui::TouchComboBox* cb, int idx) {
    if (cb && idx >= 0 && static_cast<unsigned int>(idx) < cb->GetCount()) {
        cb->SetSelection(idx);
    }
    if (ch && idx >= 0 && static_cast<unsigned int>(idx) < ch->GetCount()) {
        ch->SetSelection(idx);
    }
}

void enable_selector(wxChoice* ch, ui::TouchComboBox* cb, bool enabled) {
    if (cb) {
        cb->Enable(enabled);
    }
    if (ch) {
        ch->Enable(enabled);
    }
}

std::string format_bar_quantity(std::int64_t amount_base, const std::string& unit_dimension) {
    if (unit_dimension == "MASS") {
        const bool negative = amount_base < 0;
        const std::uint64_t mag = negative
            ? static_cast<std::uint64_t>(-(amount_base + 1)) + 1ULL
            : static_cast<std::uint64_t>(amount_base);
        if (mag == 0) {
            return "0 g";
        }
        if (mag < 1000ULL) {
            std::string dec = std::to_string(mag);
            while (dec.size() < 3) {
                dec = "0" + dec;
            }
            while (!dec.empty() && dec.back() == '0') {
                dec.pop_back();
            }
            return (negative ? "-0." : "0.") + dec + " g";
        }
    }
    return format_ingredient_quantity(amount_base, unit_dimension);
}

std::string format_signed_movement(std::int64_t qty_base, const std::string& base_unit) {
    const std::string dim = dimension_from_base_unit(base_unit);
    const std::string formatted = format_bar_quantity(qty_base, dim);
    if (qty_base > 0) {
        return "+" + formatted;
    }
    return formatted;
}

} // namespace

InventoryBarPanel::InventoryBarPanel(
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

InventoryBarPanel::~InventoryBarPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &InventoryBarPanel::on_size, this);
    if (stock_list_) {
        stock_list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryBarPanel::on_stock_selected, this);
    }
    if (mov_list_) {
        mov_list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryBarPanel::on_movement_selected, this);
        mov_list_->Unbind(wxEVT_LIST_ITEM_DESELECTED, &InventoryBarPanel::on_movement_deselected, this);
    }
    if (keypad_) {
        keypad_->hide_keypad(false);
    }
    DestroyChildren();
}

void InventoryBarPanel::build_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h_logical = touch ? 48 : 36;
    const int field_h_logical = touch ? 48 : 36;

    auto* root = new wxBoxSizer(wxVERTICAL);

    // Vista cuando no tiene permiso inventory.view
    pnl_no_permission_ = new CardPanel(this);
    static_cast<CardPanel*>(pnl_no_permission_)->set_card_background(Theme::ColorSurface);
    auto* np_sizer = new wxBoxSizer(wxVERTICAL);
    auto* np_title = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Barra"));
    np_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    np_title->SetForegroundColour(Theme::ColorBrandDeep);
    np_sizer->Add(np_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* np_msg = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Sin permiso para ver existencias"));
    np_msg->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    np_msg->SetForegroundColour(Theme::ColorError);
    np_sizer->Add(np_msg, 0, wxALL, FromDIP(20));
    pnl_no_permission_->SetSizer(np_sizer);
    pnl_no_permission_->Hide();
    root->Add(pnl_no_permission_, 1, wxEXPAND);

    // Cuerpo principal: izquierda (Existencias + Movimientos), derecha (Formulario merma/ajuste o aviso)
    main_body_ = new wxBoxSizer(wxHORIZONTAL);

    auto* left_col = new wxBoxSizer(wxVERTICAL);

    // Arriba izquierda: Existencias buscables (relleno interior 16 lógicas)
    stock_card_ = new CardPanel(this);
    auto* stock_outer = new wxBoxSizer(wxVERTICAL);
    auto* stock_sizer = new wxBoxSizer(wxVERTICAL);

    auto* stock_header = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_stock_title = new wxStaticText(stock_card_, wxID_ANY, wxT("Existencias"));
    lbl_stock_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_stock_title->SetForegroundColour(Theme::ColorBrandDeep);
    stock_header->Add(lbl_stock_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    wxWindow* search_box = ui::make_text_field(
        stock_card_, txt_search_, field_h_logical, 120, Theme::get_font_body()
    );
    txt_search_->SetHint(wxT("Buscar ingrediente..."));
    txt_search_->Bind(wxEVT_TEXT, &InventoryBarPanel::on_search_changed, this);
    stock_header->Add(search_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    btn_refresh_ = new wxButton(stock_card_, wxID_ANY, wxT("Actualizar"));
    btn_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_refresh_, btn_h_logical);
    Theme::ensure_button_min_size(btn_refresh_, 110, btn_h_logical);
    btn_refresh_->Bind(wxEVT_BUTTON, &InventoryBarPanel::on_refresh, this);
    stock_header->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);
    stock_sizer->Add(stock_header, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    lbl_stock_empty_ = new wxStaticText(
        stock_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_stock_empty_->SetFont(Theme::get_font_body());
    lbl_stock_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_stock_empty_->Hide();
    stock_sizer->Add(lbl_stock_empty_, 1, wxEXPAND | wxTOP, FromDIP(8));

    stock_list_ = new wxListView(
        stock_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    stock_list_->SetFont(Theme::get_font_body());
    stock_list_->SetBackgroundColour(Theme::ColorSurface);
    stock_list_->SetForegroundColour(Theme::ColorText);
    stock_list_->InsertColumn(0, wxT("Ingrediente"));
    stock_list_->InsertColumn(1, wxT("Existencia"));
    stock_list_->InsertColumn(2, wxT("Estado"));
    if (touch) {
        ui::apply_touch_row_height(stock_list_);
    }
    stock_list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryBarPanel::on_stock_selected, this);
    stock_sizer->Add(stock_list_, 1, wxEXPAND);

    stock_outer->Add(stock_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    stock_card_->SetSizer(stock_outer);
    left_col->Add(stock_card_, 11, wxEXPAND | wxBOTTOM, FromDIP(touch ? 6 : 8));

    // Abajo izquierda: Movimientos recientes de merma y ajuste (relleno interior 16 lógicas)
    mov_card_ = new CardPanel(this);
    auto* mov_outer = new wxBoxSizer(wxVERTICAL);
    auto* mov_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_mov_title = new wxStaticText(mov_card_, wxID_ANY, wxT("Movimientos recientes"));
    lbl_mov_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_mov_title->SetForegroundColour(Theme::ColorText);
    mov_sizer->Add(lbl_mov_title, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    lbl_mov_empty_ = new wxStaticText(
        mov_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_mov_empty_->SetFont(Theme::get_font_body());
    lbl_mov_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_mov_empty_->Hide();
    mov_sizer->Add(lbl_mov_empty_, 1, wxEXPAND | wxTOP, FromDIP(8));

    mov_list_ = new wxListView(
        mov_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    mov_list_->SetFont(Theme::get_font_body());
    mov_list_->SetBackgroundColour(Theme::ColorSurface);
    mov_list_->SetForegroundColour(Theme::ColorText);
    mov_list_->InsertColumn(0, wxT("Fecha"));
    mov_list_->InsertColumn(1, wxT("Ingrediente"));
    mov_list_->InsertColumn(2, wxT("Cantidad"));
    mov_list_->InsertColumn(3, wxT("Motivo"));
    mov_list_->InsertColumn(4, wxT("Quién"));
    if (touch) {
        ui::apply_touch_row_height(mov_list_);
    }
    mov_list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryBarPanel::on_movement_selected, this);
    mov_list_->Bind(wxEVT_LIST_ITEM_DESELECTED, &InventoryBarPanel::on_movement_deselected, this);
    mov_sizer->Add(mov_list_, 1, wxEXPAND);

    lbl_mov_detail_ = new wxStaticText(mov_card_, wxID_ANY, wxEmptyString);
    lbl_mov_detail_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_mov_detail_->SetForegroundColour(Theme::ColorText);
    lbl_mov_detail_->Hide();
    mov_sizer->Add(lbl_mov_detail_, 0, wxEXPAND | wxTOP, FromDIP(8));

    mov_outer->Add(mov_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    mov_card_->SetSizer(mov_outer);
    left_col->Add(mov_card_, 10, wxEXPAND);

    main_body_->Add(left_col, 1, wxEXPAND | wxRIGHT, FromDIP(8));

    // Derecha: Registro de merma o ajuste (o aviso si no tiene inventory.adjust)
    action_card_ = new CardPanel(this);
    action_card_->SetMinSize(FromDIP(wxSize(touch ? 285 : 275, -1)));
    auto* action_outer = new wxBoxSizer(wxVERTICAL);

    lbl_no_adjust_ = new wxStaticText(
        action_card_,
        wxID_ANY,
        wxT("Para registrar mermas o ajustes pide el permiso al Dueño.")
    );
    lbl_no_adjust_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_no_adjust_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_no_adjust_->Wrap(FromDIP(260));
    lbl_no_adjust_->Hide();
    action_outer->Add(lbl_no_adjust_, 0, wxEXPAND | wxALL, FromDIP(16));

    form_scroll_ = new wxScrolledWindow(action_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    form_scroll_->SetBackgroundColour(Theme::ColorSurface);
    form_scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    form_scroll_->SetScrollRate(0, FromDIP(14));

    form_sizer_ = new wxBoxSizer(wxVERTICAL);

    // Selector claro: Merma | Ajuste
    auto* mode_row = new wxBoxSizer(wxHORIZONTAL);
    btn_mode_waste_ = new wxButton(form_scroll_, wxID_ANY, wxT("Merma"));
    btn_mode_waste_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_mode_waste_->Bind(wxEVT_BUTTON, &InventoryBarPanel::on_mode_waste, this);
    mode_row->Add(btn_mode_waste_, 1, wxEXPAND | wxRIGHT, FromDIP(6));

    btn_mode_adjust_ = new wxButton(form_scroll_, wxID_ANY, wxT("Ajuste"));
    btn_mode_adjust_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_mode_adjust_->Bind(wxEVT_BUTTON, &InventoryBarPanel::on_mode_adjustment, this);
    mode_row->Add(btn_mode_adjust_, 1, wxEXPAND);
    form_sizer_->Add(mode_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    // Dirección en modo Ajuste: Sumar / Restar
    pnl_adjust_dir_ = new wxPanel(form_scroll_, wxID_ANY);
    pnl_adjust_dir_->SetBackgroundColour(Theme::ColorSurface);
    auto* dir_col = new wxBoxSizer(wxVERTICAL);
    auto* lbl_dir = new wxStaticText(pnl_adjust_dir_, wxID_ANY, wxT("Tipo de ajuste:"));
    lbl_dir->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_dir->SetForegroundColour(Theme::ColorTextSecondary);
    dir_col->Add(lbl_dir, 0, wxBOTTOM, FromDIP(6));

    auto* dir_row = new wxBoxSizer(wxHORIZONTAL);
    btn_dir_add_ = new wxButton(pnl_adjust_dir_, wxID_ANY, wxT("Sumar"));
    btn_dir_add_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_dir_add_->Bind(wxEVT_BUTTON, &InventoryBarPanel::on_adjust_add, this);
    dir_row->Add(btn_dir_add_, 1, wxEXPAND | wxRIGHT, FromDIP(6));

    btn_dir_sub_ = new wxButton(pnl_adjust_dir_, wxID_ANY, wxT("Restar"));
    btn_dir_sub_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_dir_sub_->Bind(wxEVT_BUTTON, &InventoryBarPanel::on_adjust_subtract, this);
    dir_row->Add(btn_dir_sub_, 1, wxEXPAND);
    dir_col->Add(dir_row, 0, wxEXPAND);
    pnl_adjust_dir_->SetSizer(dir_col);
    pnl_adjust_dir_->Hide();
    form_sizer_->Add(pnl_adjust_dir_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* lbl_ing = new wxStaticText(form_scroll_, wxID_ANY, wxT("Ingrediente:"));
    lbl_ing->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_ing->SetForegroundColour(Theme::ColorText);
    form_sizer_->Add(lbl_ing, 0, wxEXPAND);

    if (touch) {
        cb_ingredient_ = new ui::TouchComboBox(
            form_scroll_, wxID_ANY, wxSize(-1, FromDIP(field_h_logical)), wxArrayString()
        );
        cb_ingredient_->SetFont(Theme::get_font_body());
        cb_ingredient_->SetMinSize(wxSize(-1, FromDIP(field_h_logical)));
        cb_ingredient_->Bind(wxEVT_COMBOBOX, &InventoryBarPanel::on_ingredient_changed, this);
        form_sizer_->Add(cb_ingredient_, 0, wxEXPAND | wxTOP, FromDIP(6));
    } else {
        ch_ingredient_ = new wxChoice(form_scroll_, wxID_ANY);
        ch_ingredient_->SetFont(Theme::get_font_body());
        ch_ingredient_->SetMinSize(wxSize(-1, FromDIP(field_h_logical)));
        ch_ingredient_->Bind(wxEVT_CHOICE, &InventoryBarPanel::on_ingredient_changed, this);
        form_sizer_->Add(ch_ingredient_, 0, wxEXPAND | wxTOP, FromDIP(6));
    }

    auto* lbl_qty = new wxStaticText(form_scroll_, wxID_ANY, wxT("Cantidad y unidad:"));
    lbl_qty->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_qty->SetForegroundColour(Theme::ColorText);
    form_sizer_->Add(lbl_qty, 0, wxEXPAND | wxTOP, FromDIP(8));

    auto* qty_row = new wxBoxSizer(wxHORIZONTAL);
    wxWindow* qty_box = ui::make_text_field(
        form_scroll_, txt_qty_, field_h_logical, 80, Theme::get_font_body()
    );
    qty_row->Add(qty_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    if (touch) {
        cb_unit_ = new ui::TouchComboBox(
            form_scroll_, wxID_ANY, wxSize(FromDIP(108), FromDIP(field_h_logical)), wxArrayString()
        );
        cb_unit_->SetFont(Theme::get_font_body());
        cb_unit_->SetMinSize(wxSize(FromDIP(108), FromDIP(field_h_logical)));
        qty_row->Add(cb_unit_, 0, wxALIGN_CENTER_VERTICAL);
    } else {
        ch_unit_ = new wxChoice(form_scroll_, wxID_ANY);
        ch_unit_->SetFont(Theme::get_font_body());
        ch_unit_->SetMinSize(wxSize(FromDIP(96), FromDIP(field_h_logical)));
        qty_row->Add(ch_unit_, 0, wxALIGN_CENTER_VERTICAL);
    }
    form_sizer_->Add(qty_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    auto* reason_hdr = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_reason = new wxStaticText(form_scroll_, wxID_ANY, wxT("Motivo (obligatorio):"));
    lbl_reason->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_reason->SetForegroundColour(Theme::ColorText);
    reason_hdr->Add(lbl_reason, 1, wxALIGN_CENTER_VERTICAL);

    lbl_reason_count_ = new wxStaticText(form_scroll_, wxID_ANY, wxT("0 / 200"));
    lbl_reason_count_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_reason_count_->SetForegroundColour(Theme::ColorTextSecondary);
    reason_hdr->Add(lbl_reason_count_, 0, wxALIGN_CENTER_VERTICAL);
    form_sizer_->Add(reason_hdr, 0, wxEXPAND | wxTOP, FromDIP(8));

    wxWindow* reason_box = ui::make_text_field(
        form_scroll_, txt_reason_, field_h_logical, 100, Theme::get_font_body()
    );
    txt_reason_->SetMaxLength(200);
    txt_reason_->Bind(wxEVT_TEXT, &InventoryBarPanel::on_reason_changed, this);
    form_sizer_->Add(reason_box, 0, wxEXPAND | wxTOP, FromDIP(6));

    btn_submit_ = new wxButton(form_scroll_, wxID_ANY, wxT("Registrar merma"));
    btn_submit_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_submit_, btn_h_logical);
    Theme::ensure_button_min_size(btn_submit_, 180, btn_h_logical);
    btn_submit_->Bind(wxEVT_BUTTON, &InventoryBarPanel::on_submit, this);
    form_sizer_->Add(btn_submit_, 0, wxEXPAND | wxTOP, FromDIP(10));

    lbl_feedback_ = new wxStaticText(form_scroll_, wxID_ANY, wxEmptyString);
    lbl_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_feedback_->SetForegroundColour(Theme::ColorText);
    form_sizer_->Add(lbl_feedback_, 0, wxEXPAND | wxTOP, FromDIP(6));

    form_scroll_->SetSizer(form_sizer_);
    action_outer->Add(form_scroll_, 1, wxEXPAND | wxALL, FromDIP(16));

    action_card_->SetSizer(action_outer);
    main_body_->Add(action_card_, 0, wxEXPAND);

    root->Add(main_body_, 1, wxEXPAND);
    SetSizer(root);

    update_mode_ui();

    if (touch) {
        keypad_ = new ui::NumericKeypad(this);
        keypad_->bind_field(
            txt_qty_,
            qty_box,
            false,
            action_card_,
            [this]() {
                std::vector<wxRect> rects;
                auto add_rect = [&](wxWindow* w) {
                    if (!w || !w->IsShownOnScreen()) return;
                    rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
                };
                add_rect(action_card_);
                add_rect(btn_submit_);
                return rects;
            }
        );
    }

    Bind(wxEVT_SIZE, &InventoryBarPanel::on_size, this);
}

void InventoryBarPanel::refresh_data(const std::string& keep_selection_id) {
    if (destroying_ || IsBeingDeleted()) {
        return;
    }
    const std::string target_id = keep_selection_id.empty() ? selected_ingredient_id() : keep_selection_id;

    can_view_ = account_service_ && account_service_->has_permission("inventory.view");
    can_adjust_ = account_service_ && account_service_->has_permission("inventory.adjust");

    ingredients_.clear();
    filtered_ingredients_.clear();
    low_stock_ids_.clear();
    movements_.clear();
    stock_load_failed_ = false;
    mov_load_failed_ = false;
    last_stock_error_.clear();
    last_mov_error_.clear();
    stock_list_->DeleteAllItems();
    mov_list_->DeleteAllItems();
    update_movement_detail(-1);

    if (!can_view_) {
        update_permissions_ui();
        Layout();
        return;
    }

    if (!inventory_service_) {
        stock_load_failed_ = true;
        mov_load_failed_ = true;
        last_stock_error_ = "Servicio de inventario no disponible.";
        last_mov_error_ = last_stock_error_;
        update_permissions_ui();
        update_empty_states();
        Layout();
        return;
    }

    auto ing_res = inventory_service_->list_ingredients();
    if (!ing_res.is_success()) {
        stock_load_failed_ = true;
        last_stock_error_ = ing_res.error_message();
    } else {
        ingredients_ = ing_res.value();
        auto alert_res = inventory_service_->get_stock_alerts();
        if (alert_res.is_success()) {
            for (const auto& al : alert_res.value()) {
                low_stock_ids_.insert(al.ingredient_id);
            }
        }
    }

    contracts::StockMovementFilterDto mov_filter;
    mov_filter.limit = 200;
    auto mov_res = inventory_service_->list_stock_movements(mov_filter);
    if (!mov_res.is_success()) {
        mov_load_failed_ = true;
        last_mov_error_ = mov_res.error_message();
    } else {
        for (const auto& m : mov_res.value()) {
            if (m.movement_type == "WASTE" || m.movement_type == "ADJUSTMENT") {
                movements_.push_back(m);
                if (movements_.size() >= 60) {
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < movements_.size(); ++i) {
            const auto& m = movements_[i];
            const std::string date_short = m.created_at.size() >= 16 ? m.created_at.substr(0, 16) : m.created_at;
            const long idx = mov_list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(date_short));
            mov_list_->SetItem(idx, 1, wxString::FromUTF8(m.ingredient_name));
            mov_list_->SetItem(idx, 2, wxString::FromUTF8(format_signed_movement(m.quantity_base_units, m.base_unit)));
            mov_list_->SetItem(idx, 3, wxString::FromUTF8(m.reason));
            const std::string actor = m.actor_name.empty() ? "—" : m.actor_name;
            mov_list_->SetItem(idx, 4, wxString::FromUTF8(actor));
        }
    }

    populate_ingredient_selector(target_id);
    update_unit_choices_for_selected();
    apply_stock_filter(selected_ingredient_id());
    update_permissions_ui();
    update_empty_states();
    Layout();
    update_list_column_widths();
}

void InventoryBarPanel::apply_stock_filter(const std::string& keep_selection_id) {
    if (!stock_list_) {
        return;
    }
    sync_selection_guard_ = true;
    stock_list_->DeleteAllItems();
    filtered_ingredients_.clear();

    wxString query = txt_search_ ? txt_search_->GetValue() : wxString();
    query.Trim(true).Trim(false);
    const wxString query_l = query.Lower();

    long row_to_select = -1;
    for (const auto& ing : ingredients_) {
        const wxString name_wx = wxString::FromUTF8(ing.name);
        if (!query_l.empty() && name_wx.Lower().Find(query_l) == wxNOT_FOUND) {
            continue;
        }
        const long idx = stock_list_->InsertItem(static_cast<long>(filtered_ingredients_.size()), name_wx);
        stock_list_->SetItem(
            idx, 1, wxString::FromUTF8(format_bar_quantity(ing.current_stock_base, ing.unit_dimension))
        );
        std::string status_txt = "Suficiente";
        const bool out_of_stock = ing.current_stock_base <= 0;
        const bool low_stock = !out_of_stock && low_stock_ids_.count(ing.id) > 0;
        if (out_of_stock) {
            status_txt = "Agotado";
        } else if (low_stock) {
            status_txt = "Poco";
        }
        stock_list_->SetItem(idx, 2, wxString::FromUTF8(status_txt));
        if (out_of_stock || low_stock) {
            stock_list_->SetItemTextColour(idx, Theme::ColorError);
            stock_list_->SetItemBackgroundColour(idx, Theme::ColorErrorSoft);
        } else {
            stock_list_->SetItemTextColour(idx, Theme::ColorSuccess);
            stock_list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
        }

        if (!keep_selection_id.empty() && ing.id == keep_selection_id) {
            row_to_select = idx;
        }
        filtered_ingredients_.push_back(ing);
    }

    if (row_to_select >= 0) {
        stock_list_->SetItemState(
            row_to_select, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED
        );
    }
    sync_selection_guard_ = false;
    update_empty_states();
    update_list_column_widths();
}

void InventoryBarPanel::update_permissions_ui() {
    if (!can_view_) {
        if (keypad_) {
            keypad_->hide_keypad(false);
        }
        stock_card_->Hide();
        mov_card_->Hide();
        action_card_->Hide();
        pnl_no_permission_->Show();
        return;
    }

    pnl_no_permission_->Hide();
    stock_card_->Show();
    mov_card_->Show();
    action_card_->Show();

    if (!can_adjust_) {
        if (keypad_) {
            keypad_->hide_keypad(false);
        }
        form_scroll_->Hide();
        lbl_no_adjust_->Show();
        const int wrap_w = std::max(FromDIP(180), action_card_->GetClientSize().GetWidth() - FromDIP(28));
        lbl_no_adjust_->Wrap(wrap_w);
    } else {
        lbl_no_adjust_->Hide();
        form_scroll_->Show();
        const bool has_ing = !ingredients_.empty();
        enable_selector(ch_ingredient_, cb_ingredient_, has_ing);
        enable_selector(ch_unit_, cb_unit_, has_ing);
        ui::set_text_field_enabled(txt_qty_, has_ing);
        ui::set_text_field_enabled(txt_reason_, has_ing);
        btn_submit_->Enable(has_ing);
        if (!has_ing && last_feedback_.empty()) {
            set_feedback("Primero registra ingredientes en la pestaña Ingredientes.", false);
        }
        form_scroll_->FitInside();
        form_scroll_->Layout();
    }
}

void InventoryBarPanel::update_mode_ui() {
    const int btn_h_logical = Theme::is_touch() ? 48 : 36;
    const bool is_waste = (action_mode_ == ActionMode::Waste);

    if (is_waste) {
        Theme::style_button_primary(btn_mode_waste_, btn_h_logical);
        Theme::style_button_secondary(btn_mode_adjust_, btn_h_logical);
        pnl_adjust_dir_->Hide();
        btn_submit_->SetLabel(wxT("Registrar merma"));
    } else {
        Theme::style_button_secondary(btn_mode_waste_, btn_h_logical);
        Theme::style_button_primary(btn_mode_adjust_, btn_h_logical);
        pnl_adjust_dir_->Show();
        if (adjust_adds_) {
            Theme::style_button_primary(btn_dir_add_, btn_h_logical);
            Theme::style_button_secondary(btn_dir_sub_, btn_h_logical);
        } else {
            Theme::style_button_secondary(btn_dir_add_, btn_h_logical);
            Theme::style_button_primary(btn_dir_sub_, btn_h_logical);
        }
        btn_submit_->SetLabel(wxT("Registrar ajuste"));
    }

    Theme::ensure_button_min_size(btn_mode_waste_, 90, btn_h_logical);
    Theme::ensure_button_min_size(btn_mode_adjust_, 90, btn_h_logical);
    Theme::ensure_button_min_size(btn_dir_add_, 90, btn_h_logical);
    Theme::ensure_button_min_size(btn_dir_sub_, 90, btn_h_logical);
    Theme::style_button_primary(btn_submit_, btn_h_logical);
    Theme::ensure_button_min_size(btn_submit_, 180, btn_h_logical);

    if (form_scroll_) {
        form_scroll_->FitInside();
        form_scroll_->Layout();
    }
    if (action_card_) {
        action_card_->Layout();
    }
}

void InventoryBarPanel::populate_ingredient_selector(const std::string& keep_id) {
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

void InventoryBarPanel::update_unit_choices_for_selected() {
    current_units_ = compatible_units_for_dimension(selected_ingredient_dimension());
    const int select_idx = current_units_.empty() ? wxNOT_FOUND : 0;
    set_selector_items(ch_unit_, cb_unit_, current_units_, select_idx);
}

void InventoryBarPanel::update_reason_counter() {
    if (!lbl_reason_count_ || !txt_reason_) {
        return;
    }
    const std::size_t len = txt_reason_->GetValue().length();
    lbl_reason_count_->SetLabel(wxString::Format(wxT("%zu / 200"), len));
}

void InventoryBarPanel::update_empty_states() {
    if (!can_view_) {
        return;
    }
    if (stock_load_failed_) {
        stock_list_->Hide();
        lbl_stock_empty_->SetLabel(wxString::FromUTF8(last_stock_error_));
        lbl_stock_empty_->SetForegroundColour(Theme::ColorError);
        lbl_stock_empty_->Show();
    } else if (filtered_ingredients_.empty()) {
        stock_list_->Hide();
        const wxString empty_msg = ingredients_.empty()
            ? wxT("Todavía no hay ingredientes registrados.")
            : wxT("Ningún ingrediente coincide con la búsqueda.");
        lbl_stock_empty_->SetLabel(empty_msg);
        lbl_stock_empty_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_stock_empty_->Show();
    } else {
        lbl_stock_empty_->Hide();
        stock_list_->Show();
    }
    if (lbl_stock_empty_->IsShown() && stock_card_) {
        lbl_stock_empty_->Wrap(std::max(FromDIP(160), stock_card_->GetClientSize().GetWidth() - FromDIP(32)));
    }

    if (mov_load_failed_) {
        mov_list_->Hide();
        lbl_mov_empty_->SetLabel(wxString::FromUTF8(last_mov_error_));
        lbl_mov_empty_->SetForegroundColour(Theme::ColorError);
        lbl_mov_empty_->Show();
    } else if (movements_.empty()) {
        mov_list_->Hide();
        lbl_mov_empty_->SetLabel(wxT("Todavía no hay mermas ni ajustes registrados."));
        lbl_mov_empty_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_mov_empty_->Show();
    } else {
        lbl_mov_empty_->Hide();
        mov_list_->Show();
    }
    if (lbl_mov_empty_->IsShown() && mov_card_) {
        lbl_mov_empty_->Wrap(std::max(FromDIP(160), mov_card_->GetClientSize().GetWidth() - FromDIP(32)));
    }
}

void InventoryBarPanel::update_list_column_widths() {
    if (destroying_ || sizing_columns_) {
        return;
    }
    sizing_columns_ = true;

    if (stock_list_ && stock_list_->IsShown() && stock_list_->GetClientSize().GetWidth() > 0) {
        wxClientDC dc(stock_list_);
        dc.SetFont(stock_list_->GetFont());
        int max_name = dc.GetTextExtent(wxT("Ingrediente")).GetWidth();
        int max_stock = dc.GetTextExtent(wxT("Existencia")).GetWidth();
        const int status_w = std::max(
            dc.GetTextExtent(wxT("Suficiente")).GetWidth(),
            dc.GetTextExtent(wxT("Agotado")).GetWidth()
        );
        for (const auto& ing : filtered_ingredients_) {
            max_name = std::max(max_name, dc.GetTextExtent(wxString::FromUTF8(ing.name)).GetWidth());
            max_stock = std::max(
                max_stock,
                dc.GetTextExtent(wxString::FromUTF8(format_bar_quantity(ing.current_stock_base, ing.unit_dimension))).GetWidth()
            );
        }
        const int pad = 18;
        std::vector<ui::ColumnSpec> specs;
        specs.push_back({wxT("Ingrediente"), max_name, 0, pad, 0});
        specs.push_back({wxT("Existencia"), max_stock, 0, pad, 0});
        specs.push_back({wxT("Estado"), status_w, 0, pad, 0});
        ui::distribute_list_columns(stock_list_, specs, 0);
    }

    if (mov_list_ && mov_list_->IsShown() && mov_list_->GetClientSize().GetWidth() > 0) {
        wxClientDC dc(mov_list_);
        dc.SetFont(mov_list_->GetFont());
        const int hdr_date = dc.GetTextExtent(wxT("Fecha")).GetWidth();
        const int hdr_ing = dc.GetTextExtent(wxT("Ingrediente")).GetWidth();
        const int hdr_qty = dc.GetTextExtent(wxT("Cantidad")).GetWidth();
        const int hdr_reason = dc.GetTextExtent(wxT("Motivo")).GetWidth();
        const int hdr_actor = dc.GetTextExtent(wxT("Quién")).GetWidth();

        int max_date = std::max(hdr_date, dc.GetTextExtent(wxT("2026-11-16 20:30")).GetWidth());
        int max_ing = hdr_ing;
        int max_qty = hdr_qty;
        int max_reason = hdr_reason;
        int max_actor = hdr_actor;
        for (const auto& m : movements_) {
            const std::string date_short = m.created_at.size() >= 16 ? m.created_at.substr(0, 16) : m.created_at;
            max_date = std::max(max_date, dc.GetTextExtent(wxString::FromUTF8(date_short)).GetWidth());
            max_ing = std::max(max_ing, dc.GetTextExtent(wxString::FromUTF8(m.ingredient_name)).GetWidth());
            max_qty = std::max(
                max_qty,
                dc.GetTextExtent(wxString::FromUTF8(format_signed_movement(m.quantity_base_units, m.base_unit))).GetWidth()
            );
            max_reason = std::max(max_reason, dc.GetTextExtent(wxString::FromUTF8(m.reason)).GetWidth());
            const std::string actor = m.actor_name.empty() ? "—" : m.actor_name;
            max_actor = std::max(max_actor, dc.GetTextExtent(wxString::FromUTF8(actor)).GetWidth());
        }
        const int pad = 16;
        const int pad_px = FromDIP(pad);
        const int avail = mov_list_->GetClientSize().GetWidth() - FromDIP(18);
        int cap_ing = max_ing;
        int cap_actor = max_actor;
        const int need_fixed = (max_date + pad_px) + (max_qty + pad_px) + (max_reason + pad_px);
        const int total_need = need_fixed + (cap_ing + pad_px) + (cap_actor + pad_px);
        if (avail > 0 && total_need > avail) {
            int over = total_need - avail;
            const int ing_soft_min = std::max(hdr_ing, FromDIP(95));
            const int cut_ing_soft = std::min(over, std::max(0, cap_ing - ing_soft_min));
            cap_ing -= cut_ing_soft;
            over -= cut_ing_soft;
            if (over > 0) {
                const int act_soft_min = std::max(hdr_actor, FromDIP(85));
                const int cut_act_soft = std::min(over, std::max(0, cap_actor - act_soft_min));
                cap_actor -= cut_act_soft;
                over -= cut_act_soft;
            }
            if (over > 0) {
                const int cut_ing_hard = std::min(over, std::max(0, cap_ing - hdr_ing));
                cap_ing -= cut_ing_hard;
                over -= cut_ing_hard;
            }
            if (over > 0) {
                const int cut_act_hard = std::min(over, std::max(0, cap_actor - hdr_actor));
                cap_actor -= cut_act_hard;
            }
        }
        std::vector<ui::ColumnSpec> specs;
        specs.push_back({wxT("Fecha"), max_date, 0, pad, 0});
        specs.push_back({wxT("Ingrediente"), cap_ing, 0, pad, 0});
        specs.push_back({wxT("Cantidad"), max_qty, 0, pad, 0});
        specs.push_back({wxT("Motivo"), max_reason, 0, pad, 0});
        specs.push_back({wxT("Quién"), cap_actor, 0, pad, 0});
        ui::distribute_list_columns(mov_list_, specs, 3);
        const int cw = mov_list_->GetClientSize().GetWidth();
        if (cw > 0) {
            int sum = 0;
            for (int c = 0; c < 5; ++c) sum += mov_list_->GetColumnWidth(c);
            int excess = sum - (cw - 2);
            const int hdrs[5] = {hdr_date, hdr_ing, hdr_qty, hdr_reason, hdr_actor};
            const int order[5] = {3, 1, 4, 2, 0};
            for (int idx : order) {
                if (excess <= 0) break;
                const int cur_w = mov_list_->GetColumnWidth(idx);
                const int floor_w = hdrs[idx] + FromDIP(8);
                const int cut = std::min(excess, std::max(0, cur_w - floor_w));
                if (cut > 0) {
                    mov_list_->SetColumnWidth(idx, cur_w - cut);
                    excess -= cut;
                }
            }
        }
    }

    sizing_columns_ = false;
}

void InventoryBarPanel::set_feedback(const std::string& message, bool is_error) {
    last_feedback_ = message;
    last_feedback_is_error_ = is_error;
    if (!lbl_feedback_) {
        return;
    }
    lbl_feedback_->SetForegroundColour(is_error ? Theme::ColorError : Theme::ColorSuccess);
    lbl_feedback_->SetLabel(wxString::FromUTF8(message));
    if (form_scroll_) {
        const int wrap_w = std::max(FromDIP(180), form_scroll_->GetClientSize().GetWidth() - FromDIP(16));
        lbl_feedback_->Wrap(wrap_w);
        form_scroll_->FitInside();
        form_scroll_->Layout();
    }
}

void InventoryBarPanel::on_refresh(wxCommandEvent&) {
    if (destroying_) return;
    refresh_data();
}

void InventoryBarPanel::on_search_changed(wxCommandEvent&) {
    if (destroying_) return;
    apply_stock_filter(selected_ingredient_id());
    Layout();
}

void InventoryBarPanel::on_stock_selected(wxListEvent& event) {
    if (destroying_ || sync_selection_guard_) {
        return;
    }
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= filtered_ingredients_.size()) {
        return;
    }
    const std::string chosen_id = filtered_ingredients_[static_cast<std::size_t>(row)].id;
    for (std::size_t i = 0; i < ingredients_.size(); ++i) {
        if (ingredients_[i].id == chosen_id) {
            set_selector_index(ch_ingredient_, cb_ingredient_, static_cast<int>(i));
            update_unit_choices_for_selected();
            if (form_scroll_) {
                form_scroll_->Layout();
            }
            break;
        }
    }
}

void InventoryBarPanel::update_movement_detail(long selected_row) {
    if (!lbl_mov_detail_) {
        return;
    }
    if (selected_row < 0 || static_cast<std::size_t>(selected_row) >= movements_.size()) {
        last_mov_detail_.clear();
        lbl_mov_detail_->Hide();
        if (mov_list_) {
            mov_list_->UnsetToolTip();
        }
        if (mov_card_) {
            mov_card_->Layout();
        }
        return;
    }
    const auto& m = movements_[static_cast<std::size_t>(selected_row)];
    const std::string qty_str = format_signed_movement(m.quantity_base_units, m.base_unit);
    const std::string reason_text = m.reason.empty() ? "Sin motivo" : m.reason;
    last_mov_detail_ = "Motivo (" + m.ingredient_name + ", " + qty_str + "): " + reason_text;
    const wxString detail_wx = wxString::FromUTF8(last_mov_detail_);
    lbl_mov_detail_->SetLabel(detail_wx);
    if (mov_card_) {
        const int wrap_w = std::max(FromDIP(180), mov_card_->GetClientSize().GetWidth() - FromDIP(24));
        lbl_mov_detail_->Wrap(wrap_w);
    }
    lbl_mov_detail_->Show();
    if (mov_list_) {
        mov_list_->SetToolTip(detail_wx);
    }
    if (mov_card_) {
        mov_card_->Layout();
    }
    update_list_column_widths();
}

void InventoryBarPanel::on_movement_selected(wxListEvent& event) {
    if (destroying_) return;
    update_movement_detail(event.GetIndex());
}

void InventoryBarPanel::on_movement_deselected(wxListEvent&) {
    if (destroying_) return;
    update_movement_detail(-1);
}

void InventoryBarPanel::on_ingredient_changed(wxCommandEvent&) {
    if (destroying_) return;
    update_unit_choices_for_selected();
    if (form_scroll_) {
        form_scroll_->Layout();
    }
}

void InventoryBarPanel::on_mode_waste(wxCommandEvent&) {
    if (destroying_) return;
    action_mode_ = ActionMode::Waste;
    update_mode_ui();
}

void InventoryBarPanel::on_mode_adjustment(wxCommandEvent&) {
    if (destroying_) return;
    action_mode_ = ActionMode::Adjustment;
    update_mode_ui();
}

void InventoryBarPanel::on_adjust_add(wxCommandEvent&) {
    if (destroying_) return;
    adjust_adds_ = true;
    update_mode_ui();
}

void InventoryBarPanel::on_adjust_subtract(wxCommandEvent&) {
    if (destroying_) return;
    adjust_adds_ = false;
    update_mode_ui();
}

void InventoryBarPanel::on_reason_changed(wxCommandEvent&) {
    if (destroying_) return;
    update_reason_counter();
}

void InventoryBarPanel::on_submit(wxCommandEvent&) {
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

    const std::string unit = selected_unit_code();
    const std::string reason = trim_ascii(std::string(txt_reason_->GetValue().ToUTF8().data()));

    contracts::OperationResult res = contracts::OperationResult::ok();
    if (action_mode_ == ActionMode::Waste) {
        res = inventory_service_->register_waste(ing_id, qty, unit, reason);
    } else {
        const std::int64_t signed_qty = adjust_adds_ ? qty : -qty;
        res = inventory_service_->register_adjustment(ing_id, signed_qty, unit, reason);
    }

    if (!res.is_success()) {
        set_feedback(res.error_message(), true);
        return;
    }

    txt_qty_->ChangeValue(wxEmptyString);
    txt_reason_->ChangeValue(wxEmptyString);
    update_reason_counter();
    refresh_data(ing_id);
    set_feedback(
        action_mode_ == ActionMode::Waste ? "Merma registrada." : "Ajuste registrado.",
        false
    );
}

void InventoryBarPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted()) {
            return;
        }
        update_empty_states();
        if (lbl_mov_detail_ && lbl_mov_detail_->IsShown() && mov_card_) {
            const int wrap_w = std::max(FromDIP(180), mov_card_->GetClientSize().GetWidth() - FromDIP(24));
            lbl_mov_detail_->SetLabel(wxString::FromUTF8(last_mov_detail_));
            lbl_mov_detail_->Wrap(wrap_w);
            mov_card_->Layout();
        }
        update_list_column_widths();
        if (lbl_no_adjust_ && lbl_no_adjust_->IsShown() && action_card_) {
            const int wrap_w = std::max(FromDIP(180), action_card_->GetClientSize().GetWidth() - FromDIP(28));
            lbl_no_adjust_->SetLabel(wxT("Para registrar mermas o ajustes pide el permiso al Dueño."));
            lbl_no_adjust_->Wrap(wrap_w);
        }
        if (lbl_feedback_ && form_scroll_) {
            const int wrap_w = std::max(FromDIP(180), form_scroll_->GetClientSize().GetWidth() - FromDIP(16));
            lbl_feedback_->SetLabel(wxString::FromUTF8(last_feedback_));
            lbl_feedback_->Wrap(wrap_w);
            form_scroll_->FitInside();
            form_scroll_->Layout();
        }
        Layout();
    });
}

std::string InventoryBarPanel::selected_ingredient_id() const {
    const int idx = get_selector_index(ch_ingredient_, cb_ingredient_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ingredients_.size()) {
        return ingredients_[static_cast<std::size_t>(idx)].id;
    }
    return "";
}

std::string InventoryBarPanel::selected_ingredient_dimension() const {
    const int idx = get_selector_index(ch_ingredient_, cb_ingredient_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ingredients_.size()) {
        return ingredients_[static_cast<std::size_t>(idx)].unit_dimension;
    }
    return "MASS";
}

std::string InventoryBarPanel::selected_unit_code() const {
    const int idx = get_selector_index(ch_unit_, cb_unit_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < current_units_.size()) {
        return current_units_[static_cast<std::size_t>(idx)];
    }
    return current_units_.empty() ? "g" : current_units_.front();
}

} // namespace app::frontend
