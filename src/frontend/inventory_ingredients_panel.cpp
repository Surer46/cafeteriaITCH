#include "src/frontend/inventory_ingredients_panel.hpp"

#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <algorithm>
#include <limits>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

namespace app::frontend {

namespace {

std::string format_scaled_three_decimals(std::uint64_t whole, std::uint64_t millis, const char* unit) {
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

std::string friendly_dimension_label(const std::string& unit_dimension) {
    if (unit_dimension == "MASS") {
        return "Peso";
    }
    if (unit_dimension == "VOLUME") {
        return "Volumen";
    }
    if (unit_dimension == "COUNT") {
        return "Piezas";
    }
    return "Peso";
}

std::string friendly_threshold_unit(const std::string& unit_dimension) {
    if (unit_dimension == "MASS") {
        return "gramos";
    }
    if (unit_dimension == "VOLUME") {
        return "mililitros";
    }
    return "piezas";
}

std::string threshold_base_to_input_string(std::int64_t threshold_base, const std::string& unit_dimension) {
    if (threshold_base <= 0) {
        return "0";
    }
    if (unit_dimension == "MASS") {
        const std::uint64_t mag = static_cast<std::uint64_t>(threshold_base);
        const std::uint64_t grams = (mag + 500ULL) / 1000ULL;
        return std::to_string(grams);
    }
    return std::to_string(threshold_base);
}

} // namespace

std::string format_ingredient_quantity(std::int64_t amount_base, const std::string& unit_dimension) {
    const bool negative = amount_base < 0;
    const std::uint64_t mag = negative
        ? static_cast<std::uint64_t>(-(amount_base + 1)) + 1ULL
        : static_cast<std::uint64_t>(amount_base);
    const std::string prefix = negative ? "-" : "";

    if (unit_dimension == "MASS") {
        if (mag < 1000ULL) {
            return prefix + std::to_string(mag) + " mg";
        }
        if (mag < 1000000ULL) {
            const std::uint64_t whole = mag / 1000ULL;
            const std::uint64_t millis = mag % 1000ULL;
            return prefix + format_scaled_three_decimals(whole, millis, "g");
        }
        std::uint64_t whole = mag / 1000000ULL;
        const std::uint64_t rem = mag % 1000000ULL;
        std::uint64_t millis = (rem + 500ULL) / 1000ULL;
        if (millis >= 1000ULL) {
            ++whole;
            millis = 0;
        }
        return prefix + format_scaled_three_decimals(whole, millis, "kg");
    }

    if (unit_dimension == "VOLUME") {
        if (mag < 1000ULL) {
            return prefix + std::to_string(mag) + " ml";
        }
        const std::uint64_t whole = mag / 1000ULL;
        const std::uint64_t millis = mag % 1000ULL;
        return prefix + format_scaled_three_decimals(whole, millis, "L");
    }

    return prefix + std::to_string(mag) + " pz";
}

bool parse_threshold_to_base(
    const std::string& raw_text,
    const std::string& unit_dimension,
    std::int64_t& out_base,
    std::string& out_error) {
    const std::string trimmed = trim_ascii(raw_text);
    if (trimmed.empty()) {
        out_error = "Escribe un número entero en el aviso (0 significa sin aviso).";
        return false;
    }
    if (trimmed[0] == '-') {
        out_error = "El aviso debe ser un número entero igual o mayor que 0.";
        return false;
    }

    constexpr std::int64_t kMaxInt64 = std::numeric_limits<std::int64_t>::max();
    std::int64_t value = 0;
    for (unsigned char ch : trimmed) {
        if (ch < '0' || ch > '9') {
            out_error = "El aviso debe ser un número entero sin puntos ni letras (0 significa sin aviso).";
            return false;
        }
        const int digit = static_cast<int>(ch - '0');
        if (value > (kMaxInt64 - digit) / 10) {
            out_error = "El número escrito para el aviso es demasiado grande.";
            return false;
        }
        value = value * 10 + digit;
    }

    if (unit_dimension == "MASS") {
        if (value > kMaxInt64 / 1000) {
            out_error = "El número en gramos es demasiado grande para guardarse.";
            return false;
        }
        out_base = value * 1000;
        return true;
    }
    if (unit_dimension == "VOLUME" || unit_dimension == "COUNT") {
        out_base = value;
        return true;
    }

    out_error = "Selecciona cómo se mide el ingrediente.";
    return false;
}

InventoryIngredientsPanel::InventoryIngredientsPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::IInventoryService> inventory_service,
    std::shared_ptr<contracts::IAccountService> account_service)
    : wxPanel(parent, wxID_ANY),
      inventory_service_(std::move(inventory_service)),
      account_service_(std::move(account_service)) {
    SetBackgroundColour(Theme::ColorBackground);
    build_ui();
    refresh_data();
    CallAfter([this]() {
        update_list_column_widths();
        Layout();
    });
}

void InventoryIngredientsPanel::build_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? std::max(48, Theme::get_button_height()) : Theme::get_button_height();
    const int field_h = touch ? std::max(48, Theme::get_touch_target_min()) : 34;

