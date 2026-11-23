#include "src/frontend/inventory_products_panel.hpp"

#include "src/frontend/inventory_ingredients_panel.hpp"
#include "src/frontend/inventory_restock_panel.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/settings.h>
#include <wx/statline.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <type_traits>
#include <utility>

namespace app::frontend {

namespace {

constexpr int kWideSplitThresholdLogical = 1100;
constexpr const char* kNewCategoryOptionUtf8 = "Nueva categoría...";

void wrap_static_text_deterministic(wxStaticText* lbl, const wxString& raw, int wrap_w) {
    if (!lbl) return;
    const int safe_w = std::max(lbl->FromDIP(100), wrap_w);
    wxClientDC dc(lbl);
    dc.SetFont(lbl->GetFont());
    wxString wrapped;
    wxString cur_line;
    int line_count = 0;
    size_t pos = 0;
    while (pos < raw.length()) {
        size_t next = raw.find(wxT(' '), pos);
        wxString word = (next == wxString::npos) ? raw.Mid(pos) : raw.Mid(pos, next - pos);
        wxString cand = cur_line.IsEmpty() ? word : (cur_line + wxT(" ") + word);
        if (!cur_line.IsEmpty() && dc.GetTextExtent(cand).GetWidth() > safe_w) {
            if (!wrapped.IsEmpty()) wrapped += wxT("\n");
            wrapped += cur_line;
            ++line_count;
            cur_line = word;
        } else {
            cur_line = cand;
        }
        if (next == wxString::npos) break;
        pos = next + 1;
    }
    if (!cur_line.IsEmpty()) {
        if (!wrapped.IsEmpty()) wrapped += wxT("\n");
        wrapped += cur_line;
        ++line_count;
    }
    line_count = std::max(1, line_count);
    const int line_h = dc.GetTextExtent(wxT("Ág")).GetHeight();
    const wxSize ext = dc.GetMultiLineTextExtent(wrapped);
    const int min_h = std::max(ext.GetHeight(), line_count * line_h) + lbl->FromDIP(2);

    lbl->SetMinSize(wxDefaultSize);
    lbl->SetLabel(wrapped);
    lbl->InvalidateBestSize();
    lbl->SetMinSize(wxSize(-1, min_h));
}

template <typename T, typename = void>
struct HasIsCombo : std::false_type {};

template <typename T>
struct HasIsCombo<T, std::void_t<decltype(std::declval<const T&>().is_combo)>> : std::true_type {};

template <typename T>
bool is_combo_product(const T& p) {
    if constexpr (HasIsCombo<T>::value) {
        return p.is_combo;
    }
    return false;
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

std::string to_lower_ascii(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
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
    if (cb) return cb->GetSelection();
    if (ch) return ch->GetSelection();
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
    if (cb) cb->Enable(enabled);
    if (ch) ch->Enable(enabled);
}

std::string format_readable_recipe_qty(std::int64_t qty_base, const std::string& unit_dimension) {
    if (unit_dimension == "COUNT") {
        return std::to_string(qty_base) + (qty_base == 1 ? " pieza" : " piezas");
    }
    if (unit_dimension == "MASS") {
        if (qty_base == 0) return "0 g";
        if (qty_base < 1000) {
            std::string dec = std::to_string(qty_base);
            while (dec.size() < 3) dec = "0" + dec;
            while (!dec.empty() && dec.back() == '0') dec.pop_back();
            return "0." + dec + " g";
        }
    }
    return format_ingredient_quantity(qty_base, unit_dimension);
}

void decompose_base_to_input(
    std::int64_t qty_base,
    const std::string& unit_dimension,
    std::string& out_qty_str,
    std::string& out_unit_code) {
    if (unit_dimension == "COUNT") {
        out_qty_str = std::to_string(std::max<std::int64_t>(1, qty_base));
        out_unit_code = "piezas";
        return;
    }
    if (unit_dimension == "VOLUME") {
        if (qty_base >= 1000 && qty_base % 1000 == 0) {
            out_qty_str = std::to_string(qty_base / 1000);
            out_unit_code = "l";
        } else {
            out_qty_str = std::to_string(std::max<std::int64_t>(1, qty_base));
            out_unit_code = "ml";
        }
        return;
    }
    if (qty_base >= 1000000 && qty_base % 1000000 == 0) {
        out_qty_str = std::to_string(qty_base / 1000000);
        out_unit_code = "kg";
    } else {
        const std::int64_t grams = std::max<std::int64_t>(1, (qty_base + 500) / 1000);
        out_qty_str = std::to_string(grams);
        out_unit_code = "g";
    }
}

std::string format_pesos_plain(contracts::Money money) {
    std::int64_t cents = money.cents();
    const bool neg = cents < 0;
    std::uint64_t mag = neg
        ? static_cast<std::uint64_t>(-(cents + 1)) + 1ULL
        : static_cast<std::uint64_t>(cents);
    std::ostringstream oss;
    if (neg) oss << "-";
    oss << (mag / 100ULL) << "." << ((mag % 100ULL) < 10ULL ? "0" : "") << (mag % 100ULL);
    return oss.str();
}

std::string format_margin_percent(std::int64_t basis_points) {
    const bool neg = basis_points < 0;
    const std::uint64_t mag = neg
        ? static_cast<std::uint64_t>(-(basis_points + 1)) + 1ULL
        : static_cast<std::uint64_t>(basis_points);
    const std::uint64_t tenths = (mag + 5ULL) / 10ULL;
    std::ostringstream oss;
    if (neg) oss << "-";
    oss << (tenths / 10ULL) << "." << (tenths % 10ULL) << "%";
    return oss.str();
}

wxBitmap make_placeholder_bitmap(const wxSize& size) {
    wxImage img(std::max(16, size.GetWidth()), std::max(16, size.GetHeight()));
    img.SetRGB(wxRect(0, 0, img.GetWidth(), img.GetHeight()), 0xE2, 0xE8, 0xF0);
    return wxBitmap(img);
}

void ensure_btn_width_fits(wxButton* btn, int min_w_logical, int min_h_logical) {
    if (!btn) return;
    const int text_w = btn->GetTextExtent(btn->GetLabel()).GetWidth();
    const int needed_w = std::max(btn->FromDIP(min_w_logical), text_w + btn->FromDIP(26));
    const int min_h = btn->FromDIP(min_h_logical);
    btn->SetMinSize(wxSize(needed_w, min_h));
    btn->SetSize(wxSize(needed_w, min_h));
}

} // namespace

InventoryProductsPanel::InventoryProductsPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::ICatalogService> catalog_service,
    std::shared_ptr<contracts::IInventoryService> inventory_service,
    std::shared_ptr<contracts::IAccountService> account_service)
    : wxPanel(parent, wxID_ANY),
      catalog_service_(std::move(catalog_service)),
      inventory_service_(std::move(inventory_service)),
      account_service_(std::move(account_service)) {
    SetBackgroundColour(Theme::ColorBackground);
    build_ui();
    start_new_product();
    refresh_all();
}

InventoryProductsPanel::~InventoryProductsPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &InventoryProductsPanel::on_size, this);
    if (list_) {
        list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryProductsPanel::on_product_selected, this);
    }
    if (recipe_list_) {
        recipe_list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryProductsPanel::on_recipe_row_selected, this);
    }
    if (keypad_) {
        keypad_->hide_keypad(false);
    }
    DestroyChildren();
}

