#include "src/frontend/pos_panel.hpp"
#include "src/frontend/checkout_dialog.hpp"
#include "src/frontend/ticket_print_dialog.hpp"
#include "src/frontend/ticket_history_dialog.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"
#include <wx/statline.h>
#include <wx/imaglist.h>
#include <wx/dcclient.h>
#include <wx/wrapsizer.h>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace app::frontend {

namespace {

std::string format_line_discount_text(const contracts::CartItemDto& item) {
    if (item.line_discount.cents() <= 0) {
        return "";
    }
    const std::string promo = item.promotion_name.empty() ? std::string("Descuento") : item.promotion_name;
    return promo + ": -" + item.line_discount.to_string();
}

} // namespace

PosPanel::PosPanel(
    wxWindow* parent,
    services::ServiceRegistry services
) : wxPanel(parent, wxID_ANY), services_(std::move(services)) {
    setup_ui();
    update_shift_display();
    load_catalog_products();
    refresh_cart();
}

PosPanel::~PosPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &PosPanel::on_size, this);
    if (list_items_) {
        list_items_->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &PosPanel::on_item_activated, this);
    }
    if (keypad_) {
        keypad_->hide_keypad(false);
    }
    DestroyChildren();
}

void PosPanel::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    bool touch = Theme::is_touch();
    auto* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    // =========================================================================
    // Columna Izquierda: Captura de Pedido, Búsqueda, Notas y Tabla de Artículos
    // =========================================================================
    left_box_ = new wxBoxSizer(wxVERTICAL);
    auto* left_box = left_box_;

    // Tarjeta superior para búsqueda, selección y notas
    auto* search_card = new CardPanel(this);
    auto* card_inner_sizer = new wxBoxSizer(wxVERTICAL);

    // ORDEN ESTRICTO DE CREACIÓN DE TEXTCTRL PARA TEST AUTOMATIZADO (edits[0], edits[1], edits[2]):
    // 1. txt_search_ en contenedor centrado verticalmente
    auto* search_box = new wxPanel(search_card, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(touch ? 120 : 120, touch ? 48 : 32)), wxBORDER_SIMPLE);
    search_box->SetBackgroundColour(Theme::ColorSurface);
    txt_search_ = new wxTextCtrl(search_box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTE_PROCESS_ENTER);
    txt_search_->SetHint(wxT("Buscar código o nombre..."));
    txt_search_->SetFont(touch ? Theme::get_font_body(wxFONTWEIGHT_MEDIUM) : Theme::get_font_body());
    txt_search_->SetBackgroundColour(Theme::ColorSurface);
    auto* search_box_sizer = new wxBoxSizer(wxVERTICAL);
    search_box_sizer->AddStretchSpacer(1);
    search_box_sizer->Add(txt_search_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(6));
    search_box_sizer->AddStretchSpacer(1);
    search_box->SetSizer(search_box_sizer);
    search_box->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        txt_search_->SetFocus();
    });

    // 2. txt_qty_ en contenedor centrado vertical y horizontalmente (elimina desfase superior en campos altos)
    auto* qty_box = new wxPanel(search_card, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(touch ? 64 : 48, touch ? 48 : 32)), wxBORDER_SIMPLE);
    qty_box->SetBackgroundColour(Theme::ColorSurface);
    txt_qty_ = new wxTextCtrl(qty_box, wxID_ANY, wxT("1"), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTE_CENTRE);
    txt_qty_->SetFont(Theme::get_font_body());
    txt_qty_->SetBackgroundColour(Theme::ColorSurface);
    auto* qty_box_sizer = new wxBoxSizer(wxVERTICAL);
    qty_box_sizer->AddStretchSpacer(1);
    qty_box_sizer->Add(txt_qty_, 0, wxEXPAND);
    qty_box_sizer->AddStretchSpacer(1);
    qty_box->SetSizer(qty_box_sizer);
    qty_box->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& evt) {
        evt.Skip();
        txt_qty_->SetFocus();
        txt_qty_->SelectAll();
    });

    // 3. txt_notes_ en contenedor centrado verticalmente
    auto* notes_box = new wxPanel(search_card, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, touch ? 48 : 32)), wxBORDER_SIMPLE);
    notes_box->SetBackgroundColour(Theme::ColorSurface);
    txt_notes_ = new wxTextCtrl(notes_box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTE_PROCESS_ENTER);
    txt_notes_->SetHint(wxT("Nota de preparación (ej. sin hielo, con poca azúcar, para llevar...)"));
    txt_notes_->SetFont(touch ? Theme::get_font_body(wxFONTWEIGHT_MEDIUM) : Theme::get_font_body());
    txt_notes_->SetBackgroundColour(Theme::ColorSurface);
    auto* notes_box_sizer = new wxBoxSizer(wxVERTICAL);
    notes_box_sizer->AddStretchSpacer(1);
    notes_box_sizer->Add(txt_notes_, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(8));
    notes_box_sizer->AddStretchSpacer(1);
    notes_box->SetSizer(notes_box_sizer);
    notes_box->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        txt_notes_->SetFocus();
    });

    // Botones stepper para cantidad (facilitan interacción táctil sin depender de teclado)
    const int qty_btn_sz = touch ? 48 : 32;
    btn_qty_minus_ = new wxButton(search_card, wxID_ANY, wxT("−"));
    btn_qty_plus_ = new wxButton(search_card, wxID_ANY, wxT("+"));
    Theme::style_button_secondary(btn_qty_minus_, qty_btn_sz);
    Theme::style_button_secondary(btn_qty_plus_, qty_btn_sz);
    btn_qty_minus_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    btn_qty_plus_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    Theme::ensure_button_min_size(btn_qty_minus_, qty_btn_sz, qty_btn_sz);
    Theme::ensure_button_min_size(btn_qty_plus_, qty_btn_sz, qty_btn_sz);

    btn_qty_minus_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        long q = 1;
        txt_qty_->GetValue().ToLong(&q);
        if (q > 1) {
            txt_qty_->SetValue(wxString::Format(wxT("%ld"), q - 1));
        }
    });
    btn_qty_plus_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        long q = 1;
        txt_qty_->GetValue().ToLong(&q);
        if (q + 1 <= 999999) {
            txt_qty_->SetValue(wxString::Format(wxT("%ld"), q + 1));
        }
    });

    // Otros controles de la tarjeta
    btn_search_ = new wxButton(search_card, wxID_ANY, wxT("Buscar"));
    Theme::style_button_secondary(btn_search_, qty_btn_sz);
    Theme::setup_button_icon(btn_search_, "search", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorTextSecondary);
    Theme::ensure_button_min_size(btn_search_, touch ? 100 : 90, qty_btn_sz);

    choice_products_ = new wxOwnerDrawnComboBox(
        search_card,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        FromDIP(wxSize(-1, qty_btn_sz)),
        0,
        nullptr,
        wxCB_READONLY
    );
    choice_products_->SetFont(Theme::get_font_body());
    choice_products_->SetMinSize(FromDIP(wxSize(120, qty_btn_sz)));

    auto* lbl_qty = new wxStaticText(search_card, wxID_ANY, wxT("Cant:"));
    lbl_qty->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_qty->SetForegroundColour(Theme::ColorTextSecondary);

    // Etiqueta limpia sin + textual repetido y un único icono Material add
    const int add_btn_h = touch ? 56 : 36;
    btn_add_ = new wxButton(search_card, wxID_ANY, wxT("Agregar al Carrito"));
    Theme::style_button_primary(btn_add_, add_btn_h);
    Theme::setup_button_icon(btn_add_, "add", wxSize(touch ? 22 : 18, touch ? 22 : 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_add_, touch ? 210 : 180, add_btn_h);

    // Distribución en 3 filas (Estándar y Táctil) para evitar recortes en 960x600:
    // Fila 1: Búsqueda y Selección en catálogo
    const int row_gap = FromDIP(touch ? 10 : 6);
    auto* search_row = new wxBoxSizer(wxHORIZONTAL);
    search_row->Add(search_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, row_gap);
    search_row->Add(btn_search_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, row_gap);
    search_row->Add(choice_products_, 2, wxALIGN_CENTER_VERTICAL);
    card_inner_sizer->Add(search_row, 0, wxEXPAND | wxBOTTOM, row_gap);

    // Fila 2: Notas específicas de preparación
    auto* notes_row = new wxBoxSizer(wxHORIZONTAL);
    notes_row->Add(notes_box, 1, wxALIGN_CENTER_VERTICAL);
    card_inner_sizer->Add(notes_row, 0, wxEXPAND | wxBOTTOM, row_gap);

    // Fila 3: Steppers de cantidad [−] [ 1 ] [+] y Botón Agregar (con wrap en 960x600)
    auto* qty_add_row = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    auto* qty_group = new wxBoxSizer(wxHORIZONTAL);
    qty_group->Add(lbl_qty, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(touch ? 8 : 4));
    qty_group->Add(btn_qty_minus_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(touch ? 6 : 3));
    qty_group->Add(qty_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(touch ? 6 : 3));
    qty_group->Add(btn_qty_plus_, 0, wxALIGN_CENTER_VERTICAL);
    qty_add_row->Add(qty_group, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(touch ? 8 : 4));
    qty_add_row->Add(btn_add_, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, FromDIP(touch ? 4 : 2));
    card_inner_sizer->Add(qty_add_row, 0, wxEXPAND);

    search_card->SetSizer(card_inner_sizer);
    left_box->Add(search_card, 0, wxEXPAND | wxBOTTOM, FromDIP(touch ? 12 : 10));

    // Tabla de líneas de venta (ListCtrl)
    list_items_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_items_->SetFont(Theme::get_font_body());

    // Altura de fila de la tabla del carrito >= 40 px lógicos en Touch mediante ImageList
    auto* img_list = new wxImageList(1, list_items_->FromDIP(touch ? 42 : 24));
    list_items_->AssignImageList(img_list, wxIMAGE_LIST_SMALL);

    wxClientDC list_dc(list_items_);
    list_dc.SetFont(list_items_->GetFont());
    auto calc_col_width = [&](const wxString& title, int min_pad_logical, int fallback_w_logical) {
        int tw = 0, th = 0;
        list_dc.GetTextExtent(title, &tw, &th);
        return std::max(tw + list_items_->FromDIP(min_pad_logical), list_items_->FromDIP(fallback_w_logical));
    };

    const int header_pad_logical = touch ? 18 : 14;
    // Ancho mínimo de SKU calculado con GetTextExtent del SKU más largo ("BAG-01")
    // más ancho de imagen (1px) y padding/reserva nativa de icono en SysListView32
    int sku_sample_w = 0, sku_sample_h = 0;
    list_dc.GetTextExtent(wxT("BAG-01"), &sku_sample_w, &sku_sample_h);
    int img_and_native_pad = list_items_->FromDIP(touch ? 30 : 26);
    int col_sku = std::max(calc_col_width(wxT("SKU"), header_pad_logical, touch ? 55 : 50), sku_sample_w + 1 + img_and_native_pad);

    int col_cant = calc_col_width(wxT("Cant."), header_pad_logical + 2, touch ? 52 : 48);
    int col_subtotal = calc_col_width(wxT("Subtotal"), header_pad_logical + 4, touch ? 74 : 70);
    int col_descuento = calc_col_width(wxT("Descuento"), header_pad_logical + 4, touch ? 110 : 100);
    int col_total = calc_col_width(wxT("Total"), header_pad_logical + 4, touch ? 68 : 64);
    int col_prod = list_items_->FromDIP(touch ? 120 : 115);
    int col_notas = list_items_->FromDIP(touch ? 85 : 80);

    list_items_->InsertColumn(0, wxT("SKU"), wxLIST_FORMAT_LEFT, col_sku);
    list_items_->InsertColumn(1, wxT("Producto"), wxLIST_FORMAT_LEFT, col_prod);
    list_items_->InsertColumn(2, wxT("Notas"), wxLIST_FORMAT_LEFT, col_notas);
    list_items_->InsertColumn(3, wxT("Cant."), wxLIST_FORMAT_RIGHT, col_cant);
    list_items_->InsertColumn(4, wxT("Subtotal"), wxLIST_FORMAT_RIGHT, col_subtotal);
    list_items_->InsertColumn(5, wxT("Descuento"), wxLIST_FORMAT_LEFT, col_descuento);
    list_items_->InsertColumn(6, wxT("Total"), wxLIST_FORMAT_RIGHT, col_total);
    list_items_->SetMinSize(list_items_->FromDIP(wxSize(250, touch ? 80 : 100)));

    left_box->Add(list_items_, 1, wxEXPAND | wxBOTTOM, FromDIP(touch ? 6 : 6));

    lbl_cart_line_detail_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_cart_line_detail_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_cart_line_detail_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_cart_line_detail_->Hide();
    left_box->Add(lbl_cart_line_detail_, 0, wxEXPAND | wxBOTTOM, FromDIP(touch ? 8 : 6));

    // Fila inferior de tabla: Acciones contextuales (Editar cantidad, nota, quitar línea) y secundarios en Más acciones
    const int action_btn_h = touch ? 48 : 34;

    btn_edit_qty_ = new wxButton(this, wxID_ANY, wxT("Editar cantidad"));
    Theme::style_button_secondary(btn_edit_qty_, action_btn_h);
    Theme::setup_button_icon(btn_edit_qty_, "edit", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorTextSecondary);
    Theme::ensure_button_min_size(btn_edit_qty_, touch ? 170 : 145, action_btn_h);

    btn_edit_notes_ = new wxButton(this, wxID_ANY, wxT("Editar nota"));
    Theme::style_button_secondary(btn_edit_notes_, action_btn_h);
    Theme::setup_button_icon(btn_edit_notes_, "sticky_note_2", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorTextSecondary);
    Theme::ensure_button_min_size(btn_edit_notes_, touch ? 145 : 125, action_btn_h);

    btn_remove_item_ = new wxButton(this, wxID_ANY, wxT("Quitar línea"));
    Theme::style_button_destructive(btn_remove_item_, action_btn_h);
    Theme::setup_button_icon(btn_remove_item_, "delete", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorError);
    Theme::ensure_button_min_size(btn_remove_item_, touch ? 145 : 125, action_btn_h);

    btn_more_actions_ = new wxButton(this, wxID_ANY, wxT("Más acciones ▾"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_secondary(btn_more_actions_, action_btn_h);
    const wxSize more_icon_sz(touch ? 20 : 18, touch ? 20 : 18);
    Theme::setup_button_icon(btn_more_actions_, "more_horiz", more_icon_sz, Theme::ColorTextSecondary);
    int more_tw = 0, more_th = 0;
    btn_more_actions_->GetTextExtent(btn_more_actions_->GetLabel(), &more_tw, &more_th);
    const int more_icon_margins = btn_more_actions_->FromDIP(touch ? 10 : 8) * 2;
    const int more_btn_padding = btn_more_actions_->FromDIP(touch ? 28 : 24);
    const int more_w = more_tw + btn_more_actions_->FromDIP(more_icon_sz.GetWidth()) + more_icon_margins + more_btn_padding;
    btn_more_actions_->InvalidateBestSize();
    const wxSize more_best = btn_more_actions_->GetBestSize();
    const int final_more_w = std::max(more_w, more_best.GetWidth());
    const int final_more_h = std::max(btn_more_actions_->FromDIP(action_btn_h), more_best.GetHeight());
    btn_more_actions_->SetMinSize(wxSize(final_more_w, final_more_h));
    btn_more_actions_->SetSize(wxSize(final_more_w, final_more_h));

    auto* table_actions_wrap = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    const int act_gap = FromDIP(touch ? 8 : 6);
    table_actions_wrap->Add(btn_edit_qty_, 0, wxRIGHT | wxBOTTOM, act_gap);
    table_actions_wrap->Add(btn_edit_notes_, 0, wxRIGHT | wxBOTTOM, act_gap);
    table_actions_wrap->Add(btn_remove_item_, 0, wxRIGHT | wxBOTTOM, act_gap);
    table_actions_wrap->Add(btn_more_actions_, 0, wxBOTTOM, act_gap);
    left_box->Add(table_actions_wrap, 0, wxEXPAND);

    left_box->SetMinSize(FromDIP(wxSize(350, -1)));
    main_sizer->Add(left_box, 1, wxEXPAND | wxRIGHT, FromDIP(touch ? 16 : 14));

    // =========================================================================
    // Columna Derecha: CUENTA ACTUAL (Tarjeta blanca Cupertino)
    // =========================================================================
    auto* account_card = new CardPanel(this);
    const int card_w = FromDIP(touch ? 295 : 285);
    account_card->SetMinSize(wxSize(card_w, -1));
    account_card->SetMaxSize(wxSize(card_w, -1));

    auto* acc_sizer = new wxBoxSizer(wxVERTICAL);
    const int pad_lr = FromDIP(touch ? 18 : 16);

    auto* acc_title = new wxStaticText(account_card, wxID_ANY, wxT("CUENTA ACTUAL"));
    acc_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    acc_title->SetForegroundColour(Theme::ColorText);
    acc_sizer->Add(acc_title, 0, wxALL, pad_lr);

    lbl_shift_status_ = new wxStaticText(account_card, wxID_ANY, wxT("Turno: Verificando..."));
    lbl_shift_status_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_shift_status_->SetForegroundColour(Theme::ColorTextMuted);
    acc_sizer->Add(lbl_shift_status_, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);

    lbl_items_count_ = new wxStaticText(account_card, wxID_ANY, wxT("0 artículos en carrito"));
    lbl_items_count_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_items_count_->SetForegroundColour(Theme::ColorTextSecondary);
    acc_sizer->Add(lbl_items_count_, 0, wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);

    acc_sizer->Add(new wxStaticLine(account_card, wxID_ANY), 0, wxEXPAND | wxLEFT | wxRIGHT, pad_lr);
    acc_sizer->AddSpacer(FromDIP(touch ? 16 : 12));

    auto make_summary_row = [&](const wxString& label, wxStaticText** out_val, bool is_total = false, wxStaticText** out_cap = nullptr) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* l = new wxStaticText(account_card, wxID_ANY, label);
        auto* v = new wxStaticText(account_card, wxID_ANY, wxT("$0.00"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
        *out_val = v;
        if (out_cap) {
            *out_cap = l;
        }

        if (is_total) {
            l->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
            l->SetForegroundColour(Theme::ColorText);
            v->SetFont(Theme::get_font_hero(wxFONTWEIGHT_BOLD));
            v->SetForegroundColour(Theme::ColorBrand);
        } else {
            l->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
            l->SetForegroundColour(Theme::ColorTextSecondary);
            v->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
            v->SetForegroundColour(Theme::ColorText);
        }
        row->Add(l, 0, wxALIGN_CENTER_VERTICAL);
        row->AddStretchSpacer(1);
        row->Add(v, 0, wxALIGN_CENTER_VERTICAL);
        return row;
    };

    acc_sizer->Add(make_summary_row(wxT("Subtotal:"), &lbl_subtotal_val_), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);
    acc_sizer->Add(make_summary_row(wxT("Descuento:"), &lbl_discount_val_, false, &lbl_discount_caption_), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);
    lbl_discount_caption_->Hide();
    lbl_discount_val_->Hide();
    acc_sizer->Add(make_summary_row(wxT("TOTAL:"), &lbl_total_val_, true), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);

    // Botón Cobrar (verde éxito Cupertino #166534 con receipt_long)
    const int checkout_h = touch ? 60 : 50;
    btn_checkout_ = new wxButton(account_card, wxID_ANY, wxT("Cobrar"));
    Theme::style_button_success(btn_checkout_, checkout_h);
    Theme::setup_button_icon(btn_checkout_, "receipt_long", wxSize(touch ? 26 : 22, touch ? 26 : 22), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_checkout_, -1, checkout_h);
    acc_sizer->Add(btn_checkout_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);

    // Botón Cancelar Pedido (separado y distinguido de quitar línea)
    const int cancel_h = touch ? 48 : 36;
    btn_cancel_order_ = new wxButton(account_card, wxID_ANY, wxT("Cancelar pedido"));
    Theme::style_button_secondary(btn_cancel_order_, cancel_h);
    Theme::setup_button_icon(btn_cancel_order_, "cancel", wxSize(touch ? 18 : 16, touch ? 18 : 16), Theme::ColorTextSecondary);
    Theme::ensure_button_min_size(btn_cancel_order_, -1, cancel_h);
    btn_cancel_order_->SetToolTip(wxT("Vaciar el carrito actual sin cobrar"));
    acc_sizer->Add(btn_cancel_order_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);

    acc_sizer->AddStretchSpacer(1);
    account_card->SetSizer(acc_sizer);

    main_sizer->Add(account_card, 0, wxEXPAND);

    SetSizer(main_sizer);

    // Eventos
    btn_search_->Bind(wxEVT_BUTTON, &PosPanel::on_search, this);
    txt_search_->Bind(wxEVT_TEXT_ENTER, &PosPanel::on_search, this);
    btn_add_->Bind(wxEVT_BUTTON, &PosPanel::on_add_item, this);
    txt_notes_->Bind(wxEVT_TEXT_ENTER, &PosPanel::on_add_item, this);
    btn_remove_item_->Bind(wxEVT_BUTTON, &PosPanel::on_remove_selected_item, this);
    btn_edit_qty_->Bind(wxEVT_BUTTON, &PosPanel::on_edit_quantity, this);
    btn_edit_notes_->Bind(wxEVT_BUTTON, &PosPanel::on_edit_notes, this);
    btn_more_actions_->Bind(wxEVT_BUTTON, &PosPanel::on_more_actions, this);
    list_items_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &PosPanel::on_item_activated, this);
    list_items_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { update_contextual_actions(); });
    list_items_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) { update_contextual_actions(); });
    btn_cancel_order_->Bind(wxEVT_BUTTON, &PosPanel::on_cancel_order, this);
    btn_checkout_->Bind(wxEVT_BUTTON, &PosPanel::on_checkout, this);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { wxCommandEvent d; on_open_ticket_history(d); }, 1001);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { wxCommandEvent d; on_cancel_order(d); }, 1002);

    Bind(wxEVT_SIZE, &PosPanel::on_size, this);

    update_contextual_actions();

    if (touch) {
        keypad_ = new ui::NumericKeypad(this);
        keypad_->bind_field(
            txt_qty_,
            qty_box,
            false,
            search_card,
            [this, search_card, account_card]() {
                std::vector<wxRect> rects;
                auto add_rect = [&](wxWindow* w) {
                    if (!w || !w->IsShownOnScreen()) return;
                    rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
                };
                add_rect(search_card);
                add_rect(lbl_total_val_);
                add_rect(btn_checkout_);
                add_rect(btn_cancel_order_);
                // Evita invadir los controles de la columna de cuenta si hay holgura suficiente.
                if (GetClientSize().GetWidth() >= FromDIP(800)) {
                    add_rect(account_card);
                }
                return rects;
            }
        );
    }
}