    auto* root = new wxBoxSizer(wxVERTICAL);

    // 1. Encabezado con acciones principales
    auto* top_bar = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_title = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("Ingredientes"));
    lbl_title->SetFont(Theme::get_font_title());
    lbl_title->SetForegroundColour(Theme::ColorText);
    top_bar->Add(lbl_title, 0, wxALIGN_CENTER_VERTICAL);
    top_bar->AddStretchSpacer(1);

    btn_new_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("Nuevo ingrediente"));
    btn_new_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_new_, btn_h);
    {
        int tw = 0, th = 0;
        btn_new_->GetTextExtent(btn_new_->GetLabel(), &tw, &th);
        btn_new_->SetMinSize(wxSize(tw + (touch ? 36 : 28), btn_h));
    }
    btn_new_->Bind(wxEVT_BUTTON, &InventoryIngredientsPanel::on_new_ingredient, this);
    top_bar->Add(btn_new_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    btn_refresh_ = new wxButton(this, wxID_ANY, wxString::FromUTF8("Actualizar"));
    btn_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_refresh_, btn_h);
    {
        int tw = 0, th = 0;
        btn_refresh_->GetTextExtent(btn_refresh_->GetLabel(), &tw, &th);
        btn_refresh_->SetMinSize(wxSize(tw + (touch ? 36 : 28), btn_h));
    }
    btn_refresh_->Bind(wxEVT_BUTTON, &InventoryIngredientsPanel::on_refresh, this);
    top_bar->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(top_bar, 0, wxEXPAND);
    root->AddSpacer(touch ? 4 : 6);

    // 2. Cuerpo dividido: Lista izquierda + Detalle derecha
    auto* body = new wxBoxSizer(wxHORIZONTAL);

    left_card_ = new CardPanel(this);
    left_card_->SetMinSize(wxSize(touch ? 488 : 472, -1));
    auto* left_sizer = new wxBoxSizer(wxVERTICAL);

    lbl_empty_state_ = new wxStaticText(
        left_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    lbl_empty_state_->SetFont(Theme::get_font_body());
    lbl_empty_state_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_empty_state_->Hide();
    left_sizer->Add(lbl_empty_state_, 1, wxEXPAND | wxALL, 16);

    list_ = new wxListView(left_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->SetFont(Theme::get_font_body());
    list_->SetBackgroundColour(Theme::ColorSurface);
    list_->SetForegroundColour(Theme::ColorText);
    list_->InsertColumn(0, wxString::FromUTF8("Nombre"));
    list_->InsertColumn(1, wxString::FromUTF8("Existencia"));
    list_->InsertColumn(2, wxString::FromUTF8("Avisar con menos de"));
    list_->InsertColumn(3, wxString::FromUTF8("Estado"));
    if (touch) {
        ui::apply_touch_row_height(list_);
    }
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryIngredientsPanel::on_list_selected, this);
    left_sizer->Add(list_, 1, wxEXPAND | wxALL, 4);

    left_card_->SetSizer(left_sizer);
    body->Add(left_card_, 14, wxEXPAND | wxRIGHT, 6);

    // 3. Tarjeta de detalle derecha
    right_card_ = new CardPanel(this);
    right_card_->SetMinSize(wxSize(202, -1));
    auto* right_sizer = new wxBoxSizer(wxVERTICAL);

    lbl_form_title_ = new wxStaticText(right_card_, wxID_ANY, wxString::FromUTF8("Ingrediente"));
    lbl_form_title_->SetFont(Theme::get_font_subtitle());
    lbl_form_title_->SetForegroundColour(Theme::ColorText);
    right_sizer->Add(lbl_form_title_, 0, wxEXPAND | wxBOTTOM, 2);

    pnl_warning_ = new wxPanel(right_card_, wxID_ANY);
    pnl_warning_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* warn_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_warning_ = new wxStaticText(pnl_warning_, wxID_ANY, wxEmptyString);
    lbl_warning_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_warning_->SetForegroundColour(Theme::ColorError);
    warn_sizer->Add(lbl_warning_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 4);
    pnl_warning_->SetSizer(warn_sizer);
    pnl_warning_->Hide();
    right_sizer->Add(pnl_warning_, 0, wxEXPAND | wxBOTTOM, 4);

    auto* form_grid = new wxFlexGridSizer(2, 3, 4);
    form_grid->AddGrowableCol(1, 1);

    // Nombre
    auto* lbl_name = new wxStaticText(right_card_, wxID_ANY, wxString::FromUTF8("Nombre:"));
    lbl_name->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_name->SetForegroundColour(Theme::ColorText);
    form_grid->Add(lbl_name, 0, wxALIGN_CENTER_VERTICAL);

    wxWindow* name_box = ui::make_text_field(right_card_, txt_name_, field_h, -1, Theme::get_font_body());
    name_box->SetMinSize(wxSize(90, name_box->GetMinSize().GetHeight()));
    form_grid->Add(name_box, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    // Cómo se mide
    auto* lbl_dim = new wxStaticText(right_card_, wxID_ANY, wxString::FromUTF8("Cómo se\nmide:"));
    lbl_dim->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_dim->SetForegroundColour(Theme::ColorText);
    form_grid->Add(lbl_dim, 0, wxALIGN_CENTER_VERTICAL);

    auto* dim_holder = new wxBoxSizer(wxHORIZONTAL);
    wxArrayString dim_choices;
    dim_choices.Add(wxString::FromUTF8("Peso"));
    dim_choices.Add(wxString::FromUTF8("Volumen"));
    dim_choices.Add(wxString::FromUTF8("Piezas"));
    ch_dimension_ = new wxChoice(right_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, dim_choices);
    ch_dimension_->SetFont(Theme::get_font_body());
    ch_dimension_->SetSelection(0);
    ch_dimension_->SetMinSize(wxSize(96, field_h));
    auto sync_touch_choice_height = [choice = ch_dimension_, field_h]() {
#ifdef __WXMSW__
        if (choice == nullptr || !Theme::is_touch()) {
            return;
        }
        HWND hwnd = reinterpret_cast<HWND>(choice->GetHWND());
        if (hwnd == nullptr) {
            return;
        }
        ::SendMessageW(hwnd, CB_SETITEMHEIGHT, static_cast<WPARAM>(0), 40);
        RECT rc{};
        if (!::GetWindowRect(hwnd, &rc)) {
            return;
        }
        const int cur_h = rc.bottom - rc.top;
        if (cur_h >= field_h) {
            return;
        }
        const LRESULT cur_item = ::SendMessageW(hwnd, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0);
        const int border_h = (cur_item > 0 && cur_h > cur_item) ? (cur_h - static_cast<int>(cur_item)) : 6;
        const int desired_item = field_h - border_h;
        if (desired_item > 0) {
            ::SendMessageW(hwnd, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), static_cast<LPARAM>(desired_item));
        }
#else
        (void)choice;
        (void)field_h;
#endif
    };
    if (touch) {
        sync_touch_choice_height();
        ch_dimension_->Bind(wxEVT_SIZE, [sync_touch_choice_height](wxSizeEvent& ev) {
            ev.Skip();
            sync_touch_choice_height();
        });
        ch_dimension_->Bind(wxEVT_SHOW, [sync_touch_choice_height](wxShowEvent& ev) {
            ev.Skip();
            sync_touch_choice_height();
        });
        CallAfter(sync_touch_choice_height);
    }
    ch_dimension_->Bind(wxEVT_CHOICE, &InventoryIngredientsPanel::on_dimension_changed, this);

    pnl_dimension_ro_ = new wxPanel(right_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
    pnl_dimension_ro_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    pnl_dimension_ro_->SetMinSize(wxSize(96, field_h));
    auto* dim_ro_sizer = new wxBoxSizer(wxVERTICAL);
    dim_ro_sizer->AddStretchSpacer(1);
    lbl_dimension_ro_ = new wxStaticText(pnl_dimension_ro_, wxID_ANY, wxString::FromUTF8("Peso"));
    lbl_dimension_ro_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_dimension_ro_->SetForegroundColour(Theme::ColorTextSecondary);
    dim_ro_sizer->Add(lbl_dimension_ro_, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    dim_ro_sizer->AddStretchSpacer(1);
    pnl_dimension_ro_->SetSizer(dim_ro_sizer);
    pnl_dimension_ro_->Hide();

    dim_holder->Add(ch_dimension_, 1, wxALIGN_CENTER_VERTICAL);
    dim_holder->Add(pnl_dimension_ro_, 1, wxALIGN_CENTER_VERTICAL);
    form_grid->Add(dim_holder, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    right_sizer->Add(form_grid, 0, wxEXPAND);

    // Avisar con menos de (etiqueta encima del campo para caber en 960x600 Touch)
    auto* lbl_thresh = new wxStaticText(
        right_card_, wxID_ANY, wxString::FromUTF8("Avisar con menos de:"));
    lbl_thresh->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_thresh->SetForegroundColour(Theme::ColorText);
    right_sizer->Add(lbl_thresh, 0, wxEXPAND | wxTOP, 4);

    auto* thresh_row = new wxBoxSizer(wxHORIZONTAL);
    wxWindow* thresh_box = ui::make_text_field(right_card_, txt_threshold_, field_h, 60, Theme::get_font_body());
    thresh_row->Add(thresh_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    lbl_threshold_unit_ = new wxStaticText(right_card_, wxID_ANY, wxString::FromUTF8("gramos"));
    lbl_threshold_unit_->SetFont(Theme::get_font_body());
    lbl_threshold_unit_->SetForegroundColour(Theme::ColorText);
    thresh_row->Add(lbl_threshold_unit_, 0, wxALIGN_CENTER_VERTICAL);
    right_sizer->Add(thresh_row, 0, wxEXPAND | wxTOP, 2);

    lbl_threshold_hint_ = new wxStaticText(right_card_, wxID_ANY, wxString::FromUTF8("0 significa sin aviso."));
    lbl_threshold_hint_->SetFont(Theme::get_font_small());
    lbl_threshold_hint_->SetForegroundColour(Theme::ColorTextSecondary);
    right_sizer->Add(lbl_threshold_hint_, 0, wxEXPAND | wxTOP, 2);

    // Existencia actual (solo lectura, en su propia línea para evitar recorte en 960x600)
    auto* lbl_stock_caption = new wxStaticText(right_card_, wxID_ANY, wxString::FromUTF8("Existencia actual:"));
    lbl_stock_caption->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_stock_caption->SetForegroundColour(Theme::ColorText);
    right_sizer->Add(lbl_stock_caption, 0, wxEXPAND | wxTOP, 3);

    lbl_stock_value_ = new wxStaticText(right_card_, wxID_ANY, wxString::FromUTF8("-"));
    lbl_stock_value_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_stock_value_->SetForegroundColour(Theme::ColorText);
    right_sizer->Add(lbl_stock_value_, 0, wxEXPAND | wxTOP, 1);

    lbl_stock_hint_ = new wxStaticText(
        right_card_,
        wxID_ANY,
        wxString::FromUTF8("Se modifica registrando compras o mermas."));
    lbl_stock_hint_->SetFont(Theme::get_font_small());
    lbl_stock_hint_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_stock_hint_->Wrap(180);
    right_sizer->Add(lbl_stock_hint_, 0, wxEXPAND | wxTOP | wxBOTTOM, 2);

    // Botón Guardar siempre visible sin desplazarse
    btn_save_ = new wxButton(right_card_, wxID_ANY, wxString::FromUTF8("Guardar"));
    btn_save_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_save_, btn_h);
    btn_save_->SetMinSize(wxSize(120, btn_h));
    btn_save_->Bind(wxEVT_BUTTON, &InventoryIngredientsPanel::on_save, this);
    right_sizer->Add(btn_save_, 0, wxALIGN_LEFT | wxTOP, 2);

    lbl_feedback_ = new wxStaticText(right_card_, wxID_ANY, wxEmptyString);
    lbl_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_feedback_->SetForegroundColour(Theme::ColorText);
    right_sizer->Add(lbl_feedback_, 0, wxEXPAND | wxTOP, 2);

    right_sizer->AddStretchSpacer(1);

    auto* right_outer = new wxBoxSizer(wxVERTICAL);
    right_outer->Add(right_sizer, 1, wxEXPAND | wxALL, touch ? 5 : 12);
    right_card_->SetSizer(right_outer);
    body->Add(right_card_, 8, wxEXPAND);

    root->Add(body, 1, wxEXPAND);
    SetSizer(root);

    Bind(wxEVT_SIZE, &InventoryIngredientsPanel::on_size, this);
}

void InventoryIngredientsPanel::refresh_data(const std::string& keep_selection_id) {
    can_manage_products_ = account_service_ && account_service_->has_permission("inventory.manage_products");
    can_manage_thresholds_ = account_service_ && account_service_->has_permission("inventory.manage_thresholds");

    ingredients_.clear();
    low_stock_ids_.clear();
    last_list_error_.clear();
    load_failed_ = false;
    list_->DeleteAllItems();

    if (!inventory_service_) {
        load_failed_ = true;
        last_list_error_ = "Servicio de inventario no disponible.";
        update_empty_state();
        update_permissions();
        return;
    }

    auto list_res = inventory_service_->list_ingredients();
    if (!list_res.is_success()) {
        load_failed_ = true;
        last_list_error_ = list_res.error_message();
        set_feedback(list_res.error_message(), true);
        update_empty_state();
        update_permissions();
        Layout();
        return;
    }

    ingredients_ = list_res.value();

    auto alerts_res = inventory_service_->get_stock_alerts();
    if (alerts_res.is_success()) {
        for (const auto& alert : alerts_res.value()) {
            low_stock_ids_.insert(alert.ingredient_id);
        }
    }

    for (std::size_t i = 0; i < ingredients_.size(); ++i) {
        const auto& ing = ingredients_[i];
        const long idx = list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(ing.name));
        list_->SetItem(idx, 1, wxString::FromUTF8(format_ingredient_quantity(ing.current_stock_base, ing.unit_dimension)));
        if (ing.low_stock_threshold_base <= 0) {
            list_->SetItem(idx, 2, wxString::FromUTF8("Sin aviso"));
        } else {
            list_->SetItem(idx, 2, wxString::FromUTF8(
                format_ingredient_quantity(ing.low_stock_threshold_base, ing.unit_dimension)));
        }

        const bool low = low_stock_ids_.count(ing.id) > 0;
        list_->SetItem(idx, 3, wxString::FromUTF8(low ? "Poco inventario" : "Suficiente"));
        if (low) {
            list_->SetItemTextColour(idx, Theme::ColorError);
            list_->SetItemBackgroundColour(idx, Theme::ColorErrorSoft);
        } else {
            list_->SetItemTextColour(idx, Theme::ColorSuccess);
            list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
        }
    }

    std::string target_id = keep_selection_id.empty() ? selected_ingredient_id_ : keep_selection_id;
    long selected_row = -1;
    if (!target_id.empty()) {
        for (std::size_t i = 0; i < ingredients_.size(); ++i) {
            if (ingredients_[i].id == target_id) {
                selected_row = static_cast<long>(i);
                break;
            }
        }
    }
    if (selected_row < 0 && !ingredients_.empty() && !create_mode_) {
        selected_row = 0;
    }

    if (selected_row >= 0) {
        create_mode_ = false;
        list_->SetItemState(selected_row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        load_ingredient_into_form(ingredients_[static_cast<std::size_t>(selected_row)]);
    } else if (!create_mode_) {
        selected_ingredient_id_.clear();
        txt_name_->ChangeValue(wxEmptyString);
        txt_threshold_->ChangeValue(wxEmptyString);
        lbl_dimension_ro_->SetLabel(wxString::FromUTF8("-"));
        lbl_threshold_unit_->SetLabel(wxString::FromUTF8("gramos"));
        lbl_stock_value_->SetLabel(wxString::FromUTF8("-"));
        lbl_form_title_->SetLabel(wxString::FromUTF8("Ingrediente"));
    }

    update_empty_state();
    update_permissions();
    Layout();
    update_list_column_widths();
}

void InventoryIngredientsPanel::update_empty_state() {
    if (load_failed_) {
        list_->Hide();
        lbl_empty_state_->SetLabel(wxString::FromUTF8(last_list_error_));
        lbl_empty_state_->SetForegroundColour(Theme::ColorError);
        lbl_empty_state_->Show();
        const int wrap_w = std::max(180, left_card_->GetClientSize().GetWidth() - 40);
        lbl_empty_state_->Wrap(wrap_w);
    } else if (ingredients_.empty()) {
        list_->Hide();
        const wxString empty_msg = can_manage_products_
            ? wxString::FromUTF8("Todavía no hay ingredientes registrados.\n\nPresiona «Nuevo ingrediente» para agregar el primero.")
            : wxString::FromUTF8("Todavía no hay ingredientes registrados.");
        lbl_empty_state_->SetLabel(empty_msg);
        lbl_empty_state_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_empty_state_->Show();
        const int wrap_w = std::max(180, left_card_->GetClientSize().GetWidth() - 40);
        lbl_empty_state_->Wrap(wrap_w);
    } else {
        lbl_empty_state_->Hide();
        list_->Show();
    }
    left_card_->Layout();
}

void InventoryIngredientsPanel::update_permissions() {
    const int warn_wrap_w = std::max(160, right_card_->GetClientSize().GetWidth() - (Theme::is_touch() ? 18 : 28));
    if (load_failed_) {
        btn_new_->Enable(false);
        ui::set_text_field_enabled(txt_name_, false);
        ch_dimension_->Show(false);
        pnl_dimension_ro_->Show(true);
        ui::set_text_field_enabled(txt_threshold_, false);
        btn_save_->Enable(false);
        lbl_warning_->SetLabel(wxString::FromUTF8("Solo lectura: no se pudo consultar el inventario."));
        lbl_warning_->Wrap(warn_wrap_w);
        pnl_warning_->Show(true);
        lbl_form_title_->Show(false);
        right_card_->Layout();
        return;
    }

    const bool read_only = !can_manage_products_ && !can_manage_thresholds_;
    if (read_only) {
        lbl_warning_->SetLabel(wxString::FromUTF8("Solo lectura: no tienes permiso para modificar ingredientes."));
        lbl_warning_->Wrap(warn_wrap_w);
        pnl_warning_->Show(true);
        lbl_form_title_->Show(false);
    } else {
        pnl_warning_->Hide();
        lbl_form_title_->Show(true);
    }

    btn_new_->Enable(can_manage_products_);

    if (create_mode_) {
        ui::set_text_field_enabled(txt_name_, can_manage_products_);
        ch_dimension_->Show(true);
        ch_dimension_->Enable(can_manage_products_);
        pnl_dimension_ro_->Show(false);
        ui::set_text_field_enabled(txt_threshold_, can_manage_thresholds_);
        btn_save_->Enable(can_manage_products_);
    } else if (!selected_ingredient_id_.empty()) {
        ui::set_text_field_enabled(txt_name_, can_manage_products_);
        ch_dimension_->Show(false);
        pnl_dimension_ro_->Show(true);
        ui::set_text_field_enabled(txt_threshold_, can_manage_thresholds_);
        btn_save_->Enable(can_manage_products_ || can_manage_thresholds_);
    } else {
        ui::set_text_field_enabled(txt_name_, false);
        ch_dimension_->Show(false);
        pnl_dimension_ro_->Show(true);
        ui::set_text_field_enabled(txt_threshold_, false);
        btn_save_->Enable(false);
    }

    right_card_->Layout();
}

void InventoryIngredientsPanel::load_ingredient_into_form(const contracts::IngredientDto& ing) {
    create_mode_ = false;
    selected_ingredient_id_ = ing.id;
    lbl_form_title_->SetLabel(wxString::FromUTF8("Editar ingrediente"));

    txt_name_->ChangeValue(wxString::FromUTF8(ing.name));
    ch_dimension_->SetSelection(dimension_choice_index(ing.unit_dimension));
    lbl_dimension_ro_->SetLabel(wxString::FromUTF8(friendly_dimension_label(ing.unit_dimension)));
    txt_threshold_->ChangeValue(wxString::FromUTF8(
        threshold_base_to_input_string(ing.low_stock_threshold_base, ing.unit_dimension)));
    lbl_threshold_unit_->SetLabel(wxString::FromUTF8(friendly_threshold_unit(ing.unit_dimension)));
    lbl_stock_value_->SetLabel(wxString::FromUTF8(
        format_ingredient_quantity(ing.current_stock_base, ing.unit_dimension)));
    set_feedback("", false);
    update_permissions();
}

void InventoryIngredientsPanel::enter_create_mode() {
    create_mode_ = true;
    selected_ingredient_id_.clear();
    const long sel = list_->GetFirstSelected();
    if (sel >= 0) {
        list_->SetItemState(sel, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }

    lbl_form_title_->SetLabel(wxString::FromUTF8("Nuevo ingrediente"));
    txt_name_->ChangeValue(wxEmptyString);
    ch_dimension_->SetSelection(0);
    txt_threshold_->ChangeValue(wxString::FromUTF8("0"));
    lbl_stock_value_->SetLabel(wxString::FromUTF8("0 (nuevo)"));
    set_feedback("", false);
    update_unit_ui();
    update_permissions();
    if (can_manage_products_) {
        txt_name_->SetFocus();
    }
}

void InventoryIngredientsPanel::update_unit_ui() {
    const std::string dim = selected_dimension_code();
    lbl_threshold_unit_->SetLabel(wxString::FromUTF8(friendly_threshold_unit(dim)));
    lbl_dimension_ro_->SetLabel(wxString::FromUTF8(friendly_dimension_label(dim)));
    right_card_->Layout();
}

void InventoryIngredientsPanel::update_list_column_widths() {
    if (sizing_columns_ || list_ == nullptr || !list_->IsShown()) {
        return;
    }
    if (list_->GetClientSize().GetWidth() <= 0) {
        return;
    }

    sizing_columns_ = true;

    wxClientDC dc(list_);
    dc.SetFont(list_->GetFont());

    int max_name = dc.GetTextExtent(wxString::FromUTF8("Ingrediente de ejemplo")).GetWidth();
    int max_stock = dc.GetTextExtent(wxString::FromUTF8("250.5 kg")).GetWidth();
    int max_thresh = dc.GetTextExtent(wxString::FromUTF8("Sin aviso")).GetWidth();
    const int status_w = std::max(
        dc.GetTextExtent(wxString::FromUTF8("Poco inventario")).GetWidth(),
        dc.GetTextExtent(wxString::FromUTF8("Suficiente")).GetWidth());

    for (const auto& ing : ingredients_) {
        const int nw = dc.GetTextExtent(wxString::FromUTF8(ing.name)).GetWidth();
        if (nw > max_name) {
            max_name = nw;
        }
        const int sw = dc.GetTextExtent(
            wxString::FromUTF8(format_ingredient_quantity(ing.current_stock_base, ing.unit_dimension))).GetWidth();
        if (sw > max_stock) {
            max_stock = sw;
        }
        const std::string thresh_txt = ing.low_stock_threshold_base <= 0
            ? "Sin aviso"
            : format_ingredient_quantity(ing.low_stock_threshold_base, ing.unit_dimension);
        const int tw = dc.GetTextExtent(wxString::FromUTF8(thresh_txt)).GetWidth();
        if (tw > max_thresh) {
            max_thresh = tw;
        }
    }

    const int pad = 12;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxString::FromUTF8("Nombre"), max_name, 0, pad, 0});
    specs.push_back({wxString::FromUTF8("Existencia"), max_stock, max_stock, pad, 0});
    specs.push_back({wxString::FromUTF8("Avisar con menos de"), max_thresh, max_thresh, pad, 0});
    specs.push_back({wxString::FromUTF8("Estado"), status_w, status_w, pad, 0});

    ui::distribute_list_columns(list_, specs, 0);
    sizing_columns_ = false;
}

void InventoryIngredientsPanel::set_feedback(const std::string& message, bool is_error) {
    last_feedback_ = message;
    last_feedback_is_error_ = is_error;
    lbl_feedback_->SetLabel(wxString::FromUTF8(last_feedback_));
    lbl_feedback_->SetForegroundColour(last_feedback_is_error_ ? Theme::ColorError : Theme::ColorSuccess);
    const int wrap_w = std::max(160, right_card_->GetClientSize().GetWidth() - (Theme::is_touch() ? 14 : 26));
    lbl_feedback_->Wrap(wrap_w);
    right_card_->Layout();
}

void InventoryIngredientsPanel::on_new_ingredient(wxCommandEvent& /*event*/) {
    if (load_failed_ || !can_manage_products_) {
        return;
    }
    enter_create_mode();
}

void InventoryIngredientsPanel::on_refresh(wxCommandEvent& /*event*/) {
    refresh_data(selected_ingredient_id_);
}

void InventoryIngredientsPanel::on_list_selected(wxListEvent& event) {
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= ingredients_.size()) {
        return;
    }
    load_ingredient_into_form(ingredients_[static_cast<std::size_t>(row)]);
}

void InventoryIngredientsPanel::on_dimension_changed(wxCommandEvent& /*event*/) {
    update_unit_ui();
}

void InventoryIngredientsPanel::on_save(wxCommandEvent& /*event*/) {
    if (!inventory_service_ || load_failed_) {
        set_feedback("La pantalla está en solo lectura.", true);
        return;
    }

    const std::string trimmed_name = trim_ascii(std::string(txt_name_->GetValue().ToUTF8().data()));

    if (create_mode_) {
        if (!can_manage_products_) {
            set_feedback("No tienes permiso para crear ingredientes.", true);
            return;
        }
        if (trimmed_name.empty()) {
            set_feedback("Escribe el nombre del ingrediente.", true);
            return;
        }

        const std::string dim = selected_dimension_code();
        std::string base_unit = "mg";
        if (dim == "VOLUME") {
            base_unit = "ml";
        } else if (dim == "COUNT") {
            base_unit = "pz";
        }

        std::int64_t threshold_base = 0;
        if (can_manage_thresholds_) {
            std::string parse_err;
            if (!parse_threshold_to_base(
                    std::string(txt_threshold_->GetValue().ToUTF8().data()),
                    dim,
                    threshold_base,
                    parse_err)) {
                set_feedback(parse_err, true);
                return;
            }
        }

        contracts::CreateIngredientDto dto;
        dto.name = trimmed_name;
        dto.unit_dimension = dim;
        dto.base_unit = base_unit;
        dto.low_stock_threshold_base = threshold_base;

        auto res = inventory_service_->create_ingredient(dto);
        if (!res.is_success()) {
            set_feedback(res.error_message(), true);
            return;
        }

        create_mode_ = false;
        const std::string created_id = res.value().id;
        refresh_data(created_id);
        set_feedback("Ingrediente guardado.", false);
        return;
    }

    if (selected_ingredient_id_.empty()) {
        set_feedback("Selecciona un ingrediente o presiona Nuevo ingrediente.", true);
        return;
    }

    std::string existing_dim = "MASS";
    for (const auto& ing : ingredients_) {
        if (ing.id == selected_ingredient_id_) {
            existing_dim = ing.unit_dimension;
            break;
        }
    }

    std::optional<std::int64_t> parsed_threshold;
    if (can_manage_thresholds_) {
        std::int64_t threshold_val = 0;
        std::string parse_err;
        if (!parse_threshold_to_base(
                std::string(txt_threshold_->GetValue().ToUTF8().data()),
                existing_dim,
                threshold_val,
                parse_err)) {
            set_feedback(parse_err, true);
            return;
        }
        parsed_threshold = threshold_val;
    }

    if (can_manage_products_) {
        if (trimmed_name.empty()) {
            set_feedback("El nombre del ingrediente no puede quedar vacío.", true);
            return;
        }
        contracts::UpdateIngredientDto dto;
        dto.ingredient_id = selected_ingredient_id_;
        dto.name = trimmed_name;
        dto.low_stock_threshold_base = parsed_threshold;
        auto res = inventory_service_->update_ingredient(dto);
        if (!res.is_success()) {
            set_feedback(res.error_message(), true);
            return;
        }
    } else if (can_manage_thresholds_ && parsed_threshold.has_value()) {
        auto res = inventory_service_->update_low_stock_threshold(selected_ingredient_id_, *parsed_threshold);
        if (!res.is_success()) {
            set_feedback(res.error_message(), true);
            return;
        }
    } else {
        set_feedback("No tienes permiso para modificar ingredientes.", true);
        return;
    }

    const std::string keep_id = selected_ingredient_id_;
    refresh_data(keep_id);
    set_feedback("Cambios guardados.", false);
}

void InventoryIngredientsPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    CallAfter([this]() {
        update_empty_state();
        update_list_column_widths();
        if (lbl_warning_ != nullptr && pnl_warning_ != nullptr && pnl_warning_->IsShown() && right_card_ != nullptr) {
            const int warn_w = std::max(160, right_card_->GetClientSize().GetWidth() - (Theme::is_touch() ? 18 : 28));
            const wxString warn_txt = load_failed_
                ? wxString::FromUTF8("Solo lectura: no se pudo consultar el inventario.")
                : wxString::FromUTF8("Solo lectura: no tienes permiso para modificar ingredientes.");
            lbl_warning_->SetLabel(warn_txt);
            lbl_warning_->Wrap(warn_w);
        }
        if (lbl_feedback_ != nullptr && right_card_ != nullptr) {
            const int wrap_w = std::max(160, right_card_->GetClientSize().GetWidth() - (Theme::is_touch() ? 14 : 26));
            lbl_feedback_->SetLabel(wxString::FromUTF8(last_feedback_));
            lbl_feedback_->Wrap(wrap_w);
        }
        if (lbl_stock_hint_ != nullptr && right_card_ != nullptr) {
            const int wrap_w = std::max(160, right_card_->GetClientSize().GetWidth() - (Theme::is_touch() ? 14 : 26));
            lbl_stock_hint_->SetLabel(wxString::FromUTF8("Se modifica registrando compras o mermas."));
            lbl_stock_hint_->Wrap(wrap_w);
        }
        Layout();
    });
}

std::string InventoryIngredientsPanel::selected_dimension_code() const {
    const int sel = ch_dimension_ != nullptr ? ch_dimension_->GetSelection() : 0;
    if (sel == 1) {
        return "VOLUME";
    }
    if (sel == 2) {
        return "COUNT";
    }
    return "MASS";
}

int InventoryIngredientsPanel::dimension_choice_index(const std::string& unit_dimension) const {
    if (unit_dimension == "VOLUME") {
        return 1;
    }
    if (unit_dimension == "COUNT") {
        return 2;
    }
    return 0;
}

} // namespace app::frontend