void InventoryProductsPanel::build_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h_logical = touch ? 48 : 34;
    const int field_h_logical = touch ? 48 : 34;

    auto* root = new wxBoxSizer(wxVERTICAL);

    // Vista sin permiso inventory.manage_products
    pnl_no_permission_ = new CardPanel(this);
    static_cast<CardPanel*>(pnl_no_permission_)->set_card_background(Theme::ColorSurface);
    auto* np_sizer = new wxBoxSizer(wxVERTICAL);
    auto* np_title = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Productos y recetas"));
    np_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    np_title->SetForegroundColour(Theme::ColorBrandDeep);
    np_sizer->Add(np_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* np_msg = new wxStaticText(
        pnl_no_permission_, wxID_ANY, wxT("Sin permiso para administrar productos y recetas")
    );
    np_msg->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    np_msg->SetForegroundColour(Theme::ColorError);
    np_sizer->Add(np_msg, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto* np_sub = new wxStaticText(
        pnl_no_permission_,
        wxID_ANY,
        wxT("Para administrar productos y recetas pide el permiso al Dueño.")
    );
    np_sub->SetFont(Theme::get_font_body());
    np_sub->SetForegroundColour(Theme::ColorTextSecondary);
    np_sizer->Add(np_sub, 0, wxALL, FromDIP(20));
    pnl_no_permission_->SetSizer(np_sizer);
    pnl_no_permission_->Hide();
    root->Add(pnl_no_permission_, 1, wxEXPAND);

    // Cuerpo principal: lista izquierda + detalle/receta derecha
    main_body_ = new wxBoxSizer(wxHORIZONTAL);

    // --- Tarjeta izquierda: Catálogo de productos ---
    list_card_ = new CardPanel(this);
    list_card_->SetMinSize(FromDIP(wxSize(touch ? 372 : 352, -1)));
    auto* list_card_outer = new wxBoxSizer(wxVERTICAL);
    auto* list_sizer = new wxBoxSizer(wxVERTICAL);

    auto* list_top = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_list_title = new wxStaticText(list_card_, wxID_ANY, wxT("Productos"));
    lbl_list_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_list_title->SetForegroundColour(Theme::ColorBrandDeep);
    list_top->Add(lbl_list_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    list_top->AddStretchSpacer(1);

    btn_new_ = new wxButton(list_card_, wxID_ANY, wxT("Nuevo producto"));
    btn_new_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_new_, btn_h_logical);
    ensure_btn_width_fits(btn_new_, 115, btn_h_logical);
    btn_new_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_new_product, this);
    list_top->Add(btn_new_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    btn_refresh_ = new wxButton(list_card_, wxID_ANY, wxT("Actualizar"));
    btn_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_refresh_, btn_h_logical);
    ensure_btn_width_fits(btn_refresh_, 88, btn_h_logical);
    btn_refresh_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_refresh, this);
    list_top->Add(btn_refresh_, 0, wxALIGN_CENTER_VERTICAL);
    list_sizer->Add(list_top, 0, wxEXPAND);

    wxWindow* search_box = ui::make_text_field(
        list_card_, txt_search_, field_h_logical, 120, Theme::get_font_body()
    );
    txt_search_->SetHint(wxT("Buscar por nombre, SKU o código..."));
    txt_search_->Bind(wxEVT_TEXT, &InventoryProductsPanel::on_search_changed, this);
    list_sizer->Add(search_box, 0, wxEXPAND | wxTOP, FromDIP(touch ? 8 : 10));

    auto* filter_row = new wxBoxSizer(wxHORIZONTAL);
    if (touch) {
        cb_filter_cat_ = new ui::TouchComboBox(
            list_card_, wxID_ANY, wxSize(-1, FromDIP(field_h_logical)), wxArrayString()
        );
        cb_filter_cat_->SetFont(Theme::get_font_body());
        cb_filter_cat_->SetMinSize(wxSize(-1, FromDIP(field_h_logical)));
        cb_filter_cat_->Bind(wxEVT_COMBOBOX, &InventoryProductsPanel::on_filter_category_changed, this);
        filter_row->Add(cb_filter_cat_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    } else {
        ch_filter_cat_ = new wxChoice(list_card_, wxID_ANY);
        ch_filter_cat_->SetFont(Theme::get_font_body());
        ch_filter_cat_->SetMinSize(wxSize(-1, FromDIP(field_h_logical)));
        ch_filter_cat_->Bind(wxEVT_CHOICE, &InventoryProductsPanel::on_filter_category_changed, this);
        filter_row->Add(ch_filter_cat_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    }

    btn_toggle_inactive_ = new wxButton(list_card_, wxID_ANY, wxT("Ver dados de baja"));
    btn_toggle_inactive_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_toggle_inactive_, btn_h_logical);
    ensure_btn_width_fits(btn_toggle_inactive_, 132, btn_h_logical);
    btn_toggle_inactive_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_toggle_inactive, this);
    filter_row->Add(btn_toggle_inactive_, 0, wxALIGN_CENTER_VERTICAL);
    list_sizer->Add(filter_row, 0, wxEXPAND | wxTOP, FromDIP(touch ? 6 : 8));

    lbl_list_empty_ = new wxStaticText(
        list_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_list_empty_->SetFont(Theme::get_font_body());
    lbl_list_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_list_empty_->Hide();
    list_sizer->Add(lbl_list_empty_, 1, wxEXPAND | wxALL, FromDIP(14));

    list_ = new wxListView(
        list_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    list_->SetFont(Theme::get_font_body());
    list_->SetBackgroundColour(Theme::ColorSurface);
    list_->SetForegroundColour(Theme::ColorText);
    list_->InsertColumn(0, wxT("Nombre"));
    list_->InsertColumn(1, wxT("Categoría"));
    list_->InsertColumn(2, wxT("Precio"));
    list_->InsertColumn(3, wxT("Tipo"));
    list_->InsertColumn(4, wxT("Estado"));
    if (touch) {
        ui::apply_touch_row_height(list_);
    }
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryProductsPanel::on_product_selected, this);
    list_sizer->Add(list_, 1, wxEXPAND | wxTOP, FromDIP(touch ? 8 : 8));

    list_card_outer->Add(list_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    list_card_->SetSizer(list_card_outer);
    main_body_->Add(list_card_, 48, wxEXPAND | wxRIGHT, FromDIP(8));

    // --- Tarjeta derecha: Detalle, imagen y receta ---
    detail_card_ = new CardPanel(this);
    detail_card_->SetMinSize(FromDIP(wxSize(touch ? 320 : 336, -1)));
    auto* detail_outer = new wxBoxSizer(wxVERTICAL);

    detail_scroll_ = new wxScrolledWindow(detail_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    detail_scroll_->SetBackgroundColour(Theme::ColorSurface);
    detail_scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    detail_scroll_->SetScrollRate(0, FromDIP(14));

    detail_sizer_ = new wxBoxSizer(wxVERTICAL);

    lbl_detail_title_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("Producto nuevo"));
    lbl_detail_title_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_detail_title_->SetForegroundColour(Theme::ColorBrandDeep);
    detail_sizer_->Add(lbl_detail_title_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    // Fila 1: Nombre y Precio (etiquetas inline en pantalla ancha, apiladas cuando es estrecho)
    auto* row_np = new wxBoxSizer(wxHORIZONTAL);
    col_name_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_name_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("Nombre:"));
    lbl_name_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_name_->SetForegroundColour(Theme::ColorText);
    col_name_sizer_->Add(lbl_name_, 0, wxEXPAND);
    name_box_ = ui::make_text_field(detail_scroll_, txt_name_, field_h_logical, 110, Theme::get_font_body());
    col_name_sizer_->Add(name_box_, 0, wxEXPAND | wxTOP, FromDIP(6));
    row_np->Add(col_name_sizer_, 3, wxEXPAND | wxRIGHT, FromDIP(8));

    col_price_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_price_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("Precio ($):"));
    lbl_price_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_price_->SetForegroundColour(Theme::ColorText);
    col_price_sizer_->Add(lbl_price_, 0, wxEXPAND);
    price_box_ = ui::make_text_field(detail_scroll_, txt_price_, field_h_logical, 80, Theme::get_font_body());
    col_price_sizer_->Add(price_box_, 0, wxEXPAND | wxTOP, FromDIP(6));
    row_np->Add(col_price_sizer_, 2, wxEXPAND);
    detail_sizer_->Add(row_np, 0, wxEXPAND);

    // Fila 2: SKU y Código de barras (horizontal con reparto 2:3 cuando cabe, apilado en 960x600 estrecho)
    row_sb_sizer_ = new wxBoxSizer(wxHORIZONTAL);
    col_sku_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_sku_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("SKU:"));
    lbl_sku_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_sku_->SetForegroundColour(Theme::ColorText);
    col_sku_sizer_->Add(lbl_sku_, 0, wxEXPAND);
    wxWindow* sku_box = ui::make_text_field(detail_scroll_, txt_sku_, field_h_logical, 80, Theme::get_font_body());
    col_sku_sizer_->Add(sku_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    row_sb_sizer_->Add(col_sku_sizer_, 2, wxEXPAND | wxRIGHT, FromDIP(8));

    col_bc_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_bc_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("Código de barras (opcional):"));
    lbl_bc_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_bc_->SetForegroundColour(Theme::ColorText);
    col_bc_sizer_->Add(lbl_bc_, 0, wxEXPAND);
    wxWindow* bc_box = ui::make_text_field(detail_scroll_, txt_barcode_, field_h_logical, 80, Theme::get_font_body());
    col_bc_sizer_->Add(bc_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    row_sb_sizer_->Add(col_bc_sizer_, 3, wxEXPAND);
    detail_sizer_->Add(row_sb_sizer_, 0, wxEXPAND | wxTOP, FromDIP(touch ? 6 : 8));

    // Fila 3: Categoría (desplegable único + "Nueva categoría...") y Tipo de producto
    cat_type_sizer_ = new wxBoxSizer(wxHORIZONTAL);

    col_cat_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_cat_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("Categoría:"));
    lbl_cat_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_cat_->SetForegroundColour(Theme::ColorText);
    col_cat_sizer_->Add(lbl_cat_, 0, wxEXPAND);

    if (touch) {
        cb_category_ = new ui::TouchComboBox(
            detail_scroll_, wxID_ANY, wxSize(-1, FromDIP(field_h_logical)), wxArrayString()
        );
        cb_category_->SetFont(Theme::get_font_body());
        cb_category_->SetMinSize(wxSize(FromDIP(130), FromDIP(field_h_logical)));
        cb_category_->Bind(wxEVT_COMBOBOX, &InventoryProductsPanel::on_category_combo_changed, this);
        col_cat_sizer_->Add(cb_category_, 0, wxEXPAND | wxTOP, FromDIP(6));
    } else {
        ch_category_ = new wxChoice(detail_scroll_, wxID_ANY);
        ch_category_->SetFont(Theme::get_font_body());
        ch_category_->SetMinSize(wxSize(FromDIP(130), FromDIP(field_h_logical)));
        ch_category_->Bind(wxEVT_CHOICE, &InventoryProductsPanel::on_category_combo_changed, this);
        col_cat_sizer_->Add(ch_category_, 0, wxEXPAND | wxTOP, FromDIP(6));
    }

    col_type_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_type_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("Tipo de producto:"));
    lbl_type_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_type_->SetForegroundColour(Theme::ColorText);
    col_type_sizer_->Add(lbl_type_, 0, wxEXPAND);

    type_btns_sizer_ = new wxBoxSizer(wxHORIZONTAL);
    btn_type_direct_ = new wxButton(detail_scroll_, wxID_ANY, wxT("Directo"));
    btn_type_direct_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_type_direct_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_type_direct, this);
    type_btns_sizer_->Add(btn_type_direct_, 1, wxRIGHT, FromDIP(6));

    btn_type_prepared_ = new wxButton(detail_scroll_, wxID_ANY, wxT("Preparado"));
    btn_type_prepared_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_type_prepared_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_type_prepared, this);
    type_btns_sizer_->Add(btn_type_prepared_, 1);
    col_type_sizer_->Add(type_btns_sizer_, 0, wxEXPAND | wxTOP, FromDIP(6));

    cat_type_sizer_->Add(col_cat_sizer_, 1, wxEXPAND | wxRIGHT, FromDIP(8));
    cat_type_sizer_->Add(col_type_sizer_, 1, wxEXPAND);
    detail_sizer_->Add(cat_type_sizer_, 0, wxEXPAND | wxTOP, FromDIP(touch ? 6 : 8));

    // Campo de texto para nueva categoría (visible únicamente al elegir "Nueva categoría...")
    cat_box_ = ui::make_text_field(detail_scroll_, txt_category_, field_h_logical, 100, Theme::get_font_body());
    txt_category_->SetHint(wxT("Escribe el nombre de la nueva categoría..."));
    cat_box_->Hide();
    detail_sizer_->Add(cat_box_, 0, wxEXPAND | wxTOP, FromDIP(6));

    // Fila 4: Imagen opcional (inline en ancho suficiente, apilada en 960x600)
    img_row_sizer_ = new wxBoxSizer(wxHORIZONTAL);
    const wxSize thumb_sz = FromDIP(wxSize(48, 48));
    bmp_thumb_ = new wxStaticBitmap(detail_scroll_, wxID_ANY, make_placeholder_bitmap(thumb_sz), wxDefaultPosition, thumb_sz);
    img_row_sizer_->Add(bmp_thumb_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    img_right_sizer_ = new wxBoxSizer(wxVERTICAL);
    auto* img_btn_wrap = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    btn_pick_image_ = new wxButton(detail_scroll_, wxID_ANY, wxT("Elegir imagen"));
    btn_pick_image_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_pick_image_, btn_h_logical);
    ensure_btn_width_fits(btn_pick_image_, 115, btn_h_logical);
    btn_pick_image_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_pick_image, this);
    img_btn_wrap->Add(btn_pick_image_, 0, wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_remove_image_ = new wxButton(detail_scroll_, wxID_ANY, wxT("Quitar imagen"));
    btn_remove_image_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_remove_image_, btn_h_logical);
    ensure_btn_width_fits(btn_remove_image_, 115, btn_h_logical);
    btn_remove_image_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_remove_image, this);
    img_btn_wrap->Add(btn_remove_image_, 0, wxBOTTOM, FromDIP(6));

    lbl_image_status_ = new wxStaticText(detail_scroll_, wxID_ANY, wxT("Sin imagen"));
    lbl_image_status_->SetFont(Theme::get_font_small());
    lbl_image_status_->SetForegroundColour(Theme::ColorTextSecondary);

    img_right_sizer_->Add(img_btn_wrap, 0, wxEXPAND);
    img_right_sizer_->Add(lbl_image_status_, 0, wxEXPAND | wxTOP, FromDIP(2));

    img_row_sizer_->Add(img_right_sizer_, 1, wxALIGN_CENTER_VERTICAL);
    detail_sizer_->Add(img_row_sizer_, 0, wxEXPAND | wxTOP, FromDIP(touch ? 6 : 8));

    // Existencia de productos directos
    pnl_direct_stock_ = new wxPanel(detail_scroll_, wxID_ANY);
    pnl_direct_stock_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    auto* direct_stock_sizer = new wxBoxSizer(wxVERTICAL);

    chk_track_stock_ = new wxCheckBox(pnl_direct_stock_, wxID_ANY, wxT("Controlar existencia"));
    chk_track_stock_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    chk_track_stock_->SetValue(true);
    {
        int chk_tw = 0, chk_th = 0;
        chk_track_stock_->GetTextExtent(wxT("Controlar existencia"), &chk_tw, &chk_th);
        chk_track_stock_->SetMinSize(wxSize(chk_tw + FromDIP(44), FromDIP(btn_h_logical)));
    }
    direct_stock_sizer->Add(chk_track_stock_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    lbl_direct_stock_status_ = new wxStaticText(pnl_direct_stock_, wxID_ANY, wxEmptyString);
    lbl_direct_stock_status_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_direct_stock_status_->SetForegroundColour(Theme::ColorText);
    direct_stock_sizer->Add(lbl_direct_stock_status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    last_direct_stock_hint_ = "Se creará un ingrediente por piezas con el mismo nombre para registrar compras y mermas.";
    lbl_direct_stock_hint_ = new wxStaticText(
        pnl_direct_stock_,
        wxID_ANY,
        wxString::FromUTF8(last_direct_stock_hint_)
    );
    lbl_direct_stock_hint_->SetFont(Theme::get_font_small());
    lbl_direct_stock_hint_->SetForegroundColour(Theme::ColorTextSecondary);
    direct_stock_sizer->Add(lbl_direct_stock_hint_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));

    btn_enable_stock_ = new wxButton(pnl_direct_stock_, wxID_ANY, wxT("Controlar existencia"));
    btn_enable_stock_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_enable_stock_, btn_h_logical);
    ensure_btn_width_fits(btn_enable_stock_, 170, btn_h_logical);
    btn_enable_stock_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_enable_direct_stock, this);
    direct_stock_sizer->Add(btn_enable_stock_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(6));
    direct_stock_sizer->AddSpacer(FromDIP(8));

    pnl_direct_stock_->SetSizer(direct_stock_sizer);
    detail_sizer_->Add(pnl_direct_stock_, 0, wxEXPAND | wxTOP, FromDIP(touch ? 6 : 8));

    // Botones principales de producto
    auto* action_row = new wxBoxSizer(wxHORIZONTAL);
    btn_save_ = new wxButton(detail_scroll_, wxID_ANY, wxT("Guardar"));
    btn_save_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_save_, btn_h_logical);
    ensure_btn_width_fits(btn_save_, 110, btn_h_logical);
    btn_save_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_save_product, this);
    action_row->Add(btn_save_, 1, wxRIGHT, FromDIP(6));

    btn_toggle_active_ = new wxButton(detail_scroll_, wxID_ANY, wxT("Dar de baja"));
    btn_toggle_active_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_toggle_active_, btn_h_logical);
    ensure_btn_width_fits(btn_toggle_active_, 120, btn_h_logical);
    btn_toggle_active_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_toggle_active_product, this);
    action_row->Add(btn_toggle_active_, 1);
    detail_sizer_->Add(action_row, 0, wxEXPAND | wxTOP, FromDIP(touch ? 8 : 10));

    lbl_feedback_ = new wxStaticText(detail_scroll_, wxID_ANY, wxEmptyString);
    lbl_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_feedback_->SetForegroundColour(Theme::ColorText);
    lbl_feedback_->Hide();
    detail_sizer_->Add(lbl_feedback_, 0, wxEXPAND | wxTOP, FromDIP(6));

    // --- Sección Receta (solo productos preparados) ---
    pnl_recipe_ = new wxPanel(detail_scroll_, wxID_ANY);
    pnl_recipe_->SetBackgroundColour(Theme::ColorSurface);
    recipe_sizer_ = new wxBoxSizer(wxVERTICAL);

    recipe_sizer_->Add(new wxStaticLine(pnl_recipe_), 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));

    auto* lbl_rcp_title = new wxStaticText(pnl_recipe_, wxID_ANY, wxT("Receta por porción"));
    lbl_rcp_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_rcp_title->SetForegroundColour(Theme::ColorBrandDeep);
    recipe_sizer_->Add(lbl_rcp_title, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    lbl_recipe_new_hint_ = new wxStaticText(
        pnl_recipe_, wxID_ANY, wxT("Guarda primero el producto para capturar su receta.")
    );
    lbl_recipe_new_hint_->SetFont(Theme::get_font_small());
    lbl_recipe_new_hint_->SetForegroundColour(Theme::ColorTextSecondary);
    recipe_sizer_->Add(lbl_recipe_new_hint_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    pnl_recipe_editor_ = new wxPanel(pnl_recipe_, wxID_ANY);
    pnl_recipe_editor_->SetBackgroundColour(Theme::ColorSurface);
    auto* rcp_ed_sizer = new wxBoxSizer(wxVERTICAL);

    // Franja compacta de costo por porción y margen (visible sin desplazarse en 1200x720 y pantalla completa)
    pnl_costing_ = new wxPanel(pnl_recipe_editor_, wxID_ANY);
    pnl_costing_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    cost_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_cost_ = new wxStaticText(pnl_costing_, wxID_ANY, wxEmptyString);
    lbl_cost_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_cost_->SetForegroundColour(Theme::ColorText);
    cost_sizer_->Add(lbl_cost_, 0, wxEXPAND);

    lbl_margin_ = new wxStaticText(pnl_costing_, wxID_ANY, wxEmptyString);
    lbl_margin_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_margin_->SetForegroundColour(Theme::ColorSuccess);
    cost_sizer_->Add(lbl_margin_, 0, wxEXPAND | wxTOP, FromDIP(4));

    auto* cost_outer = new wxBoxSizer(wxVERTICAL);
    cost_outer->Add(cost_sizer_, 1, wxEXPAND | wxALL, FromDIP(8));
    pnl_costing_->SetSizer(cost_outer);
    rcp_ed_sizer->Add(pnl_costing_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    recipe_list_ = new wxListView(
        pnl_recipe_editor_, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, touch ? 132 : 112)),
        wxLC_REPORT | wxLC_SINGLE_SEL
    );
    recipe_list_->SetFont(Theme::get_font_body());
    recipe_list_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    recipe_list_->SetForegroundColour(Theme::ColorText);
    recipe_list_->InsertColumn(0, wxT("Ingrediente"));
    recipe_list_->InsertColumn(1, wxT("Cantidad por porción"));
    recipe_list_->SetMinSize(FromDIP(wxSize(-1, touch ? 132 : 112)));
    if (touch) {
        ui::apply_touch_row_height(recipe_list_);
    }
    recipe_list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryProductsPanel::on_recipe_row_selected, this);
    rcp_ed_sizer->Add(recipe_list_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    auto* rcp_add_lbl = new wxStaticText(pnl_recipe_editor_, wxID_ANY, wxT("Ingrediente y cantidad por porción:"));
    rcp_add_lbl->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    rcp_add_lbl->SetForegroundColour(Theme::ColorText);
    rcp_ed_sizer->Add(rcp_add_lbl, 0, wxEXPAND);

    auto* rcp_input_row = new wxBoxSizer(wxHORIZONTAL);
    if (touch) {
        cb_recipe_ing_ = new ui::TouchComboBox(
            pnl_recipe_editor_, wxID_ANY, wxSize(-1, FromDIP(field_h_logical)), wxArrayString()
        );
        cb_recipe_ing_->SetFont(Theme::get_font_body());
        cb_recipe_ing_->SetMinSize(wxSize(FromDIP(110), FromDIP(field_h_logical)));
        cb_recipe_ing_->Bind(wxEVT_COMBOBOX, &InventoryProductsPanel::on_recipe_ing_changed, this);
        rcp_input_row->Add(cb_recipe_ing_, 2, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    } else {
        ch_recipe_ing_ = new wxChoice(pnl_recipe_editor_, wxID_ANY);
        ch_recipe_ing_->SetFont(Theme::get_font_body());
        ch_recipe_ing_->SetMinSize(wxSize(FromDIP(110), FromDIP(field_h_logical)));
        ch_recipe_ing_->Bind(wxEVT_CHOICE, &InventoryProductsPanel::on_recipe_ing_changed, this);
        rcp_input_row->Add(ch_recipe_ing_, 2, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    }

    wxWindow* rcp_qty_box = ui::make_text_field(
        pnl_recipe_editor_, txt_recipe_qty_, field_h_logical, 64, Theme::get_font_body()
    );
    rcp_input_row->Add(rcp_qty_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));

    if (touch) {
        cb_recipe_unit_ = new ui::TouchComboBox(
            pnl_recipe_editor_, wxID_ANY, wxSize(FromDIP(96), FromDIP(field_h_logical)), wxArrayString()
        );
        cb_recipe_unit_->SetFont(Theme::get_font_body());
        cb_recipe_unit_->SetMinSize(wxSize(FromDIP(96), FromDIP(field_h_logical)));
        rcp_input_row->Add(cb_recipe_unit_, 0, wxALIGN_CENTER_VERTICAL);
    } else {
        ch_recipe_unit_ = new wxChoice(pnl_recipe_editor_, wxID_ANY);
        ch_recipe_unit_->SetFont(Theme::get_font_body());
        ch_recipe_unit_->SetMinSize(wxSize(FromDIP(86), FromDIP(field_h_logical)));
        rcp_input_row->Add(ch_recipe_unit_, 0, wxALIGN_CENTER_VERTICAL);
    }
    rcp_ed_sizer->Add(rcp_input_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    auto* rcp_btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_recipe_add_ = new wxButton(pnl_recipe_editor_, wxID_ANY, wxT("Añadir o cambiar"));
    btn_recipe_add_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_recipe_add_, btn_h_logical);
    ensure_btn_width_fits(btn_recipe_add_, 122, btn_h_logical);
    btn_recipe_add_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_recipe_add_or_update, this);
    rcp_btn_row->Add(btn_recipe_add_, 1, wxRIGHT, FromDIP(4));

    btn_recipe_remove_ = new wxButton(pnl_recipe_editor_, wxID_ANY, wxT("Quitar"));
    btn_recipe_remove_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_recipe_remove_, btn_h_logical);
    ensure_btn_width_fits(btn_recipe_remove_, 76, btn_h_logical);
    btn_recipe_remove_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_recipe_remove_item, this);
    rcp_btn_row->Add(btn_recipe_remove_, 0, wxRIGHT, FromDIP(4));

    btn_recipe_save_ = new wxButton(pnl_recipe_editor_, wxID_ANY, wxT("Guardar receta"));
    btn_recipe_save_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_recipe_save_, btn_h_logical);
    ensure_btn_width_fits(btn_recipe_save_, 115, btn_h_logical);
    btn_recipe_save_->Bind(wxEVT_BUTTON, &InventoryProductsPanel::on_recipe_save, this);
    rcp_btn_row->Add(btn_recipe_save_, 1);
    rcp_ed_sizer->Add(rcp_btn_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    lbl_recipe_feedback_ = new wxStaticText(pnl_recipe_editor_, wxID_ANY, wxEmptyString);
    lbl_recipe_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_recipe_feedback_->SetForegroundColour(Theme::ColorText);
    lbl_recipe_feedback_->Hide();
    rcp_ed_sizer->Add(lbl_recipe_feedback_, 0, wxEXPAND | wxTOP, FromDIP(6));

    pnl_recipe_editor_->SetSizer(rcp_ed_sizer);
    recipe_sizer_->Add(pnl_recipe_editor_, 0, wxEXPAND);

    pnl_recipe_->SetSizer(recipe_sizer_);
    detail_sizer_->Add(pnl_recipe_, 0, wxEXPAND);

    detail_scroll_->SetSizer(detail_sizer_);
    detail_outer->Add(detail_scroll_, 1, wxEXPAND | wxALL, FromDIP(16));
    detail_card_->SetSizer(detail_outer);
    main_body_->Add(detail_card_, 52, wxEXPAND);

    root->Add(main_body_, 1, wxEXPAND);
    SetSizer(root);

    update_type_buttons_ui();

    if (touch) {
        keypad_ = new ui::NumericKeypad(this);
        auto protected_fn = [this]() {
            std::vector<wxRect> rects;
            auto add_rect = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_rect(detail_card_);
            add_rect(btn_save_);
            add_rect(btn_recipe_save_);
            return rects;
        };
        keypad_->bind_field(txt_price_, price_box_, true, detail_card_, protected_fn);
        keypad_->bind_field(txt_recipe_qty_, rcp_qty_box, false, detail_card_, protected_fn);
    }

    Bind(wxEVT_SIZE, &InventoryProductsPanel::on_size, this);
}

