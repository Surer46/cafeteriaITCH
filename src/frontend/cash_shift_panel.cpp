#include "src/frontend/cash_shift_panel.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <wx/datetime.h>
#include <wx/statline.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace app::frontend {

namespace {

contracts::Money money_abs_cents(std::int64_t cents) {
    if (cents >= 0) {
        return contracts::Money::from_cents(cents);
    }
    if (cents == std::numeric_limits<std::int64_t>::min()) {
        return contracts::Money::from_cents(std::numeric_limits<std::int64_t>::max());
    }
    return contracts::Money::from_cents(-cents);
}

std::string format_difference_plain(std::int64_t diff_cents) {
    if (diff_cents == 0) {
        return "Cuadra";
    }
    if (diff_cents > 0) {
        return "Sobran " + contracts::Money::from_cents(diff_cents).to_string();
    }
    return "Faltan " + money_abs_cents(diff_cents).to_string();
}

std::string format_mismatch_warning(std::int64_t mismatch_cents) {
    return "Las ventas no cuadran con los pagos registrados por " +
           money_abs_cents(mismatch_cents).to_string() + ". Avise al Dueño.";
}

std::string movement_kind_plain(contracts::CashMovementKind kind) {
    return (kind == contracts::CashMovementKind::Inflow) ? "Entrada" : "Salida";
}

std::string movement_time_plain(const std::string& created_at) {
    if (created_at.size() >= 19) {
        return created_at.substr(11, 8);
    }
    return created_at;
}

std::string format_closeout_summary_text(const contracts::ShiftCloseoutDto& dto, bool include_header_ids) {
    std::string out;
    if (include_header_ids) {
        out += "Caja: " + (dto.register_name.empty() ? std::string("Caja") : dto.register_name) + "\n";
        out += "Turno de: " + dto.opened_by_name + " (abierto: " + dto.opened_at + ")\n";
        if (dto.is_closed) {
            if (!dto.closed_by_name.empty() && dto.closed_by_user_id != dto.opened_by_user_id) {
                out += "Cerrado por otra persona: " + dto.closed_by_name + " (" + dto.closed_at + ")\n";
            } else {
                out += "Cerrado por: " + dto.closed_by_name + " (" + dto.closed_at + ")\n";
            }
        }
    }
    out += "Fondo inicial: " + dto.initial_cash.to_string() +
           " · Ventas: " + std::to_string(dto.sales_count) +
           " (Bruto: " + dto.gross_sales.to_string() +
           ", Descuento: " + dto.discount_total.to_string() +
           ", Neto: " + dto.net_sales.to_string() + ")\n";
    out += "Métodos: Efectivo " + dto.cash_sales.to_string() +
           " · Tarjeta " + dto.card_sales.to_string() +
           " · Transferencia " + dto.transfer_sales.to_string() + "\n";
    out += "Entradas de efectivo: " + dto.cash_inflows.to_string() +
           " · Salidas de efectivo: " + dto.cash_outflows.to_string() + "\n";
    out += "Efectivo esperado: " + dto.expected_cash.to_string();
    if (dto.is_closed) {
        out += " · Efectivo contado: " + dto.counted_cash.to_string() +
               " · Diferencia: " + format_difference_plain(dto.difference_cents);
        if (!dto.difference_reason.empty()) {
            out += "\nMotivo: " + dto.difference_reason;
        }
    }
    return out;
}

void ensure_btn_fits(wxButton* btn, int min_w_logical, int min_h_logical) {
    if (!btn) return;
    const int text_w = btn->GetTextExtent(btn->GetLabel()).GetWidth();
    const int needed_w = std::max(btn->FromDIP(min_w_logical), text_w + btn->FromDIP(26));
    const int min_h = btn->FromDIP(min_h_logical);
    btn->SetMinSize(wxSize(needed_w, min_h));
    btn->SetSize(wxSize(needed_w, min_h));
}

constexpr const char* kCloseReasonLabelUtf8 = "Motivo de diferencia (obligatorio si no cuadra):";
constexpr const char* kSnapshotNoticeUtf8 = "Corte anterior a la conciliación: importes calculados al consultar";
constexpr const char* kClosedPromptUtf8 = "No hay ningún turno abierto en la caja actual. Debe abrir un turno con un fondo inicial para registrar cobros en el punto de venta.";

void wrap_static_text_deterministic(wxStaticText* lbl, const wxString& raw, int wrap_w) {
    if (!lbl) return;
    const int safe_w = std::max(lbl->FromDIP(120), wrap_w);
    wxClientDC dc(lbl);
    dc.SetFont(lbl->GetFont());
    wxString wrapped;
    int line_count = 0;

    size_t para_start = 0;
    while (para_start <= raw.length()) {
        size_t nl = raw.find(wxT('\n'), para_start);
        wxString para = (nl == wxString::npos) ? raw.Mid(para_start) : raw.Mid(para_start, nl - para_start);
        if (para.IsEmpty()) {
            if (line_count > 0) wrapped += wxT("\n");
            ++line_count;
        } else {
            wxString cur_line;
            size_t pos = 0;
            while (pos < para.length()) {
                size_t next = para.find(wxT(' '), pos);
                wxString word = (next == wxString::npos) ? para.Mid(pos) : para.Mid(pos, next - pos);
                wxString cand = cur_line.IsEmpty() ? word : (cur_line + wxT(" ") + word);
                if (!cur_line.IsEmpty() && dc.GetTextExtent(cand).GetWidth() > safe_w) {
                    if (line_count > 0) wrapped += wxT("\n");
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
                if (line_count > 0) wrapped += wxT("\n");
                wrapped += cur_line;
                ++line_count;
            }
        }
        if (nl == wxString::npos) break;
        para_start = nl + 1;
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

std::string iso_date(const wxDateTime& dt) {
    return std::string(dt.FormatISODate().ToUTF8());
}

} // namespace

CashShiftPanel::CashShiftPanel(
    wxWindow* parent,
    services::ServiceRegistry services,
    std::function<void()> on_shift_changed
) : wxPanel(parent, wxID_ANY),
    services_(std::move(services)),
    on_shift_changed_(std::move(on_shift_changed)) {
    setup_ui();
    apply_history_preset(2); // Este mes por omisión al consultar historial
    refresh_status();
}

CashShiftPanel::~CashShiftPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &CashShiftPanel::on_size, this);
    if (txt_final_cash_) {
        txt_final_cash_->Unbind(wxEVT_TEXT, &CashShiftPanel::on_final_cash_changed, this);
    }
    if (history_list_) {
        history_list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &CashShiftPanel::on_hist_row_selected, this);
    }
    if (keypad_) {
        keypad_->hide_keypad(false);
    }
    DestroyChildren();
}

void CashShiftPanel::setup_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;

    SetBackgroundColour(Theme::ColorBackground);
    auto* root = new wxBoxSizer(wxVERTICAL);

    // Encabezado + pestañas de sección
    auto* top_bar = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_title = new wxStaticText(this, wxID_ANY, wxT("Control de Turno de Caja"));
    lbl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    top_bar->Add(lbl_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));

    btn_tab_current_ = new wxButton(this, wxID_ANY, wxT("Turno actual"));
    btn_tab_current_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_tab_current_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_tab_current, this);
    ensure_btn_fits(btn_tab_current_, 120, btn_h);
    top_bar->Add(btn_tab_current_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    btn_tab_history_ = new wxButton(this, wxID_ANY, wxT("Cortes anteriores"));
    btn_tab_history_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_tab_history_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_tab_history, this);
    ensure_btn_fits(btn_tab_history_, 150, btn_h);
    top_bar->Add(btn_tab_history_, 0, wxALIGN_CENTER_VERTICAL);
    top_bar->AddStretchSpacer(1);
    root->Add(top_bar, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    build_current_view(this);
    build_history_view(this);

    root->Add(pnl_current_, 1, wxEXPAND);
    root->Add(pnl_history_, 1, wxEXPAND);
    pnl_history_->Hide();

    SetSizer(root);

    if (touch) {
        keypad_ = new ui::NumericKeypad(this);

        auto open_prot = [this]() {
            std::vector<wxRect> rects;
            auto add_r = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_r(shift_badge_);
            add_r(lbl_details_);
            add_r(txt_initial_cash_ ? txt_initial_cash_->GetParent() : nullptr);
            add_r(btn_open_shift_);
            return rects;
        };
        keypad_->bind_field(
            txt_initial_cash_,
            txt_initial_cash_ ? txt_initial_cash_->GetParent() : nullptr,
            true,
            btn_open_shift_,
            open_prot
        );

        auto move_prot = [this]() {
            std::vector<wxRect> rects;
            auto add_r = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_r(txt_move_amount_ ? txt_move_amount_->GetParent() : nullptr);
            add_r(txt_move_desc_ ? txt_move_desc_->GetParent() : nullptr);
            add_r(btn_move_save_);
            return rects;
        };
        keypad_->bind_field(
            txt_move_amount_,
            txt_move_amount_ ? txt_move_amount_->GetParent() : nullptr,
            true,
            btn_move_save_,
            move_prot
        );

        auto close_prot = [this]() {
            std::vector<wxRect> rects;
            auto add_r = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_r(txt_final_cash_ ? txt_final_cash_->GetParent() : nullptr);
            add_r(txt_close_reason_ ? txt_close_reason_->GetParent() : nullptr);
            add_r(btn_close_shift_);
            return rects;
        };
        keypad_->bind_field(
            txt_final_cash_,
            txt_final_cash_ ? txt_final_cash_->GetParent() : nullptr,
            true,
            btn_close_shift_,
            close_prot
        );
    }

    update_tab_styles();
    update_movement_kind_styles();
    Bind(wxEVT_SIZE, &CashShiftPanel::on_size, this);
}