void PosPanel::update_contextual_actions() {
    if (destroying_) return;
    long sel = list_items_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    bool has_sel = (sel >= 0 && sel < static_cast<long>(current_cart_items_.size()));

    if (btn_edit_qty_) btn_edit_qty_->Enable(has_sel);
    if (btn_edit_notes_) btn_edit_notes_->Enable(has_sel);
    if (btn_remove_item_) btn_remove_item_->Enable(has_sel);

    if (has_sel) {
        const auto& item = current_cart_items_[sel];
        if (btn_edit_qty_) btn_edit_qty_->SetToolTip(wxString::Format(wxT("Cambiar cantidad de '%s'"), wxString::FromUTF8(item.name.c_str())));
        if (btn_edit_notes_) btn_edit_notes_->SetToolTip(wxString::Format(wxT("Editar nota de '%s'"), wxString::FromUTF8(item.name.c_str())));
        if (btn_remove_item_) btn_remove_item_->SetToolTip(wxString::Format(wxT("Quitar '%s' del carrito"), wxString::FromUTF8(item.name.c_str())));

        if (lbl_cart_line_detail_) {
            std::string detail = item.name + " (" + std::to_string(item.quantity) + " x " +
                                 item.unit_price.to_string() + ") — Original: " + item.subtotal.to_string();
            const std::string disc = format_line_discount_text(item);
            if (!disc.empty()) {
                detail += " | " + disc;
                lbl_cart_line_detail_->SetForegroundColour(Theme::ColorSuccess);
            } else {
                lbl_cart_line_detail_->SetForegroundColour(Theme::ColorTextSecondary);
            }
            detail += " | Total: " + item.line_total.to_string();
            if (!item.notes.empty()) {
                detail += " · Nota: " + item.notes;
            }
            last_line_detail_ = std::move(detail);
            lbl_cart_line_detail_->Show();
            update_line_detail_wrap();
        }
    } else {
        if (btn_edit_qty_) btn_edit_qty_->SetToolTip(wxT("Seleccione una línea de la tabla para editar su cantidad"));
        if (btn_edit_notes_) btn_edit_notes_->SetToolTip(wxT("Seleccione una línea de la tabla para editar su nota"));
        if (btn_remove_item_) btn_remove_item_->SetToolTip(wxT("Seleccione una línea de la tabla para quitarla"));
        last_line_detail_.clear();
        if (lbl_cart_line_detail_) {
            lbl_cart_line_detail_->Hide();
        }
    }
    if (left_box_) {
        Layout();
    }
}