void InventoryProductsPanel::refresh_all(const std::string& keep_product_id) {
    if (destroying_ || IsBeingDeleted()) return;
    can_manage_ = account_service_ && account_service_->has_permission("inventory.manage_products");
    if (!can_manage_) {
        update_permissions_ui();
        Layout();
        return;
    }

    reload_categories();
    reload_ingredients_catalog();
    reload_product_list(keep_product_id);
    update_permissions_ui();
    apply_responsive_layout();
    Layout();
    update_detail_scroll_layout();
}

void InventoryProductsPanel::reload_categories() {
    const std::string prev_filter = selected_filter_category();
    std::string prev_cat_choice;
    const bool was_new_cat = is_new_category_selected();
    {
        const int prev_idx = get_selector_index(ch_category_, cb_category_);
        if (prev_idx >= 0 && static_cast<std::size_t>(prev_idx) < categories_.size()) {
            prev_cat_choice = categories_[static_cast<std::size_t>(prev_idx)];
        }
    }

    categories_.clear();
    if (catalog_service_) {
        auto res = catalog_service_->list_categories();
        if (res.is_success()) {
            categories_ = res.value();
        }
    }

    std::vector<std::string> filter_items;
    filter_items.push_back("Todas las categorías");
    int filter_idx = 0;
    for (std::size_t i = 0; i < categories_.size(); ++i) {
        filter_items.push_back(categories_[i]);
        if (!prev_filter.empty() && categories_[i] == prev_filter) {
            filter_idx = static_cast<int>(i + 1);
        }
    }
    set_selector_items(ch_filter_cat_, cb_filter_cat_, filter_items, filter_idx);

    std::vector<std::string> detail_items;
    detail_items.reserve(categories_.size() + 1);
    for (const auto& c : categories_) {
        detail_items.push_back(c);
    }
    detail_items.push_back(kNewCategoryOptionUtf8);

    int detail_idx = 0;
    if (was_new_cat || categories_.empty()) {
        detail_idx = static_cast<int>(categories_.size());
    } else if (!prev_cat_choice.empty()) {
        for (std::size_t i = 0; i < categories_.size(); ++i) {
            if (categories_[i] == prev_cat_choice) {
                detail_idx = static_cast<int>(i);
                break;
            }
        }
    }
    set_selector_items(ch_category_, cb_category_, detail_items, detail_idx);
    sync_category_input_visibility(false);
}

