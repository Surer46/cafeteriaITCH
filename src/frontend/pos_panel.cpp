#include "src/frontend/pos_panel.hpp"
#include "src/frontend/checkout_dialog.hpp"
#include "src/frontend/ticket_print_dialog.hpp"
#include "src/frontend/ticket_history_dialog.hpp"
#include "src/frontend/theme.hpp"
#include <wx/statline.h>
#include <wx/imaglist.h>
#include <wx/dcclient.h>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace app::frontend {

PosPanel::PosPanel(
    wxWindow* parent,
    services::ServiceRegistry services
) : wxPanel(parent, wxID_ANY), services_(std::move(services)) {
    setup_ui();
    update_shift_display();
    load_catalog_products();
    refresh_cart();
}

void PosPanel::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    bool touch = Theme::is_touch();
    auto* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    // =========================================================================
    // Columna Izquierda: Captura de Pedido, Búsqueda, Notas y Tabla de Artículos
    // =========================================================================
    auto* left_box = new wxBoxSizer(wxVERTICAL);

    // Tarjeta superior para búsqueda, selección y notas
    auto* search_card = new CardPanel(this);
    auto* card_inner_sizer = new wxBoxSizer(wxVERTICAL);

    // ORDEN ESTRICTO DE CREACIÓN DE TEXTCTRL PARA TEST AUTOMATIZADO (edits[0], edits[1], edits[2]):
    // 1. txt_search_ en contenedor centrado verticalmente
    auto* search_box = new wxPanel(search_card, wxID_ANY, wxDefaultPosition, wxSize(touch ? 220 : 180, touch ? 48 : 32), wxBORDER_SIMPLE);
    search_box->SetBackgroundColour(Theme::ColorSurface);
    txt_search_ = new wxTextCtrl(search_box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTE_PROCESS_ENTER);
    txt_search_->SetHint(wxT("Buscar código o nombre..."));
    txt_search_->SetFont(touch ? Theme::get_font_body(wxFONTWEIGHT_MEDIUM) : Theme::get_font_body());
    txt_search_->SetBackgroundColour(Theme::ColorSurface);
    auto* search_box_sizer = new wxBoxSizer(wxVERTICAL);
    search_box_sizer->AddStretchSpacer(1);
    search_box_sizer->Add(txt_search_, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
    search_box_sizer->AddStretchSpacer(1);
    search_box->SetSizer(search_box_sizer);
    search_box->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        txt_search_->SetFocus();
    });

    // 2. txt_qty_ en contenedor centrado vertical y horizontalmente (elimina desfase superior en campos altos)
    auto* qty_box = new wxPanel(search_card, wxID_ANY, wxDefaultPosition, wxSize(touch ? 64 : 48, touch ? 48 : 32), wxBORDER_SIMPLE);
    qty_box->SetBackgroundColour(Theme::ColorSurface);
    txt_qty_ = new wxTextCtrl(qty_box, wxID_ANY, wxT("1"), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTE_CENTRE);
    txt_qty_->SetFont(Theme::get_font_body());
    txt_qty_->SetBackgroundColour(Theme::ColorSurface);
    auto* qty_box_sizer = new wxBoxSizer(wxVERTICAL);
    qty_box_sizer->AddStretchSpacer(1);
    qty_box_sizer->Add(txt_qty_, 0, wxEXPAND);
    qty_box_sizer->AddStretchSpacer(1);
    qty_box->SetSizer(qty_box_sizer);
    qty_box->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        txt_qty_->SetFocus();
        txt_qty_->SelectAll();
    });

    // 3. txt_notes_ en contenedor centrado verticalmente
    auto* notes_box = new wxPanel(search_card, wxID_ANY, wxDefaultPosition, wxSize(-1, touch ? 48 : 32), wxBORDER_SIMPLE);
    notes_box->SetBackgroundColour(Theme::ColorSurface);
    txt_notes_ = new wxTextCtrl(notes_box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTE_PROCESS_ENTER);
    txt_notes_->SetHint(wxT("Nota de preparación (ej. sin hielo, con poca azúcar, para llevar...)"));
    txt_notes_->SetFont(touch ? Theme::get_font_body(wxFONTWEIGHT_MEDIUM) : Theme::get_font_body());
    txt_notes_->SetBackgroundColour(Theme::ColorSurface);
    auto* notes_box_sizer = new wxBoxSizer(wxVERTICAL);
    notes_box_sizer->AddStretchSpacer(1);
    notes_box_sizer->Add(txt_notes_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    notes_box_sizer->AddStretchSpacer(1);
    notes_box->SetSizer(notes_box_sizer);
    notes_box->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        txt_notes_->SetFocus();
    });

    // Botones stepper para cantidad (facilitan interacción táctil sin depender de teclado)
    btn_qty_minus_ = new wxButton(search_card, wxID_ANY, wxT("−"), wxDefaultPosition, wxSize(touch ? 48 : 32, touch ? 48 : 32));
    btn_qty_plus_ = new wxButton(search_card, wxID_ANY, wxT("+"), wxDefaultPosition, wxSize(touch ? 48 : 32, touch ? 48 : 32));
    Theme::style_button_secondary(btn_qty_minus_, touch ? 48 : 32);
    Theme::style_button_secondary(btn_qty_plus_, touch ? 48 : 32);
    btn_qty_minus_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    btn_qty_plus_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));

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
    btn_search_ = new wxButton(search_card, wxID_ANY, wxT("Buscar"), wxDefaultPosition, wxSize(touch ? 100 : 90, touch ? 48 : 32));
    Theme::style_button_secondary(btn_search_, touch ? 48 : 32);
    Theme::setup_button_icon(btn_search_, "search", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorTextSecondary);

    choice_products_ = new wxOwnerDrawnComboBox(
        search_card,
        wxID_ANY,
        wxEmptyString,
        wxDefaultPosition,
        wxSize(-1, touch ? 48 : 32),
        0,
        nullptr,
        wxCB_READONLY
    );
    choice_products_->SetFont(Theme::get_font_body());
    choice_products_->SetMinSize(wxSize(-1, touch ? 48 : 32));

    auto* lbl_qty = new wxStaticText(search_card, wxID_ANY, wxT("Cant:"));
    lbl_qty->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_qty->SetForegroundColour(Theme::ColorTextSecondary);

    // Etiqueta limpia sin + textual repetido y un único icono Material add
    btn_add_ = new wxButton(search_card, wxID_ANY, wxT("Agregar al Carrito"), wxDefaultPosition, wxSize(touch ? 210 : 180, touch ? 56 : 36));
    Theme::style_button_primary(btn_add_, touch ? 56 : 36);
    Theme::setup_button_icon(btn_add_, "add", wxSize(touch ? 22 : 18, touch ? 22 : 18), Theme::ColorSurface);

    if (touch) {
        // Distribución Touch reorganizada (3 filas limpias, espacios generosos y ergonómicos):
        // Fila 1: Búsqueda y Selección en catálogo
        auto* search_row = new wxBoxSizer(wxHORIZONTAL);
        search_row->Add(search_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        search_row->Add(btn_search_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        search_row->Add(choice_products_, 2, wxALIGN_CENTER_VERTICAL);
        card_inner_sizer->Add(search_row, 0, wxEXPAND | wxBOTTOM, 10);

        // Fila 2: Notas específicas de preparación (ancho completo >500px, 48px alto)
        auto* notes_row = new wxBoxSizer(wxHORIZONTAL);
        notes_row->Add(notes_box, 1, wxALIGN_CENTER_VERTICAL);
        card_inner_sizer->Add(notes_row, 0, wxEXPAND | wxBOTTOM, 10);

        // Fila 3: Steppers táctiles de cantidad [−] [ 1 ] [+] y Botón Hero Agregar
        auto* qty_add_row = new wxBoxSizer(wxHORIZONTAL);
        qty_add_row->Add(lbl_qty, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        qty_add_row->Add(btn_qty_minus_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        qty_add_row->Add(qty_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        qty_add_row->Add(btn_qty_plus_, 0, wxALIGN_CENTER_VERTICAL);
        qty_add_row->AddStretchSpacer(1);
        qty_add_row->Add(btn_add_, 0, wxALIGN_CENTER_VERTICAL);
        card_inner_sizer->Add(qty_add_row, 0, wxEXPAND);
    } else {
        // Distribución Estándar compacta:
        // Fila 1: Búsqueda, Selección y Cantidad con Stepper
        auto* search_row = new wxBoxSizer(wxHORIZONTAL);
        search_row->Add(search_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        search_row->Add(btn_search_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        search_row->Add(choice_products_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        search_row->Add(lbl_qty, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        search_row->Add(btn_qty_minus_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
        search_row->Add(qty_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
        search_row->Add(btn_qty_plus_, 0, wxALIGN_CENTER_VERTICAL);
        card_inner_sizer->Add(search_row, 0, wxEXPAND | wxBOTTOM, 8);

        // Fila 2: Notas específicas de preparación y Botón Agregar
        auto* notes_row = new wxBoxSizer(wxHORIZONTAL);
        notes_row->Add(notes_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
        notes_row->Add(btn_add_, 0, wxALIGN_CENTER_VERTICAL);
        card_inner_sizer->Add(notes_row, 0, wxEXPAND);
    }

    search_card->SetSizer(card_inner_sizer);
    left_box->Add(search_card, 0, wxEXPAND | wxBOTTOM, touch ? 12 : 10);

    // Tabla de líneas de venta (ListCtrl)
    list_items_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    list_items_->SetFont(Theme::get_font_body());

    // Altura de fila de la tabla del carrito >= 40 px en Touch mediante ImageList
    auto* img_list = new wxImageList(1, touch ? 42 : 24);
    list_items_->AssignImageList(img_list, wxIMAGE_LIST_SMALL);

    wxClientDC list_dc(list_items_);
    list_dc.SetFont(list_items_->GetFont());
    auto calc_col_width = [&](const wxString& title, int min_pad, int fallback_w) {
        int tw = 0, th = 0;
        list_dc.GetTextExtent(title, &tw, &th);
        return std::max(tw + min_pad, fallback_w);
    };

    int header_pad = touch ? 18 : 14;
    // Ancho mínimo de SKU calculado con GetTextExtent del SKU más largo ("BAG-01")
    // más ancho de imagen (1px) y padding/reserva nativa de icono en SysListView32 (aprox 28-30px)
    int sku_sample_w = 0, sku_sample_h = 0;
    list_dc.GetTextExtent(wxT("BAG-01"), &sku_sample_w, &sku_sample_h);
    int img_and_native_pad = touch ? 30 : 26;
    int col_sku = std::max(calc_col_width(wxT("SKU"), header_pad, touch ? 55 : 50), sku_sample_w + 1 + img_and_native_pad);

    int col_cant = calc_col_width(wxT("Cant."), header_pad + 4, touch ? 58 : 54);
    int col_precio = calc_col_width(wxT("Precio"), header_pad + 4, touch ? 66 : 62);
    int col_subtotal = calc_col_width(wxT("Subtotal"), header_pad + 6, touch ? 82 : 76);
    int col_prod = touch ? 135 : 130;
    int col_notas = touch ? 135 : 125;

    list_items_->InsertColumn(0, wxT("SKU"), wxLIST_FORMAT_LEFT, col_sku);
    list_items_->InsertColumn(1, wxT("Producto"), wxLIST_FORMAT_LEFT, col_prod);
    list_items_->InsertColumn(2, wxT("Notas de Preparación"), wxLIST_FORMAT_LEFT, col_notas);
    list_items_->InsertColumn(3, wxT("Cant."), wxLIST_FORMAT_RIGHT, col_cant);
    list_items_->InsertColumn(4, wxT("Precio"), wxLIST_FORMAT_RIGHT, col_precio);
    list_items_->InsertColumn(5, wxT("Subtotal"), wxLIST_FORMAT_RIGHT, col_subtotal);
    list_items_->SetMinSize(wxSize(250, 150));

    left_box->Add(list_items_, 1, wxEXPAND | wxBOTTOM, touch ? 10 : 8);

    // Fila inferior de tabla: Acciones contextuales (Editar cantidad, nota, quitar línea) y secundarios en Más acciones
    int action_btn_h = touch ? 48 : 34;

    btn_edit_qty_ = new wxButton(this, wxID_ANY, wxT("Editar cantidad"), wxDefaultPosition, wxSize(touch ? 170 : 145, action_btn_h));
    Theme::style_button_secondary(btn_edit_qty_, action_btn_h);
    Theme::setup_button_icon(btn_edit_qty_, "edit", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorTextSecondary);

    btn_edit_notes_ = new wxButton(this, wxID_ANY, wxT("Editar nota"), wxDefaultPosition, wxSize(touch ? 145 : 125, action_btn_h));
    Theme::style_button_secondary(btn_edit_notes_, action_btn_h);
    Theme::setup_button_icon(btn_edit_notes_, "sticky_note_2", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorTextSecondary);

    btn_remove_item_ = new wxButton(this, wxID_ANY, wxT("Quitar línea"), wxDefaultPosition, wxSize(touch ? 145 : 125, action_btn_h));
    Theme::style_button_destructive(btn_remove_item_, action_btn_h);
    Theme::setup_button_icon(btn_remove_item_, "delete", wxSize(touch ? 20 : 16, touch ? 20 : 16), Theme::ColorError);

    btn_more_actions_ = new wxButton(this, wxID_ANY, wxT("Más acciones ▾"), wxDefaultPosition, wxDefaultSize);
    Theme::style_button_secondary(btn_more_actions_, action_btn_h);
    wxSize more_icon_sz(touch ? 20 : 18, touch ? 20 : 18);
    Theme::setup_button_icon(btn_more_actions_, "more_horiz", more_icon_sz, Theme::ColorTextSecondary);
    int more_tw = 0, more_th = 0;
    btn_more_actions_->GetTextExtent(btn_more_actions_->GetLabel(), &more_tw, &more_th);
    int more_icon_margins = (touch ? 10 : 8) * 2;
    int more_btn_padding = touch ? 28 : 24;
    int more_w = more_tw + more_icon_sz.GetWidth() + more_icon_margins + more_btn_padding;
    wxSize more_best = btn_more_actions_->GetBestSize();
    if (more_best.GetWidth() > more_w) {
        more_w = more_best.GetWidth();
    }
    btn_more_actions_->SetMinSize(wxSize(more_w, action_btn_h));
    btn_more_actions_->SetSize(wxSize(more_w, action_btn_h));

    if (touch) {
        // En Touch: organizar en dos filas para asegurar que Más acciones nunca se corte
        auto* table_actions_box = new wxBoxSizer(wxVERTICAL);

        auto* line_actions = new wxBoxSizer(wxHORIZONTAL);
        line_actions->Add(btn_edit_qty_, 0, wxRIGHT, 10);
        line_actions->Add(btn_edit_notes_, 0, wxRIGHT, 10);
        line_actions->Add(btn_remove_item_, 0);
        line_actions->AddStretchSpacer(1);
        table_actions_box->Add(line_actions, 0, wxEXPAND | wxBOTTOM, 8);

        auto* extra_actions = new wxBoxSizer(wxHORIZONTAL);
        extra_actions->AddStretchSpacer(1);
        extra_actions->Add(btn_more_actions_, 0);
        table_actions_box->Add(extra_actions, 0, wxEXPAND);

        left_box->Add(table_actions_box, 0, wxEXPAND);
    } else {
        auto* table_actions_row = new wxBoxSizer(wxHORIZONTAL);
        table_actions_row->Add(btn_edit_qty_, 0, wxRIGHT, 6);
        table_actions_row->Add(btn_edit_notes_, 0, wxRIGHT, 6);
        table_actions_row->Add(btn_remove_item_, 0, wxRIGHT, 6);
        table_actions_row->AddStretchSpacer(1);
        table_actions_row->Add(btn_more_actions_, 0);
        left_box->Add(table_actions_row, 0, wxEXPAND);
    }

    left_box->SetMinSize(wxSize(350, -1));
    main_sizer->Add(left_box, 1, wxEXPAND | wxRIGHT, touch ? 16 : 14);

    // =========================================================================
    // Columna Derecha: CUENTA ACTUAL (Tarjeta blanca Cupertino)
    // =========================================================================
    auto* account_card = new CardPanel(this);
    int card_w = touch ? 330 : 310;
    account_card->SetMinSize(wxSize(card_w, -1));
    account_card->SetMaxSize(wxSize(card_w, -1));

    auto* acc_sizer = new wxBoxSizer(wxVERTICAL);
    int pad_lr = touch ? 18 : 16;

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
    acc_sizer->AddSpacer(touch ? 16 : 12);

    auto make_summary_row = [&](const wxString& label, wxStaticText** out_val, bool is_total = false) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* l = new wxStaticText(account_card, wxID_ANY, label);
        auto* v = new wxStaticText(account_card, wxID_ANY, wxT("$0.00"), wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
        *out_val = v;

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
    acc_sizer->Add(make_summary_row(wxT("Descuento:"), &lbl_discount_val_), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);
    acc_sizer->Add(make_summary_row(wxT("TOTAL:"), &lbl_total_val_, true), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);

    // Botón Cobrar (verde éxito Cupertino #166534 con receipt_long)
    int checkout_h = touch ? 60 : 50;
    btn_checkout_ = new wxButton(account_card, wxID_ANY, wxT("Cobrar"), wxDefaultPosition, wxSize(-1, checkout_h));
    Theme::style_button_success(btn_checkout_, checkout_h);
    Theme::setup_button_icon(btn_checkout_, "receipt_long", wxSize(touch ? 26 : 22, touch ? 26 : 22), Theme::ColorSurface);
    acc_sizer->Add(btn_checkout_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad_lr);

    // Botón Cancelar Pedido (separado y distinguido de quitar línea)
    int cancel_h = touch ? 48 : 36;
    btn_cancel_order_ = new wxButton(account_card, wxID_ANY, wxT("Cancelar pedido"), wxDefaultPosition, wxSize(-1, cancel_h));
    Theme::style_button_secondary(btn_cancel_order_, cancel_h);
    Theme::setup_button_icon(btn_cancel_order_, "cancel", wxSize(touch ? 18 : 16, touch ? 18 : 16), Theme::ColorTextSecondary);
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

    update_contextual_actions();
}

void PosPanel::update_contextual_actions() {
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
    } else {
        if (btn_edit_qty_) btn_edit_qty_->SetToolTip(wxT("Seleccione una línea de la tabla para editar su cantidad"));
        if (btn_edit_notes_) btn_edit_notes_->SetToolTip(wxT("Seleccione una línea de la tabla para editar su nota"));
        if (btn_remove_item_) btn_remove_item_->SetToolTip(wxT("Seleccione una línea de la tabla para quitarla"));
    }
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
    std::string query = txt_search_->GetValue().ToStdString();
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

    std::string notes = txt_notes_->GetValue().ToStdString();
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
        list_items_->SetItem(row, 4, wxString::FromUTF8(item.unit_price.to_string().c_str()));
        list_items_->SetItem(row, 5, wxString::FromUTF8(item.subtotal.to_string().c_str()));
        idx++;
    }

    if (!current_cart_items_.empty()) {
        list_items_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);

        wxClientDC r_dc(list_items_);
        r_dc.SetFont(list_items_->GetFont());
        int max_cart_sku_w = 0;
        for (const auto& item : current_cart_items_) {
            int tw = 0, th = 0;
            r_dc.GetTextExtent(wxString::FromUTF8(item.sku.c_str()), &tw, &th);
            if (tw > max_cart_sku_w) max_cart_sku_w = tw;
        }
        if (max_cart_sku_w > 0) {
            int img_and_native_pad = Theme::is_touch() ? 30 : 26;
            int needed_col0 = max_cart_sku_w + 1 + img_and_native_pad;
            if (list_items_->GetColumnWidth(0) < needed_col0) {
                list_items_->SetColumnWidth(0, needed_col0);
            }
        }
    }

    lbl_items_count_->SetLabel(wxString::Format(wxT("%lld artículos en carrito"), cart.total_items));
    lbl_subtotal_val_->SetLabel(wxString::FromUTF8(cart.subtotal.to_string().c_str()));
    lbl_discount_val_->SetLabel(wxString::FromUTF8(cart.discount.to_string().c_str()));
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
    wxTextEntryDialog dlg(
        this,
        wxString::Format(wxT("Ingrese la nueva cantidad para '%s':"), wxString::FromUTF8(item.name.c_str())),
        wxT("Editar Cantidad"),
        wxString::Format(wxT("%lld"), item.quantity)
    );

    if (dlg.ShowModal() != wxID_OK) return;

    long new_qty = 0;
    if (!dlg.GetValue().ToLong(&new_qty) || new_qty < 0) {
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

    CheckoutDialog dlg(this, total);
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