void PosPanel::update_line_detail_wrap() {
    if (!lbl_cart_line_detail_ || !lbl_cart_line_detail_->IsShown()) return;
    int avail_w = list_items_ ? list_items_->GetSize().GetWidth() : GetClientSize().GetWidth();
    if (avail_w <= 40) avail_w = FromDIP(340);
    lbl_cart_line_detail_->SetLabel(wxString::FromUTF8(last_line_detail_));
    lbl_cart_line_detail_->Wrap(std::max(FromDIP(160), avail_w - FromDIP(8)));
}

void PosPanel::update_list_column_widths() {
    if (destroying_ || sizing_columns_ || !list_items_ || !list_items_->IsShown() ||
        list_items_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(list_items_);
    dc.SetFont(list_items_->GetFont());

    int max_sku = dc.GetTextExtent(wxT("BAG-01")).GetWidth();
    int max_prod = dc.GetTextExtent(wxT("Producto")).GetWidth();
    int max_notas = dc.GetTextExtent(wxT("Notas")).GetWidth();
    int max_cant = dc.GetTextExtent(wxT("Cant.")).GetWidth();
    int max_sub = dc.GetTextExtent(wxT("Subtotal")).GetWidth();
    int max_desc = dc.GetTextExtent(wxT("Descuento")).GetWidth();
    int max_tot = dc.GetTextExtent(wxT("Total")).GetWidth();

    for (const auto& item : current_cart_items_) {
        max_sku = std::max(max_sku, dc.GetTextExtent(wxString::FromUTF8(item.sku)).GetWidth());
        max_prod = std::max(max_prod, dc.GetTextExtent(wxString::FromUTF8(item.name)).GetWidth());
        if (!item.notes.empty()) {
            max_notas = std::max(max_notas, dc.GetTextExtent(wxString::FromUTF8(item.notes)).GetWidth());
        }
        max_cant = std::max(max_cant, dc.GetTextExtent(wxString::Format(wxT("%lld"), item.quantity)).GetWidth());
        max_sub = std::max(max_sub, dc.GetTextExtent(wxString::FromUTF8(item.subtotal.to_string())).GetWidth());
        const std::string disc = format_line_discount_text(item);
        if (!disc.empty()) {
            max_desc = std::max(max_desc, dc.GetTextExtent(wxString::FromUTF8(disc)).GetWidth());
        }
        max_tot = std::max(max_tot, dc.GetTextExtent(wxString::FromUTF8(item.line_total.to_string())).GetWidth());
    }

    const int pad = 18;
    const int sku_extra = Theme::is_touch() ? 30 : 26;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("SKU"), max_sku, max_sku, 0, sku_extra});
    specs.push_back({wxT("Producto"), std::min(max_prod, FromDIP(140)), dc.GetTextExtent(wxT("Producto")).GetWidth(), pad, 0});
    specs.push_back({wxT("Notas"), max_notas, 0, pad, 0});
    specs.push_back({wxT("Cant."), max_cant, max_cant, pad, 0});
    specs.push_back({wxT("Subtotal"), max_sub, max_sub, pad, 0});
    specs.push_back({wxT("Descuento"), max_desc, max_desc, pad, 0});
    specs.push_back({wxT("Total"), max_tot, max_tot, pad, 0});
    ui::distribute_list_columns(list_items_, specs, 2);
    sizing_columns_ = false;
}