void InventoryProductsPanel::reload_ingredients_catalog() {
    const std::string keep_ing = selected_recipe_ingredient_id();
    ingredients_.clear();
    if (inventory_service_) {
        auto res = inventory_service_->list_ingredients();
        if (res.is_success()) {
            ingredients_ = res.value();
        }
    }
    std::vector<std::string> labels;
    labels.reserve(ingredients_.size());
    int select_idx = ingredients_.empty() ? wxNOT_FOUND : 0;
    for (std::size_t i = 0; i < ingredients_.size(); ++i) {
        labels.push_back(ingredients_[i].name);
        if (!keep_ing.empty() && ingredients_[i].id == keep_ing) {
            select_idx = static_cast<int>(i);
        }
    }
    set_selector_items(ch_recipe_ing_, cb_recipe_ing_, labels, select_idx);
    update_recipe_unit_choices();
    const bool has_ings = !ingredients_.empty();
    enable_selector(ch_recipe_ing_, cb_recipe_ing_, has_ings);
    enable_selector(ch_recipe_unit_, cb_recipe_unit_, has_ings);
    ui::set_text_field_enabled(txt_recipe_qty_, has_ings);
    if (btn_recipe_add_) btn_recipe_add_->Enable(has_ings);
}

void InventoryProductsPanel::reload_product_list(const std::string& keep_product_id) {
    if (destroying_ || !list_) return;
    const std::string target_id = keep_product_id.empty() ? editing_product_id_ : keep_product_id;

    products_.clear();
    list_load_failed_ = false;
    last_list_error_.clear();

    sync_selection_guard_ = true;
    list_->DeleteAllItems();

    if (!catalog_service_) {
        list_load_failed_ = true;
        last_list_error_ = "Servicio de catálogo no disponible.";
        sync_selection_guard_ = false;
        update_empty_state();
        return;
    }

    const std::string q = txt_search_ ? trim_ascii(std::string(txt_search_->GetValue().ToUTF8().data())) : "";
    const std::string cat_f = selected_filter_category();
    const std::optional<std::string> cat_opt = cat_f.empty() ? std::nullopt : std::optional<std::string>(cat_f);

    auto res = catalog_service_->search_products(q, cat_opt, 0, 500, include_inactive_);
    if (!res.is_success()) {
        list_load_failed_ = true;
        last_list_error_ = res.error_message();
        sync_selection_guard_ = false;
        update_empty_state();
        return;
    }

    long row_to_select = -1;
    for (const auto& p : res.value()) {
        if (is_combo_product(p)) continue;
        const long idx = list_->InsertItem(static_cast<long>(products_.size()), wxString::FromUTF8(p.name));
        list_->SetItem(idx, 1, wxString::FromUTF8(p.category));
        list_->SetItem(idx, 2, wxString::FromUTF8(p.price.to_string()));
        list_->SetItem(idx, 3, wxString::FromUTF8(p.is_prepared ? "Preparado" : "Directo"));
        list_->SetItem(idx, 4, wxString::FromUTF8(p.is_active ? "Activo" : "Dado de baja"));

        if (!p.is_active) {
            list_->SetItemTextColour(idx, Theme::ColorError);
            list_->SetItemBackgroundColour(idx, Theme::ColorErrorSoft);
        } else {
            list_->SetItemTextColour(idx, Theme::ColorText);
            list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
        }
        if (!target_id.empty() && p.id == target_id) {
            row_to_select = idx;
        }
        products_.push_back(p);
    }

    if (row_to_select >= 0) {
        list_->SetItemState(
            row_to_select, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED
        );
    }
    sync_selection_guard_ = false;

    update_empty_state();
    update_list_column_widths();
}

void InventoryProductsPanel::update_permissions_ui() {
    if (!can_manage_) {
        if (keypad_) keypad_->hide_keypad(false);
        list_card_->Hide();
        detail_card_->Hide();
        pnl_no_permission_->Show();
        return;
    }
    pnl_no_permission_->Hide();
    list_card_->Show();
    detail_card_->Show();
}

void InventoryProductsPanel::update_empty_state() {
    if (!list_card_ || !list_card_->IsShown()) return;
    if (list_load_failed_) {
        list_->Hide();
        lbl_list_empty_->SetLabel(wxString::FromUTF8(last_list_error_));
        lbl_list_empty_->SetForegroundColour(Theme::ColorError);
        lbl_list_empty_->Show();
    } else if (products_.empty()) {
        list_->Hide();
        lbl_list_empty_->SetLabel(wxT("No hay productos que coincidan."));
        lbl_list_empty_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_list_empty_->Show();
    } else {
        lbl_list_empty_->Hide();
        list_->Show();
    }
    if (lbl_list_empty_->IsShown()) {
        lbl_list_empty_->Wrap(std::max(FromDIP(160), list_card_->GetClientSize().GetWidth() - FromDIP(28)));
    }
}