void CashShiftPanel::build_current_view(wxWindow* parent) {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;
    const int field_h = touch ? 48 : 34;

    pnl_current_ = new wxPanel(parent, wxID_ANY);
    pnl_current_->SetBackgroundColour(Theme::ColorBackground);
    auto* cur_sizer = new wxBoxSizer(wxVERTICAL);

    // =========================================================================
    // 1. Estado CERRADO (abrir turno + resumen último corte opcional)
    // =========================================================================
    pnl_closed_wrap_ = new wxPanel(pnl_current_, wxID_ANY);
    pnl_closed_wrap_->SetBackgroundColour(Theme::ColorBackground);
    auto* closed_wrap_sizer = new wxBoxSizer(wxVERTICAL);

    status_card_ = new CardPanel(pnl_closed_wrap_);
    status_card_->set_card_background(Theme::ColorSurface);
    auto* status_outer = new wxBoxSizer(wxVERTICAL);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    shift_badge_ = new StatusBadge(status_card_, wxT("Turno Cerrado"), false);
    card_sizer->Add(shift_badge_, 0, wxBOTTOM, FromDIP(12));

    lbl_details_ = new wxStaticText(status_card_, wxID_ANY, wxEmptyString);
    lbl_details_->SetFont(Theme::get_font_body());
    lbl_details_->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_details_, 0, wxEXPAND | wxBOTTOM, FromDIP(12));

    card_sizer->Add(new wxStaticLine(status_card_, wxID_ANY), 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    auto* lbl_open_prompt = new wxStaticText(
        status_card_, wxID_ANY, wxT("Ingrese el fondo inicial en efectivo para iniciar el turno:")
    );
    lbl_open_prompt->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_open_prompt->SetForegroundColour(Theme::ColorText);
    card_sizer->Add(lbl_open_prompt, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    auto* open_row = new wxBoxSizer(touch ? wxVERTICAL : wxHORIZONTAL);
    auto* initial_field = ui::make_text_field(
        status_card_, txt_initial_cash_, field_h, 140, Theme::get_font_body()
    );
    txt_initial_cash_->SetValue(wxT("300.00"));

    btn_open_shift_ = new wxButton(status_card_, wxID_ANY, wxT("Abrir Turno de Caja"));
    Theme::style_button_success(btn_open_shift_, btn_h);
    Theme::setup_button_icon(btn_open_shift_, "lock_open", wxSize(18, 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_open_shift_, 200, btn_h);
    btn_open_shift_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_open_shift_clicked, this);

    if (touch) {
        open_row->Add(initial_field, 0, wxBOTTOM | wxALIGN_LEFT, FromDIP(10));
        open_row->Add(btn_open_shift_, 0, wxALIGN_LEFT);
    } else {
        open_row->Add(initial_field, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
        open_row->Add(btn_open_shift_, 0, wxALIGN_CENTER_VERTICAL);
    }
    card_sizer->Add(open_row, 0, wxEXPAND);
    status_outer->Add(card_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    status_card_->SetSizer(status_outer);
    closed_wrap_sizer->Add(status_card_, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    // Resumen del último corte recién cerrado
    last_closed_card_ = new CardPanel(pnl_closed_wrap_);
    last_closed_card_->set_card_background(Theme::ColorSurface);
    auto* lc_outer = new wxBoxSizer(wxVERTICAL);
    auto* lc_sizer = new wxBoxSizer(wxVERTICAL);
    lbl_last_closed_title_ = new wxStaticText(last_closed_card_, wxID_ANY, wxT("Resumen del corte guardado"));
    lbl_last_closed_title_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_last_closed_title_->SetForegroundColour(Theme::ColorBrandDeep);
    lc_sizer->Add(lbl_last_closed_title_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    pnl_last_mismatch_ = new wxPanel(last_closed_card_, wxID_ANY);
    pnl_last_mismatch_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* lm_s = new wxBoxSizer(wxHORIZONTAL);
    lbl_last_mismatch_ = new wxStaticText(pnl_last_mismatch_, wxID_ANY, wxEmptyString);
    lbl_last_mismatch_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_last_mismatch_->SetForegroundColour(Theme::ColorError);
    lm_s->Add(lbl_last_mismatch_, 1, wxALL, FromDIP(8));
    pnl_last_mismatch_->SetSizer(lm_s);
    pnl_last_mismatch_->Hide();
    lc_sizer->Add(pnl_last_mismatch_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    lbl_last_closed_body_ = new wxStaticText(last_closed_card_, wxID_ANY, wxEmptyString);
    lbl_last_closed_body_->SetFont(Theme::get_font_body());
    lbl_last_closed_body_->SetForegroundColour(Theme::ColorText);
    lc_sizer->Add(lbl_last_closed_body_, 1, wxEXPAND);
    lc_outer->Add(lc_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    last_closed_card_->SetSizer(lc_outer);
    last_closed_card_->Hide();
    closed_wrap_sizer->Add(last_closed_card_, 0, wxEXPAND);

    pnl_closed_wrap_->SetSizer(closed_wrap_sizer);
    cur_sizer->Add(pnl_closed_wrap_, 1, wxEXPAND);

    // =========================================================================
    // 2. Estado ABIERTO (barra turno + split Movimientos | Corte con arqueo)
    // =========================================================================
    pnl_open_wrap_ = new wxPanel(pnl_current_, wxID_ANY);
    pnl_open_wrap_->SetBackgroundColour(Theme::ColorBackground);
    auto* open_wrap_sizer = new wxBoxSizer(wxVERTICAL);

    open_header_card_ = new CardPanel(pnl_open_wrap_);
    open_header_card_->set_card_background(Theme::ColorSurface);
    auto* oh_pad = new wxBoxSizer(wxVERTICAL);
    auto* oh_outer = new wxBoxSizer(wxVERTICAL);
    auto* oh_row = new wxBoxSizer(wxHORIZONTAL);

    open_badge_ = new StatusBadge(open_header_card_, wxT("Turno Activo"), true);
    oh_row->Add(open_badge_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    lbl_open_details_ = new wxStaticText(open_header_card_, wxID_ANY, wxEmptyString);
    lbl_open_details_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_open_details_->SetForegroundColour(Theme::ColorText);
    oh_row->Add(lbl_open_details_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

    btn_preview_refresh_ = new wxButton(open_header_card_, wxID_ANY, wxT("Actualizar"));
    btn_preview_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_preview_refresh_, btn_h);
    ensure_btn_fits(btn_preview_refresh_, 105, btn_h);
    btn_preview_refresh_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_refresh_preview, this);
    oh_row->Add(btn_preview_refresh_, 0, wxALIGN_CENTER_VERTICAL);
    oh_outer->Add(oh_row, 0, wxEXPAND);

    pnl_open_mismatch_ = new wxPanel(open_header_card_, wxID_ANY);
    pnl_open_mismatch_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* om_s = new wxBoxSizer(wxHORIZONTAL);
    lbl_open_mismatch_ = new wxStaticText(pnl_open_mismatch_, wxID_ANY, wxEmptyString);
    lbl_open_mismatch_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_open_mismatch_->SetForegroundColour(Theme::ColorError);
    om_s->Add(lbl_open_mismatch_, 1, wxALL, FromDIP(8));
    pnl_open_mismatch_->SetSizer(om_s);
    pnl_open_mismatch_->Hide();
    oh_outer->Add(pnl_open_mismatch_, 0, wxEXPAND | wxTOP, FromDIP(8));

    oh_pad->Add(oh_outer, 1, wxEXPAND | wxALL, FromDIP(16));
    open_header_card_->SetSizer(oh_pad);
    open_wrap_sizer->Add(open_header_card_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* split = new wxBoxSizer(wxHORIZONTAL);

    // --- Izquierda: Entradas y salidas de efectivo ---
    movements_card_ = new CardPanel(pnl_open_wrap_);
    movements_card_->set_card_background(Theme::ColorSurface);
    movements_card_->SetMinSize(FromDIP(wxSize(touch ? 340 : 330, -1)));
    auto* mv_pad = new wxBoxSizer(wxVERTICAL);
    auto* mv_sizer = new wxBoxSizer(wxVERTICAL);

    auto* mv_title = new wxStaticText(movements_card_, wxID_ANY, wxT("Entradas y salidas de efectivo"));
    mv_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    mv_title->SetForegroundColour(Theme::ColorBrandDeep);
    mv_sizer->Add(mv_title, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* kind_row = new wxBoxSizer(wxHORIZONTAL);
    btn_move_inflow_ = new wxButton(movements_card_, wxID_ANY, wxT("Entrada de efectivo"));
    btn_move_inflow_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    btn_move_inflow_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_move_kind_inflow, this);
    ensure_btn_fits(btn_move_inflow_, 120, btn_h);
    kind_row->Add(btn_move_inflow_, 1, wxRIGHT, FromDIP(6));

    btn_move_outflow_ = new wxButton(movements_card_, wxID_ANY, wxT("Salida de efectivo"));
    btn_move_outflow_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    btn_move_outflow_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_move_kind_outflow, this);
    ensure_btn_fits(btn_move_outflow_, 120, btn_h);
    kind_row->Add(btn_move_outflow_, 1);
    mv_sizer->Add(kind_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* mv_fields = new wxBoxSizer(wxHORIZONTAL);
    auto* col_amt = new wxBoxSizer(wxVERTICAL);
    auto* lbl_mamt = new wxStaticText(movements_card_, wxID_ANY, wxT("Importe ($):"));
    lbl_mamt->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    col_amt->Add(lbl_mamt, 0, wxEXPAND);
    wxWindow* mamt_box = ui::make_text_field(
        movements_card_, txt_move_amount_, field_h, 95, Theme::get_font_body()
    );
    txt_move_amount_->SetHint(wxT("Ej. 100.00"));
    col_amt->Add(mamt_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    mv_fields->Add(col_amt, 1, wxEXPAND | wxRIGHT, FromDIP(8));

    auto* col_desc = new wxBoxSizer(wxVERTICAL);
    auto* lbl_mdesc = new wxStaticText(movements_card_, wxID_ANY, wxT("Descripción:"));
    lbl_mdesc->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    col_desc->Add(lbl_mdesc, 0, wxEXPAND);
    wxWindow* mdesc_box = ui::make_text_field(
        movements_card_, txt_move_desc_, field_h, 130, Theme::get_font_body()
    );
    txt_move_desc_->SetHint(wxT("Motivo (3 a 200 caracteres)"));
    col_desc->Add(mdesc_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    mv_fields->Add(col_desc, 2, wxEXPAND);
    mv_sizer->Add(mv_fields, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* mv_btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_move_save_ = new wxButton(movements_card_, wxID_ANY, wxT("Registrar movimiento"));
    btn_move_save_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_move_save_, btn_h);
    ensure_btn_fits(btn_move_save_, 165, btn_h);
    btn_move_save_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_register_movement, this);
    mv_btn_row->Add(btn_move_save_, 0, wxALIGN_CENTER_VERTICAL);
    mv_sizer->Add(mv_btn_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    lbl_move_feedback_ = new wxStaticText(movements_card_, wxID_ANY, wxEmptyString);
    lbl_move_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_move_feedback_->Hide();
    mv_sizer->Add(lbl_move_feedback_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    lbl_move_empty_ = new wxStaticText(
        movements_card_, wxID_ANY, wxT("Sin movimientos de efectivo en este turno."),
        wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_move_empty_->SetFont(Theme::get_font_body());
    lbl_move_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_move_empty_->Hide();
    mv_sizer->Add(lbl_move_empty_, 1, wxEXPAND | wxTOP, FromDIP(6));

    movements_list_ = new wxListView(
        movements_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    movements_list_->SetFont(Theme::get_font_body());
    movements_list_->SetBackgroundColour(Theme::ColorSurface);
    movements_list_->SetForegroundColour(Theme::ColorText);
    movements_list_->InsertColumn(0, wxT("Hora"));
    movements_list_->InsertColumn(1, wxT("Tipo"));
    movements_list_->InsertColumn(2, wxT("Importe"));
    movements_list_->InsertColumn(3, wxT("Descripción"));
    movements_list_->SetMinSize(FromDIP(wxSize(-1, 68)));
    if (touch) {
        ui::apply_touch_row_height(movements_list_);
    }
    mv_sizer->Add(movements_list_, 1, wxEXPAND);

    mv_pad->Add(mv_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    movements_card_->SetSizer(mv_pad);
    split->Add(movements_card_, 10, wxEXPAND | wxRIGHT, FromDIP(8));

    // --- Derecha: Corte de turno con arqueo ---
    closeout_card_ = new CardPanel(pnl_open_wrap_);
    closeout_card_->set_card_background(Theme::ColorSurface);
    closeout_card_->SetMinSize(FromDIP(wxSize(touch ? 340 : 340, -1)));
    auto* co_outer = new wxBoxSizer(wxVERTICAL);

    closeout_scroll_ = new wxScrolledWindow(
        closeout_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL
    );
    closeout_scroll_->SetBackgroundColour(Theme::ColorSurface);
    closeout_scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    closeout_scroll_->SetScrollRate(0, FromDIP(14));
    auto* co_s = new wxBoxSizer(wxVERTICAL);

    auto* lbl_co_title = new wxStaticText(closeout_scroll_, wxID_ANY, wxT("Corte de turno (Arqueo)"));
    lbl_co_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_co_title->SetForegroundColour(Theme::ColorBrandDeep);
    co_s->Add(lbl_co_title, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    auto add_co_line = [&](wxStaticText*& ptr) {
        ptr = new wxStaticText(closeout_scroll_, wxID_ANY, wxEmptyString);
        ptr->SetFont(Theme::get_font_small());
        ptr->SetForegroundColour(Theme::ColorText);
        co_s->Add(ptr, 0, wxEXPAND | wxBOTTOM, FromDIP(2));
    };
    add_co_line(lbl_co_initial_);
    add_co_line(lbl_co_sales_);
    add_co_line(lbl_co_methods_);
    add_co_line(lbl_co_flows_);

    lbl_co_expected_ = new wxStaticText(closeout_scroll_, wxID_ANY, wxEmptyString);
    lbl_co_expected_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_co_expected_->SetForegroundColour(Theme::ColorBrandDeep);
    co_s->Add(lbl_co_expected_, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));

    co_s->Add(new wxStaticLine(closeout_scroll_, wxID_ANY), 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* lbl_cnt = new wxStaticText(closeout_scroll_, wxID_ANY, wxT("Efectivo contado ($):"));
    lbl_cnt->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_cnt->SetForegroundColour(Theme::ColorText);
    co_s->Add(lbl_cnt, 0, wxEXPAND);
    wxWindow* final_box = ui::make_text_field(
        closeout_scroll_, txt_final_cash_, field_h, 130, Theme::get_font_body()
    );
    txt_final_cash_->ChangeValue(wxEmptyString);
    txt_final_cash_->SetHint(wxT("Ej. 500.00"));
    txt_final_cash_->Bind(wxEVT_TEXT, &CashShiftPanel::on_final_cash_changed, this);
    co_s->Add(final_box, 0, wxEXPAND | wxTOP, FromDIP(6));

    lbl_diff_preview_ = new wxStaticText(closeout_scroll_, wxID_ANY, wxEmptyString);
    lbl_diff_preview_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_diff_preview_->Hide();
    co_s->Add(lbl_diff_preview_, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(6));

    lbl_rsn_ = new wxStaticText(
        closeout_scroll_, wxID_ANY, wxString::FromUTF8(kCloseReasonLabelUtf8)
    );
    lbl_rsn_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_rsn_->SetForegroundColour(Theme::ColorText);
    co_s->Add(lbl_rsn_, 0, wxEXPAND | wxTOP, FromDIP(8));
    wxWindow* reason_box = ui::make_text_field(
        closeout_scroll_, txt_close_reason_, field_h, 140, Theme::get_font_body()
    );
    txt_close_reason_->SetHint(wxT("Obligatorio si sobra o falta (3 a 200 caracteres)"));
    co_s->Add(reason_box, 0, wxEXPAND | wxTOP, FromDIP(6));

    closeout_scroll_->SetSizer(co_s);
    const int co_pad = FromDIP(16);
    co_outer->Add(closeout_scroll_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, co_pad);

    // Pie fijo de closeout_card_ para que el botón de cierre siempre sea visible en Táctil
    lbl_close_feedback_ = new wxStaticText(closeout_card_, wxID_ANY, wxEmptyString);
    lbl_close_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_close_feedback_->SetForegroundColour(Theme::ColorError);
    lbl_close_feedback_->Hide();
    co_outer->Add(lbl_close_feedback_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, co_pad);

    btn_close_shift_ = new wxButton(closeout_card_, wxID_ANY, wxT("Cerrar Turno de Caja"));
    Theme::style_button_destructive(btn_close_shift_, btn_h);
    Theme::setup_button_icon(btn_close_shift_, "lock", wxSize(18, 18), Theme::ColorError);
    ensure_btn_fits(btn_close_shift_, 200, btn_h);
    btn_close_shift_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_close_shift_clicked, this);
    co_outer->Add(btn_close_shift_, 0, wxEXPAND | wxALL, co_pad);
    closeout_card_->SetSizer(co_outer);
    split->Add(closeout_card_, 11, wxEXPAND);

    open_wrap_sizer->Add(split, 1, wxEXPAND);
    pnl_open_wrap_->SetSizer(open_wrap_sizer);
    cur_sizer->Add(pnl_open_wrap_, 1, wxEXPAND);

    pnl_current_->SetSizer(cur_sizer);
}

void CashShiftPanel::build_history_view(wxWindow* parent) {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;
    const int field_h = touch ? 48 : 34;

    pnl_history_ = new wxPanel(parent, wxID_ANY);
    pnl_history_->SetBackgroundColour(Theme::ColorBackground);
    auto* hs = new wxBoxSizer(wxVERTICAL);

    // Filtros de rango
    hist_filter_card_ = new CardPanel(pnl_history_);
    hist_filter_card_->set_card_background(Theme::ColorSurface);
    auto* hf_pad = new wxBoxSizer(wxVERTICAL);
    auto* hf_outer = new wxBoxSizer(wxVERTICAL);

    auto* preset_row = new wxBoxSizer(wxHORIZONTAL);
    btn_hist_today_ = new wxButton(hist_filter_card_, wxID_ANY, wxT("Hoy"));
    btn_hist_today_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_hist_today_, btn_h);
    ensure_btn_fits(btn_hist_today_, 75, btn_h);
    btn_hist_today_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_hist_preset_today, this);
    preset_row->Add(btn_hist_today_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    btn_hist_week_ = new wxButton(hist_filter_card_, wxID_ANY, wxT("Esta semana"));
    btn_hist_week_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_hist_week_, btn_h);
    ensure_btn_fits(btn_hist_week_, 115, btn_h);
    btn_hist_week_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_hist_preset_week, this);
    preset_row->Add(btn_hist_week_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    btn_hist_month_ = new wxButton(hist_filter_card_, wxID_ANY, wxT("Este mes"));
    btn_hist_month_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_hist_month_, btn_h);
    ensure_btn_fits(btn_hist_month_, 95, btn_h);
    btn_hist_month_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_hist_preset_month, this);
    preset_row->Add(btn_hist_month_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    preset_row->AddStretchSpacer(1);

    btn_hist_search_ = new wxButton(hist_filter_card_, wxID_ANY, wxT("Buscar"));
    btn_hist_search_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_hist_search_, btn_h);
    ensure_btn_fits(btn_hist_search_, 95, btn_h);
    btn_hist_search_->Bind(wxEVT_BUTTON, &CashShiftPanel::on_hist_search, this);
    preset_row->Add(btn_hist_search_, 0, wxALIGN_CENTER_VERTICAL);
    hf_outer->Add(preset_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* range_row = new wxBoxSizer(wxHORIZONTAL);
    auto* lbl_from = new wxStaticText(hist_filter_card_, wxID_ANY, wxT("Desde:"));
    lbl_from->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    range_row->Add(lbl_from, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    wxWindow* hfrom_box = ui::make_text_field(
        hist_filter_card_, txt_hist_from_, field_h, 112, Theme::get_font_body()
    );
    range_row->Add(hfrom_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

    auto* lbl_to = new wxStaticText(hist_filter_card_, wxID_ANY, wxT("Hasta:"));
    lbl_to->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    range_row->Add(lbl_to, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    wxWindow* hto_box = ui::make_text_field(
        hist_filter_card_, txt_hist_to_, field_h, 112, Theme::get_font_body()
    );
    range_row->Add(hto_box, 0, wxALIGN_CENTER_VERTICAL);
    hf_outer->Add(range_row, 0, wxEXPAND);

    pnl_hist_banner_ = new wxPanel(hist_filter_card_, wxID_ANY);
    pnl_hist_banner_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* hb_s = new wxBoxSizer(wxHORIZONTAL);
    lbl_hist_banner_ = new wxStaticText(pnl_hist_banner_, wxID_ANY, wxEmptyString);
    lbl_hist_banner_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_hist_banner_->SetForegroundColour(Theme::ColorError);
    hb_s->Add(lbl_hist_banner_, 1, wxALL, FromDIP(6));
    pnl_hist_banner_->SetSizer(hb_s);
    pnl_hist_banner_->Hide();
    hf_outer->Add(pnl_hist_banner_, 0, wxEXPAND | wxTOP, FromDIP(8));

    hf_pad->Add(hf_outer, 1, wxEXPAND | wxALL, FromDIP(16));
    hist_filter_card_->SetSizer(hf_pad);
    hs->Add(hist_filter_card_, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    // Lista superior de cortes cerrados
    hist_list_card_ = new CardPanel(pnl_history_);
    hist_list_card_->set_card_background(Theme::ColorSurface);
    auto* hl_s = new wxBoxSizer(wxVERTICAL);

    lbl_hist_empty_ = new wxStaticText(
        hist_list_card_, wxID_ANY, wxT("No hay cortes registrados en el rango seleccionado."),
        wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_hist_empty_->SetFont(Theme::get_font_body());
    lbl_hist_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_hist_empty_->Hide();
    hl_s->Add(lbl_hist_empty_, 1, wxEXPAND | wxALL, FromDIP(16));

    history_list_ = new wxListView(
        hist_list_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    history_list_->SetFont(Theme::get_font_body());
    history_list_->SetBackgroundColour(Theme::ColorSurface);
    history_list_->SetForegroundColour(Theme::ColorText);
    history_list_->InsertColumn(0, wxT("Cierre"));
    history_list_->InsertColumn(1, wxT("Abrió"));
    history_list_->InsertColumn(2, wxT("Cerró"));
    history_list_->InsertColumn(3, wxT("Esperado"));
    history_list_->InsertColumn(4, wxT("Contado"));
    history_list_->InsertColumn(5, wxT("Diferencia"));
    history_list_->InsertColumn(6, wxT("Motivo"));
    history_list_->SetMinSize(FromDIP(wxSize(-1, touch ? 82 : 80)));
    if (touch) {
        ui::apply_touch_row_height(history_list_);
    }
    history_list_->Bind(wxEVT_LIST_ITEM_SELECTED, &CashShiftPanel::on_hist_row_selected, this);
    hl_s->Add(history_list_, 1, wxEXPAND | wxALL, FromDIP(16));

    hist_list_card_->SetSizer(hl_s);
    hs->Add(hist_list_card_, 4, wxEXPAND | wxBOTTOM, FromDIP(8));

    // Detalle inferior de corte seleccionado: altura del resumen calculada dinámicamente (sin cota fija de 98)
    hist_detail_card_ = new CardPanel(pnl_history_);
    hist_detail_card_->set_card_background(Theme::ColorSurface);
    auto* hd_pad = new wxBoxSizer(wxVERTICAL);
    auto* hd_outer = new wxBoxSizer(wxVERTICAL);
    hist_detail_scroll_ = new wxScrolledWindow(
        hist_detail_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL
    );
    hist_detail_scroll_->SetBackgroundColour(Theme::ColorSurface);
    hist_detail_scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    hist_detail_scroll_->SetScrollRate(0, FromDIP(14));
    hist_detail_scroll_->SetMinSize(FromDIP(wxSize(-1, 74)));
    auto* hd_s = new wxBoxSizer(wxVERTICAL);

    auto* lbl_hd_title = new wxStaticText(hist_detail_scroll_, wxID_ANY, wxT("Detalle del corte"));
    lbl_hd_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_hd_title->SetForegroundColour(Theme::ColorBrandDeep);
    hd_s->Add(lbl_hd_title, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    pnl_hist_snapshot_ = new wxPanel(hist_detail_scroll_, wxID_ANY);
    pnl_hist_snapshot_->SetBackgroundColour(wxColour(254, 243, 199));
    auto* hs_s = new wxBoxSizer(wxHORIZONTAL);
    lbl_hist_snapshot_ = new wxStaticText(
        pnl_hist_snapshot_, wxID_ANY,
        wxString::FromUTF8(kSnapshotNoticeUtf8)
    );
    lbl_hist_snapshot_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_hist_snapshot_->SetForegroundColour(wxColour(180, 83, 9));
    hs_s->Add(lbl_hist_snapshot_, 1, wxALL, FromDIP(6));
    pnl_hist_snapshot_->SetSizer(hs_s);
    pnl_hist_snapshot_->Hide();
    hd_s->Add(pnl_hist_snapshot_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    pnl_hist_mismatch_ = new wxPanel(hist_detail_scroll_, wxID_ANY);
    pnl_hist_mismatch_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* hm_s = new wxBoxSizer(wxHORIZONTAL);
    lbl_hist_mismatch_ = new wxStaticText(pnl_hist_mismatch_, wxID_ANY, wxEmptyString);
    lbl_hist_mismatch_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_hist_mismatch_->SetForegroundColour(Theme::ColorError);
    hm_s->Add(lbl_hist_mismatch_, 1, wxALL, FromDIP(6));
    pnl_hist_mismatch_->SetSizer(hm_s);
    pnl_hist_mismatch_->Hide();
    hd_s->Add(pnl_hist_mismatch_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    lbl_hist_detail_ = new wxStaticText(
        hist_detail_scroll_, wxID_ANY, wxT("Selecciona un corte para ver su detalle y movimientos.")
    );
    lbl_hist_detail_->SetFont(Theme::get_font_body());
    lbl_hist_detail_->SetForegroundColour(Theme::ColorText);
    hd_s->Add(lbl_hist_detail_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    hist_detail_scroll_->SetSizer(hd_s);
    hd_outer->Add(hist_detail_scroll_, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    auto* hd_moves_col = new wxBoxSizer(wxVERTICAL);
    auto* lbl_hm_title = new wxStaticText(hist_detail_card_, wxID_ANY, wxT("Movimientos del corte"));
    lbl_hm_title->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_hm_title->SetForegroundColour(Theme::ColorBrandDeep);
    hd_moves_col->Add(lbl_hm_title, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    lbl_hist_moves_empty_ = new wxStaticText(
        hist_detail_card_, wxID_ANY, wxT("Sin movimientos de efectivo en este corte.")
    );
    lbl_hist_moves_empty_->SetFont(Theme::get_font_small());
    lbl_hist_moves_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_hist_moves_empty_->Hide();
    hd_moves_col->Add(lbl_hist_moves_empty_, 1, wxEXPAND | wxTOP, FromDIP(4));

    hist_movements_list_ = new wxListView(
        hist_detail_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    hist_movements_list_->SetFont(Theme::get_font_body());
    hist_movements_list_->SetBackgroundColour(Theme::ColorSurface);
    hist_movements_list_->SetForegroundColour(Theme::ColorText);
    hist_movements_list_->InsertColumn(0, wxT("Hora"));
    hist_movements_list_->InsertColumn(1, wxT("Tipo"));
    hist_movements_list_->InsertColumn(2, wxT("Importe"));
    hist_movements_list_->InsertColumn(3, wxT("Descripción"));
    hist_movements_list_->SetMinSize(FromDIP(wxSize(-1, touch ? 88 : 92)));
    if (touch) {
        ui::apply_touch_row_height(hist_movements_list_);
    }
    hist_movements_list_->Hide();
    hd_moves_col->Add(hist_movements_list_, 1, wxEXPAND);
    hd_outer->Add(hd_moves_col, 1, wxEXPAND);

    hd_pad->Add(hd_outer, 1, wxEXPAND | wxALL, FromDIP(16));
    hist_detail_card_->SetSizer(hd_pad);
    hs->Add(hist_detail_card_, 8, wxEXPAND);

    pnl_history_->SetSizer(hs);
}

void CashShiftPanel::switch_tab(ActiveTab tab) {
    if (destroying_) return;
    if (keypad_) {
        keypad_->hide_keypad(false);
    }
    active_tab_ = tab;
    update_tab_styles();
    if (tab == ActiveTab::Current) {
        pnl_history_->Hide();
        pnl_current_->Show();
        Layout();
        refresh_status();
    } else {
        pnl_current_->Hide();
        pnl_history_->Show();
        Layout();
        load_history();
        CallAfter([this]() {
            if (destroying_ || IsBeingDeleted() || active_tab_ != ActiveTab::History) return;
            layout_history_detail();
            hist_list_card_->Layout();
            hist_detail_card_->Layout();
            pnl_history_->Layout();
            fit_list_columns(history_list_, {3, 2, 2, 2, 2, 2, 3});
            fit_list_columns(hist_movements_list_, {1, 1, 2, 4});
        });
    }
    Layout();
}

void CashShiftPanel::update_tab_styles() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;
    if (active_tab_ == ActiveTab::Current) {
        Theme::style_button_primary(btn_tab_current_, btn_h);
        Theme::style_button_secondary(btn_tab_history_, btn_h);
    } else {
        Theme::style_button_secondary(btn_tab_current_, btn_h);
        Theme::style_button_primary(btn_tab_history_, btn_h);
    }
    ensure_btn_fits(btn_tab_current_, 120, btn_h);
    ensure_btn_fits(btn_tab_history_, 150, btn_h);
}

void CashShiftPanel::update_movement_kind_styles() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;
    if (move_kind_ == contracts::CashMovementKind::Inflow) {
        Theme::style_button_primary(btn_move_inflow_, btn_h);
        Theme::style_button_secondary(btn_move_outflow_, btn_h);
    } else {
        Theme::style_button_secondary(btn_move_inflow_, btn_h);
        Theme::style_button_primary(btn_move_outflow_, btn_h);
    }
    ensure_btn_fits(btn_move_inflow_, 120, btn_h);
    ensure_btn_fits(btn_move_outflow_, 120, btn_h);
}

void CashShiftPanel::fit_list_columns(wxListView* list, const std::vector<int>&) {
    if (destroying_ || sizing_ || !list || !list->IsShown() || list->GetColumnCount() <= 0) return;
    if (list->GetClientSize().GetWidth() <= 0) return;
    sizing_ = true;

    wxClientDC dc(list);
    dc.SetFont(list->GetFont());
    const int ncols = list->GetColumnCount();
    const std::size_t flex = static_cast<std::size_t>(ncols - 1);
    std::vector<ui::ColumnSpec> specs;
    specs.reserve(static_cast<std::size_t>(ncols));

    for (int c = 0; c < ncols; ++c) {
        wxListItem col_info;
        col_info.SetMask(wxLIST_MASK_TEXT);
        wxString hdr;
        if (list->GetColumn(c, col_info)) {
            hdr = col_info.GetText();
        }
        int max_px = dc.GetTextExtent(hdr).GetWidth();
        const int rows = std::min(list->GetItemCount(), 40);
        for (int r = 0; r < rows; ++r) {
            max_px = std::max(max_px, dc.GetTextExtent(list->GetItemText(r, c)).GetWidth());
        }
        const int floor_px = 0;
        specs.push_back({hdr, max_px, floor_px, 16, 0});
    }
    ui::distribute_list_columns(list, specs, flex);
    // Si una barra vertical transitoria dejó sum > client_w, recortar la flexible para que nunca quede WS_HSCROLL.
    const int cur_cw = list->GetClientSize().GetWidth();
    if (cur_cw > 0) {
        int sum_w = 0;
        for (int c = 0; c < ncols; ++c) sum_w += list->GetColumnWidth(c);
        const int flex_idx = static_cast<int>(flex);
        if (sum_w > cur_cw - 2) {
            const int cur_flex = list->GetColumnWidth(flex_idx);
            const int trimmed = std::max(dc.GetTextExtent(specs[flex].header).GetWidth() + FromDIP(16),
                                         cur_flex - (sum_w - (cur_cw - 2)));
            if (trimmed != cur_flex) {
                list->SetColumnWidth(flex_idx, trimmed);
            }
        }
    }
    sizing_ = false;
}

void CashShiftPanel::populate_movements_list(
    wxListView* list,
    wxStaticText* empty_lbl,
    const std::vector<contracts::CashMovementDto>& movements,
    const wxString& empty_msg
) {
    if (!list || !empty_lbl) return;
    list->DeleteAllItems();
    if (movements.empty()) {
        list->Hide();
        empty_lbl->SetLabel(empty_msg);
        empty_lbl->Show();
        return;
    }
    empty_lbl->Hide();
    list->Show();
    for (std::size_t i = 0; i < movements.size(); ++i) {
        const auto& m = movements[i];
        const long idx = list->InsertItem(
            static_cast<long>(i), wxString::FromUTF8(movement_time_plain(m.created_at))
        );
        list->SetItem(idx, 1, wxString::FromUTF8(movement_kind_plain(m.kind)));
        list->SetItem(idx, 2, wxString::FromUTF8(m.amount.to_string()));
        list->SetItem(idx, 3, wxString::FromUTF8(m.description));
        if (m.kind == contracts::CashMovementKind::Outflow) {
            list->SetItemTextColour(idx, Theme::ColorError);
        } else {
            list->SetItemTextColour(idx, Theme::ColorText);
        }
    }
    fit_list_columns(list, {1, 1, 2, 4});
}

void CashShiftPanel::refresh_status() {
    if (destroying_ || !services_.shift_service) return;

    auto shift_res = services_.shift_service->get_current_shift();
    if (shift_res.is_success()) {
        pnl_closed_wrap_->Hide();
        pnl_open_wrap_->Show();
        refresh_open_preview();
    } else {
        has_open_preview_ = false;
        pnl_open_wrap_->Hide();
        pnl_closed_wrap_->Show();
        shift_badge_->set_status(wxT("Turno Cerrado"), false);
        const int wrap_w = std::max(FromDIP(240), pnl_current_->GetClientSize().GetWidth() - FromDIP(40));
        wrap_static_text_deterministic(lbl_details_, wxString::FromUTF8(kClosedPromptUtf8), wrap_w);
        render_last_closed_summary();
        status_card_->Layout();
        pnl_closed_wrap_->Layout();
    }
    pnl_current_->Layout();
    Layout();
}

void CashShiftPanel::layout_open_wrap_texts() {
    if (destroying_) return;
    if (pnl_closed_wrap_ && pnl_closed_wrap_->IsShown()) {
        const int cw = std::max(FromDIP(240), pnl_current_->GetClientSize().GetWidth() - FromDIP(40));
        if (lbl_details_) {
            wrap_static_text_deterministic(lbl_details_, wxString::FromUTF8(kClosedPromptUtf8), cw);
        }
        if (last_closed_card_ && last_closed_card_->IsShown() && lbl_last_closed_body_ && !last_closed_body_txt_.empty()) {
            wrap_static_text_deterministic(lbl_last_closed_body_, wxString::FromUTF8(last_closed_body_txt_), cw);
            last_closed_card_->Layout();
        }
        if (status_card_) status_card_->Layout();
        pnl_closed_wrap_->Layout();
        return;
    }

    if (!pnl_open_wrap_ || !pnl_open_wrap_->IsShown()) return;
    if (open_header_card_ && lbl_open_details_ && !last_open_header_txt_.empty()) {
        const int h_w = open_header_card_->GetClientSize().GetWidth();
        const int badge_w = open_badge_ ? open_badge_->GetBestSize().GetWidth() : FromDIP(110);
        const int btn_w = btn_preview_refresh_ ? btn_preview_refresh_->GetBestSize().GetWidth() : FromDIP(110);
        const int avail_hdr = std::max(FromDIP(180), h_w - FromDIP(32) - badge_w - btn_w - FromDIP(24));
        wrap_static_text_deterministic(lbl_open_details_, wxString::FromUTF8(last_open_header_txt_), avail_hdr);
    }
    if (pnl_open_mismatch_ && pnl_open_mismatch_->IsShown() && lbl_open_mismatch_ && !last_open_mismatch_txt_.empty()) {
        const int mw = std::max(FromDIP(200), open_header_card_->GetClientSize().GetWidth() - FromDIP(52));
        wrap_static_text_deterministic(lbl_open_mismatch_, wxString::FromUTF8(last_open_mismatch_txt_), mw);
        pnl_open_mismatch_->Layout();
    }
    if (open_header_card_) open_header_card_->Layout();

    if (movements_card_ && lbl_move_feedback_ && lbl_move_feedback_->IsShown() && !last_move_fb_txt_.empty()) {
        const int mv_w = std::max(FromDIP(180), movements_card_->GetClientSize().GetWidth() - FromDIP(36));
        wrap_static_text_deterministic(lbl_move_feedback_, wxString::FromUTF8(last_move_fb_txt_), mv_w);
        movements_card_->Layout();
    }

    if (closeout_scroll_) {
        auto wrap_co = [&](int sc_w) {
            const int w = std::max(FromDIP(180), sc_w - FromDIP(12));
            if (lbl_co_sales_ && !last_co_sales_txt_.empty()) {
                wrap_static_text_deterministic(lbl_co_sales_, wxString::FromUTF8(last_co_sales_txt_), w);
            }
            if (lbl_co_methods_ && !last_co_methods_txt_.empty()) {
                wrap_static_text_deterministic(lbl_co_methods_, wxString::FromUTF8(last_co_methods_txt_), w);
            }
            if (lbl_rsn_) {
                wrap_static_text_deterministic(lbl_rsn_, wxString::FromUTF8(kCloseReasonLabelUtf8), w);
            }
            closeout_scroll_->Layout();
            closeout_scroll_->FitInside();
        };
        const int sw1 = closeout_scroll_->GetClientSize().GetWidth();
        if (sw1 > 0) {
            wrap_co(sw1);
            const int sw2 = closeout_scroll_->GetClientSize().GetWidth();
            if (sw2 > 0 && sw2 != sw1) {
                wrap_co(sw2);
            }
        }
    }
    if (closeout_card_ && lbl_close_feedback_ && lbl_close_feedback_->IsShown() && !last_close_fb_txt_.empty()) {
        const int cw = std::max(FromDIP(180), closeout_card_->GetClientSize().GetWidth() - FromDIP(36));
        wrap_static_text_deterministic(lbl_close_feedback_, wxString::FromUTF8(last_close_fb_txt_), cw);
    }
    if (closeout_card_) closeout_card_->Layout();
    pnl_open_wrap_->Layout();
}

void CashShiftPanel::layout_history_detail() {
    if (destroying_ || !hist_detail_card_ || !hist_detail_scroll_) return;
    const bool touch = Theme::is_touch();
    const int card_w = hist_detail_card_->GetClientSize().GetWidth();
    const int card_h = hist_detail_card_->GetClientSize().GetHeight();
    if (card_w <= 0) return;

    auto run_wrap = [&](int sw) {
        const int w = std::max(FromDIP(180), sw - FromDIP(12));
        if (pnl_hist_snapshot_ && pnl_hist_snapshot_->IsShown() && lbl_hist_snapshot_) {
            wrap_static_text_deterministic(lbl_hist_snapshot_, wxString::FromUTF8(kSnapshotNoticeUtf8), w - FromDIP(16));
            pnl_hist_snapshot_->Layout();
        }
        if (pnl_hist_mismatch_ && pnl_hist_mismatch_->IsShown() && lbl_hist_mismatch_ && !last_hist_mismatch_txt_.empty()) {
            wrap_static_text_deterministic(lbl_hist_mismatch_, wxString::FromUTF8(last_hist_mismatch_txt_), w - FromDIP(16));
            pnl_hist_mismatch_->Layout();
        }
        if (lbl_hist_detail_) {
            const wxString raw = last_hist_detail_txt_.empty()
                ? wxT("Selecciona un corte para ver su detalle y movimientos.")
                : wxString::FromUTF8(last_hist_detail_txt_);
            wrap_static_text_deterministic(lbl_hist_detail_, raw, w);
        }
        hist_detail_scroll_->Layout();
    };

    const int inner_w = std::max(FromDIP(180), card_w - FromDIP(32));
    run_wrap(inner_w);

    const int need_h = hist_detail_scroll_->GetSizer()
        ? (hist_detail_scroll_->GetSizer()->GetMinSize().GetHeight() + FromDIP(4))
        : FromDIP(98);

    // Reservar siempre el alto mínimo de la sección "Movimientos del corte" + relleno de tarjeta (16*2) + gap (10)
    const int min_moves_block_h = FromDIP(touch ? 94 : 96) + FromDIP(28);
    const int avail_for_scroll = (card_h > 0)
        ? std::max(FromDIP(52), card_h - FromDIP(32) - FromDIP(10) - min_moves_block_h)
        : need_h;
    const int target_h = std::min(need_h, avail_for_scroll);

    hist_detail_scroll_->SetMinSize(wxSize(-1, target_h));
    hist_detail_scroll_->SetMaxSize(wxSize(-1, target_h));
    hist_detail_card_->Layout();

    // Si apareció barra vertical (en 1200x720 o 960x600), re-envolver con el client width real ya descontada la barra
    const int actual_sw = hist_detail_scroll_->GetClientSize().GetWidth();
    if (actual_sw > 0 && actual_sw < inner_w - FromDIP(4)) {
        run_wrap(actual_sw);
    }
    hist_detail_scroll_->FitInside();
    hist_detail_scroll_->Layout();
    hist_detail_card_->Layout();
}

void CashShiftPanel::refresh_open_preview() {
    if (destroying_ || !services_.shift_service) return;
    auto prev_res = services_.shift_service->preview_closeout("");
    if (!prev_res.is_success()) {
        has_open_preview_ = false;
        open_badge_->set_status(wxT("Turno Activo"), true);
        lbl_open_details_->SetLabel(wxString::FromUTF8(prev_res.error_message()));
        lbl_open_details_->SetForegroundColour(Theme::ColorError);
        pnl_open_mismatch_->Hide();
        lbl_co_initial_->SetLabel(wxEmptyString);
        lbl_co_sales_->SetLabel(wxEmptyString);
        lbl_co_methods_->SetLabel(wxEmptyString);
        lbl_co_flows_->SetLabel(wxEmptyString);
        lbl_co_expected_->SetLabel(wxEmptyString);
        populate_movements_list(movements_list_, lbl_move_empty_, {}, wxT("Sin movimientos de efectivo en este turno."));
        update_live_difference();
        pnl_open_wrap_->Layout();
        return;
    }

    has_open_preview_ = true;
    open_preview_ = prev_res.value();

    open_badge_->set_status(wxT("Turno Activo"), true);
    lbl_open_details_->SetForegroundColour(Theme::ColorText);
    const std::string header_txt =
        "Caja: " + (open_preview_.register_name.empty() ? std::string("Caja") : open_preview_.register_name) +
        " · Abierto por: " + open_preview_.opened_by_name +
        " · Apertura: " + open_preview_.opened_at +
        " · Fondo inicial: " + open_preview_.initial_cash.to_string();
    const std::string sales_txt =
        "Ventas: " + std::to_string(open_preview_.sales_count) +
        " · Bruto: " + open_preview_.gross_sales.to_string() +
        " · Descuento: " + open_preview_.discount_total.to_string() +
        " · Neto: " + open_preview_.net_sales.to_string();
    const std::string methods_txt =
        "Por método: Efectivo " + open_preview_.cash_sales.to_string() +
        " · Tarjeta " + open_preview_.card_sales.to_string() +
        " · Transferencia " + open_preview_.transfer_sales.to_string();

    last_open_header_txt_ = header_txt;
    last_co_sales_txt_ = sales_txt;
    last_co_methods_txt_ = methods_txt;

    if (open_preview_.payments_mismatch_cents != 0) {
        last_open_mismatch_txt_ = format_mismatch_warning(open_preview_.payments_mismatch_cents);
        pnl_open_mismatch_->Show();
    } else {
        last_open_mismatch_txt_.clear();
        pnl_open_mismatch_->Hide();
    }

    lbl_co_initial_->SetLabel(
        wxString::FromUTF8("Fondo inicial: " + open_preview_.initial_cash.to_string())
    );
    lbl_co_flows_->SetLabel(wxString::FromUTF8(
        "Entradas: " + open_preview_.cash_inflows.to_string() +
        " · Salidas: " + open_preview_.cash_outflows.to_string()
    ));
    lbl_co_expected_->SetLabel(
        wxString::FromUTF8("Efectivo esperado: " + open_preview_.expected_cash.to_string())
    );

    populate_movements_list(
        movements_list_,
        lbl_move_empty_,
        open_preview_.movements,
        wxT("Sin movimientos de efectivo en este turno.")
    );
    update_live_difference();

    layout_open_wrap_texts();
    fit_list_columns(movements_list_, {1, 1, 2, 4});

    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted() || !has_open_preview_) return;
        layout_open_wrap_texts();
        fit_list_columns(movements_list_, {1, 1, 2, 4});
    });
}

void CashShiftPanel::render_last_closed_summary() {
    if (!last_closed_card_) return;
    if (!last_closed_summary_.has_value()) {
        last_closed_body_txt_.clear();
        last_closed_card_->Hide();
        return;
    }
    const auto& dto = *last_closed_summary_;
    if (dto.payments_mismatch_cents != 0) {
        lbl_last_mismatch_->SetLabel(wxString::FromUTF8(format_mismatch_warning(dto.payments_mismatch_cents)));
        pnl_last_mismatch_->Show();
    } else {
        pnl_last_mismatch_->Hide();
    }
    last_closed_body_txt_ = format_closeout_summary_text(dto, true);
    const int wrap_w = std::max(FromDIP(240), pnl_current_->GetClientSize().GetWidth() - FromDIP(40));
    wrap_static_text_deterministic(lbl_last_closed_body_, wxString::FromUTF8(last_closed_body_txt_), wrap_w);
    last_closed_card_->Show();
    last_closed_card_->Layout();
}

void CashShiftPanel::update_live_difference() {
    if (!lbl_diff_preview_ || !txt_final_cash_) return;
    if (!has_open_preview_) {
        lbl_diff_preview_->SetLabel(wxEmptyString);
        lbl_diff_preview_->Hide();
        return;
    }
    const std::string raw = std::string(txt_final_cash_->GetValue().Trim().Trim(false).ToUTF8());
    if (raw.empty()) {
        lbl_diff_preview_->SetLabel(wxEmptyString);
        lbl_diff_preview_->Hide();
        return;
    }
    auto parsed = contracts::Money::try_parse(raw, false);
    if (!parsed.is_success()) {
        lbl_diff_preview_->SetLabel(wxT("Importe de efectivo contado no válido"));
        lbl_diff_preview_->SetForegroundColour(Theme::ColorError);
        lbl_diff_preview_->Show();
        return;
    }
    const std::int64_t diff_cents = parsed.value().cents() - open_preview_.expected_cash.cents();
    lbl_diff_preview_->SetLabel(wxString::FromUTF8(format_difference_plain(diff_cents)));
    if (diff_cents == 0) {
        lbl_diff_preview_->SetForegroundColour(Theme::ColorSuccess);
    } else if (diff_cents > 0) {
        lbl_diff_preview_->SetForegroundColour(wxColour(180, 83, 9));
    } else {
        lbl_diff_preview_->SetForegroundColour(Theme::ColorError);
    }
    lbl_diff_preview_->Show();
}

void CashShiftPanel::on_tab_current(wxCommandEvent&) { switch_tab(ActiveTab::Current); }
void CashShiftPanel::on_tab_history(wxCommandEvent&) { switch_tab(ActiveTab::History); }

void CashShiftPanel::on_move_kind_inflow(wxCommandEvent&) {
    move_kind_ = contracts::CashMovementKind::Inflow;
    update_movement_kind_styles();
    movements_card_->Layout();
}

void CashShiftPanel::on_move_kind_outflow(wxCommandEvent&) {
    move_kind_ = contracts::CashMovementKind::Outflow;
    update_movement_kind_styles();
    movements_card_->Layout();
}

void CashShiftPanel::on_refresh_preview(wxCommandEvent&) {
    refresh_open_preview();
}

void CashShiftPanel::on_final_cash_changed(wxCommandEvent&) {
    update_live_difference();
    if (closeout_scroll_) {
        closeout_scroll_->FitInside();
        closeout_scroll_->Layout();
    }
    if (closeout_card_) {
        closeout_card_->Layout();
    }
}

void CashShiftPanel::on_open_shift_clicked(wxCommandEvent&) {
    if (keypad_) keypad_->hide_keypad(false);
    wxString str = txt_initial_cash_->GetValue().Trim().Trim(false);
    std::string s = std::string(str.ToUTF8());
    if (s.empty()) {
        wxMessageBox(wxT("Debe ingresar un monto válido para el fondo inicial."), wxT("Monto Vacío"), wxOK | wxICON_WARNING, this);
        txt_initial_cash_->SetFocus();
        return;
    }

    auto parse_res = contracts::Money::try_parse(s, false);
    if (!parse_res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Monto de fondo inicial inválido: %s"), wxString::FromUTF8(parse_res.error_message().c_str())),
            wxT("Monto Inválido"),
            wxOK | wxICON_WARNING,
            this
        );
        txt_initial_cash_->SetFocus();
        return;
    }

    contracts::Money initial_cash = parse_res.value();
    auto res = services_.shift_service->open_shift("reg-01", initial_cash);
    if (!res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Error al abrir turno: %s"), wxString::FromUTF8(res.error_message().c_str())),
            wxT("Error de Apertura"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    last_closed_summary_.reset();
    if (txt_final_cash_) txt_final_cash_->ChangeValue(wxEmptyString);
    if (txt_close_reason_) txt_close_reason_->ChangeValue(wxEmptyString);
    if (lbl_close_feedback_) lbl_close_feedback_->Hide();
    wxMessageBox(
        wxString::Format(wxT("¡Turno abierto exitosamente!\nFondo inicial: %s"), wxString::FromUTF8(initial_cash.to_string().c_str())),
        wxT("Turno Abierto"),
        wxOK | wxICON_INFORMATION,
        this
    );

    refresh_status();
    if (on_shift_changed_) {
        on_shift_changed_();
    }
}

void CashShiftPanel::on_register_movement(wxCommandEvent&) {
    if (keypad_) keypad_->hide_keypad(false);
    if (!services_.shift_service) return;

    const std::string raw_amt = std::string(txt_move_amount_->GetValue().Trim().Trim(false).ToUTF8());
    if (raw_amt.empty()) {
        lbl_move_feedback_->SetForegroundColour(Theme::ColorError);
        lbl_move_feedback_->SetLabel(wxT("Ingresa el importe del movimiento."));
        lbl_move_feedback_->Show();
        movements_card_->Layout();
        txt_move_amount_->SetFocus();
        return;
    }
    auto amt_res = contracts::Money::try_parse(raw_amt, false);
    if (!amt_res.is_success()) {
        lbl_move_feedback_->SetForegroundColour(Theme::ColorError);
        lbl_move_feedback_->SetLabel(wxString::FromUTF8(amt_res.error_message()));
        lbl_move_feedback_->Show();
        movements_card_->Layout();
        txt_move_amount_->SetFocus();
        return;
    }

    const std::string desc = std::string(txt_move_desc_->GetValue().Trim().Trim(false).ToUTF8());
    auto res = services_.shift_service->record_cash_movement(move_kind_, amt_res.value(), desc);
    if (!res.is_success()) {
        last_move_fb_txt_ = res.error_message();
        lbl_move_feedback_->SetForegroundColour(Theme::ColorError);
        const int mv_w = std::max(FromDIP(180), movements_card_->GetClientSize().GetWidth() - FromDIP(36));
        wrap_static_text_deterministic(lbl_move_feedback_, wxString::FromUTF8(last_move_fb_txt_), mv_w);
        lbl_move_feedback_->Show();
        movements_card_->Layout();
        return;
    }

    txt_move_amount_->ChangeValue(wxEmptyString);
    txt_move_desc_->ChangeValue(wxEmptyString);
    last_move_fb_txt_ = movement_kind_plain(res.value().kind) + " registrada: " + res.value().amount.to_string();
    lbl_move_feedback_->SetForegroundColour(Theme::ColorSuccess);
    const int mv_w = std::max(FromDIP(180), movements_card_->GetClientSize().GetWidth() - FromDIP(36));
    wrap_static_text_deterministic(lbl_move_feedback_, wxString::FromUTF8(last_move_fb_txt_), mv_w);
    lbl_move_feedback_->Show();
    refresh_open_preview();
}

void CashShiftPanel::on_close_shift_clicked(wxCommandEvent&) {
    if (keypad_) keypad_->hide_keypad(false);
    wxString str = txt_final_cash_->GetValue().Trim().Trim(false);
    std::string s = std::string(str.ToUTF8());
    if (s.empty()) {
        wxMessageBox(wxT("Debe ingresar el importe contado en el arqueo de cierre."), wxT("Monto Vacío"), wxOK | wxICON_WARNING, this);
        txt_final_cash_->SetFocus();
        return;
    }

    auto parse_res = contracts::Money::try_parse(s, false);
    if (!parse_res.is_success()) {
        wxMessageBox(
            wxString::Format(wxT("Monto de arqueo final inválido: %s"), wxString::FromUTF8(parse_res.error_message().c_str())),
            wxT("Monto Inválido"),
            wxOK | wxICON_WARNING,
            this
        );
        txt_final_cash_->SetFocus();
        return;
    }

    contracts::Money final_cash = parse_res.value();
    int confirm = wxMessageBox(
        wxString::Format(wxT("¿Está seguro de cerrar el turno de caja con efectivo contado de %s?"), wxString::FromUTF8(final_cash.to_string().c_str())),
        wxT("Confirmar Cierre de Turno"),
        wxYES_NO | wxICON_QUESTION,
        this
    );
    if (confirm != wxYES) return;

    const std::string shift_id = has_open_preview_ ? open_preview_.shift_id : "";
    const std::string reason = txt_close_reason_
        ? std::string(txt_close_reason_->GetValue().Trim().Trim(false).ToUTF8())
        : "";

    auto res = services_.shift_service->close_shift_with_count(shift_id, final_cash, reason);
    if (!res.is_success()) {
        last_close_fb_txt_ = res.error_message();
        const int cw = std::max(FromDIP(180), closeout_card_->GetClientSize().GetWidth() - FromDIP(36));
        wrap_static_text_deterministic(lbl_close_feedback_, wxString::FromUTF8(last_close_fb_txt_), cw);
        lbl_close_feedback_->Show();
        closeout_card_->Layout();
        pnl_open_wrap_->Layout();
        wxMessageBox(
            wxString::FromUTF8(res.error_message()),
            wxT("Error de Cierre"),
            wxOK | wxICON_ERROR,
            this
        );
        return;
    }

    last_close_fb_txt_.clear();
    lbl_close_feedback_->Hide();
    last_closed_summary_ = res.value();
    if (txt_final_cash_) txt_final_cash_->ChangeValue(wxEmptyString);
    if (txt_close_reason_) txt_close_reason_->ChangeValue(wxEmptyString);
    update_live_difference();

    wxString closed_msg = wxString::FromUTF8(
        "Turno de caja cerrado exitosamente.\n\n" +
        format_closeout_summary_text(*last_closed_summary_, true)
    );
    if (services_.backup_service) {
        const auto auto_res = services_.backup_service->get_auto_backup_status();
        if (auto_res.is_success() && auto_res.value().is_enabled) {
            closed_msg += wxT("\n\nSe está haciendo un respaldo automático; puedes seguir trabajando.");
        }
    }

    wxMessageBox(closed_msg, wxT("Turno Cerrado"), wxOK | wxICON_INFORMATION, this);

    refresh_status();
    if (on_shift_changed_) {
        on_shift_changed_();
    }
}

void CashShiftPanel::apply_history_preset(int preset_id) {
    if (!txt_hist_from_ || !txt_hist_to_) return;
    const wxDateTime today = wxDateTime::Today();
    wxDateTime from = today;
    wxDateTime to = today;

    if (preset_id == 1) { // Esta semana (lunes a hoy)
        const wxDateTime::WeekDay wd = today.GetWeekDay();
        const int days_since_monday = (wd == wxDateTime::Sun) ? 6 : (static_cast<int>(wd) - 1);
        from = today - wxDateSpan::Days(days_since_monday);
    } else if (preset_id == 2) { // Este mes
        from = wxDateTime(1, today.GetMonth(), today.GetYear());
    }

    txt_hist_from_->ChangeValue(wxString::FromUTF8(iso_date(from)));
    txt_hist_to_->ChangeValue(wxString::FromUTF8(iso_date(to)));
}

void CashShiftPanel::load_history() {
    if (destroying_ || !services_.shift_service || !history_list_) return;
    const std::string from = std::string(txt_hist_from_->GetValue().Trim().Trim(false).ToUTF8());
    const std::string to = std::string(txt_hist_to_->GetValue().Trim().Trim(false).ToUTF8());

    history_items_.clear();
    history_list_->DeleteAllItems();
    pnl_hist_snapshot_->Hide();
    pnl_hist_mismatch_->Hide();
    last_hist_mismatch_txt_.clear();
    last_hist_detail_txt_.clear();
    lbl_hist_detail_->SetLabel(wxT("Selecciona un corte para ver su detalle y movimientos."));
    populate_movements_list(hist_movements_list_, lbl_hist_moves_empty_, {}, wxT("Sin movimientos de efectivo en este corte."));

    auto res = services_.shift_service->list_closed_shifts(from, to, 100);
    if (!res.is_success()) {
        history_list_->Hide();
        lbl_hist_empty_->SetLabel(wxString::FromUTF8(res.error_message()));
        lbl_hist_empty_->SetForegroundColour(Theme::ColorError);
        lbl_hist_empty_->Show();
        pnl_hist_banner_->Hide();
        hist_list_card_->Layout();
        pnl_history_->Layout();
        return;
    }

    const auto& list_dto = res.value();
    history_items_ = list_dto.items;

    if (list_dto.truncated) {
        lbl_hist_banner_->SetForegroundColour(wxColour(180, 83, 9));
        pnl_hist_banner_->SetBackgroundColour(wxColour(254, 243, 199));
        lbl_hist_banner_->SetLabel(wxString::Format(
            wxT("Se muestran los últimos %zu cortes"), history_items_.size()
        ));
        pnl_hist_banner_->Show();
    } else {
        pnl_hist_banner_->Hide();
    }

    if (history_items_.empty()) {
        history_list_->Hide();
        lbl_hist_empty_->SetLabel(wxT("No hay cortes registrados en el rango seleccionado."));
        lbl_hist_empty_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_hist_empty_->Show();
    } else {
        lbl_hist_empty_->Hide();
        history_list_->Show();
        for (std::size_t i = 0; i < history_items_.size(); ++i) {
            const auto& item = history_items_[i];
            const long idx = history_list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(item.closed_at));
            history_list_->SetItem(idx, 1, wxString::FromUTF8(item.opened_by_name));
            history_list_->SetItem(idx, 2, wxString::FromUTF8(item.closed_by_name));
            history_list_->SetItem(idx, 3, wxString::FromUTF8(item.expected_cash.to_string()));
            history_list_->SetItem(idx, 4, wxString::FromUTF8(item.counted_cash.to_string()));
            history_list_->SetItem(idx, 5, wxString::FromUTF8(format_difference_plain(item.difference_cents)));
            history_list_->SetItem(
                idx, 6, wxString::FromUTF8(item.difference_reason.empty() ? "—" : item.difference_reason)
            );
            if (item.difference_cents < 0) {
                history_list_->SetItemTextColour(idx, Theme::ColorError);
            } else if (item.difference_cents > 0) {
                history_list_->SetItemTextColour(idx, wxColour(180, 83, 9));
            } else {
                history_list_->SetItemTextColour(idx, Theme::ColorText);
            }
        }
    }
    hist_filter_card_->Layout();
    hist_list_card_->Layout();
    pnl_history_->Layout();
    if (!history_items_.empty()) {
        fit_list_columns(history_list_, {3, 2, 2, 2, 2, 2, 3});
        history_list_->Select(0, true);
        load_history_detail(history_items_[0].shift_id);
    }
}

void CashShiftPanel::load_history_detail(const std::string& shift_id) {
    if (destroying_ || !services_.shift_service || shift_id.empty()) return;
    auto res = services_.shift_service->get_closeout(shift_id);
    if (!res.is_success()) {
        pnl_hist_snapshot_->Hide();
        pnl_hist_mismatch_->Hide();
        last_hist_mismatch_txt_.clear();
        last_hist_detail_txt_ = res.error_message();
        lbl_hist_detail_->SetForegroundColour(Theme::ColorError);
        populate_movements_list(hist_movements_list_, lbl_hist_moves_empty_, {}, wxT(""));
        layout_history_detail();
        return;
    }

    const auto& dto = res.value();
    lbl_hist_detail_->SetForegroundColour(Theme::ColorText);
    if (!dto.snapshot_saved) {
        pnl_hist_snapshot_->Show();
    } else {
        pnl_hist_snapshot_->Hide();
    }

    if (dto.payments_mismatch_cents != 0) {
        last_hist_mismatch_txt_ = format_mismatch_warning(dto.payments_mismatch_cents);
        pnl_hist_mismatch_->Show();
    } else {
        last_hist_mismatch_txt_.clear();
        pnl_hist_mismatch_->Hide();
    }

    last_hist_detail_txt_ = format_closeout_summary_text(dto, true);
    populate_movements_list(
        hist_movements_list_,
        lbl_hist_moves_empty_,
        dto.movements,
        wxT("Sin movimientos de efectivo en este corte.")
    );
    layout_history_detail();
    fit_list_columns(hist_movements_list_, {1, 1, 2, 4});
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted() || active_tab_ != ActiveTab::History) return;
        layout_history_detail();
        fit_list_columns(hist_movements_list_, {1, 1, 2, 4});
    });
}

void CashShiftPanel::on_hist_preset_today(wxCommandEvent&) {
    apply_history_preset(0);
    load_history();
}

void CashShiftPanel::on_hist_preset_week(wxCommandEvent&) {
    apply_history_preset(1);
    load_history();
}

void CashShiftPanel::on_hist_preset_month(wxCommandEvent&) {
    apply_history_preset(2);
    load_history();
}

void CashShiftPanel::on_hist_search(wxCommandEvent&) {
    load_history();
}

void CashShiftPanel::on_hist_row_selected(wxListEvent& evt) {
    if (destroying_) return;
    const long row = evt.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= history_items_.size()) return;
    load_history_detail(history_items_[static_cast<std::size_t>(row)].shift_id);
}

void CashShiftPanel::on_size(wxSizeEvent& evt) {
    evt.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted() || sizing_) return;
        if (active_tab_ == ActiveTab::Current) {
            layout_open_wrap_texts();
            if (pnl_open_wrap_ && pnl_open_wrap_->IsShown()) {
                fit_list_columns(movements_list_, {1, 1, 2, 4});
            }
        } else if (active_tab_ == ActiveTab::History && pnl_history_ && pnl_history_->IsShown()) {
            layout_history_detail();
            if (hist_list_card_) hist_list_card_->Layout();
            if (hist_detail_card_) hist_detail_card_->Layout();
            fit_list_columns(history_list_, {3, 2, 2, 2, 2, 2, 3});
            fit_list_columns(hist_movements_list_, {1, 1, 2, 4});
        }
        if (keypad_) keypad_->reposition();
    });
}

} // namespace app::frontend