void PosPanel::on_size(wxSizeEvent& evt) {
    evt.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted()) return;
        update_list_column_widths();
        update_line_detail_wrap();
        Layout();
    });
}

void PosPanel::on_more_actions(wxCommandEvent&) {
    wxMenu menu;
    menu.Append(1001, wxT("Historial y reimpresión de tickets..."));
    menu.Append(1002, wxT("Cancelar pedido actual"));

    menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        if (e.GetId() == 1001) {
            wxCommandEvent dummy;
            on_open_ticket_history(dummy);
        } else if (e.GetId() == 1002) {
            wxCommandEvent dummy;
            on_cancel_order(dummy);
        }
    });

    PopupMenu(&menu);
}

void PosPanel::update_shift_display() {
    if (!services_.shift_service) return;

    auto shift_res = services_.shift_service->get_current_shift();
    if (shift_res.is_success()) {
        lbl_shift_status_->SetLabel(wxString::Format(
            wxT("Turno activo: %s"),
            wxString::FromUTF8(shift_res.value().register_name.c_str())
        ));
        lbl_shift_status_->SetForegroundColour(wxColour(34, 150, 70));
    } else {
        lbl_shift_status_->SetLabel(wxT("Caja Principal · Cerrada"));
        lbl_shift_status_->SetForegroundColour(wxColour(180, 20, 30));
    }
}