void InventoryProductsPanel::update_list_column_widths() {
    if (destroying_ || sizing_columns_ || !list_ || !list_->IsShown() || list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(list_);
    dc.SetFont(list_->GetFont());

    int max_name = dc.GetTextExtent(wxT("Nombre")).GetWidth();
    int max_cat = dc.GetTextExtent(wxT("Categoría")).GetWidth();
    int max_price = dc.GetTextExtent(wxT("Precio")).GetWidth();
    int max_type = dc.GetTextExtent(wxT("Tipo")).GetWidth();
    int max_state = dc.GetTextExtent(wxT("Estado")).GetWidth();
    for (const auto& p : products_) {
        max_name = std::max(max_name, dc.GetTextExtent(wxString::FromUTF8(p.name)).GetWidth());
        max_cat = std::max(max_cat, dc.GetTextExtent(wxString::FromUTF8(p.category)).GetWidth());
        max_price = std::max(max_price, dc.GetTextExtent(wxString::FromUTF8(p.price.to_string())).GetWidth());
        max_type = std::max(
            max_type,
            dc.GetTextExtent(p.is_prepared ? wxT("Preparado") : wxT("Directo")).GetWidth()
        );
        max_state = std::max(
            max_state,
            dc.GetTextExtent(p.is_active ? wxT("Activo") : wxT("Dado de baja")).GetWidth()
        );
    }

    const int pad = 14;
    const int touch_col0_extra = Theme::is_touch() ? 7 : 0;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Nombre"), max_name, 0, pad, touch_col0_extra});
    specs.push_back({wxT("Categoría"), max_cat, 0, pad, 0});
    specs.push_back({wxT("Precio"), max_price, 0, pad, 0});
    specs.push_back({wxT("Tipo"), max_type, 0, pad, 0});
    specs.push_back({wxT("Estado"), max_state, 0, pad, 0});
    ui::distribute_list_columns(list_, specs, 0);
    sizing_columns_ = false;
}

void InventoryProductsPanel::update_recipe_column_widths() {
    if (destroying_ || sizing_columns_ || !recipe_list_ || !recipe_list_->IsShownOnScreen() ||
        recipe_list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(recipe_list_);
    dc.SetFont(recipe_list_->GetFont());

    int max_ing = dc.GetTextExtent(wxT("Ingrediente")).GetWidth();
    int max_qty = dc.GetTextExtent(wxT("Cantidad por porción")).GetWidth();
    for (const auto& r : recipe_rows_) {
        max_ing = std::max(max_ing, dc.GetTextExtent(wxString::FromUTF8(r.ingredient_name)).GetWidth());
        max_qty = std::max(
            max_qty,
            dc.GetTextExtent(wxString::FromUTF8(format_readable_recipe_qty(r.quantity_base_units, r.unit_dimension))).GetWidth()
        );
    }

    const int pad = 16;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Ingrediente"), max_ing, 0, pad, 0});
    specs.push_back({wxT("Cantidad por porción"), max_qty, 0, pad, 0});
    ui::distribute_list_columns(recipe_list_, specs, 0);
    sizing_columns_ = false;
}

void InventoryProductsPanel::update_type_buttons_ui() {
    const int btn_h_logical = Theme::is_touch() ? 48 : 34;
    if (is_prepared_selected_) {
        Theme::style_button_secondary(btn_type_direct_, btn_h_logical);
        Theme::style_button_primary(btn_type_prepared_, btn_h_logical);
    } else {
        Theme::style_button_primary(btn_type_direct_, btn_h_logical);
        Theme::style_button_secondary(btn_type_prepared_, btn_h_logical);
    }
    ensure_btn_width_fits(btn_type_direct_, 85, btn_h_logical);
    ensure_btn_width_fits(btn_type_prepared_, 100, btn_h_logical);

    const bool show_recipe = is_prepared_selected_;
    if (pnl_direct_stock_) {
        const bool show_direct = !is_prepared_selected_;
        pnl_direct_stock_->Show(show_direct);
        if (show_direct) {
            const bool is_new = editing_product_id_.empty();
            chk_track_stock_->Show(is_new);
            lbl_direct_stock_status_->Show(!is_new);
            if (is_new) {
                last_direct_stock_status_.clear();
                last_direct_stock_hint_ = "Se creará un ingrediente por piezas con el mismo nombre para registrar compras y mermas.";
                lbl_direct_stock_hint_->SetLabel(wxString::FromUTF8(last_direct_stock_hint_));
                lbl_direct_stock_hint_->Show(true);
                btn_enable_stock_->Show(false);
            } else if (editing_stock_tracked_) {
                last_direct_stock_status_ = std::string(wxString::Format(
                    wxT("Existencia controlada (%lld %s)"),
                    editing_stock_units_,
                    editing_stock_units_ == 1 ? wxT("pieza") : wxT("piezas")
                ).ToUTF8().data());
                lbl_direct_stock_status_->SetLabel(wxString::FromUTF8(last_direct_stock_status_));
                lbl_direct_stock_status_->SetForegroundColour(Theme::ColorSuccess);
                last_direct_stock_hint_ = "Puedes registrar compras y mermas desde Reabastecimiento y Barra.";
                lbl_direct_stock_hint_->SetLabel(wxString::FromUTF8(last_direct_stock_hint_));
                lbl_direct_stock_hint_->Show(true);
                btn_enable_stock_->Show(false);
            } else {
                last_direct_stock_status_ = "Sin control de existencia";
                lbl_direct_stock_status_->SetLabel(wxString::FromUTF8(last_direct_stock_status_));
                lbl_direct_stock_status_->SetForegroundColour(Theme::ColorTextSecondary);
                last_direct_stock_hint_ = "Se creará un ingrediente por piezas con el mismo nombre para registrar compras y mermas.";
                lbl_direct_stock_hint_->SetLabel(wxString::FromUTF8(last_direct_stock_hint_));
                lbl_direct_stock_hint_->Show(true);
                btn_enable_stock_->Show(true);
            }
            pnl_direct_stock_->Layout();
        }
    }
    pnl_recipe_->Show(show_recipe);
    if (show_recipe) {
        const bool has_saved = !editing_product_id_.empty();
        lbl_recipe_new_hint_->Show(!has_saved);
        pnl_recipe_editor_->Show(has_saved);
    }
    update_detail_scroll_layout();
    if (show_recipe && !editing_product_id_.empty()) {
        update_recipe_column_widths();
    }
}

void InventoryProductsPanel::update_recipe_unit_choices() {
    const std::string dim = selected_recipe_ingredient_dimension();
    recipe_units_ = compatible_units_for_dimension(dim);
    const int idx = recipe_units_.empty() ? wxNOT_FOUND : 0;
    set_selector_items(ch_recipe_unit_, cb_recipe_unit_, recipe_units_, idx);
}

void InventoryProductsPanel::apply_responsive_layout() {
    if (destroying_ || !main_body_ || !list_card_ || !detail_card_) return;
    const int panel_w = GetClientSize().GetWidth();
    if (panel_w <= 0) return;

    const bool want_wide = panel_w >= FromDIP(kWideSplitThresholdLogical);
    if (want_wide != wide_split_active_) {
        wide_split_active_ = want_wide;
        if (auto* item_l = main_body_->GetItem(list_card_)) {
            item_l->SetProportion(want_wide ? 52 : 48);
        }
        if (auto* item_d = main_body_->GetItem(detail_card_)) {
            item_d->SetProportion(want_wide ? 48 : 52);
        }
        Layout();
    }
}

bool InventoryProductsPanel::is_new_category_selected() const {
    const int idx = get_selector_index(ch_category_, cb_category_);
    return idx >= static_cast<int>(categories_.size());
}

void InventoryProductsPanel::sync_category_input_visibility(bool focus_new_input) {
    if (!cat_box_) return;
    const bool show_new = is_new_category_selected();
    if (cat_box_->IsShown() != show_new) {
        cat_box_->Show(show_new);
    }
    if (!show_new && txt_category_) {
        txt_category_->ChangeValue(wxEmptyString);
    }
    if (show_new && focus_new_input && txt_category_) {
        txt_category_->SetFocus();
    }
}

std::string InventoryProductsPanel::resolve_selected_category(std::string& out_error) const {
    out_error.clear();
    if (is_new_category_selected()) {
        const std::string typed = txt_category_
            ? trim_ascii(std::string(txt_category_->GetValue().ToUTF8().data()))
            : std::string();
        if (typed.empty()) {
            out_error = "Escribe el nombre de la nueva categoría.";
            return "";
        }
        if (typed == kNewCategoryOptionUtf8) {
            out_error = "Escribe un nombre válido para la nueva categoría.";
            return "";
        }
        return typed;
    }
    const int idx = get_selector_index(ch_category_, cb_category_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < categories_.size()) {
        const std::string chosen = trim_ascii(categories_[static_cast<std::size_t>(idx)]);
        if (chosen.empty() || chosen == kNewCategoryOptionUtf8) {
            out_error = "Selecciona una categoría válida.";
            return "";
        }
        return chosen;
    }
    out_error = "Selecciona una categoría o crea una nueva.";
    return "";
}

void InventoryProductsPanel::update_detail_scroll_layout() {
    if (!detail_scroll_ || !detail_sizer_) return;
    const bool touch = Theme::is_touch();

    auto run_pass = [&](int inner_w) {
        // Regla de fila 1: Nombre y Precio con etiquetas inline cuando caben con campo amplio (umbral calculado)
        if (col_name_sizer_ && col_price_sizer_ && lbl_name_ && lbl_price_ && name_box_ && price_box_) {
            const int np_min_w = lbl_name_->GetBestSize().GetWidth() + FromDIP(8) + FromDIP(160) +
                                 FromDIP(8) + lbl_price_->GetBestSize().GetWidth() + FromDIP(8) + FromDIP(80);
            const bool want_inline = inner_w >= np_min_w;
            if (want_inline != np_inline_active_) {
                np_inline_active_ = want_inline;
                auto apply_pair = [&](wxBoxSizer* sz, wxStaticText* lbl, wxWindow* box) {
                    sz->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                    if (auto* il = sz->GetItem(lbl)) {
                        il->SetProportion(0);
                        il->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                        il->SetBorder(want_inline ? FromDIP(8) : 0);
                    }
                    if (auto* ib = sz->GetItem(box)) {
                        ib->SetProportion(want_inline ? 1 : 0);
                        ib->SetFlag(want_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                        ib->SetBorder(want_inline ? 0 : FromDIP(6));
                    }
                };
                apply_pair(col_name_sizer_, lbl_name_, name_box_);
                apply_pair(col_price_sizer_, lbl_price_, price_box_);
            }
        }

        // Regla de fila SKU | Código de barras (umbral calculado con controles reales)
        if (row_sb_sizer_ && col_sku_sizer_ && col_bc_sizer_ && lbl_sku_ && lbl_bc_) {
            const int sku_min = std::max(lbl_sku_->GetBestSize().GetWidth() + FromDIP(8), FromDIP(90));
            const int bc_min = std::max(lbl_bc_->GetBestSize().GetWidth() + FromDIP(8), FromDIP(140));
            const int sb_min_w = sku_min + FromDIP(8) + bc_min + FromDIP(4);
            const bool want_horiz = inner_w >= sb_min_w;
            if (want_horiz != sku_bc_horizontal_) {
                sku_bc_horizontal_ = want_horiz;
                row_sb_sizer_->SetOrientation(want_horiz ? wxHORIZONTAL : wxVERTICAL);
                if (auto* it_s = row_sb_sizer_->GetItem(col_sku_sizer_)) {
                    it_s->SetProportion(want_horiz ? 2 : 0);
                    it_s->SetFlag(want_horiz ? (wxEXPAND | wxRIGHT) : (wxEXPAND | wxBOTTOM));
                    it_s->SetBorder(FromDIP(want_horiz ? 8 : 6));
                }
                if (auto* it_b = row_sb_sizer_->GetItem(col_bc_sizer_)) {
                    it_b->SetProportion(want_horiz ? 3 : 0);
                    it_b->SetFlag(wxEXPAND);
                    it_b->SetBorder(0);
                }
            }
        }

        // Regla de fila combinada Categoría | Tipo de producto (umbral medido en tiempo de ejecución)
        if (cat_type_sizer_ && col_cat_sizer_ && col_type_sizer_ && btn_type_direct_ && btn_type_prepared_ &&
            lbl_type_ && lbl_cat_ && type_btns_sizer_) {
            const int type_btns_w = btn_type_direct_->GetBestSize().GetWidth() + FromDIP(6) +
                                    btn_type_prepared_->GetBestSize().GetWidth();
            const int type_min_w = std::max(lbl_type_->GetBestSize().GetWidth(), type_btns_w);
            const int cat_type_min_w = FromDIP(150) + FromDIP(8) + type_min_w + FromDIP(8);
            const bool want_horiz = inner_w >= cat_type_min_w;
            if (want_horiz != cat_type_horizontal_) {
                cat_type_horizontal_ = want_horiz;
                cat_type_sizer_->SetOrientation(want_horiz ? wxHORIZONTAL : wxVERTICAL);
                if (auto* it_cat = cat_type_sizer_->GetItem(col_cat_sizer_)) {
                    it_cat->SetProportion(want_horiz ? 1 : 0);
                    it_cat->SetFlag(want_horiz ? (wxEXPAND | wxRIGHT) : (wxEXPAND | wxBOTTOM));
                    it_cat->SetBorder(FromDIP(want_horiz ? 8 : 6));
                }
                if (auto* it_type = cat_type_sizer_->GetItem(col_type_sizer_)) {
                    it_type->SetProportion(want_horiz ? 1 : 0);
                    it_type->SetFlag(wxEXPAND);
                    it_type->SetBorder(0);
                }
            }

            // En pantalla ancha, etiquetas "Categoría:" y "Tipo:" inline para ganar altura útil sin apretar márgenes.
            wxClientDC tdc(lbl_type_);
            tdc.SetFont(lbl_type_->GetFont());
            const int short_type_w = tdc.GetTextExtent(wxT("Tipo:")).GetWidth();
            const int labels_inline_min_w = lbl_cat_->GetBestSize().GetWidth() + FromDIP(8) + FromDIP(145) +
                                            FromDIP(8) + short_type_w + FromDIP(8) + type_btns_w + FromDIP(8);
            const bool want_labels_inline = want_horiz && (inner_w >= labels_inline_min_w);
            if (want_labels_inline != cat_type_labels_inline_) {
                cat_type_labels_inline_ = want_labels_inline;
                lbl_type_->SetLabel(want_labels_inline ? wxT("Tipo:") : wxT("Tipo de producto:"));
                wxWindow* cat_ctrl = cb_category_ ? static_cast<wxWindow*>(cb_category_) : static_cast<wxWindow*>(ch_category_);
                col_cat_sizer_->SetOrientation(want_labels_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* il = col_cat_sizer_->GetItem(lbl_cat_)) {
                    il->SetProportion(0);
                    il->SetFlag(want_labels_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    il->SetBorder(want_labels_inline ? FromDIP(8) : 0);
                }
                if (cat_ctrl) {
                    if (auto* ic = col_cat_sizer_->GetItem(cat_ctrl)) {
                        ic->SetProportion(want_labels_inline ? 1 : 0);
                        ic->SetFlag(want_labels_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                        ic->SetBorder(want_labels_inline ? 0 : FromDIP(6));
                    }
                }
                col_type_sizer_->SetOrientation(want_labels_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* il = col_type_sizer_->GetItem(lbl_type_)) {
                    il->SetProportion(0);
                    il->SetFlag(want_labels_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    il->SetBorder(want_labels_inline ? FromDIP(8) : 0);
                }
                if (auto* ib = col_type_sizer_->GetItem(type_btns_sizer_)) {
                    ib->SetProportion(want_labels_inline ? 1 : 0);
                    ib->SetFlag(want_labels_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                    ib->SetBorder(want_labels_inline ? 0 : FromDIP(6));
                }
            }
        }

        // Regla de fila de imagen compacta (umbral medido en tiempo de ejecución)
        if (img_right_sizer_ && bmp_thumb_ && btn_pick_image_ && btn_remove_image_ && lbl_image_status_) {
            wxClientDC idc(lbl_image_status_);
            idc.SetFont(lbl_image_status_->GetFont());
            const int status_w = std::max(
                FromDIP(80),
                idc.GetTextExtent(lbl_image_status_->GetLabel()).GetWidth() + FromDIP(8)
            );
            const int img_inline_min_w = bmp_thumb_->GetMinSize().GetWidth() + FromDIP(8) +
                                         btn_pick_image_->GetBestSize().GetWidth() + FromDIP(6) +
                                         btn_remove_image_->GetBestSize().GetWidth() + FromDIP(8) +
                                         status_w;
            const bool want_inline = inner_w >= img_inline_min_w;
            if (want_inline != img_inline_active_) {
                img_inline_active_ = want_inline;
                img_right_sizer_->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* it_st = img_right_sizer_->GetItem(lbl_image_status_)) {
                    if (want_inline) {
                        it_st->SetFlag(wxALIGN_CENTER_VERTICAL | wxLEFT);
                        it_st->SetBorder(FromDIP(8));
                    } else {
                        it_st->SetFlag(wxEXPAND | wxTOP);
                        it_st->SetBorder(FromDIP(2));
                    }
                }
            }
        }

        const int wrap_w = std::max(FromDIP(140), inner_w - FromDIP(10));
        if (lbl_feedback_) {
            const bool has_fb = !last_feedback_.empty();
            lbl_feedback_->Show(has_fb);
            if (has_fb) {
                wrap_static_text_deterministic(lbl_feedback_, wxString::FromUTF8(last_feedback_), wrap_w);
            }
        }
        if (lbl_recipe_feedback_) {
            const bool has_rfb = !last_recipe_feedback_.empty();
            lbl_recipe_feedback_->Show(has_rfb);
            if (has_rfb) {
                wrap_static_text_deterministic(lbl_recipe_feedback_, wxString::FromUTF8(last_recipe_feedback_), wrap_w);
            }
        }
        if (lbl_cost_ && pnl_costing_ && pnl_costing_->IsShown() && cost_sizer_) {
            const int cost_wrap_w = std::max(FromDIP(140), inner_w - FromDIP(22));
            const bool margin_vis = lbl_margin_ && lbl_margin_->IsShown() && !last_margin_text_.empty();
            bool want_cost_horiz = false;
            if (margin_vis) {
                wxClientDC cdc(lbl_cost_);
                cdc.SetFont(lbl_cost_->GetFont());
                const int cw = cdc.GetTextExtent(wxString::FromUTF8(last_cost_text_)).GetWidth();
                wxClientDC mdc(lbl_margin_);
                mdc.SetFont(lbl_margin_->GetFont());
                const int mw = mdc.GetTextExtent(wxString::FromUTF8(last_margin_text_)).GetWidth();
                want_cost_horiz = (cw + mw + FromDIP(24) <= cost_wrap_w);
            }
            if (want_cost_horiz != costing_horizontal_) {
                costing_horizontal_ = want_cost_horiz;
                cost_sizer_->SetOrientation(want_cost_horiz ? wxHORIZONTAL : wxVERTICAL);
                if (auto* it_c = cost_sizer_->GetItem(lbl_cost_)) {
                    it_c->SetProportion(0);
                    it_c->SetFlag(want_cost_horiz ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    it_c->SetBorder(want_cost_horiz ? FromDIP(16) : 0);
                }
                if (auto* it_m = cost_sizer_->GetItem(lbl_margin_)) {
                    it_m->SetProportion(0);
                    it_m->SetFlag(want_cost_horiz ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                    it_m->SetBorder(want_cost_horiz ? 0 : FromDIP(4));
                }
            }
            if (want_cost_horiz) {
                lbl_cost_->SetMinSize(wxDefaultSize);
                lbl_cost_->SetLabel(wxString::FromUTF8(last_cost_text_));
                lbl_cost_->InvalidateBestSize();
                lbl_margin_->SetMinSize(wxDefaultSize);
                lbl_margin_->SetLabel(wxString::FromUTF8(last_margin_text_));
                lbl_margin_->InvalidateBestSize();
            } else {
                wrap_static_text_deterministic(lbl_cost_, wxString::FromUTF8(last_cost_text_), cost_wrap_w);
                if (margin_vis) {
                    wrap_static_text_deterministic(lbl_margin_, wxString::FromUTF8(last_margin_text_), cost_wrap_w);
                }
            }
            pnl_costing_->Layout();
        }
        if (pnl_direct_stock_ && pnl_direct_stock_->IsShown()) {
            const int ds_wrap_w = std::max(FromDIP(140), inner_w - FromDIP(16));
            if (lbl_direct_stock_status_ && lbl_direct_stock_status_->IsShown() && !last_direct_stock_status_.empty()) {
                wrap_static_text_deterministic(lbl_direct_stock_status_, wxString::FromUTF8(last_direct_stock_status_), ds_wrap_w);
            }
            if (lbl_direct_stock_hint_ && lbl_direct_stock_hint_->IsShown() && !last_direct_stock_hint_.empty()) {
                // Conservar el texto sin '\n' artificiales y reservar la altura exacta para el ajuste nativo del control Static.
                const wxString raw_hint = wxString::FromUTF8(last_direct_stock_hint_);
                wxClientDC hdc(lbl_direct_stock_hint_);
                hdc.SetFont(lbl_direct_stock_hint_->GetFont());
                int line_count = 1;
                wxString cur;
                size_t pos = 0;
                while (pos < raw_hint.length()) {
                    size_t next = raw_hint.find(wxT(' '), pos);
                    wxString word = (next == wxString::npos) ? raw_hint.Mid(pos) : raw_hint.Mid(pos, next - pos);
                    wxString cand = cur.IsEmpty() ? word : (cur + wxT(" ") + word);
                    if (!cur.IsEmpty() && hdc.GetTextExtent(cand).GetWidth() > ds_wrap_w) {
                        ++line_count;
                        cur = word;
                    } else {
                        cur = cand;
                    }
                    if (next == wxString::npos) break;
                    pos = next + 1;
                }
                const int line_h = hdc.GetTextExtent(wxT("Ág")).GetHeight();
                lbl_direct_stock_hint_->SetMinSize(wxDefaultSize);
                lbl_direct_stock_hint_->SetLabel(raw_hint);
                lbl_direct_stock_hint_->InvalidateBestSize();
                lbl_direct_stock_hint_->SetMinSize(wxSize(FromDIP(60), line_count * line_h + FromDIP(2)));
            }
            pnl_direct_stock_->Layout();
        }

        if (recipe_list_ && pnl_recipe_editor_ && pnl_recipe_editor_->IsShown()) {
            const int base_h = FromDIP(touch ? 132 : 112);
            recipe_list_->SetMinSize(wxSize(-1, base_h));
            pnl_recipe_editor_->Layout();
            pnl_recipe_->Layout();
            detail_sizer_->Layout();
            const int avail_h = detail_scroll_->GetClientSize().GetHeight();
            const int min_h = detail_sizer_->GetMinSize().GetHeight();
            if (avail_h > min_h + FromDIP(8)) {
                const int extra = avail_h - min_h - FromDIP(4);
                recipe_list_->SetMinSize(wxSize(-1, base_h + extra));
            }
        }

        if (pnl_recipe_editor_) pnl_recipe_editor_->Layout();
        if (pnl_recipe_) pnl_recipe_->Layout();
        detail_sizer_->Layout();
        detail_scroll_->FitInside();
        detail_scroll_->Layout();
        if (detail_card_) detail_card_->Layout();
    };

    const int w1 = std::max(FromDIP(180), detail_scroll_->GetClientSize().GetWidth());
    run_pass(w1);
    const int w2 = std::max(FromDIP(180), detail_scroll_->GetClientSize().GetWidth());
    if (w2 != w1) {
        run_pass(w2);
    }
    if (pnl_recipe_editor_ && pnl_recipe_editor_->IsShown()) {
        update_recipe_column_widths();
    }
}

void InventoryProductsPanel::start_new_product() {
    if (keypad_) keypad_->hide_keypad(false);
    editing_product_id_.clear();
    editing_is_active_ = true;
    editing_has_image_ = false;
    editing_stock_tracked_ = false;
    editing_stock_units_ = 0;
    pending_image_bytes_.clear();
    pending_image_mime_.clear();

    if (chk_track_stock_) {
        chk_track_stock_->SetValue(true);
    }

    if (list_) {
        sync_selection_guard_ = true;
        const long sel = list_->GetFirstSelected();
        if (sel >= 0) list_->Select(sel, false);
        sync_selection_guard_ = false;
    }

    lbl_detail_title_->SetLabel(wxT("Producto nuevo"));
    txt_name_->ChangeValue(wxEmptyString);
    txt_price_->ChangeValue(wxEmptyString);
    txt_sku_->ChangeValue(wxEmptyString);
    txt_barcode_->ChangeValue(wxEmptyString);
    txt_category_->ChangeValue(wxEmptyString);
    set_selector_index(ch_category_, cb_category_, 0);
    sync_category_input_visibility(false);
    is_prepared_selected_ = false;
    btn_toggle_active_->Hide();
    clear_image_preview(wxT("Sin imagen"));

    recipe_rows_.clear();
    rebuild_recipe_list_ui();
    if (txt_recipe_qty_) txt_recipe_qty_->ChangeValue(wxEmptyString);
    set_recipe_feedback("", false);
    set_feedback("", false);
    update_type_buttons_ui();
}

void InventoryProductsPanel::load_product_into_form(const std::string& product_id) {
    if (!catalog_service_) return;
    auto res = catalog_service_->get_product_by_id(product_id);
    if (!res.is_success()) {
        set_feedback(res.error_message(), true);
        return;
    }
    const auto& p = res.value();
    editing_product_id_ = p.id;
    editing_is_active_ = p.is_active;
    editing_has_image_ = p.has_image;
    editing_stock_tracked_ = p.stock_tracked;
    editing_stock_units_ = p.stock_units;
    pending_image_bytes_.clear();
    pending_image_mime_.clear();

    lbl_detail_title_->SetLabel(wxT("Detalle del producto"));
    txt_name_->ChangeValue(wxString::FromUTF8(p.name));
    txt_price_->ChangeValue(wxString::FromUTF8(format_pesos_plain(p.price)));
    txt_sku_->ChangeValue(wxString::FromUTF8(p.sku));
    txt_barcode_->ChangeValue(wxString::FromUTF8(p.barcode));
    int cat_sel = static_cast<int>(categories_.size());
    for (std::size_t i = 0; i < categories_.size(); ++i) {
        if (categories_[i] == p.category) {
            cat_sel = static_cast<int>(i);
            break;
        }
    }
    set_selector_index(ch_category_, cb_category_, cat_sel);
    if (cat_sel >= static_cast<int>(categories_.size())) {
        txt_category_->ChangeValue(wxString::FromUTF8(p.category));
    } else {
        txt_category_->ChangeValue(wxEmptyString);
    }
    sync_category_input_visibility(false);

    is_prepared_selected_ = p.is_prepared;
    btn_toggle_active_->SetLabel(p.is_active ? wxT("Dar de baja") : wxT("Reactivar"));
    btn_toggle_active_->Show();
    ensure_btn_width_fits(btn_toggle_active_, 120, Theme::is_touch() ? 48 : 34);

    if (p.has_image) {
        auto img_res = catalog_service_->get_product_image(p.id);
        if (img_res.is_success()) {
            load_image_preview_from_bytes(img_res.value().thumbnail_blob, wxT("Imagen asignada"));
            btn_remove_image_->Enable(true);
        } else {
            clear_image_preview(wxT("Sin imagen"));
        }
    } else {
        clear_image_preview(wxT("Sin imagen"));
    }

    set_feedback("", false);
    set_recipe_feedback("", false);
    if (p.is_prepared) {
        reload_recipe_and_costing();
    } else {
        recipe_rows_.clear();
        rebuild_recipe_list_ui();
    }
    update_type_buttons_ui();
}

void InventoryProductsPanel::load_image_preview_from_bytes(
    const std::vector<std::uint8_t>& bytes,
    const wxString& status_text) {
    if (!bmp_thumb_) return;
    if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) {
        wxImage::AddHandler(new wxPNGHandler());
    }
    if (!wxImage::FindHandler(wxBITMAP_TYPE_JPEG)) {
        wxImage::AddHandler(new wxJPEGHandler());
    }
    const wxSize thumb_sz = FromDIP(wxSize(48, 48));
    if (!bytes.empty()) {
        wxMemoryInputStream mis(bytes.data(), bytes.size());
        wxImage img;
        if (img.LoadFile(mis) && img.IsOk()) {
            img.Rescale(thumb_sz.GetWidth(), thumb_sz.GetHeight(), wxIMAGE_QUALITY_HIGH);
            bmp_thumb_->SetBitmap(wxBitmap(img));
            lbl_image_status_->SetLabel(status_text);
            btn_remove_image_->Enable(true);
            update_detail_scroll_layout();
            return;
        }
    }
    clear_image_preview(wxT("Sin imagen"));
}

void InventoryProductsPanel::clear_image_preview(const wxString& status_text) {
    if (!bmp_thumb_) return;
    bmp_thumb_->SetBitmap(make_placeholder_bitmap(FromDIP(wxSize(48, 48))));
    lbl_image_status_->SetLabel(status_text);
    btn_remove_image_->Enable(false);
    update_detail_scroll_layout();
}

void InventoryProductsPanel::reload_recipe_and_costing() {
    recipe_rows_.clear();
    if (!catalog_service_ || editing_product_id_.empty()) {
        rebuild_recipe_list_ui();
        return;
    }

    auto rcp_res = catalog_service_->get_recipe(editing_product_id_);
    if (rcp_res.is_success()) {
        for (const auto& it : rcp_res.value().items) {
            RecipeDraftRow row;
            row.ingredient_id = it.ingredient_id;
            row.ingredient_name = it.ingredient_name;
            row.base_unit = it.base_unit;
            row.unit_dimension = dimension_from_base_unit(it.base_unit);
            for (const auto& ing : ingredients_) {
                if (ing.id == it.ingredient_id) {
                    row.unit_dimension = ing.unit_dimension;
                    break;
                }
            }
            row.quantity_base_units = it.quantity_base_units;
            recipe_rows_.push_back(std::move(row));
        }
    }
    rebuild_recipe_list_ui();

    auto cost_res = catalog_service_->get_recipe_costing(editing_product_id_);
    if (!cost_res.is_success()) {
        last_margin_text_.clear();
        if (cost_res.error_code() == contracts::ErrorCode::NotFound) {
            last_cost_text_ = "Todavía no hay receta guardada para calcular el costo.";
            lbl_cost_->SetForegroundColour(Theme::ColorTextSecondary);
            lbl_margin_->Hide();
        } else {
            last_cost_text_ = cost_res.error_message();
            lbl_cost_->SetForegroundColour(Theme::ColorError);
            lbl_margin_->Hide();
        }
        lbl_cost_->SetLabel(wxString::FromUTF8(last_cost_text_));
        update_detail_scroll_layout();
        return;
    }

    const auto& c = cost_res.value();
    if (!c.all_costs_known) {
        std::string names;
        for (std::size_t i = 0; i < c.unknown_ingredient_names.size(); ++i) {
            if (i > 0) names += ", ";
            names += c.unknown_ingredient_names[i];
        }
        last_cost_text_ = names.empty()
            ? "Falta registrar compras para calcular el costo."
            : "Falta registrar una compra de: " + names;
        last_margin_text_.clear();
        lbl_cost_->SetLabel(wxString::FromUTF8(last_cost_text_));
        lbl_cost_->SetForegroundColour(Theme::ColorError);
        lbl_margin_->Hide();
    } else {
        std::string cost_line = "Costo por porción: " + c.cost_per_portion.to_string();
        if (c.uses_last_known_cost) {
            cost_line += " (se usó el último costo conocido)";
        }
        last_cost_text_ = cost_line;
        lbl_cost_->SetLabel(wxString::FromUTF8(last_cost_text_));
        lbl_cost_->SetForegroundColour(Theme::ColorText);

        if (c.margin_known) {
            std::string margin_line = "Margen: " + c.margin.to_string();
            if (c.margin_percent_known) {
                margin_line += " (" + format_margin_percent(c.margin_basis_points) + ")";
            }
            last_margin_text_ = margin_line;
            lbl_margin_->SetLabel(wxString::FromUTF8(last_margin_text_));
            lbl_margin_->SetForegroundColour(c.margin.cents() < 0 ? Theme::ColorError : Theme::ColorSuccess);
            lbl_margin_->Show();
        } else {
            last_margin_text_.clear();
            lbl_margin_->Hide();
        }
    }
    update_detail_scroll_layout();
}

void InventoryProductsPanel::rebuild_recipe_list_ui(const std::string& keep_ing_id) {
    if (!recipe_list_) return;
    recipe_list_->DeleteAllItems();
    long sel_idx = -1;
    for (std::size_t i = 0; i < recipe_rows_.size(); ++i) {
        const auto& r = recipe_rows_[i];
        const long idx = recipe_list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(r.ingredient_name));
        recipe_list_->SetItem(
            idx, 1, wxString::FromUTF8(format_readable_recipe_qty(r.quantity_base_units, r.unit_dimension))
        );
        if (!keep_ing_id.empty() && r.ingredient_id == keep_ing_id) {
            sel_idx = idx;
        }
    }
    if (sel_idx >= 0) {
        recipe_list_->SetItemState(
            sel_idx, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED
        );
    }
    update_recipe_column_widths();
}

void InventoryProductsPanel::set_feedback(const std::string& message, bool is_error) {
    last_feedback_ = message;
    last_feedback_is_error_ = is_error;
    if (!lbl_feedback_) return;
    lbl_feedback_->SetForegroundColour(is_error ? Theme::ColorError : Theme::ColorSuccess);
    lbl_feedback_->SetLabel(wxString::FromUTF8(message));
    update_detail_scroll_layout();
}

void InventoryProductsPanel::set_recipe_feedback(const std::string& message, bool is_error) {
    last_recipe_feedback_ = message;
    last_recipe_feedback_is_error_ = is_error;
    if (!lbl_recipe_feedback_) return;
    lbl_recipe_feedback_->SetForegroundColour(is_error ? Theme::ColorError : Theme::ColorSuccess);
    lbl_recipe_feedback_->SetLabel(wxString::FromUTF8(message));
    update_detail_scroll_layout();
}

void InventoryProductsPanel::on_new_product(wxCommandEvent&) {
    if (destroying_) return;
    start_new_product();
}

void InventoryProductsPanel::on_refresh(wxCommandEvent&) {
    if (destroying_) return;
    refresh_all(editing_product_id_);
}

void InventoryProductsPanel::on_search_changed(wxCommandEvent&) {
    if (destroying_) return;
    reload_product_list(editing_product_id_);
}

void InventoryProductsPanel::on_filter_category_changed(wxCommandEvent&) {
    if (destroying_) return;
    reload_product_list(editing_product_id_);
}

void InventoryProductsPanel::on_toggle_inactive(wxCommandEvent&) {
    if (destroying_) return;
    include_inactive_ = !include_inactive_;
    const int btn_h_logical = Theme::is_touch() ? 48 : 34;
    if (include_inactive_) {
        btn_toggle_inactive_->SetLabel(wxT("Viendo dados de baja"));
        Theme::style_button_primary(btn_toggle_inactive_, btn_h_logical);
    } else {
        btn_toggle_inactive_->SetLabel(wxT("Ver dados de baja"));
        Theme::style_button_secondary(btn_toggle_inactive_, btn_h_logical);
    }
    ensure_btn_width_fits(btn_toggle_inactive_, 132, btn_h_logical);
    list_card_->Layout();
    reload_product_list(editing_product_id_);
}

void InventoryProductsPanel::on_product_selected(wxListEvent& event) {
    if (destroying_ || sync_selection_guard_) return;
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= products_.size()) return;
    load_product_into_form(products_[static_cast<std::size_t>(row)].id);
}

void InventoryProductsPanel::on_category_combo_changed(wxCommandEvent&) {
    if (destroying_) return;
    sync_category_input_visibility(true);
    update_detail_scroll_layout();
}

void InventoryProductsPanel::on_type_prepared(wxCommandEvent&) {
    if (destroying_) return;
    is_prepared_selected_ = true;
    update_type_buttons_ui();
    if (!editing_product_id_.empty()) {
        reload_recipe_and_costing();
    }
}

void InventoryProductsPanel::on_type_direct(wxCommandEvent&) {
    if (destroying_) return;
    is_prepared_selected_ = false;
    update_type_buttons_ui();
}

void InventoryProductsPanel::on_enable_direct_stock(wxCommandEvent&) {
    if (destroying_ || !catalog_service_ || editing_product_id_.empty()) return;
    auto res = catalog_service_->enable_stock_for_direct_product(editing_product_id_);
    if (!res.is_success()) {
        set_feedback(res.error_message(), true);
        return;
    }
    const std::string target_id = editing_product_id_;
    refresh_all(target_id);
    load_product_into_form(target_id);
    set_feedback("Control de existencia activado.", false);
}

void InventoryProductsPanel::on_pick_image(wxCommandEvent&) {
    if (destroying_ || !catalog_service_) return;
    wxFileDialog dlg(
        this,
        wxT("Seleccionar imagen del producto"),
        wxEmptyString,
        wxEmptyString,
        wxT("Imágenes (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );
    if (dlg.ShowModal() != wxID_OK) return;

    wxFile file(dlg.GetPath(), wxFile::read);
    if (!file.IsOpened()) {
        set_feedback("No se pudo abrir el archivo de imagen.", true);
        return;
    }
    const wxFileOffset len = file.Length();
    if (len <= 0 || len > 5 * 1024 * 1024) {
        set_feedback("El archivo de imagen está vacío o es demasiado grande.", true);
        return;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(len));
    if (file.Read(bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size())) {
        set_feedback("No se pudo leer el archivo de imagen.", true);
        return;
    }

    const wxString ext = wxFileName(dlg.GetPath()).GetExt().Lower();
    const std::string mime = (ext == wxT("png")) ? "image/png" : "image/jpeg";

    if (editing_product_id_.empty()) {
        pending_image_bytes_ = std::move(bytes);
        pending_image_mime_ = mime;
        load_image_preview_from_bytes(pending_image_bytes_, wxT("Imagen lista para guardar"));
        set_feedback("", false);
        return;
    }

    auto res = catalog_service_->set_product_image(editing_product_id_, mime, bytes);
    if (!res.is_success()) {
        set_feedback(res.error_message(), true);
        return;
    }
    editing_has_image_ = true;
    load_image_preview_from_bytes(bytes, wxT("Imagen asignada"));
    set_feedback("Imagen guardada.", false);
}

void InventoryProductsPanel::on_remove_image(wxCommandEvent&) {
    if (destroying_ || !catalog_service_) return;
    if (editing_product_id_.empty()) {
        pending_image_bytes_.clear();
        pending_image_mime_.clear();
        clear_image_preview(wxT("Sin imagen"));
        return;
    }
    auto res = catalog_service_->remove_product_image(editing_product_id_);
    if (!res.is_success()) {
        set_feedback(res.error_message(), true);
        return;
    }
    editing_has_image_ = false;
    clear_image_preview(wxT("Sin imagen"));
    set_feedback("Imagen quitada.", false);
}

void InventoryProductsPanel::on_save_product(wxCommandEvent&) {
    if (destroying_ || !catalog_service_) return;
    if (keypad_) keypad_->hide_keypad(false);

    const std::string name = trim_ascii(std::string(txt_name_->GetValue().ToUTF8().data()));
    const std::string sku = trim_ascii(std::string(txt_sku_->GetValue().ToUTF8().data()));
    const std::string barcode = trim_ascii(std::string(txt_barcode_->GetValue().ToUTF8().data()));
    std::string cat_err;
    const std::string category = resolve_selected_category(cat_err);
    const std::string price_raw = trim_ascii(std::string(txt_price_->GetValue().ToUTF8().data()));

    if (!cat_err.empty()) {
        set_feedback(cat_err, true);
        return;
    }

    if (price_raw.empty()) {
        set_feedback("Escribe el precio en pesos.", true);
        return;
    }
    auto price_res = contracts::Money::try_parse(price_raw, false);
    if (!price_res.is_success()) {
        set_feedback(price_res.error_message(), true);
        return;
    }

    std::string target_id = editing_product_id_;
    if (editing_product_id_.empty()) {
        contracts::CreateProductDto dto;
        dto.name = name;
        dto.sku = sku;
        dto.barcode = barcode;
        dto.category = category;
        dto.price = price_res.value();
        dto.is_prepared = is_prepared_selected_;
        dto.track_stock = is_prepared_selected_ ? false : (chk_track_stock_ ? chk_track_stock_->GetValue() : true);
        auto res = catalog_service_->create_product(dto);
        if (!res.is_success()) {
            set_feedback(res.error_message(), true);
            return;
        }
        target_id = res.value().id;
        if (!pending_image_bytes_.empty()) {
            auto img_res = catalog_service_->set_product_image(target_id, pending_image_mime_, pending_image_bytes_);
            if (!img_res.is_success()) {
                refresh_all(target_id);
                load_product_into_form(target_id);
                set_feedback(img_res.error_message(), true);
                return;
            }
            pending_image_bytes_.clear();
            pending_image_mime_.clear();
        }
        refresh_all(target_id);
        load_product_into_form(target_id);
        set_feedback("Producto guardado.", false);
    } else {
        contracts::UpdateProductDto dto;
        dto.product_id = editing_product_id_;
        dto.name = name;
        dto.sku = sku;
        dto.barcode = barcode;
        dto.category = category;
        dto.price = price_res.value();
        dto.is_prepared = is_prepared_selected_;
        auto res = catalog_service_->update_product(dto);
        if (!res.is_success()) {
            set_feedback(res.error_message(), true);
            return;
        }
        refresh_all(target_id);
        load_product_into_form(target_id);
        set_feedback("Cambios guardados.", false);
    }
}

void InventoryProductsPanel::on_toggle_active_product(wxCommandEvent&) {
    if (destroying_ || !catalog_service_ || editing_product_id_.empty()) return;
    const std::string name = trim_ascii(std::string(txt_name_->GetValue().ToUTF8().data()));
    const wxString q = editing_is_active_
        ? wxString::Format(wxT("¿Seguro que deseas dar de baja \"%s\"?"), wxString::FromUTF8(name))
        : wxString::Format(wxT("¿Seguro que deseas reactivar \"%s\"?"), wxString::FromUTF8(name));
    wxMessageDialog dlg(
        this, q, wxT("Confirmar cambio de estado"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION
    );
    if (dlg.ShowModal() != wxID_YES) return;

    auto res = editing_is_active_
        ? catalog_service_->deactivate_product(editing_product_id_)
        : catalog_service_->reactivate_product(editing_product_id_);
    if (!res.is_success()) {
        set_feedback(res.error_message(), true);
        return;
    }
    const bool deactivated_now = editing_is_active_;
    const std::string id_keep = editing_product_id_;
    refresh_all(id_keep);
    if (deactivated_now && !include_inactive_) {
        start_new_product();
        set_feedback("Producto dado de baja.", false);
    } else {
        load_product_into_form(id_keep);
        set_feedback(deactivated_now ? "Producto dado de baja." : "Producto reactivado.", false);
    }
}

void InventoryProductsPanel::on_recipe_ing_changed(wxCommandEvent&) {
    if (destroying_) return;
    update_recipe_unit_choices();
}

void InventoryProductsPanel::on_recipe_row_selected(wxListEvent& event) {
    if (destroying_) return;
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= recipe_rows_.size()) return;
    const auto& r = recipe_rows_[static_cast<std::size_t>(row)];
    for (std::size_t i = 0; i < ingredients_.size(); ++i) {
        if (ingredients_[i].id == r.ingredient_id) {
            set_selector_index(ch_recipe_ing_, cb_recipe_ing_, static_cast<int>(i));
            update_recipe_unit_choices();
            std::string qty_s;
            std::string unit_c;
            decompose_base_to_input(r.quantity_base_units, r.unit_dimension, qty_s, unit_c);
            txt_recipe_qty_->ChangeValue(wxString::FromUTF8(qty_s));
            for (std::size_t u = 0; u < recipe_units_.size(); ++u) {
                if (recipe_units_[u] == unit_c) {
                    set_selector_index(ch_recipe_unit_, cb_recipe_unit_, static_cast<int>(u));
                    break;
                }
            }
            break;
        }
    }
}

void InventoryProductsPanel::on_recipe_add_or_update(wxCommandEvent&) {
    if (destroying_) return;
    if (keypad_) keypad_->hide_keypad(false);
    const std::string ing_id = selected_recipe_ingredient_id();
    if (ing_id.empty()) {
        set_recipe_feedback("Elige un ingrediente.", true);
        return;
    }

    std::int64_t qty_base = 0;
    std::string err;
    const std::string unit_code = selected_recipe_unit_code();
    const std::string raw_qty = std::string(txt_recipe_qty_->GetValue().ToUTF8().data());
    if (!ui::parse_quantity_to_base_units(raw_qty, unit_code, qty_base, err)) {
        set_recipe_feedback(err, true);
        return;
    }

    std::string ing_name;
    std::string dim = "MASS";
    std::string base_unit = "mg";
    for (const auto& ing : ingredients_) {
        if (ing.id == ing_id) {
            ing_name = ing.name;
            dim = ing.unit_dimension;
            base_unit = ing.base_unit;
            break;
        }
    }

    bool updated = false;
    for (auto& r : recipe_rows_) {
        if (r.ingredient_id == ing_id) {
            r.quantity_base_units = qty_base;
            updated = true;
            break;
        }
    }
    if (!updated) {
        recipe_rows_.push_back({ing_id, ing_name, dim, base_unit, qty_base});
    }
    txt_recipe_qty_->ChangeValue(wxEmptyString);
    rebuild_recipe_list_ui(ing_id);
    set_recipe_feedback("Ingrediente listo. Pulsa \"Guardar receta\" para aplicar.", false);
}

void InventoryProductsPanel::on_recipe_remove_item(wxCommandEvent&) {
    if (destroying_ || !recipe_list_) return;
    const long sel = recipe_list_->GetFirstSelected();
    if (sel < 0 || static_cast<std::size_t>(sel) >= recipe_rows_.size()) {
        set_recipe_feedback("Selecciona un ingrediente de la receta para quitarlo.", true);
        return;
    }
    recipe_rows_.erase(recipe_rows_.begin() + sel);
    rebuild_recipe_list_ui();
    set_recipe_feedback("Ingrediente quitado. Pulsa \"Guardar receta\" para aplicar.", false);
}

void InventoryProductsPanel::on_recipe_save(wxCommandEvent&) {
    if (destroying_ || !catalog_service_ || editing_product_id_.empty()) return;
    if (keypad_) keypad_->hide_keypad(false);

    std::vector<contracts::RecipeItemDto> items;
    items.reserve(recipe_rows_.size());
    for (const auto& r : recipe_rows_) {
        contracts::RecipeItemDto item;
        item.ingredient_id = r.ingredient_id;
        item.ingredient_name = r.ingredient_name;
        item.quantity_base_units = r.quantity_base_units;
        item.base_unit = r.base_unit;
        items.push_back(std::move(item));
    }

    auto res = catalog_service_->set_recipe(editing_product_id_, items);
    if (!res.is_success()) {
        set_recipe_feedback(res.error_message(), true);
        return;
    }
    reload_recipe_and_costing();
    set_recipe_feedback("Receta guardada.", false);
}

void InventoryProductsPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted()) return;
        apply_responsive_layout();
        Layout();
        update_empty_state();
        update_list_column_widths();
        if (pnl_recipe_editor_ && pnl_recipe_editor_->IsShown()) {
            update_recipe_column_widths();
        }
        update_detail_scroll_layout();
        Layout();
    });
}

std::string InventoryProductsPanel::selected_filter_category() const {
    const int idx = get_selector_index(ch_filter_cat_, cb_filter_cat_);
    if (idx > 0 && static_cast<std::size_t>(idx - 1) < categories_.size()) {
        return categories_[static_cast<std::size_t>(idx - 1)];
    }
    return "";
}

std::string InventoryProductsPanel::selected_recipe_ingredient_id() const {
    const int idx = get_selector_index(ch_recipe_ing_, cb_recipe_ing_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ingredients_.size()) {
        return ingredients_[static_cast<std::size_t>(idx)].id;
    }
    return "";
}

std::string InventoryProductsPanel::selected_recipe_ingredient_dimension() const {
    const int idx = get_selector_index(ch_recipe_ing_, cb_recipe_ing_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < ingredients_.size()) {
        return ingredients_[static_cast<std::size_t>(idx)].unit_dimension;
    }
    return "MASS";
}

std::string InventoryProductsPanel::selected_recipe_unit_code() const {
    const int idx = get_selector_index(ch_recipe_unit_, cb_recipe_unit_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < recipe_units_.size()) {
        return recipe_units_[static_cast<std::size_t>(idx)];
    }
    return recipe_units_.empty() ? "g" : recipe_units_.front();
}

} // namespace app::frontend