void PosPanel::load_catalog_products(const std::string& query) {
    if (!services_.catalog_service) return;

    auto res = services_.catalog_service->search_products(query, std::nullopt, 0, 50, false);
    if (!res.is_success()) return;

    loaded_products_ = res.value();
    choice_products_->Clear();

    for (const auto& prod : loaded_products_) {
        wxString item_label = wxString::Format(
            wxT("[%s] %s — %s"),
            wxString::FromUTF8(prod.sku.c_str()),
            wxString::FromUTF8(prod.name.c_str()),
            wxString::FromUTF8(prod.price.to_string().c_str())
        );
        choice_products_->Append(item_label);
    }

    if (!loaded_products_.empty()) {
        choice_products_->SetSelection(0);
    }
}

void PosPanel::on_search(wxCommandEvent&) {
    std::string query = std::string(txt_search_->GetValue().ToUTF8().data());
    load_catalog_products(query);
}

void PosPanel::on_add_item(wxCommandEvent&) {
    int sel = choice_products_->GetSelection();
    if (sel < 0 || sel >= static_cast<int>(loaded_products_.size())) {
        wxMessageBox(wxT("Seleccione un producto del catálogo para agregar."), wxT("Aviso"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto& prod = loaded_products_[sel];

    long qty = 1;
    txt_qty_->GetValue().ToLong(&qty);
    if (qty <= 0) {
        wxMessageBox(wxT("La cantidad debe ser mayor a 0."), wxT("Cantidad Inválida"), wxOK | wxICON_WARNING, this);
        txt_qty_->SetFocus();
        return;
    }

    std::string notes = std::string(txt_notes_->GetValue().ToUTF8().data());
    if (notes.length() > 250) {
        wxMessageBox(wxT("La nota de preparación no puede exceder 250 caracteres."), wxT("Nota demasiado larga"), wxOK | wxICON_WARNING, this);
        txt_notes_->SetFocus();
        return;
    }

    auto res = services_.cart_service->add_item(prod.id, qty, notes);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al agregar artículo: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error de Carrito"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    txt_notes_->Clear();
    txt_qty_->SetValue(wxT("1"));
    active_idempotency_key_.clear();
    refresh_cart();
}

void PosPanel::refresh_cart() {
    if (!services_.cart_service) return;

    auto res = services_.cart_service->get_cart();
    if (!res.is_success()) return;

    const auto& cart = res.value();
    current_cart_items_ = cart.items;

    list_items_->DeleteAllItems();
    long idx = 0;
    for (const auto& item : current_cart_items_) {
        long row = list_items_->InsertItem(idx, wxString::FromUTF8(item.sku.c_str()));
        list_items_->SetItem(row, 1, wxString::FromUTF8(item.name.c_str()));
        list_items_->SetItem(row, 2, wxString::FromUTF8(item.notes.c_str()));
        list_items_->SetItem(row, 3, wxString::Format(wxT("%lld"), item.quantity));
        list_items_->SetItem(row, 4, wxString::FromUTF8(item.subtotal.to_string().c_str()));
        list_items_->SetItem(row, 5, wxString::FromUTF8(format_line_discount_text(item)));
        list_items_->SetItem(row, 6, wxString::FromUTF8(item.line_total.to_string().c_str()));
        if (item.line_discount.cents() > 0) {
            list_items_->SetItemTextColour(row, Theme::ColorBrandDeep);
        }
        idx++;
    }

    if (!current_cart_items_.empty()) {
        list_items_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
    update_list_column_widths();

    const bool has_discount = cart.discount.cents() > 0;
    lbl_items_count_->SetLabel(wxString::Format(wxT("%lld artículos en carrito"), cart.total_items));
    lbl_subtotal_val_->SetLabel(wxString::FromUTF8(cart.subtotal.to_string().c_str()));
    if (has_discount) {
        lbl_discount_val_->SetLabel(wxString::Format(wxT("-%s"), wxString::FromUTF8(cart.discount.to_string().c_str())));
        lbl_discount_val_->SetForegroundColour(Theme::ColorSuccess);
    }
    if (lbl_discount_caption_) lbl_discount_caption_->Show(has_discount);
    lbl_discount_val_->Show(has_discount);
    lbl_total_val_->SetLabel(wxString::FromUTF8(cart.total.to_string().c_str()));

    lbl_subtotal_val_->InvalidateBestSize();
    lbl_discount_val_->InvalidateBestSize();
    lbl_total_val_->InvalidateBestSize();
    if (lbl_subtotal_val_->GetParent()) {
        lbl_subtotal_val_->GetParent()->Layout();
    }
    Layout();

    update_contextual_actions();
}

void PosPanel::on_remove_selected_item(wxCommandEvent&) {
    long sel = list_items_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(current_cart_items_.size())) {
        wxMessageBox(wxT("Seleccione una línea de la tabla para quitarla."), wxT("Aviso"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto& item = current_cart_items_[sel];
    auto res = services_.cart_service->remove_item(item.line_id);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al quitar artículo: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    active_idempotency_key_.clear();
    refresh_cart();
}

void PosPanel::on_edit_quantity(wxCommandEvent&) {
    long sel = list_items_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(current_cart_items_.size())) {
        wxMessageBox(wxT("Seleccione una línea de la tabla para editar su cantidad."), wxT("Aviso"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto& item = current_cart_items_[sel];
    const wxString prompt_msg = wxString::Format(
        wxT("Ingrese la nueva cantidad para '%s':"), wxString::FromUTF8(item.name.c_str()));
    const wxString initial_val = wxString::Format(wxT("%lld"), item.quantity);

    wxString entered_val;
    if (Theme::is_touch()) {
        wxDialog dlg(this, wxID_ANY, wxT("Editar Cantidad"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
        dlg.SetBackgroundColour(Theme::ColorBackground);
        auto* root = new wxBoxSizer(wxVERTICAL);

        auto* lbl_msg = new wxStaticText(&dlg, wxID_ANY, prompt_msg);
        lbl_msg->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        lbl_msg->SetForegroundColour(Theme::ColorText);
        lbl_msg->Wrap(dlg.FromDIP(340));
        root->Add(lbl_msg, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dlg.FromDIP(16));

        wxTextCtrl* txt_edit_qty = nullptr;
        auto* field_box = ui::make_text_field(
            &dlg, txt_edit_qty, 48, -1, Theme::get_font_body(), wxTE_CENTRE | wxTE_PROCESS_ENTER);
        txt_edit_qty->ChangeValue(initial_val);
        txt_edit_qty->SelectAll();
        root->Add(field_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dlg.FromDIP(12));

        auto* kpad = new ui::NumericKeypad(&dlg, false, false);
        kpad->set_target_field(txt_edit_qty, false);
        root->Add(kpad, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxTOP, dlg.FromDIP(14));

        auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
        const int btn_h = 56;
        auto* btn_cancel = new wxButton(&dlg, wxID_CANCEL, wxT("Cancelar"));
        Theme::style_button_secondary(btn_cancel, btn_h);
        Theme::ensure_button_min_size(btn_cancel, 130, btn_h);

        auto* btn_ok = new wxButton(&dlg, wxID_OK, wxT("Aceptar"));
        Theme::style_button_primary(btn_ok, btn_h);
        Theme::ensure_button_min_size(btn_ok, 140, btn_h);

        btn_row->Add(btn_cancel, 1, wxRIGHT, dlg.FromDIP(10));
        btn_row->Add(btn_ok, 1);
        root->Add(btn_row, 0, wxEXPAND | wxALL, dlg.FromDIP(16));

        dlg.SetSizerAndFit(root);
        dlg.Centre();
        txt_edit_qty->SetFocus();
        txt_edit_qty->Bind(wxEVT_TEXT_ENTER, [&dlg](wxCommandEvent&) { dlg.EndModal(wxID_OK); });

        if (dlg.ShowModal() != wxID_OK) return;
        entered_val = txt_edit_qty->GetValue();
    } else {
        wxTextEntryDialog dlg(this, prompt_msg, wxT("Editar Cantidad"), initial_val);
        if (dlg.ShowModal() != wxID_OK) return;
        entered_val = dlg.GetValue();
    }

    long new_qty = 0;
    if (!entered_val.ToLong(&new_qty) || new_qty < 0) {
        wxMessageBox(wxT("La cantidad debe ser un número entero mayor o igual a 0."), wxT("Valor Inválido"), wxOK | wxICON_WARNING, this);
        return;
    }

    auto res = services_.cart_service->update_quantity(item.line_id, new_qty);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al actualizar cantidad: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    active_idempotency_key_.clear();
    refresh_cart();
}

void PosPanel::on_edit_notes(wxCommandEvent&) {
    long sel = list_items_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(current_cart_items_.size())) {
        wxMessageBox(wxT("Seleccione una línea de la tabla para editar su nota."), wxT("Aviso"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    const auto& item = current_cart_items_[sel];
    wxTextEntryDialog dlg(
        this,
        wxString::Format(wxT("Nota de preparación para '%s' (máx. 250 caracteres):"), wxString::FromUTF8(item.name.c_str())),
        wxT("Editar Nota de Preparación"),
        wxString::FromUTF8(item.notes.c_str())
    );

    if (dlg.ShowModal() != wxID_OK) return;

    std::string new_notes = std::string(dlg.GetValue().Trim().Trim(false).ToUTF8());
    auto res = services_.cart_service->update_item_notes(item.line_id, new_notes);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al actualizar nota: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    active_idempotency_key_.clear();
    refresh_cart();
}

void PosPanel::on_open_ticket_history(wxCommandEvent&) {
    TicketHistoryDialog dlg(this, services_);
    dlg.ShowModal();
}

void PosPanel::on_item_activated(wxListEvent&) {
    wxCommandEvent dummy;
    on_edit_quantity(dummy);
}

void PosPanel::on_cancel_order(wxCommandEvent&) {
    if (current_cart_items_.empty()) {
        wxMessageBox(wxT("El carrito ya está vacío."), wxT("Aviso"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    int ans = wxMessageBox(
        wxT("¿Deseas cancelar y vaciar la orden actual sin cobrar?"),
        wxT("Confirmar Cancelación de Pedido"),
        wxYES_NO | wxICON_QUESTION,
        this
    );
    if (ans != wxYES) return;

    auto res = services_.cart_service->clear_cart();
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al cancelar orden: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    active_idempotency_key_.clear();
    refresh_cart();
}

void PosPanel::on_checkout(wxCommandEvent&) {
    if (current_cart_items_.empty()) {
        wxMessageBox(wxT("No hay artículos en el carrito para cobrar."), wxT("Carrito Vacío"), wxOK | wxICON_WARNING, this);
        return;
    }

    // 1. Revalidar turno activo antes de abrir el diálogo de cobro
    if (services_.shift_service) {
        auto shift_res = services_.shift_service->get_current_shift();
        if (!shift_res.is_success()) {
            wxMessageBox(
                wxT("No hay un turno de caja abierto para registrar ventas.\n"
                    "Debe abrir un turno primero desde 'Caja y Reportes > Caja y turnos'."),
                wxT("Turno Cerrado"),
                wxOK | wxICON_WARNING,
                this
            );
            update_shift_display();
            return;
        }
    }

    auto cart_res = services_.cart_service->get_cart();
    if (!cart_res.is_success()) return;

    contracts::Money total = cart_res.value().total;
    contracts::Money discount = cart_res.value().discount;

    CheckoutDialog dlg(this, total, discount, services_.sale_service);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }

    // 2. Clave de idempotencia estable para este cobro
    if (active_idempotency_key_.empty()) {
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        active_idempotency_key_ = "pos-sale-" + std::to_string(now_ms);
    }

    // 3. Bloquear botón Cobrar para impedir dobles clics
    btn_checkout_->Enable(false);

    auto sale_res = services_.sale_service->confirm_sale(
        dlg.get_payment_method(),
        dlg.get_cash_received(),
        active_idempotency_key_
    );

    btn_checkout_->Enable(true);

    if (!sale_res.is_success()) {
        if (sale_res.error_code() == contracts::ErrorCode::Conflict) {
            active_idempotency_key_.clear();
            refresh_cart();
            wxMessageBox(
                wxString::FromUTF8(sale_res.error_message().c_str()),
                wxT("Promociones Actualizadas"),
                wxOK | wxICON_WARNING,
                this
            );
            return;
        }
        wxMessageBox(
            wxString::Format(wxT("Error al confirmar venta: %s"), wxString::FromUTF8(sale_res.error_message().c_str())),
            wxT("Fallo al Cobrar"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    // Cobro exitoso: reiniciar la clave de idempotencia
    active_idempotency_key_.clear();

    const auto& conf = sale_res.value();

    // Obtener ticket inmutable de la venta confirmada
    auto ticket_res = services_.sale_service->get_ticket(conf.folio);
    if (ticket_res.is_success()) {
        contracts::PrinterStationConfig print_cfg;
        if (services_.print_config_service) {
            auto cfg_res = services_.print_config_service->get_station_config();
            if (cfg_res.is_success()) {
                print_cfg = cfg_res.value();
            }
        }
        TicketPrintDialog print_dlg(this, ticket_res.value(), print_cfg);
        print_dlg.ShowModal();
    } else {
        wxMessageBox(
            wxString::Format(wxT("¡Venta confirmada exitosamente con Folio %s!\nTotal: %s\nCambio: %s"),
                wxString::FromUTF8(conf.folio.c_str()),
                wxString::FromUTF8(conf.total.to_string().c_str()),
                wxString::FromUTF8(conf.change.to_string().c_str())),
            wxT("Venta Registrada"),
            wxOK | wxICON_INFORMATION,
            this
        );
    }

    refresh_cart();
}

} // namespace app::frontend
