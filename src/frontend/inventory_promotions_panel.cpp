#include "src/frontend/inventory_promotions_panel.hpp"

#include "src/frontend/theme.hpp"
#include "src/frontend/ui_helpers.hpp"

#include <wx/settings.h>
#include <wx/statline.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <utility>

namespace app::frontend {

namespace {

constexpr int kWideSplitThresholdLogical = 1100;
constexpr const char* kNewCategoryOptionUtf8 = "Nueva categoría...";
constexpr const char* kPromoScopeHintUtf8 = "Toca o haz clic en los productos para marcar o desmarcar:";
constexpr const char* kPromoPriorityHintUtf8 = "Si dos promociones aplican al mismo producto, gana la de mayor prioridad.";

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

void distribute_promo_list_columns(
    wxListCtrl* list,
    const std::vector<ui::ColumnSpec>& specs) {
    // Prioridad de columnas en promo_list_ (T16e):
    //   Compactas: Vigencia (3), Prioridad (4), Estado (5)
    //   Prioritaria: Nombre (0) — en pantalla completa debe verse entero antes que Descuento/Aplica a
    //   Secundarias flexibles: Descuento (1) y Aplica a (2), cediendo hasta hdr_floor si hace falta
    if (!list || specs.size() != 6) return;
    const int client_w = list->GetClientSize().GetWidth();
    if (client_w <= 0) return;

    int budget = client_w;
    if (!ui::has_vertical_scrollbar(list)) {
        const int item_count = list->GetItemCount();
        if (item_count > 0) {
            wxRect r0;
            int row_h = list->FromDIP(Theme::is_touch() ? 42 : 24);
            if (list->GetItemRect(0, r0, wxLIST_RECT_BOUNDS) && r0.GetHeight() > 0) {
                row_h = r0.GetHeight();
            }
            const int avail_h = list->GetClientSize().GetHeight() - ui::header_height(list);
            if (avail_h > 0 && item_count * row_h > avail_h) {
                budget -= wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, list);
            }
        }
    }
    budget = std::max(0, budget - list->FromDIP(2));
    if (budget <= 0) return;

    constexpr std::size_t n = 6;
    std::vector<int> hdr_floor(n, 0);
    std::vector<int> want(n, 0);
    std::vector<int> w(n, 0);

    const int hdr_pad_px = list->FromDIP(10);
    for (std::size_t i = 0; i < n; ++i) {
        hdr_floor[i] = ui::header_text_width(list, specs[i].header) + hdr_pad_px;
        const int pad_px = list->FromDIP(specs[i].content_pad);
        const int extra_px = list->FromDIP(specs[i].extra_px);
        want[i] = std::max(hdr_floor[i], specs[i].content_px + pad_px + extra_px);
        w[i] = want[i];
    }

    // 1. Compactas (3, 4, 5)
    int sum_compact = w[3] + w[4] + w[5];
    const int min_flex_all = hdr_floor[0] + hdr_floor[1] + hdr_floor[2];
    int rem = budget - sum_compact;
    if (rem < min_flex_all) {
        int deficit = min_flex_all - rem;
        for (std::size_t i = 3; i < 6 && deficit > 0; ++i) {
            const int sp = std::max(0, w[i] - hdr_floor[i]);
            const int cut = std::min(sp, deficit);
            w[i] -= cut;
            sum_compact -= cut;
            deficit -= cut;
        }
        rem = budget - sum_compact;
    }

    // 2. Reparto entre Nombre (0), Descuento (1) y Aplica a (2)
    if (rem >= want[0] + want[1] + want[2]) {
        const int extra = rem - want[0] - want[1] - want[2];
        const int extra_name = (extra * 45) / 100;
        const int extra_app = (extra * 35) / 100;
        w[0] = want[0] + extra_name;
        w[2] = want[2] + extra_app;
        w[1] = want[1] + (extra - extra_name - extra_app);
    } else if (rem <= min_flex_all) {
        w[0] = hdr_floor[0];
        w[1] = hdr_floor[1];
        w[2] = std::max(list->FromDIP(40), rem - w[0] - w[1]);
    } else {
        // Prioridad: primero Nombre hasta want[0], reservando al menos hdr_floor[1] + hdr_floor[2]
        const int max_for_name = std::max(hdr_floor[0], rem - hdr_floor[1] - hdr_floor[2]);
        w[0] = std::min(want[0], max_for_name);
        int rem_12 = rem - w[0];
        w[1] = hdr_floor[1];
        w[2] = hdr_floor[2];
        int avail_12 = std::max(0, rem_12 - w[1] - w[2]);
        const int need_1 = std::max(0, want[1] - w[1]);
        const int need_2 = std::max(0, want[2] - w[2]);
        const int total_need = need_1 + need_2;
        if (avail_12 > 0 && total_need > 0) {
            const int add_1 = std::min(need_1, (avail_12 * need_1) / total_need);
            const int add_2 = std::min(need_2, avail_12 - add_1);
            w[1] += add_1;
            w[2] += add_2;
            avail_12 -= (add_1 + add_2);
            if (avail_12 > 0) {
                const int more_1 = std::min(need_1 - add_1, avail_12);
                w[1] += more_1;
                avail_12 -= more_1;
            }
            if (avail_12 > 0) {
                const int more_2 = std::min(need_2 - add_2, avail_12);
                w[2] += more_2;
                avail_12 -= more_2;
            }
            if (avail_12 > 0) {
                w[0] += avail_12;
            }
        } else if (avail_12 > 0) {
            w[0] += avail_12;
        }
    }

    int sum_all = 0;
    for (std::size_t i = 0; i < n; ++i) sum_all += w[i];
    if (sum_all > budget) {
        int excess = sum_all - budget;
        const std::size_t order[6] = {2, 1, 0, 5, 4, 3};
        for (std::size_t idx : order) {
            if (excess <= 0) break;
            const int floor_i = std::max(list->FromDIP(40), hdr_floor[idx]);
            const int can_cut = std::max(0, w[idx] - floor_i);
            const int cut = std::min(can_cut, excess);
            w[idx] -= cut;
            excess -= cut;
        }
        if (excess > 0) {
            w[2] = std::max(list->FromDIP(40), w[2] - excess);
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        list->SetColumnWidth(static_cast<int>(i), w[i]);
    }
}

void distribute_two_flex_columns(
    wxListCtrl* list,
    const std::vector<ui::ColumnSpec>& specs,
    std::size_t flex_a,
    std::size_t flex_b,
    int weight_a_pct,
    bool cap_a_by_pct_when_tight) {
    if (!list || specs.empty() || flex_a >= specs.size() || flex_b >= specs.size() || flex_a == flex_b) {
        return;
    }
    const int client_w = list->GetClientSize().GetWidth();
    if (client_w <= 0) return;

    int budget = client_w;
    if (!ui::has_vertical_scrollbar(list)) {
        const int item_count = list->GetItemCount();
        if (item_count > 0) {
            wxRect r0;
            int row_h = list->FromDIP(Theme::is_touch() ? 42 : 24);
            if (list->GetItemRect(0, r0, wxLIST_RECT_BOUNDS) && r0.GetHeight() > 0) {
                row_h = r0.GetHeight();
            }
            const int avail_h = list->GetClientSize().GetHeight() - ui::header_height(list);
            if (avail_h > 0 && item_count * row_h > avail_h) {
                budget -= wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, list);
            }
        }
    }
    budget = std::max(0, budget - list->FromDIP(2));
    if (budget <= 0) return;

    const std::size_t n = specs.size();
    std::vector<int> hdr_floor(n, 0);
    std::vector<int> want(n, 0);
    std::vector<int> w(n, 0);

    const int hdr_pad_px = list->FromDIP(10);
    for (std::size_t i = 0; i < n; ++i) {
        hdr_floor[i] = ui::header_text_width(list, specs[i].header) + hdr_pad_px;
        const int pad_px = list->FromDIP(specs[i].content_pad);
        const int extra_px = list->FromDIP(specs[i].extra_px);
        want[i] = std::max(hdr_floor[i], specs[i].content_px + pad_px + extra_px);
        w[i] = want[i];
    }

    int sum_compact = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (i != flex_a && i != flex_b) sum_compact += w[i];
    }

    int rem = budget - sum_compact;
    const int min_flex = hdr_floor[flex_a] + hdr_floor[flex_b];
    if (rem < min_flex) {
        int deficit = min_flex - rem;
        int total_spare = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (i != flex_a && i != flex_b) {
                total_spare += std::max(0, w[i] - hdr_floor[i]);
            }
        }
        if (total_spare > 0) {
            for (std::size_t i = 0; i < n && deficit > 0; ++i) {
                if (i == flex_a || i == flex_b) continue;
                const int sp = std::max(0, w[i] - hdr_floor[i]);
                if (sp <= 0) continue;
                const int cut = std::min(sp, std::min(deficit, (deficit * sp + total_spare - 1) / total_spare));
                w[i] -= cut;
                sum_compact -= cut;
                deficit -= cut;
            }
            for (std::size_t i = 0; i < n && deficit > 0; ++i) {
                if (i == flex_a || i == flex_b) continue;
                const int sp = std::max(0, w[i] - hdr_floor[i]);
                const int cut = std::min(sp, deficit);
                w[i] -= cut;
                sum_compact -= cut;
                deficit -= cut;
            }
        }
        rem = budget - sum_compact;
    }

    if (rem >= want[flex_a] + want[flex_b]) {
        const int extra = rem - want[flex_a] - want[flex_b];
        const int extra_a = (extra * weight_a_pct) / 100;
        w[flex_a] = want[flex_a] + extra_a;
        w[flex_b] = want[flex_b] + (extra - extra_a);
    } else if (rem <= min_flex) {
        w[flex_a] = std::max(list->FromDIP(40), hdr_floor[flex_a]);
        w[flex_b] = std::max(list->FromDIP(40), rem - w[flex_a]);
    } else {
        int wa = hdr_floor[flex_a];
        int wb = hdr_floor[flex_b];
        int avail = rem - wa - wb;
        const int need_a = std::max(0, want[flex_a] - wa);
        const int need_b = std::max(0, want[flex_b] - wb);

        if (cap_a_by_pct_when_tight) {
            const int target_a = std::min(want[flex_a], std::max(hdr_floor[flex_a], (rem * weight_a_pct) / 100));
            const int add_a = std::min(avail, std::max(0, target_a - wa));
            wa += add_a;
            avail -= add_a;
            const int add_b = std::min(avail, need_b);
            wb += add_b;
            avail -= add_b;
            if (avail > 0 && wa < want[flex_a]) {
                const int more_a = std::min(avail, want[flex_a] - wa);
                wa += more_a;
                avail -= more_a;
            }
            wb += avail;
        } else {
            const int sum_need = need_a + need_b;
            int add_a = (sum_need > 0)
                ? std::min(need_a, (avail * need_a) / sum_need)
                : (avail * weight_a_pct) / 100;
            int add_b = std::min(need_b, avail - add_a);
            int left = avail - add_a - add_b;
            if (left > 0 && add_a < need_a) {
                const int more = std::min(left, need_a - add_a);
                add_a += more;
                left -= more;
            }
            if (left > 0 && add_b < need_b) {
                const int more = std::min(left, need_b - add_b);
                add_b += more;
                left -= more;
            }
            wa += add_a;
            wb += add_b + left;
        }
        w[flex_a] = wa;
        w[flex_b] = wb;
    }

    int total_w = 0;
    for (int col_w : w) total_w += col_w;
    if (total_w < budget) {
        w[flex_b] += (budget - total_w);
    } else if (total_w > budget) {
        int excess = total_w - budget;
        const std::size_t trim_order[2] = {flex_b, flex_a};
        for (std::size_t idx : trim_order) {
            if (excess <= 0) break;
            const int can_cut = std::max(0, w[idx] - hdr_floor[idx]);
            const int cut = std::min(excess, can_cut);
            w[idx] -= cut;
            excess -= cut;
        }
        for (std::size_t i = 0; i < n && excess > 0; ++i) {
            const int can_cut = std::max(0, w[i] - hdr_floor[i]);
            const int cut = std::min(excess, can_cut);
            w[i] -= cut;
            excess -= cut;
        }
        if (excess > 0) {
            const int min_col = list->FromDIP(28);
            const int cut_b = std::min(excess, std::max(0, w[flex_b] - min_col));
            w[flex_b] -= cut_b;
            excess -= cut_b;
            if (excess > 0) {
                const int cut_a = std::min(excess, std::max(0, w[flex_a] - min_col));
                w[flex_a] -= cut_a;
            }
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (list->GetColumnWidth(static_cast<int>(i)) != w[i]) {
            list->SetColumnWidth(static_cast<int>(i), w[i]);
        }
    }
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

bool parse_strict_int64(const std::string& raw, bool allow_negative, std::int64_t& out_val) {
    const std::string s = trim_ascii(raw);
    if (s.empty()) return false;
    std::size_t i = 0;
    bool neg = false;
    if (s[0] == '-') {
        if (!allow_negative) return false;
        neg = true;
        i = 1;
    } else if (s[0] == '+') {
        i = 1;
    }
    if (i >= s.size()) return false;
    constexpr std::uint64_t kMaxPos = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t limit = neg ? (kMaxPos + 1ULL) : kMaxPos;
    std::uint64_t mag = 0;
    for (; i < s.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch < '0' || ch > '9') return false;
        const std::uint64_t d = static_cast<std::uint64_t>(ch - '0');
        if (mag > (limit - d) / 10ULL) return false;
        mag = mag * 10ULL + d;
    }
    if (neg) {
        out_val = (mag == kMaxPos + 1ULL) ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(mag);
    } else {
        out_val = static_cast<std::int64_t>(mag);
    }
    return true;
}

// Convierte un porcentaje decimal ("12.5" -> 1250) a centesimas de punto usando enteros exactos.
bool parse_percent_to_bp(const std::string& raw, std::int64_t& out_bp, std::string& out_error) {
    const std::string s = trim_ascii(raw);
    if (s.empty()) {
        out_error = "Escribe el porcentaje de descuento.";
        return false;
    }
    if (s[0] == '-') {
        out_error = "El porcentaje de descuento debe estar entre 0.01 % y 100 %.";
        return false;
    }
    const std::size_t dot = s.find('.');
    if (dot != std::string::npos && s.find('.', dot + 1) != std::string::npos) {
        out_error = "El porcentaje tiene formato inválido.";
        return false;
    }
    const std::string whole_s = (dot == std::string::npos) ? s : s.substr(0, dot);
    const std::string frac_s = (dot == std::string::npos) ? "" : s.substr(dot + 1);
    if (whole_s.empty() && frac_s.empty()) {
        out_error = "El porcentaje tiene formato inválido.";
        return false;
    }
    if (frac_s.size() > 2) {
        out_error = "El porcentaje admite hasta dos decimales.";
        return false;
    }
    std::int64_t whole = 0;
    for (unsigned char ch : whole_s) {
        if (ch < '0' || ch > '9') {
            out_error = "El porcentaje tiene formato inválido.";
            return false;
        }
        if (whole > 1000) {
            out_error = "El porcentaje de descuento debe estar entre 0.01 % y 100 %.";
            return false;
        }
        whole = whole * 10 + static_cast<std::int64_t>(ch - '0');
    }
    std::int64_t frac = 0;
    for (unsigned char ch : frac_s) {
        if (ch < '0' || ch > '9') {
            out_error = "El porcentaje tiene formato inválido.";
            return false;
        }
        frac = frac * 10 + static_cast<std::int64_t>(ch - '0');
    }
    if (frac_s.size() == 1) {
        frac *= 10;
    }
    const std::int64_t bp = whole * 100 + frac;
    if (bp < 1 || bp > 10000) {
        out_error = "El porcentaje de descuento debe estar entre 0.01 % y 100 %.";
        return false;
    }
    out_bp = bp;
    return true;
}

std::string format_bp_input(std::int64_t bp) {
    if (bp <= 0) return "";
    const std::int64_t whole = bp / 100;
    const std::int64_t frac = bp % 100;
    if (frac == 0) return std::to_string(whole);
    std::string f = (frac < 10 ? "0" : "") + std::to_string(frac);
    if (f.back() == '0') f.pop_back();
    return std::to_string(whole) + "." + f;
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

std::string format_promo_applies(const contracts::PromotionDto& p) {
    if (p.scope_kind == "CATEGORY") {
        if (!p.categories.empty()) return p.categories.front();
        return "";
    }
    std::string out;
    for (std::size_t i = 0; i < p.product_names.size(); ++i) {
        if (i > 0) out += ", ";
        out += p.product_names[i];
    }
    return out;
}

std::string format_promo_status(const contracts::PromotionDto& p) {
    if (p.is_valid_now) return "Vigente ahora";
    if (p.is_active) return "Activa, fuera de horario";
    return "Desactivada";
}

std::string format_combo_parts(const contracts::ComboDto& c) {
    std::string out;
    for (std::size_t i = 0; i < c.components.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::to_string(c.components[i].quantity) + " x " + c.components[i].product_name;
    }
    return out;
}

void set_selector_items(wxChoice* ch, ui::TouchComboBox* cb, const std::vector<std::string>& labels, int select_idx) {
    if (ch) {
        ch->Clear();
        for (const auto& lbl : labels) ch->Append(wxString::FromUTF8(lbl));
        if (!labels.empty() && select_idx >= 0 && select_idx < static_cast<int>(labels.size())) {
            ch->SetSelection(select_idx);
        }
    }
    if (cb) {
        cb->Clear();
        for (const auto& lbl : labels) cb->Append(wxString::FromUTF8(lbl));
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
    if (cb && idx >= 0 && static_cast<unsigned int>(idx) < cb->GetCount()) cb->SetSelection(idx);
    if (ch && idx >= 0 && static_cast<unsigned int>(idx) < ch->GetCount()) ch->SetSelection(idx);
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

InventoryPromotionsPanel::InventoryPromotionsPanel(
    wxWindow* parent,
    std::shared_ptr<contracts::IPromotionService> promotion_service,
    std::shared_ptr<contracts::ICatalogService> catalog_service,
    std::shared_ptr<contracts::IAccountService> account_service)
    : wxPanel(parent, wxID_ANY),
      promotion_service_(std::move(promotion_service)),
      catalog_service_(std::move(catalog_service)),
      account_service_(std::move(account_service)) {
    SetBackgroundColour(Theme::ColorBackground);
    build_ui();
    start_new_promotion();
    start_new_combo();
    refresh_all();
}

InventoryPromotionsPanel::~InventoryPromotionsPanel() {
    destroying_ = true;
    Unbind(wxEVT_SIZE, &InventoryPromotionsPanel::on_size, this);
    if (promo_list_) {
        promo_list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryPromotionsPanel::on_promo_row_selected, this);
    }
    if (promo_products_list_) {
        promo_products_list_->Unbind(wxEVT_LEFT_DOWN, &InventoryPromotionsPanel::on_promo_product_left_down, this);
        promo_products_list_->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &InventoryPromotionsPanel::on_promo_product_activated, this);
    }
    if (combo_list_) {
        combo_list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryPromotionsPanel::on_combo_row_selected, this);
    }
    if (combo_parts_list_) {
        combo_parts_list_->Unbind(wxEVT_LIST_ITEM_SELECTED, &InventoryPromotionsPanel::on_combo_part_row_selected, this);
    }
    if (keypad_) {
        keypad_->hide_keypad(false);
    }
    DestroyChildren();
}

void InventoryPromotionsPanel::build_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;

    root_sizer_ = new wxBoxSizer(wxVERTICAL);

    // Vista sin permiso inventory.promotions
    pnl_no_permission_ = new CardPanel(this);
    static_cast<CardPanel*>(pnl_no_permission_)->set_card_background(Theme::ColorSurface);
    auto* np_sizer = new wxBoxSizer(wxVERTICAL);
    auto* np_title = new wxStaticText(pnl_no_permission_, wxID_ANY, wxT("Promociones y combos"));
    np_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    np_title->SetForegroundColour(Theme::ColorBrandDeep);
    np_sizer->Add(np_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* np_msg = new wxStaticText(
        pnl_no_permission_, wxID_ANY, wxT("Sin permiso para administrar promociones")
    );
    np_msg->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    np_msg->SetForegroundColour(Theme::ColorError);
    np_sizer->Add(np_msg, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto* np_sub = new wxStaticText(
        pnl_no_permission_,
        wxID_ANY,
        wxT("Para administrar promociones y combos pide el permiso al Dueño.")
    );
    np_sub->SetFont(Theme::get_font_body());
    np_sub->SetForegroundColour(Theme::ColorTextSecondary);
    np_sizer->Add(np_sub, 0, wxALL, FromDIP(20));
    pnl_no_permission_->SetSizer(np_sizer);
    pnl_no_permission_->Hide();
    root_sizer_->Add(pnl_no_permission_, 1, wxEXPAND);

    // Contenido autorizado
    pnl_main_ = new wxPanel(this, wxID_ANY);
    pnl_main_->SetBackgroundColour(Theme::ColorBackground);
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    auto* tab_row = new wxBoxSizer(wxHORIZONTAL);
    btn_tab_promos_ = new wxButton(pnl_main_, wxID_ANY, wxT("Promociones"));
    btn_tab_promos_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_tab_promos_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_tab_promos, this);
    tab_row->Add(btn_tab_promos_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    btn_tab_combos_ = new wxButton(pnl_main_, wxID_ANY, wxT("Combos"));
    btn_tab_combos_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn_tab_combos_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_tab_combos, this);
    tab_row->Add(btn_tab_combos_, 0, wxALIGN_CENTER_VERTICAL);
    main_sizer->Add(tab_row, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    build_promotions_tab(pnl_main_);
    build_combos_tab(pnl_main_);
    main_sizer->Add(pnl_promos_, 1, wxEXPAND);
    main_sizer->Add(pnl_combos_, 1, wxEXPAND);
    pnl_combos_->Hide();

    pnl_main_->SetSizer(main_sizer);
    root_sizer_->Add(pnl_main_, 1, wxEXPAND);
    SetSizer(root_sizer_);

    if (touch) {
        keypad_ = new ui::NumericKeypad(this);
        auto promo_prot = [this]() {
            std::vector<wxRect> rects;
            auto add_r = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_r(promo_detail_card_);
            add_r(btn_promo_save_);
            return rects;
        };
        auto combo_prot = [this]() {
            std::vector<wxRect> rects;
            auto add_r = [&](wxWindow* w) {
                if (!w || !w->IsShownOnScreen()) return;
                rects.emplace_back(ScreenToClient(w->GetScreenPosition()), w->GetSize());
            };
            add_r(combo_detail_card_);
            add_r(btn_combo_save_);
            return rects;
        };
        keypad_->bind_field(txt_promo_percent_, txt_promo_percent_->GetParent(), true, promo_detail_card_, promo_prot);
        keypad_->bind_field(txt_promo_fixed_, txt_promo_fixed_->GetParent(), true, promo_detail_card_, promo_prot);
        keypad_->bind_field(txt_promo_buy_, txt_promo_buy_->GetParent(), false, promo_detail_card_, promo_prot);
        keypad_->bind_field(txt_promo_pay_, txt_promo_pay_->GetParent(), false, promo_detail_card_, promo_prot);
        keypad_->bind_field(txt_promo_priority_, txt_promo_priority_->GetParent(), false, promo_detail_card_, promo_prot);
        keypad_->bind_field(txt_combo_price_, txt_combo_price_->GetParent(), true, combo_detail_card_, combo_prot);
        keypad_->bind_field(txt_combo_part_qty_, txt_combo_part_qty_->GetParent(), false, combo_detail_card_, combo_prot);
    }

    ensure_btn_fits(btn_tab_promos_, 140, btn_h);
    ensure_btn_fits(btn_tab_combos_, 120, btn_h);
    update_tab_ui();
    update_promo_kind_ui();
    update_promo_scope_ui();

    Bind(wxEVT_SIZE, &InventoryPromotionsPanel::on_size, this);
}

void InventoryPromotionsPanel::build_promotions_tab(wxWindow* parent) {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;
    const int field_h = touch ? 48 : 34;

    pnl_promos_ = new wxPanel(parent, wxID_ANY);
    pnl_promos_->SetBackgroundColour(Theme::ColorBackground);
    promo_split_ = new wxBoxSizer(wxHORIZONTAL);

    // --- Lista izquierda ---
    promo_list_card_ = new CardPanel(pnl_promos_);
    promo_list_card_->SetMinSize(FromDIP(wxSize(touch ? 360 : 350, -1)));
    auto* list_card_outer = new wxBoxSizer(wxVERTICAL);
    auto* list_sizer = new wxBoxSizer(wxVERTICAL);

    auto* top_row = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    auto* lbl_title = new wxStaticText(promo_list_card_, wxID_ANY, wxT("Promociones"));
    lbl_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    top_row->Add(lbl_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_promo_new_ = new wxButton(promo_list_card_, wxID_ANY, wxT("Nueva promoción"));
    btn_promo_new_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_promo_new_, btn_h);
    ensure_btn_fits(btn_promo_new_, 130, btn_h);
    btn_promo_new_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_new, this);
    top_row->Add(btn_promo_new_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_promo_refresh_ = new wxButton(promo_list_card_, wxID_ANY, wxT("Actualizar"));
    btn_promo_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_promo_refresh_, btn_h);
    ensure_btn_fits(btn_promo_refresh_, 95, btn_h);
    btn_promo_refresh_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_refresh, this);
    top_row->Add(btn_promo_refresh_, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, FromDIP(6));
    list_sizer->Add(top_row, 0, wxEXPAND);

    auto* filter_row = new wxBoxSizer(wxHORIZONTAL);
    wxWindow* search_box = ui::make_text_field(
        promo_list_card_, txt_promo_search_, field_h, 110, Theme::get_font_body()
    );
    txt_promo_search_->SetHint(wxT("Buscar por nombre..."));
    txt_promo_search_->Bind(wxEVT_TEXT, &InventoryPromotionsPanel::on_promo_search, this);
    filter_row->Add(search_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    btn_promo_only_active_ = new wxButton(promo_list_card_, wxID_ANY, wxT("Solo activas"));
    btn_promo_only_active_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_promo_only_active_, btn_h);
    ensure_btn_fits(btn_promo_only_active_, 115, btn_h);
    btn_promo_only_active_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_only_active, this);
    filter_row->Add(btn_promo_only_active_, 0, wxALIGN_CENTER_VERTICAL);
    list_sizer->Add(filter_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    lbl_promo_empty_ = new wxStaticText(
        promo_list_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_promo_empty_->SetFont(Theme::get_font_body());
    lbl_promo_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_promo_empty_->Hide();
    list_sizer->Add(lbl_promo_empty_, 1, wxEXPAND | wxALL, FromDIP(14));

    promo_list_ = new wxListView(
        promo_list_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    promo_list_->SetFont(Theme::get_font_body());
    promo_list_->SetBackgroundColour(Theme::ColorSurface);
    promo_list_->SetForegroundColour(Theme::ColorText);
    promo_list_->InsertColumn(0, wxT("Nombre"));
    promo_list_->InsertColumn(1, wxT("Descuento"));
    promo_list_->InsertColumn(2, wxT("Aplica a"));
    promo_list_->InsertColumn(3, wxT("Vigencia"));
    promo_list_->InsertColumn(4, wxT("Prioridad"));
    promo_list_->InsertColumn(5, wxT("Estado"));
    if (touch) {
        ui::apply_touch_row_height(promo_list_);
    }
    promo_list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryPromotionsPanel::on_promo_row_selected, this);
    list_sizer->Add(promo_list_, 1, wxEXPAND | wxTOP, FromDIP(8));

    lbl_promo_row_detail_ = new wxStaticText(promo_list_card_, wxID_ANY, wxEmptyString);
    lbl_promo_row_detail_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_promo_row_detail_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_promo_row_detail_->Hide();
    list_sizer->Add(lbl_promo_row_detail_, 0, wxEXPAND | wxTOP, FromDIP(8));

    list_card_outer->Add(list_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    promo_list_card_->SetSizer(list_card_outer);
    promo_split_->Add(promo_list_card_, 11, wxEXPAND | wxRIGHT, FromDIP(8));

    // --- Formulario derecho ---
    promo_detail_card_ = new CardPanel(pnl_promos_);
    promo_detail_card_->SetMinSize(FromDIP(wxSize(touch ? 332 : 335, -1)));
    auto* detail_outer = new wxBoxSizer(wxVERTICAL);

    promo_detail_scroll_ = new wxScrolledWindow(promo_detail_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    promo_detail_scroll_->SetBackgroundColour(Theme::ColorSurface);
    promo_detail_scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    promo_detail_scroll_->SetScrollRate(0, FromDIP(14));
    promo_detail_sizer_ = new wxBoxSizer(wxVERTICAL);

    lbl_promo_detail_title_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Promoción nueva"));
    lbl_promo_detail_title_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_promo_detail_title_->SetForegroundColour(Theme::ColorBrandDeep);
    promo_detail_sizer_->Add(lbl_promo_detail_title_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    promo_name_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_promo_name_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Nombre:"));
    lbl_promo_name_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_promo_name_->SetForegroundColour(Theme::ColorText);
    promo_name_sizer_->Add(lbl_promo_name_, 0, wxEXPAND);
    pname_box_ = ui::make_text_field(promo_detail_scroll_, txt_promo_name_, field_h, 120, Theme::get_font_body());
    txt_promo_name_->SetHint(wxT("Ej. 2x1 Café de la mañana"));
    promo_name_sizer_->Add(pname_box_, 0, wxEXPAND | wxTOP, FromDIP(2));
    promo_detail_sizer_->Add(promo_name_sizer_, 0, wxEXPAND);

    promo_kind_outer_ = new wxBoxSizer(wxVERTICAL);
    lbl_kind_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Tipo de promoción:"));
    lbl_kind_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_kind_->SetForegroundColour(Theme::ColorText);
    promo_kind_outer_->Add(lbl_kind_, 0, wxEXPAND);

    auto* kind_wrap = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    kind_wrap_ = kind_wrap;
    btn_kind_percent_ = new wxButton(promo_detail_scroll_, wxID_ANY, wxT("Porcentaje"));
    btn_kind_percent_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    btn_kind_percent_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_kind_percent, this);
    kind_wrap->Add(btn_kind_percent_, 0, wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_kind_fixed_ = new wxButton(promo_detail_scroll_, wxID_ANY, wxT("Monto menos por pieza"));
    btn_kind_fixed_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    btn_kind_fixed_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_kind_fixed, this);
    kind_wrap->Add(btn_kind_fixed_, 0, wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_kind_nxm_ = new wxButton(promo_detail_scroll_, wxID_ANY, wxT("Lleva N, paga M"));
    btn_kind_nxm_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    btn_kind_nxm_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_kind_nxm, this);
    kind_wrap->Add(btn_kind_nxm_, 0, wxBOTTOM, FromDIP(6));
    promo_kind_outer_->Add(kind_wrap, 0, wxEXPAND | wxTOP, FromDIP(4));
    promo_detail_sizer_->Add(promo_kind_outer_, 0, wxEXPAND | wxTOP, FromDIP(8));

    // Campos dependientes del tipo
    pnl_kind_percent_ = new wxPanel(promo_detail_scroll_, wxID_ANY);
    pnl_kind_percent_->SetBackgroundColour(Theme::ColorSurface);
    kp_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_pct_ = new wxStaticText(pnl_kind_percent_, wxID_ANY, wxT("Porcentaje de descuento (%):"));
    lbl_pct_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_pct_->SetForegroundColour(Theme::ColorText);
    kp_sizer_->Add(lbl_pct_, 0, wxEXPAND);
    pct_box_ = ui::make_text_field(pnl_kind_percent_, txt_promo_percent_, field_h, 110, Theme::get_font_body());
    txt_promo_percent_->SetHint(wxT("Ej. 12.5"));
    kp_sizer_->Add(pct_box_, 0, wxEXPAND | wxTOP, FromDIP(2));
    pnl_kind_percent_->SetSizer(kp_sizer_);
    promo_detail_sizer_->Add(pnl_kind_percent_, 0, wxEXPAND | wxTOP, FromDIP(4));

    pnl_kind_fixed_ = new wxPanel(promo_detail_scroll_, wxID_ANY);
    pnl_kind_fixed_->SetBackgroundColour(Theme::ColorSurface);
    kf_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_fix_ = new wxStaticText(pnl_kind_fixed_, wxID_ANY, wxT("Monto menos por cada pieza ($):"));
    lbl_fix_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_fix_->SetForegroundColour(Theme::ColorText);
    kf_sizer_->Add(lbl_fix_, 0, wxEXPAND);
    fix_box_ = ui::make_text_field(pnl_kind_fixed_, txt_promo_fixed_, field_h, 110, Theme::get_font_body());
    txt_promo_fixed_->SetHint(wxT("Ej. 5.00"));
    kf_sizer_->Add(fix_box_, 0, wxEXPAND | wxTOP, FromDIP(2));
    pnl_kind_fixed_->SetSizer(kf_sizer_);
    promo_detail_sizer_->Add(pnl_kind_fixed_, 0, wxEXPAND | wxTOP, FromDIP(4));

    pnl_kind_nxm_ = new wxPanel(promo_detail_scroll_, wxID_ANY);
    pnl_kind_nxm_->SetBackgroundColour(Theme::ColorSurface);
    auto* kn_s = new wxBoxSizer(wxHORIZONTAL);
    col_buy_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_buy_ = new wxStaticText(pnl_kind_nxm_, wxID_ANY, wxT("Lleva (N):"));
    lbl_buy_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_buy_->SetForegroundColour(Theme::ColorText);
    col_buy_sizer_->Add(lbl_buy_, 0, wxEXPAND);
    buy_box_ = ui::make_text_field(pnl_kind_nxm_, txt_promo_buy_, field_h, 80, Theme::get_font_body());
    txt_promo_buy_->SetHint(wxT("2"));
    col_buy_sizer_->Add(buy_box_, 0, wxEXPAND | wxTOP, FromDIP(2));
    kn_s->Add(col_buy_sizer_, 1, wxEXPAND | wxRIGHT, FromDIP(8));

    col_pay_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_pay_ = new wxStaticText(pnl_kind_nxm_, wxID_ANY, wxT("Paga (M):"));
    lbl_pay_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_pay_->SetForegroundColour(Theme::ColorText);
    col_pay_sizer_->Add(lbl_pay_, 0, wxEXPAND);
    pay_box_ = ui::make_text_field(pnl_kind_nxm_, txt_promo_pay_, field_h, 80, Theme::get_font_body());
    txt_promo_pay_->SetHint(wxT("1"));
    col_pay_sizer_->Add(pay_box_, 0, wxEXPAND | wxTOP, FromDIP(2));
    kn_s->Add(col_pay_sizer_, 1, wxEXPAND);
    pnl_kind_nxm_->SetSizer(kn_s);
    promo_detail_sizer_->Add(pnl_kind_nxm_, 0, wxEXPAND | wxTOP, FromDIP(4));

    // Alcance
    promo_scope_outer_ = new wxBoxSizer(wxVERTICAL);
    lbl_scope_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("A qué aplica:"));
    lbl_scope_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_scope_->SetForegroundColour(Theme::ColorText);
    promo_scope_outer_->Add(lbl_scope_, 0, wxEXPAND);

    scope_row_sizer_ = new wxBoxSizer(wxHORIZONTAL);
    btn_scope_product_ = new wxButton(promo_detail_scroll_, wxID_ANY, wxT("Productos"));
    btn_scope_product_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    btn_scope_product_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_scope_product, this);
    scope_row_sizer_->Add(btn_scope_product_, 1, wxRIGHT, FromDIP(6));

    btn_scope_category_ = new wxButton(promo_detail_scroll_, wxID_ANY, wxT("Una categoría"));
    btn_scope_category_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    btn_scope_category_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_scope_category, this);
    scope_row_sizer_->Add(btn_scope_category_, 1);
    promo_scope_outer_->Add(scope_row_sizer_, 0, wxEXPAND | wxTOP, FromDIP(4));
    promo_detail_sizer_->Add(promo_scope_outer_, 0, wxEXPAND | wxTOP, FromDIP(8));

    pnl_scope_product_ = new wxPanel(promo_detail_scroll_, wxID_ANY);
    pnl_scope_product_->SetBackgroundColour(Theme::ColorSurface);
    auto* sp_s = new wxBoxSizer(wxVERTICAL);
    lbl_sp_hint_ = new wxStaticText(pnl_scope_product_, wxID_ANY, wxString::FromUTF8(kPromoScopeHintUtf8));
    lbl_sp_hint_->SetFont(Theme::get_font_small());
    lbl_sp_hint_->SetForegroundColour(Theme::ColorTextSecondary);
    sp_s->Add(lbl_sp_hint_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    promo_products_list_ = new wxListView(
        pnl_scope_product_, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, touch ? 112 : 108)),
        wxLC_REPORT | wxLC_SINGLE_SEL
    );
    promo_products_list_->SetFont(Theme::get_font_body());
    promo_products_list_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    promo_products_list_->SetForegroundColour(Theme::ColorText);
    promo_products_list_->InsertColumn(0, wxT("Incluido"));
    promo_products_list_->InsertColumn(1, wxT("Producto"));
    promo_products_list_->SetMinSize(FromDIP(wxSize(-1, touch ? 112 : 108)));
    if (touch) {
        ui::apply_touch_row_height(promo_products_list_);
    }
    promo_products_list_->Bind(wxEVT_LEFT_DOWN, &InventoryPromotionsPanel::on_promo_product_left_down, this);
    promo_products_list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &InventoryPromotionsPanel::on_promo_product_activated, this);
    sp_s->Add(promo_products_list_, 0, wxEXPAND);
    pnl_scope_product_->SetSizer(sp_s);
    promo_detail_sizer_->Add(pnl_scope_product_, 0, wxEXPAND | wxTOP, FromDIP(6));

    pnl_scope_category_ = new wxPanel(promo_detail_scroll_, wxID_ANY);
    pnl_scope_category_->SetBackgroundColour(Theme::ColorSurface);
    auto* sc_s = new wxBoxSizer(wxVERTICAL);
    auto* lbl_cat_sel = new wxStaticText(pnl_scope_category_, wxID_ANY, wxT("Categoría:"));
    lbl_cat_sel->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_cat_sel->SetForegroundColour(Theme::ColorText);
    sc_s->Add(lbl_cat_sel, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    if (touch) {
        cb_promo_cat_ = new ui::TouchComboBox(
            pnl_scope_category_, wxID_ANY, wxSize(-1, FromDIP(field_h)), wxArrayString()
        );
        cb_promo_cat_->SetFont(Theme::get_font_body());
        cb_promo_cat_->SetMinSize(wxSize(-1, FromDIP(field_h)));
        sc_s->Add(cb_promo_cat_, 0, wxEXPAND);
    } else {
        ch_promo_cat_ = new wxChoice(pnl_scope_category_, wxID_ANY);
        ch_promo_cat_->SetFont(Theme::get_font_body());
        ch_promo_cat_->SetMinSize(wxSize(-1, FromDIP(field_h)));
        sc_s->Add(ch_promo_cat_, 0, wxEXPAND);
    }
    pnl_scope_category_->SetSizer(sc_s);
    promo_detail_sizer_->Add(pnl_scope_category_, 0, wxEXPAND | wxTOP, FromDIP(6));

    // Prioridad + explicación
    promo_prio_sizer_ = new wxBoxSizer(wxVERTICAL);
    lbl_prio_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Prioridad (número entero):"));
    lbl_prio_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_prio_->SetForegroundColour(Theme::ColorText);
    promo_prio_sizer_->Add(lbl_prio_, 0, wxEXPAND);
    prio_box_ = ui::make_text_field(promo_detail_scroll_, txt_promo_priority_, field_h, 84, Theme::get_font_body());
    txt_promo_priority_->ChangeValue(wxT("0"));
    promo_prio_sizer_->Add(prio_box_, 0, wxEXPAND | wxTOP, FromDIP(2));

    lbl_priority_hint_ = new wxStaticText(
        promo_detail_scroll_,
        wxID_ANY,
        wxString::FromUTF8(kPromoPriorityHintUtf8)
    );
    lbl_priority_hint_->SetFont(Theme::get_font_small());
    lbl_priority_hint_->SetForegroundColour(Theme::ColorTextSecondary);
    promo_prio_sizer_->Add(lbl_priority_hint_, 0, wxEXPAND | wxTOP, FromDIP(2));
    promo_detail_sizer_->Add(promo_prio_sizer_, 0, wxEXPAND | wxTOP, FromDIP(8));

    // Vigencia opcional
    promo_detail_sizer_->Add(new wxStaticLine(promo_detail_scroll_), 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));
    auto* lbl_val = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Vigencia opcional (vacío = siempre)"));
    lbl_val->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_val->SetForegroundColour(Theme::ColorBrandDeep);
    promo_detail_sizer_->Add(lbl_val, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

    vig_grid_ = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
    vig_grid_->AddGrowableCol(0, 1);
    vig_grid_->AddGrowableCol(1, 1);

    auto* col_df = new wxBoxSizer(wxVERTICAL);
    lbl_df_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Desde fecha:"));
    lbl_df_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    col_df->Add(lbl_df_, 0, wxEXPAND);
    wxWindow* df_box = ui::make_text_field(promo_detail_scroll_, txt_promo_valid_from_, field_h, 90, Theme::get_font_body());
    txt_promo_valid_from_->SetHint(wxT("AAAA-MM-DD"));
    col_df->Add(df_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    vig_grid_->Add(col_df, 1, wxEXPAND);

    auto* col_dt = new wxBoxSizer(wxVERTICAL);
    lbl_dt_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Hasta fecha:"));
    lbl_dt_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    col_dt->Add(lbl_dt_, 0, wxEXPAND);
    wxWindow* dt_box = ui::make_text_field(promo_detail_scroll_, txt_promo_valid_to_, field_h, 90, Theme::get_font_body());
    txt_promo_valid_to_->SetHint(wxT("AAAA-MM-DD"));
    col_dt->Add(dt_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    vig_grid_->Add(col_dt, 1, wxEXPAND);

    auto* col_tf = new wxBoxSizer(wxVERTICAL);
    lbl_tf_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Desde hora:"));
    lbl_tf_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    col_tf->Add(lbl_tf_, 0, wxEXPAND);
    wxWindow* tf_box = ui::make_text_field(promo_detail_scroll_, txt_promo_time_from_, field_h, 76, Theme::get_font_body());
    txt_promo_time_from_->SetHint(wxT("HH:MM"));
    col_tf->Add(tf_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    vig_grid_->Add(col_tf, 1, wxEXPAND);

    auto* col_tt = new wxBoxSizer(wxVERTICAL);
    lbl_tt_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Hasta hora:"));
    lbl_tt_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    col_tt->Add(lbl_tt_, 0, wxEXPAND);
    wxWindow* tt_box = ui::make_text_field(promo_detail_scroll_, txt_promo_time_to_, field_h, 76, Theme::get_font_body());
    txt_promo_time_to_->SetHint(wxT("HH:MM"));
    col_tt->Add(tt_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    vig_grid_->Add(col_tt, 1, wxEXPAND);

    promo_detail_sizer_->Add(vig_grid_, 0, wxEXPAND);

    promo_days_outer_ = new wxBoxSizer(wxVERTICAL);
    lbl_days_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxT("Días (ninguna marcada = todos los días):"));
    lbl_days_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_days_->SetToolTip(wxT("Ninguna casilla marcada aplica todos los días"));
    promo_days_outer_->Add(lbl_days_, 0, wxEXPAND);

    static const wxChar* const kDayLabels[7] = {
        wxT("Lun"), wxT("Mar"), wxT("Mié"), wxT("Jue"), wxT("Vie"), wxT("Sáb"), wxT("Dom")
    };
    auto* days_wrap = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    days_wrap_ = days_wrap;
    for (int d = 0; d < 7; ++d) {
        chk_weekdays_[d] = new wxCheckBox(promo_detail_scroll_, wxID_ANY, kDayLabels[d]);
        chk_weekdays_[d]->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
        int tw = 0, th = 0;
        chk_weekdays_[d]->GetTextExtent(kDayLabels[d], &tw, &th);
        chk_weekdays_[d]->SetMinSize(wxSize(
            std::max(FromDIP(touch ? 56 : 46), tw + FromDIP(34)),
            FromDIP(btn_h)
        ));
        days_wrap->Add(chk_weekdays_[d], 0, wxRIGHT | wxBOTTOM, FromDIP(4));
    }
    promo_days_outer_->Add(days_wrap, 0, wxEXPAND | wxTOP, FromDIP(2));
    promo_detail_sizer_->Add(promo_days_outer_, 0, wxEXPAND | wxTOP, FromDIP(6));

    auto* promo_act_row = new wxBoxSizer(wxHORIZONTAL);
    btn_promo_save_ = new wxButton(promo_detail_scroll_, wxID_ANY, wxT("Guardar promoción"));
    btn_promo_save_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_promo_save_, btn_h);
    ensure_btn_fits(btn_promo_save_, 140, btn_h);
    btn_promo_save_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_save, this);
    promo_act_row->Add(btn_promo_save_, 1, wxRIGHT, FromDIP(6));

    btn_promo_toggle_active_ = new wxButton(promo_detail_scroll_, wxID_ANY, wxT("Desactivar"));
    btn_promo_toggle_active_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_promo_toggle_active_, btn_h);
    ensure_btn_fits(btn_promo_toggle_active_, 115, btn_h);
    btn_promo_toggle_active_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_promo_toggle_active, this);
    promo_act_row->Add(btn_promo_toggle_active_, 1);
    promo_detail_sizer_->Add(promo_act_row, 0, wxEXPAND | wxTOP, FromDIP(10));

    lbl_promo_feedback_ = new wxStaticText(promo_detail_scroll_, wxID_ANY, wxEmptyString);
    lbl_promo_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_promo_feedback_->Hide();
    promo_detail_sizer_->Add(lbl_promo_feedback_, 0, wxEXPAND | wxTOP, FromDIP(6));

    promo_detail_scroll_->SetSizer(promo_detail_sizer_);
    detail_outer->Add(promo_detail_scroll_, 1, wxEXPAND | wxALL, FromDIP(16));
    promo_detail_card_->SetSizer(detail_outer);
    promo_split_->Add(promo_detail_card_, 10, wxEXPAND);

    pnl_promos_->SetSizer(promo_split_);
}

void InventoryPromotionsPanel::build_combos_tab(wxWindow* parent) {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 34;
    const int field_h = touch ? 48 : 34;

    pnl_combos_ = new wxPanel(parent, wxID_ANY);
    pnl_combos_->SetBackgroundColour(Theme::ColorBackground);
    combo_split_ = new wxBoxSizer(wxHORIZONTAL);

    // --- Lista izquierda Combos ---
    combo_list_card_ = new CardPanel(pnl_combos_);
    combo_list_card_->SetMinSize(FromDIP(wxSize(touch ? 360 : 350, -1)));
    auto* list_card_outer = new wxBoxSizer(wxVERTICAL);
    auto* list_sizer = new wxBoxSizer(wxVERTICAL);

    auto* top_row = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
    auto* lbl_title = new wxStaticText(combo_list_card_, wxID_ANY, wxT("Combos"));
    lbl_title->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    top_row->Add(lbl_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_combo_new_ = new wxButton(combo_list_card_, wxID_ANY, wxT("Nuevo combo"));
    btn_combo_new_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_combo_new_, btn_h);
    ensure_btn_fits(btn_combo_new_, 120, btn_h);
    btn_combo_new_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_combo_new, this);
    top_row->Add(btn_combo_new_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, FromDIP(6));

    btn_combo_refresh_ = new wxButton(combo_list_card_, wxID_ANY, wxT("Actualizar"));
    btn_combo_refresh_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_combo_refresh_, btn_h);
    ensure_btn_fits(btn_combo_refresh_, 95, btn_h);
    btn_combo_refresh_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_combo_refresh, this);
    top_row->Add(btn_combo_refresh_, 0, wxALIGN_CENTER_VERTICAL | wxBOTTOM, FromDIP(6));
    list_sizer->Add(top_row, 0, wxEXPAND);

    auto* filter_row = new wxBoxSizer(wxHORIZONTAL);
    wxWindow* search_box = ui::make_text_field(
        combo_list_card_, txt_combo_search_, field_h, 110, Theme::get_font_body()
    );
    txt_combo_search_->SetHint(wxT("Buscar combo por nombre o SKU..."));
    txt_combo_search_->Bind(wxEVT_TEXT, &InventoryPromotionsPanel::on_combo_search, this);
    filter_row->Add(search_box, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    btn_combo_inactive_ = new wxButton(combo_list_card_, wxID_ANY, wxT("Ver dados de baja"));
    btn_combo_inactive_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_combo_inactive_, btn_h);
    {
        const int w_on = btn_combo_inactive_->GetTextExtent(wxT("Viendo dados de baja")).GetWidth();
        const int w_off = btn_combo_inactive_->GetTextExtent(wxT("Ver dados de baja")).GetWidth();
        const int w_fit = std::max(btn_combo_inactive_->FromDIP(140), std::max(w_on, w_off) + btn_combo_inactive_->FromDIP(26));
        btn_combo_inactive_->SetMinSize(wxSize(w_fit, btn_combo_inactive_->FromDIP(btn_h)));
        btn_combo_inactive_->SetSize(wxSize(w_fit, btn_combo_inactive_->FromDIP(btn_h)));
    }
    btn_combo_inactive_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_combo_toggle_inactive, this);
    filter_row->Add(btn_combo_inactive_, 0, wxALIGN_CENTER_VERTICAL);
    list_sizer->Add(filter_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    lbl_combo_empty_ = new wxStaticText(
        combo_list_card_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL
    );
    lbl_combo_empty_->SetFont(Theme::get_font_body());
    lbl_combo_empty_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_combo_empty_->Hide();
    list_sizer->Add(lbl_combo_empty_, 1, wxEXPAND | wxALL, FromDIP(14));

    combo_list_ = new wxListView(
        combo_list_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL
    );
    combo_list_->SetFont(Theme::get_font_body());
    combo_list_->SetBackgroundColour(Theme::ColorSurface);
    combo_list_->SetForegroundColour(Theme::ColorText);
    combo_list_->InsertColumn(0, wxT("Nombre"));
    combo_list_->InsertColumn(1, wxT("Precio"));
    combo_list_->InsertColumn(2, wxT("Partes"));
    combo_list_->InsertColumn(3, wxT("Disponibles"));
    combo_list_->InsertColumn(4, wxT("Estado"));
    if (touch) {
        ui::apply_touch_row_height(combo_list_);
    }
    combo_list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryPromotionsPanel::on_combo_row_selected, this);
    list_sizer->Add(combo_list_, 1, wxEXPAND | wxTOP, FromDIP(8));

    list_card_outer->Add(list_sizer, 1, wxEXPAND | wxALL, FromDIP(16));
    combo_list_card_->SetSizer(list_card_outer);
    combo_split_->Add(combo_list_card_, 11, wxEXPAND | wxRIGHT, FromDIP(8));

    // --- Formulario derecho Combos ---
    combo_detail_card_ = new CardPanel(pnl_combos_);
    combo_detail_card_->SetMinSize(FromDIP(wxSize(touch ? 332 : 335, -1)));
    auto* detail_outer = new wxBoxSizer(wxVERTICAL);

    combo_detail_scroll_ = new wxScrolledWindow(combo_detail_card_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    combo_detail_scroll_->SetBackgroundColour(Theme::ColorSurface);
    combo_detail_scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
    combo_detail_scroll_->SetScrollRate(0, FromDIP(14));
    combo_detail_sizer_ = new wxBoxSizer(wxVERTICAL);

    lbl_combo_detail_title_ = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxT("Combo nuevo"));
    lbl_combo_detail_title_->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_combo_detail_title_->SetForegroundColour(Theme::ColorBrandDeep);
    combo_detail_sizer_->Add(lbl_combo_detail_title_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    auto* row_np = new wxBoxSizer(wxHORIZONTAL);
    auto* col_cn = new wxBoxSizer(wxVERTICAL);
    auto* lbl_cn = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxT("Nombre:"));
    lbl_cn->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    col_cn->Add(lbl_cn, 0, wxEXPAND);
    wxWindow* cn_box = ui::make_text_field(combo_detail_scroll_, txt_combo_name_, field_h, 110, Theme::get_font_body());
    col_cn->Add(cn_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    row_np->Add(col_cn, 3, wxEXPAND | wxRIGHT, FromDIP(8));

    auto* col_cp = new wxBoxSizer(wxVERTICAL);
    auto* lbl_cp = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxT("Precio ($):"));
    lbl_cp->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    col_cp->Add(lbl_cp, 0, wxEXPAND);
    wxWindow* cp_box = ui::make_text_field(combo_detail_scroll_, txt_combo_price_, field_h, 80, Theme::get_font_body());
    col_cp->Add(cp_box, 0, wxEXPAND | wxTOP, FromDIP(6));
    row_np->Add(col_cp, 2, wxEXPAND);
    combo_detail_sizer_->Add(row_np, 0, wxEXPAND);

    combo_sku_cat_sizer_ = new wxBoxSizer(wxVERTICAL);

    combo_col_sku_ = new wxBoxSizer(wxVERTICAL);
    lbl_combo_sku_ = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxT("SKU:"));
    lbl_combo_sku_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    combo_col_sku_->Add(lbl_combo_sku_, 0, wxEXPAND);
    wxWindow* sku_box = ui::make_text_field(combo_detail_scroll_, txt_combo_sku_, field_h, 100, Theme::get_font_body());
    txt_combo_sku_->SetHint(wxT("Ej. CMB-01"));
    combo_col_sku_->Add(sku_box, 0, wxEXPAND | wxTOP, FromDIP(6));

    combo_col_cat_ = new wxBoxSizer(wxVERTICAL);
    auto* lbl_ccat = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxT("Categoría:"));
    lbl_ccat->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    combo_col_cat_->Add(lbl_ccat, 0, wxEXPAND);
    if (touch) {
        cb_combo_cat_ = new ui::TouchComboBox(
            combo_detail_scroll_, wxID_ANY, wxSize(-1, FromDIP(field_h)), wxArrayString()
        );
        cb_combo_cat_->SetFont(Theme::get_font_body());
        cb_combo_cat_->SetMinSize(wxSize(FromDIP(120), FromDIP(field_h)));
        cb_combo_cat_->Bind(wxEVT_COMBOBOX, &InventoryPromotionsPanel::on_combo_cat_chosen, this);
        combo_col_cat_->Add(cb_combo_cat_, 0, wxEXPAND | wxTOP, FromDIP(6));
    } else {
        ch_combo_cat_ = new wxChoice(combo_detail_scroll_, wxID_ANY);
        ch_combo_cat_->SetFont(Theme::get_font_body());
        ch_combo_cat_->SetMinSize(wxSize(FromDIP(120), FromDIP(field_h)));
        ch_combo_cat_->Bind(wxEVT_CHOICE, &InventoryPromotionsPanel::on_combo_cat_chosen, this);
        combo_col_cat_->Add(ch_combo_cat_, 0, wxEXPAND | wxTOP, FromDIP(6));
    }

    combo_sku_cat_sizer_->Add(combo_col_sku_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
    combo_sku_cat_sizer_->Add(combo_col_cat_, 0, wxEXPAND);
    combo_detail_sizer_->Add(combo_sku_cat_sizer_, 0, wxEXPAND | wxTOP, FromDIP(6));

    combo_cat_box_ = ui::make_text_field(combo_detail_scroll_, txt_combo_category_, field_h, 100, Theme::get_font_body());
    txt_combo_category_->SetHint(wxT("Escribe el nombre de la nueva categoría..."));
    combo_cat_box_->Hide();
    combo_detail_sizer_->Add(combo_cat_box_, 0, wxEXPAND | wxTOP, FromDIP(6));

    // Partes del combo
    combo_detail_sizer_->Add(new wxStaticLine(combo_detail_scroll_), 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(8));
    auto* lbl_parts = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxT("Partes del combo"));
    lbl_parts->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_parts->SetForegroundColour(Theme::ColorBrandDeep);
    combo_detail_sizer_->Add(lbl_parts, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    combo_parts_list_ = new wxListView(
        combo_detail_scroll_, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, touch ? 125 : 105)),
        wxLC_REPORT | wxLC_SINGLE_SEL
    );
    combo_parts_list_->SetFont(Theme::get_font_body());
    combo_parts_list_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    combo_parts_list_->SetForegroundColour(Theme::ColorText);
    combo_parts_list_->InsertColumn(0, wxT("Producto"));
    combo_parts_list_->InsertColumn(1, wxT("Cantidad"));
    combo_parts_list_->SetMinSize(FromDIP(wxSize(-1, touch ? 125 : 105)));
    if (touch) {
        ui::apply_touch_row_height(combo_parts_list_);
    }
    combo_parts_list_->Bind(wxEVT_LIST_ITEM_SELECTED, &InventoryPromotionsPanel::on_combo_part_row_selected, this);
    combo_detail_sizer_->Add(combo_parts_list_, 0, wxEXPAND | wxBOTTOM, FromDIP(6));

    auto* lbl_add_part = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxT("Producto y cantidad (1 a 99):"));
    lbl_add_part->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    combo_detail_sizer_->Add(lbl_add_part, 0, wxEXPAND);

    auto* part_in_row = new wxBoxSizer(wxHORIZONTAL);
    if (touch) {
        cb_combo_part_prod_ = new ui::TouchComboBox(
            combo_detail_scroll_, wxID_ANY, wxSize(-1, FromDIP(field_h)), wxArrayString()
        );
        cb_combo_part_prod_->SetFont(Theme::get_font_body());
        cb_combo_part_prod_->SetMinSize(wxSize(FromDIP(120), FromDIP(field_h)));
        part_in_row->Add(cb_combo_part_prod_, 2, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    } else {
        ch_combo_part_prod_ = new wxChoice(combo_detail_scroll_, wxID_ANY);
        ch_combo_part_prod_->SetFont(Theme::get_font_body());
        ch_combo_part_prod_->SetMinSize(wxSize(FromDIP(120), FromDIP(field_h)));
        part_in_row->Add(ch_combo_part_prod_, 2, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    }
    wxWindow* pq_box = ui::make_text_field(combo_detail_scroll_, txt_combo_part_qty_, field_h, 64, Theme::get_font_body());
    txt_combo_part_qty_->ChangeValue(wxT("1"));
    part_in_row->Add(pq_box, 1, wxALIGN_CENTER_VERTICAL);
    combo_detail_sizer_->Add(part_in_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    auto* part_btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_combo_part_add_ = new wxButton(combo_detail_scroll_, wxID_ANY, wxT("Agregar parte"));
    btn_combo_part_add_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_combo_part_add_, btn_h);
    ensure_btn_fits(btn_combo_part_add_, 120, btn_h);
    btn_combo_part_add_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_combo_part_add, this);
    part_btn_row->Add(btn_combo_part_add_, 1, wxRIGHT, FromDIP(6));

    btn_combo_part_remove_ = new wxButton(combo_detail_scroll_, wxID_ANY, wxT("Quitar parte"));
    btn_combo_part_remove_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_combo_part_remove_, btn_h);
    ensure_btn_fits(btn_combo_part_remove_, 110, btn_h);
    btn_combo_part_remove_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_combo_part_remove, this);
    part_btn_row->Add(btn_combo_part_remove_, 1);
    combo_detail_sizer_->Add(part_btn_row, 0, wxEXPAND | wxTOP, FromDIP(6));

    auto* combo_act_row = new wxBoxSizer(wxHORIZONTAL);
    btn_combo_save_ = new wxButton(combo_detail_scroll_, wxID_ANY, wxT("Guardar combo"));
    btn_combo_save_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_primary(btn_combo_save_, btn_h);
    ensure_btn_fits(btn_combo_save_, 135, btn_h);
    btn_combo_save_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_combo_save, this);
    combo_act_row->Add(btn_combo_save_, 1, wxRIGHT, FromDIP(6));

    btn_combo_toggle_active_ = new wxButton(combo_detail_scroll_, wxID_ANY, wxT("Dar de baja"));
    btn_combo_toggle_active_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    Theme::style_button_secondary(btn_combo_toggle_active_, btn_h);
    ensure_btn_fits(btn_combo_toggle_active_, 120, btn_h);
    btn_combo_toggle_active_->Bind(wxEVT_BUTTON, &InventoryPromotionsPanel::on_combo_toggle_active, this);
    combo_act_row->Add(btn_combo_toggle_active_, 1);
    combo_detail_sizer_->Add(combo_act_row, 0, wxEXPAND | wxTOP, FromDIP(10));

    lbl_combo_feedback_ = new wxStaticText(combo_detail_scroll_, wxID_ANY, wxEmptyString);
    lbl_combo_feedback_->SetFont(Theme::get_font_small(wxFONTWEIGHT_BOLD));
    lbl_combo_feedback_->Hide();
    combo_detail_sizer_->Add(lbl_combo_feedback_, 0, wxEXPAND | wxTOP, FromDIP(6));

    pnl_combo_costing_ = new wxPanel(combo_detail_scroll_, wxID_ANY);
    pnl_combo_costing_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    auto* cc_s = new wxBoxSizer(wxVERTICAL);
    lbl_combo_cost_ = new wxStaticText(pnl_combo_costing_, wxID_ANY, wxEmptyString);
    lbl_combo_cost_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    cc_s->Add(lbl_combo_cost_, 0, wxEXPAND);
    lbl_combo_margin_ = new wxStaticText(pnl_combo_costing_, wxID_ANY, wxEmptyString);
    lbl_combo_margin_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    cc_s->Add(lbl_combo_margin_, 0, wxEXPAND | wxTOP, FromDIP(4));
    auto* cc_outer = new wxBoxSizer(wxVERTICAL);
    cc_outer->Add(cc_s, 1, wxEXPAND | wxALL, FromDIP(8));
    pnl_combo_costing_->SetSizer(cc_outer);
    pnl_combo_costing_->Hide();
    combo_detail_sizer_->Add(pnl_combo_costing_, 0, wxEXPAND | wxTOP, FromDIP(6));

    combo_detail_scroll_->SetSizer(combo_detail_sizer_);
    detail_outer->Add(combo_detail_scroll_, 1, wxEXPAND | wxALL, FromDIP(16));
    combo_detail_card_->SetSizer(detail_outer);
    combo_split_->Add(combo_detail_card_, 10, wxEXPAND);

    pnl_combos_->SetSizer(combo_split_);
}

void InventoryPromotionsPanel::refresh_all() {
    if (destroying_ || IsBeingDeleted()) return;
    can_manage_promos_ = account_service_ && account_service_->has_permission("inventory.promotions");
    can_manage_products_ = account_service_ && account_service_->has_permission("inventory.manage_products");
    update_permissions_ui();
    if (!can_manage_promos_) {
        Layout();
        return;
    }
    reload_catalog_sources();
    reload_promotions(editing_promo_id_);
    reload_combos(editing_combo_id_);
    apply_responsive_layout();
    update_tab_ui();
    Layout();
    update_promo_detail_scroll();
    update_combo_detail_scroll();
}

void InventoryPromotionsPanel::update_permissions_ui() {
    if (!can_manage_promos_) {
        if (keypad_) keypad_->hide_keypad(false);
        pnl_main_->Hide();
        pnl_no_permission_->Show();
        return;
    }
    pnl_no_permission_->Hide();
    pnl_main_->Show();
}

void InventoryPromotionsPanel::update_tab_ui() {
    const int btn_h = Theme::is_touch() ? 48 : 34;
    const bool promos = (active_tab_ == ActiveTab::Promotions);
    if (promos) {
        Theme::style_button_primary(btn_tab_promos_, btn_h);
        Theme::style_button_secondary(btn_tab_combos_, btn_h);
    } else {
        Theme::style_button_secondary(btn_tab_promos_, btn_h);
        Theme::style_button_primary(btn_tab_combos_, btn_h);
    }
    ensure_btn_fits(btn_tab_promos_, 140, btn_h);
    ensure_btn_fits(btn_tab_combos_, 120, btn_h);
    if (pnl_promos_) pnl_promos_->Show(promos);
    if (pnl_combos_) pnl_combos_->Show(!promos);
    if (pnl_main_) pnl_main_->Layout();
    Layout();
    if (promos) {
        update_promo_list_columns();
        update_promo_products_columns();
        update_promo_detail_scroll();
    } else {
        update_combo_list_columns();
        update_combo_parts_columns();
        update_combo_detail_scroll();
    }
}

void InventoryPromotionsPanel::reload_catalog_sources() {
    non_combo_products_.clear();
    categories_.clear();
    if (!catalog_service_) return;

    auto cats = catalog_service_->list_categories();
    if (cats.is_success()) {
        categories_ = cats.value();
    }

    auto prods = catalog_service_->search_products("", std::nullopt, 0, 500, false);
    if (prods.is_success()) {
        for (const auto& p : prods.value()) {
            if (!p.is_combo && p.is_active) {
                non_combo_products_.push_back(p);
            }
        }
    }

    // Categorías para Promociones
    std::vector<std::string> promo_cats = categories_;
    set_selector_items(ch_promo_cat_, cb_promo_cat_, promo_cats, promo_cats.empty() ? wxNOT_FOUND : 0);

    // Categorías para Combos (existentes + "Nueva categoría...")
    std::string prev_combo_cat;
    const bool was_new_cat = is_combo_new_category_selected();
    {
        const int pidx = get_selector_index(ch_combo_cat_, cb_combo_cat_);
        if (pidx >= 0 && static_cast<std::size_t>(pidx) < categories_.size()) {
            prev_combo_cat = categories_[static_cast<std::size_t>(pidx)];
        }
    }
    std::vector<std::string> combo_cats;
    combo_cats.reserve(categories_.size() + 1);
    for (const auto& c : categories_) combo_cats.push_back(c);
    combo_cats.push_back(kNewCategoryOptionUtf8);
    int combo_cat_idx = 0;
    if (was_new_cat || categories_.empty()) {
        combo_cat_idx = static_cast<int>(categories_.size());
    } else if (!prev_combo_cat.empty()) {
        for (std::size_t i = 0; i < categories_.size(); ++i) {
            if (categories_[i] == prev_combo_cat) {
                combo_cat_idx = static_cast<int>(i);
                break;
            }
        }
    }
    set_selector_items(ch_combo_cat_, cb_combo_cat_, combo_cats, combo_cat_idx);
    sync_combo_category_visibility(false);

    // Productos para partes del combo
    const std::string keep_part = selected_combo_part_product_id();
    std::vector<std::string> prod_labels;
    prod_labels.reserve(non_combo_products_.size());
    int part_idx = non_combo_products_.empty() ? wxNOT_FOUND : 0;
    for (std::size_t i = 0; i < non_combo_products_.size(); ++i) {
        prod_labels.push_back(non_combo_products_[i].name);
        if (!keep_part.empty() && non_combo_products_[i].id == keep_part) {
            part_idx = static_cast<int>(i);
        }
    }
    set_selector_items(ch_combo_part_prod_, cb_combo_part_prod_, prod_labels, part_idx);
    update_promo_products_list_ui();
}

void InventoryPromotionsPanel::reload_promotions(const std::string& keep_id) {
    all_promos_.clear();
    promo_list_failed_ = false;
    promo_list_error_.clear();

    if (!promotion_service_) {
        promo_list_failed_ = true;
        promo_list_error_ = "Servicio de promociones no disponible.";
        apply_promo_list_filter(keep_id);
        return;
    }

    contracts::PromotionFilterDto filter;
    filter.only_active = promo_only_active_;
    auto res = promotion_service_->list_promotions(filter);
    if (!res.is_success()) {
        promo_list_failed_ = true;
        promo_list_error_ = res.error_message();
        apply_promo_list_filter(keep_id);
        return;
    }
    all_promos_ = res.value();
    apply_promo_list_filter(keep_id);
}

void InventoryPromotionsPanel::apply_promo_list_filter(const std::string& keep_id) {
    if (destroying_ || !promo_list_) return;
    const std::string target_id = keep_id.empty() ? editing_promo_id_ : keep_id;

    sync_selection_guard_ = true;
    promo_list_->DeleteAllItems();
    filtered_promos_.clear();

    const wxString query = txt_promo_search_
        ? txt_promo_search_->GetValue().Trim().Trim(false).Lower()
        : wxString();

    long row_to_select = -1;
    if (!promo_list_failed_) {
        for (const auto& p : all_promos_) {
            if (!query.empty() && !wxString::FromUTF8(p.name).Lower().Contains(query)) {
                continue;
            }
            const long idx = promo_list_->InsertItem(static_cast<long>(filtered_promos_.size()), wxString::FromUTF8(p.name));
            promo_list_->SetItem(idx, 1, wxString::FromUTF8(p.summary_text));
            promo_list_->SetItem(idx, 2, wxString::FromUTF8(format_promo_applies(p)));
            promo_list_->SetItem(idx, 3, wxString::FromUTF8(p.validity_text));
            promo_list_->SetItem(idx, 4, wxString::Format(wxT("%d"), p.priority));
            const std::string status = format_promo_status(p);
            promo_list_->SetItem(idx, 5, wxString::FromUTF8(status));

            if (!p.is_active) {
                promo_list_->SetItemTextColour(idx, Theme::ColorError);
                promo_list_->SetItemBackgroundColour(idx, Theme::ColorErrorSoft);
            } else if (p.is_valid_now) {
                promo_list_->SetItemTextColour(idx, Theme::ColorSuccess);
                promo_list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
            } else {
                promo_list_->SetItemTextColour(idx, Theme::ColorText);
                promo_list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
            }

            if (!target_id.empty() && p.id == target_id) {
                row_to_select = idx;
            }
            filtered_promos_.push_back(p);
        }
    }

    if (row_to_select >= 0) {
        promo_list_->SetItemState(
            row_to_select, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED
        );
        const auto& sel_p = filtered_promos_[static_cast<std::size_t>(row_to_select)];
        last_promo_row_detail_ = sel_p.name + " — " + sel_p.summary_text + " · " + format_promo_applies(sel_p) +
                                 " · " + sel_p.validity_text + " · " + format_promo_status(sel_p);
    } else {
        last_promo_row_detail_.clear();
    }
    sync_selection_guard_ = false;

    if (lbl_promo_row_detail_) {
        if (last_promo_row_detail_.empty()) {
            lbl_promo_row_detail_->Hide();
        } else {
            const int w_wrap = std::max(FromDIP(180), promo_list_card_->GetClientSize().GetWidth() - FromDIP(36));
            wrap_static_text_deterministic(lbl_promo_row_detail_, wxString::FromUTF8(last_promo_row_detail_), w_wrap);
            lbl_promo_row_detail_->Show();
        }
    }

    if (promo_list_failed_) {
        promo_list_->Hide();
        lbl_promo_empty_->SetLabel(wxString::FromUTF8(promo_list_error_));
        lbl_promo_empty_->SetForegroundColour(Theme::ColorError);
        lbl_promo_empty_->Show();
    } else if (filtered_promos_.empty()) {
        promo_list_->Hide();
        lbl_promo_empty_->SetLabel(wxT("No hay promociones registradas."));
        lbl_promo_empty_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_promo_empty_->Show();
    } else {
        lbl_promo_empty_->Hide();
        promo_list_->Show();
    }
    if (lbl_promo_empty_->IsShown()) {
        lbl_promo_empty_->Wrap(std::max(FromDIP(160), promo_list_card_->GetClientSize().GetWidth() - FromDIP(28)));
    }

    promo_list_card_->Layout();
    update_promo_list_columns();
}

void InventoryPromotionsPanel::update_promo_list_columns() {
    if (destroying_ || sizing_columns_ || !promo_list_ || !promo_list_->IsShownOnScreen() ||
        promo_list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(promo_list_);
    dc.SetFont(promo_list_->GetFont());

    int max_name = dc.GetTextExtent(wxT("Nombre")).GetWidth();
    int max_desc = dc.GetTextExtent(wxT("Descuento")).GetWidth();
    int max_app = dc.GetTextExtent(wxT("Aplica a")).GetWidth();
    int max_val = dc.GetTextExtent(wxT("Vigencia")).GetWidth();
    int max_prio = dc.GetTextExtent(wxT("Prioridad")).GetWidth();
    int max_st = dc.GetTextExtent(wxT("Estado")).GetWidth();

    for (const auto& p : filtered_promos_) {
        max_name = std::max(max_name, dc.GetTextExtent(wxString::FromUTF8(p.name)).GetWidth());
        max_desc = std::max(max_desc, dc.GetTextExtent(wxString::FromUTF8(p.summary_text)).GetWidth());
        max_app = std::max(max_app, dc.GetTextExtent(wxString::FromUTF8(format_promo_applies(p))).GetWidth());
        max_val = std::max(max_val, dc.GetTextExtent(wxString::FromUTF8(p.validity_text)).GetWidth());
        max_prio = std::max(max_prio, dc.GetTextExtent(wxString::Format(wxT("%d"), p.priority)).GetWidth());
        max_st = std::max(max_st, dc.GetTextExtent(wxString::FromUTF8(format_promo_status(p))).GetWidth());
    }

    const int pad = 14;
    const int touch_col0_extra = Theme::is_touch() ? 7 : 0;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Nombre"), max_name, 0, pad, touch_col0_extra});
    specs.push_back({wxT("Descuento"), max_desc, 0, pad, 0});
    specs.push_back({wxT("Aplica a"), max_app, 0, pad, 0});
    specs.push_back({wxT("Vigencia"), max_val, max_val, pad, 0});
    specs.push_back({wxT("Prioridad"), max_prio, max_prio, pad, 0});
    specs.push_back({wxT("Estado"), max_st, max_st, pad, 0});
    distribute_promo_list_columns(promo_list_, specs);
    sizing_columns_ = false;
}

void InventoryPromotionsPanel::update_promo_products_list_ui() {
    if (!promo_products_list_) return;
    promo_products_list_->DeleteAllItems();
    for (std::size_t i = 0; i < non_combo_products_.size(); ++i) {
        const auto& p = non_combo_products_[i];
        const bool checked = selected_promo_product_ids_.count(p.id) > 0;
        const long idx = promo_products_list_->InsertItem(
            static_cast<long>(i), checked ? wxT("Sí") : wxT("No")
        );
        promo_products_list_->SetItem(idx, 1, wxString::FromUTF8(p.name));
        promo_products_list_->SetItemTextColour(idx, checked ? Theme::ColorBrandDeep : Theme::ColorText);
    }
    update_promo_products_columns();
}

void InventoryPromotionsPanel::update_promo_products_columns() {
    if (destroying_ || sizing_columns_ || !promo_products_list_ || !promo_products_list_->IsShownOnScreen() ||
        promo_products_list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(promo_products_list_);
    dc.SetFont(promo_products_list_->GetFont());
    int max_prod = dc.GetTextExtent(wxT("Producto")).GetWidth();
    for (const auto& p : non_combo_products_) {
        max_prod = std::max(max_prod, dc.GetTextExtent(wxString::FromUTF8(p.name)).GetWidth());
    }
    const int chk_w = dc.GetTextExtent(wxT("Incluido")).GetWidth();
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Incluido"), chk_w, chk_w, 12, 0});
    specs.push_back({wxT("Producto"), max_prod, 0, 14, 0});
    ui::distribute_list_columns(promo_products_list_, specs, 1);
    sizing_columns_ = false;
}

void InventoryPromotionsPanel::update_promo_kind_ui() {
    const int btn_h = Theme::is_touch() ? 48 : 34;
    if (promo_kind_ == PromoKind::Percent) Theme::style_button_primary(btn_kind_percent_, btn_h);
    else Theme::style_button_secondary(btn_kind_percent_, btn_h);
    if (promo_kind_ == PromoKind::Fixed) Theme::style_button_primary(btn_kind_fixed_, btn_h);
    else Theme::style_button_secondary(btn_kind_fixed_, btn_h);
    if (promo_kind_ == PromoKind::Nxm) Theme::style_button_primary(btn_kind_nxm_, btn_h);
    else Theme::style_button_secondary(btn_kind_nxm_, btn_h);

    ensure_btn_fits(btn_kind_percent_, 105, btn_h);
    ensure_btn_fits(btn_kind_fixed_, 180, btn_h);
    ensure_btn_fits(btn_kind_nxm_, 135, btn_h);

    if (pnl_kind_percent_) pnl_kind_percent_->Show(promo_kind_ == PromoKind::Percent);
    if (pnl_kind_fixed_) pnl_kind_fixed_->Show(promo_kind_ == PromoKind::Fixed);
    if (pnl_kind_nxm_) pnl_kind_nxm_->Show(promo_kind_ == PromoKind::Nxm);
    update_promo_detail_scroll();
}

void InventoryPromotionsPanel::update_promo_scope_ui() {
    const int btn_h = Theme::is_touch() ? 48 : 34;
    if (promo_scope_ == PromoScope::Product) {
        Theme::style_button_primary(btn_scope_product_, btn_h);
        Theme::style_button_secondary(btn_scope_category_, btn_h);
    } else {
        Theme::style_button_secondary(btn_scope_product_, btn_h);
        Theme::style_button_primary(btn_scope_category_, btn_h);
    }
    ensure_btn_fits(btn_scope_product_, 105, btn_h);
    ensure_btn_fits(btn_scope_category_, 130, btn_h);

    if (pnl_scope_product_) pnl_scope_product_->Show(promo_scope_ == PromoScope::Product);
    if (pnl_scope_category_) pnl_scope_category_->Show(promo_scope_ == PromoScope::Category);
    update_promo_detail_scroll();
    if (promo_scope_ == PromoScope::Product) {
        update_promo_products_columns();
    }
}

void InventoryPromotionsPanel::apply_responsive_layout() {
    if (destroying_) return;
    const int panel_w = GetClientSize().GetWidth();
    if (panel_w <= 0) return;

    const bool want_wide = panel_w >= FromDIP(kWideSplitThresholdLogical);
    if (want_wide != wide_split_active_) {
        wide_split_active_ = want_wide;
        if (promo_split_ && promo_list_card_ && promo_detail_card_) {
            if (auto* it_l = promo_split_->GetItem(promo_list_card_)) it_l->SetProportion(want_wide ? 58 : 11);
            if (auto* it_d = promo_split_->GetItem(promo_detail_card_)) it_d->SetProportion(want_wide ? 42 : 10);
            if (pnl_promos_) pnl_promos_->Layout();
        }
        if (combo_split_ && combo_list_card_ && combo_detail_card_) {
            if (auto* it_l = combo_split_->GetItem(combo_list_card_)) it_l->SetProportion(want_wide ? 55 : 11);
            if (auto* it_d = combo_split_->GetItem(combo_detail_card_)) it_d->SetProportion(want_wide ? 45 : 10);
            if (pnl_combos_) pnl_combos_->Layout();
        }
        Layout();
    }
}

bool InventoryPromotionsPanel::is_combo_new_category_selected() const {
    const int idx = get_selector_index(ch_combo_cat_, cb_combo_cat_);
    return idx >= static_cast<int>(categories_.size());
}

void InventoryPromotionsPanel::sync_combo_category_visibility(bool focus_new_input) {
    if (!combo_cat_box_) return;
    const bool show_new = is_combo_new_category_selected();
    if (combo_cat_box_->IsShown() != show_new) {
        combo_cat_box_->Show(show_new);
    }
    if (!show_new && txt_combo_category_) {
        txt_combo_category_->ChangeValue(wxEmptyString);
    }
    if (show_new && focus_new_input && txt_combo_category_) {
        txt_combo_category_->SetFocus();
    }
}

std::string InventoryPromotionsPanel::resolve_combo_category(std::string& out_error) const {
    out_error.clear();
    if (is_combo_new_category_selected()) {
        const std::string typed = txt_combo_category_
            ? trim_ascii(std::string(txt_combo_category_->GetValue().ToUTF8().data()))
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
    const int idx = get_selector_index(ch_combo_cat_, cb_combo_cat_);
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

void InventoryPromotionsPanel::update_promo_detail_scroll() {
    if (!promo_detail_scroll_ || !promo_detail_sizer_) return;
    const bool touch = Theme::is_touch();

    auto run_pass = [&](int inner_w) {
        // Regla B: Nombre inline (umbral calculado con controles reales; referencia ~392 lógicos)
        if (promo_name_sizer_ && lbl_promo_name_ && pname_box_) {
            const int name_min_w = lbl_promo_name_->GetBestSize().GetWidth() + FromDIP(8) + FromDIP(320);
            const bool want_inline = inner_w >= name_min_w;
            if (want_inline != promo_name_inline_) {
                promo_name_inline_ = want_inline;
                promo_name_sizer_->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* it_l = promo_name_sizer_->GetItem(lbl_promo_name_)) {
                    it_l->SetProportion(0);
                    it_l->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    it_l->SetBorder(want_inline ? FromDIP(8) : 0);
                }
                if (auto* it_b = promo_name_sizer_->GetItem(pname_box_)) {
                    it_b->SetProportion(want_inline ? 1 : 0);
                    it_b->SetFlag(want_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                    it_b->SetBorder(want_inline ? 0 : FromDIP(2));
                }
            }
        }

        // Regla B2: Tipo inline con los 3 botones cuando caben en una sola fila (umbral calculado)
        if (promo_kind_outer_ && lbl_kind_ && kind_wrap_ && btn_kind_percent_ && btn_kind_fixed_ && btn_kind_nxm_) {
            wxClientDC kdc(lbl_kind_);
            kdc.SetFont(lbl_kind_->GetFont());
            const int short_kind_w = kdc.GetTextExtent(wxT("Tipo:")).GetWidth();
            const int btns_w = btn_kind_percent_->GetBestSize().GetWidth() + FromDIP(6) +
                               btn_kind_fixed_->GetBestSize().GetWidth() + FromDIP(6) +
                               btn_kind_nxm_->GetBestSize().GetWidth();
            const int kind_min_w = short_kind_w + FromDIP(8) + btns_w + FromDIP(4);
            const bool want_inline = inner_w >= kind_min_w;
            if (want_inline != promo_kind_inline_) {
                promo_kind_inline_ = want_inline;
                lbl_kind_->SetLabel(want_inline ? wxT("Tipo:") : wxT("Tipo de promoción:"));
                promo_kind_outer_->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* il = promo_kind_outer_->GetItem(lbl_kind_)) {
                    il->SetProportion(0);
                    il->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    il->SetBorder(want_inline ? FromDIP(8) : 0);
                }
                if (auto* iw = promo_kind_outer_->GetItem(kind_wrap_)) {
                    iw->SetProportion(want_inline ? 1 : 0);
                    iw->SetFlag(want_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                    iw->SetBorder(want_inline ? 0 : FromDIP(4));
                }
            }
        }

        // Regla C: Campos de valor inline (umbral calculado; referencia ~410 lógicos)
        if (kp_sizer_ && kf_sizer_ && col_buy_sizer_ && col_pay_sizer_ &&
            lbl_pct_ && lbl_fix_ && lbl_buy_ && lbl_pay_) {
            const int pct_min = lbl_pct_->GetBestSize().GetWidth() + FromDIP(8) + FromDIP(140);
            const int fix_min = lbl_fix_->GetBestSize().GetWidth() + FromDIP(8) + FromDIP(140);
            const int nxm_min = lbl_buy_->GetBestSize().GetWidth() + FromDIP(6) + FromDIP(90) +
                                FromDIP(12) + lbl_pay_->GetBestSize().GetWidth() + FromDIP(6) + FromDIP(90);
            const int val_min_w = std::max({pct_min, fix_min, nxm_min});
            const bool want_inline = inner_w >= val_min_w;
            if (want_inline != promo_val_inline_) {
                promo_val_inline_ = want_inline;
                auto set_pair = [&](wxBoxSizer* sz, wxStaticText* lbl, wxWindow* box, int gap_dp) {
                    sz->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                    if (auto* il = sz->GetItem(lbl)) {
                        il->SetProportion(0);
                        il->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                        il->SetBorder(want_inline ? FromDIP(gap_dp) : 0);
                    }
                    if (auto* ib = sz->GetItem(box)) {
                        ib->SetProportion(want_inline ? 1 : 0);
                        ib->SetFlag(want_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                        ib->SetBorder(want_inline ? 0 : FromDIP(2));
                    }
                };
                set_pair(kp_sizer_, lbl_pct_, pct_box_, 8);
                set_pair(kf_sizer_, lbl_fix_, fix_box_, 8);
                set_pair(col_buy_sizer_, lbl_buy_, buy_box_, 6);
                set_pair(col_pay_sizer_, lbl_pay_, pay_box_, 6);
                if (pnl_kind_percent_) pnl_kind_percent_->Layout();
                if (pnl_kind_fixed_) pnl_kind_fixed_->Layout();
                if (pnl_kind_nxm_) pnl_kind_nxm_->Layout();
            }
        }

        // Regla D: "A qué aplica:" inline (umbral calculado; referencia ~460 lógicos Táctil, ~390 Estándar)
        if (promo_scope_outer_ && lbl_scope_ && scope_row_sizer_ && btn_scope_product_ && btn_scope_category_) {
            const int scope_min_w = lbl_scope_->GetBestSize().GetWidth() + FromDIP(8) +
                                    btn_scope_product_->GetBestSize().GetWidth() + FromDIP(6) +
                                    btn_scope_category_->GetBestSize().GetWidth() + FromDIP(8);
            const bool want_inline = inner_w >= scope_min_w;
            if (want_inline != promo_scope_inline_) {
                promo_scope_inline_ = want_inline;
                promo_scope_outer_->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* il = promo_scope_outer_->GetItem(lbl_scope_)) {
                    il->SetProportion(0);
                    il->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    il->SetBorder(want_inline ? FromDIP(8) : 0);
                }
                if (auto* ir = promo_scope_outer_->GetItem(scope_row_sizer_)) {
                    ir->SetProportion(want_inline ? 1 : 0);
                    ir->SetFlag(want_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                    ir->SetBorder(want_inline ? 0 : FromDIP(4));
                }
            }
        }

        // Regla E: Prioridad + campo + ayuda inline (umbral calculado; referencia ~430 lógicos)
        int prio_hint_wrap_w = std::max(FromDIP(140), inner_w - FromDIP(12));
        if (promo_prio_sizer_ && lbl_prio_ && prio_box_ && lbl_priority_hint_) {
            wxClientDC pdc(lbl_prio_);
            pdc.SetFont(lbl_prio_->GetFont());
            const int short_lbl_w = pdc.GetTextExtent(wxT("Prioridad:")).GetWidth();
            const int box_min_w = std::max(FromDIP(84), prio_box_->GetMinSize().GetWidth());
            const int prio_min_w = short_lbl_w + FromDIP(8) + box_min_w + FromDIP(10) + FromDIP(220);
            const bool want_inline = inner_w >= prio_min_w;
            if (want_inline != promo_prio_inline_) {
                promo_prio_inline_ = want_inline;
                lbl_prio_->SetLabel(want_inline ? wxT("Prioridad:") : wxT("Prioridad (número entero):"));
                promo_prio_sizer_->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* il = promo_prio_sizer_->GetItem(lbl_prio_)) {
                    il->SetProportion(0);
                    il->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    il->SetBorder(want_inline ? FromDIP(8) : 0);
                }
                if (auto* ib = promo_prio_sizer_->GetItem(prio_box_)) {
                    ib->SetProportion(0);
                    ib->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : (wxEXPAND | wxTOP));
                    ib->SetBorder(want_inline ? FromDIP(10) : FromDIP(2));
                }
                if (auto* ih = promo_prio_sizer_->GetItem(lbl_priority_hint_)) {
                    ih->SetProportion(want_inline ? 1 : 0);
                    ih->SetFlag(want_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                    ih->SetBorder(want_inline ? 0 : FromDIP(2));
                }
            }
            if (promo_prio_inline_) {
                const int lbl_actual_w = std::max(short_lbl_w, lbl_prio_->GetBestSize().GetWidth());
                prio_hint_wrap_w = std::max(
                    FromDIP(120),
                    inner_w - lbl_actual_w - FromDIP(8) - box_min_w - FromDIP(10) - FromDIP(12)
                );
            }
        }

        // Regla F: Vigencia 4 columnas vs 2x2 (umbral calculado; referencia ~440 lógicos)
        if (vig_grid_ && lbl_df_ && lbl_dt_ && lbl_tf_ && lbl_tt_) {
            const int w_df = std::max(lbl_df_->GetBestSize().GetWidth() + FromDIP(4), FromDIP(112));
            const int w_dt = std::max(lbl_dt_->GetBestSize().GetWidth() + FromDIP(4), FromDIP(112));
            const int w_tf = std::max(lbl_tf_->GetBestSize().GetWidth() + FromDIP(4), FromDIP(86));
            const int w_tt = std::max(lbl_tt_->GetBestSize().GetWidth() + FromDIP(4), FromDIP(86));
            const int vig_4col_min_w = w_df + w_dt + w_tf + w_tt + 3 * FromDIP(8) + FromDIP(8);
            const bool want_4col = inner_w >= vig_4col_min_w;
            if (want_4col != promo_vig_4col_) {
                promo_vig_4col_ = want_4col;
                if (want_4col) {
                    vig_grid_->SetRows(0);
                    vig_grid_->SetCols(4);
                    if (!vig_grid_->IsColGrowable(2)) vig_grid_->AddGrowableCol(2, 1);
                    if (!vig_grid_->IsColGrowable(3)) vig_grid_->AddGrowableCol(3, 1);
                } else {
                    if (vig_grid_->IsColGrowable(3)) vig_grid_->RemoveGrowableCol(3);
                    if (vig_grid_->IsColGrowable(2)) vig_grid_->RemoveGrowableCol(2);
                    vig_grid_->SetRows(0);
                    vig_grid_->SetCols(2);
                }
            }
        }

        // Regla F2: Días inline con las 7 casillas en una fila cuando caben con su texto completo y alto >= 48 log
        if (promo_days_outer_ && lbl_days_ && days_wrap_) {
            wxClientDC ddc(lbl_days_);
            ddc.SetFont(lbl_days_->GetFont());
            const int short_days_w = ddc.GetTextExtent(wxT("Días:")).GetWidth();
            int chks_w = 0;
            for (int d = 0; d < 7; ++d) {
                if (chk_weekdays_[d]) {
                    chks_w += std::max(
                        chk_weekdays_[d]->GetMinSize().GetWidth(),
                        chk_weekdays_[d]->GetBestSize().GetWidth()
                    );
                    if (d < 6) chks_w += FromDIP(4);
                }
            }
            const int days_min_w = short_days_w + FromDIP(8) + chks_w + FromDIP(4);
            const bool want_inline = inner_w >= days_min_w;
            if (want_inline != promo_days_inline_) {
                promo_days_inline_ = want_inline;
                lbl_days_->SetLabel(want_inline ? wxT("Días:") : wxT("Días (ninguna marcada = todos los días):"));
                promo_days_outer_->SetOrientation(want_inline ? wxHORIZONTAL : wxVERTICAL);
                if (auto* il = promo_days_outer_->GetItem(lbl_days_)) {
                    il->SetProportion(0);
                    il->SetFlag(want_inline ? (wxALIGN_CENTER_VERTICAL | wxRIGHT) : wxEXPAND);
                    il->SetBorder(want_inline ? FromDIP(8) : 0);
                }
                if (auto* iw = promo_days_outer_->GetItem(days_wrap_)) {
                    iw->SetProportion(want_inline ? 1 : 0);
                    iw->SetFlag(want_inline ? wxALIGN_CENTER_VERTICAL : (wxEXPAND | wxTOP));
                    iw->SetBorder(want_inline ? 0 : FromDIP(2));
                }
            }
        }

        const int wrap_w = std::max(FromDIP(140), inner_w - FromDIP(12));
        if (lbl_sp_hint_ && pnl_scope_product_ && pnl_scope_product_->IsShown()) {
            wrap_static_text_deterministic(lbl_sp_hint_, wxString::FromUTF8(kPromoScopeHintUtf8), wrap_w);
            pnl_scope_product_->Layout();
        }
        if (lbl_priority_hint_) {
            wrap_static_text_deterministic(lbl_priority_hint_, wxString::FromUTF8(kPromoPriorityHintUtf8), prio_hint_wrap_w);
        }
        if (lbl_promo_feedback_) {
            const bool has_fb = !last_promo_feedback_.empty();
            lbl_promo_feedback_->Show(has_fb);
            if (has_fb) {
                wrap_static_text_deterministic(lbl_promo_feedback_, wxString::FromUTF8(last_promo_feedback_), wrap_w);
            }
        }

        if (promo_products_list_ && pnl_scope_product_ && pnl_scope_product_->IsShown()) {
            const int base_h = FromDIP(touch ? (last_promo_feedback_.empty() ? 116 : 90) : 108);
            promo_products_list_->SetMinSize(wxSize(-1, base_h));
            pnl_scope_product_->Layout();
            promo_detail_sizer_->Layout();
            const int avail_h = promo_detail_scroll_->GetClientSize().GetHeight();
            const int min_h = promo_detail_sizer_->GetMinSize().GetHeight();
            if (avail_h > min_h + FromDIP(8)) {
                const int extra = avail_h - min_h - FromDIP(4);
                promo_products_list_->SetMinSize(wxSize(-1, base_h + extra));
                pnl_scope_product_->Layout();
            }
        }

        promo_detail_sizer_->Layout();
        promo_detail_scroll_->FitInside();
        promo_detail_scroll_->Layout();
        if (promo_detail_card_) promo_detail_card_->Layout();
    };

    const int w1 = std::max(FromDIP(180), promo_detail_scroll_->GetClientSize().GetWidth());
    run_pass(w1);
    const int w2 = std::max(FromDIP(180), promo_detail_scroll_->GetClientSize().GetWidth());
    if (w2 != w1) {
        run_pass(w2);
    }
    if (promo_scope_ == PromoScope::Product) {
        update_promo_products_columns();
    }
}

void InventoryPromotionsPanel::start_new_promotion() {
    if (keypad_) keypad_->hide_keypad(false);
    editing_promo_id_.clear();
    editing_promo_active_ = true;
    selected_promo_product_ids_.clear();

    if (promo_list_) {
        sync_selection_guard_ = true;
        const long sel = promo_list_->GetFirstSelected();
        if (sel >= 0) promo_list_->Select(sel, false);
        sync_selection_guard_ = false;
    }

    lbl_promo_detail_title_->SetLabel(wxT("Promoción nueva"));
    txt_promo_name_->ChangeValue(wxEmptyString);
    txt_promo_percent_->ChangeValue(wxEmptyString);
    txt_promo_fixed_->ChangeValue(wxEmptyString);
    txt_promo_buy_->ChangeValue(wxT("2"));
    txt_promo_pay_->ChangeValue(wxT("1"));
    txt_promo_priority_->ChangeValue(wxT("0"));
    txt_promo_valid_from_->ChangeValue(wxEmptyString);
    txt_promo_valid_to_->ChangeValue(wxEmptyString);
    txt_promo_time_from_->ChangeValue(wxEmptyString);
    txt_promo_time_to_->ChangeValue(wxEmptyString);
    for (int d = 0; d < 7; ++d) {
        if (chk_weekdays_[d]) chk_weekdays_[d]->SetValue(false);
    }
    promo_kind_ = PromoKind::Percent;
    promo_scope_ = PromoScope::Product;
    update_promo_products_list_ui();
    btn_promo_toggle_active_->Hide();
    set_promo_feedback("", false);
    update_promo_kind_ui();
    update_promo_scope_ui();
}

void InventoryPromotionsPanel::load_promotion_into_form(const std::string& promo_id) {
    if (!promotion_service_) return;
    auto res = promotion_service_->get_promotion(promo_id);
    if (!res.is_success()) {
        set_promo_feedback(res.error_message(), true);
        return;
    }
    const auto& p = res.value();
    editing_promo_id_ = p.id;
    editing_promo_active_ = p.is_active;

    lbl_promo_detail_title_->SetLabel(wxT("Detalle de promoción"));
    txt_promo_name_->ChangeValue(wxString::FromUTF8(p.name));
    txt_promo_percent_->ChangeValue(wxString::FromUTF8(format_bp_input(p.percent_basis_points)));
    txt_promo_fixed_->ChangeValue(wxString::FromUTF8(format_pesos_plain(p.fixed_off)));
    txt_promo_buy_->ChangeValue(p.buy_qty > 0 ? wxString::Format(wxT("%lld"), p.buy_qty) : wxString(wxT("2")));
    txt_promo_pay_->ChangeValue(p.pay_qty > 0 ? wxString::Format(wxT("%lld"), p.pay_qty) : wxString(wxT("1")));
    txt_promo_priority_->ChangeValue(wxString::Format(wxT("%d"), p.priority));
    txt_promo_valid_from_->ChangeValue(wxString::FromUTF8(p.valid_from));
    txt_promo_valid_to_->ChangeValue(wxString::FromUTF8(p.valid_to));
    txt_promo_time_from_->ChangeValue(wxString::FromUTF8(p.time_from));
    txt_promo_time_to_->ChangeValue(wxString::FromUTF8(p.time_to));

    for (int d = 0; d < 7; ++d) {
        const bool on = std::find(p.weekdays.begin(), p.weekdays.end(), d + 1) != p.weekdays.end();
        if (chk_weekdays_[d]) chk_weekdays_[d]->SetValue(on);
    }

    if (p.kind == "PERCENT") promo_kind_ = PromoKind::Percent;
    else if (p.kind == "FIXED") promo_kind_ = PromoKind::Fixed;
    else promo_kind_ = PromoKind::Nxm;

    selected_promo_product_ids_.clear();
    for (const auto& pid : p.product_ids) selected_promo_product_ids_.insert(pid);
    update_promo_products_list_ui();

    if (p.scope_kind == "CATEGORY") {
        promo_scope_ = PromoScope::Category;
        if (!p.categories.empty()) {
            for (std::size_t i = 0; i < categories_.size(); ++i) {
                if (categories_[i] == p.categories.front()) {
                    set_selector_index(ch_promo_cat_, cb_promo_cat_, static_cast<int>(i));
                    break;
                }
            }
        }
    } else {
        promo_scope_ = PromoScope::Product;
    }

    btn_promo_toggle_active_->SetLabel(p.is_active ? wxT("Desactivar") : wxT("Activar"));
    btn_promo_toggle_active_->Show();
    ensure_btn_fits(btn_promo_toggle_active_, 115, Theme::is_touch() ? 48 : 34);
    set_promo_feedback("", false);
    update_promo_kind_ui();
    update_promo_scope_ui();
}

void InventoryPromotionsPanel::set_promo_feedback(const std::string& msg, bool is_error) {
    last_promo_feedback_ = msg;
    last_promo_feedback_err_ = is_error;
    if (!lbl_promo_feedback_) return;
    lbl_promo_feedback_->SetForegroundColour(is_error ? Theme::ColorError : Theme::ColorSuccess);
    lbl_promo_feedback_->SetLabel(wxString::FromUTF8(msg));
    update_promo_detail_scroll();
}

void InventoryPromotionsPanel::reload_combos(const std::string& keep_id) {
    all_combos_.clear();
    combo_list_failed_ = false;
    combo_list_error_.clear();

    if (!promotion_service_) {
        combo_list_failed_ = true;
        combo_list_error_ = "Servicio de promociones y combos no disponible.";
        apply_combo_list_filter(keep_id);
        return;
    }

    auto res = promotion_service_->list_combos(combo_include_inactive_);
    if (!res.is_success()) {
        combo_list_failed_ = true;
        combo_list_error_ = res.error_message();
        apply_combo_list_filter(keep_id);
        return;
    }
    all_combos_ = res.value();
    apply_combo_list_filter(keep_id);
}

void InventoryPromotionsPanel::apply_combo_list_filter(const std::string& keep_id) {
    if (destroying_ || !combo_list_) return;
    const std::string target_id = keep_id.empty() ? editing_combo_id_ : keep_id;

    sync_selection_guard_ = true;
    combo_list_->DeleteAllItems();
    filtered_combos_.clear();

    const wxString query = txt_combo_search_
        ? txt_combo_search_->GetValue().Trim().Trim(false).Lower()
        : wxString();

    long row_to_select = -1;
    if (!combo_list_failed_) {
        for (const auto& c : all_combos_) {
            if (!query.empty()) {
                const bool m_name = wxString::FromUTF8(c.name).Lower().Contains(query);
                const bool m_sku = wxString::FromUTF8(c.sku).Lower().Contains(query);
                if (!m_name && !m_sku) continue;
            }
            const long idx = combo_list_->InsertItem(static_cast<long>(filtered_combos_.size()), wxString::FromUTF8(c.name));
            combo_list_->SetItem(idx, 1, wxString::FromUTF8(c.price.to_string()));
            combo_list_->SetItem(idx, 2, wxString::FromUTF8(format_combo_parts(c)));
            combo_list_->SetItem(idx, 3, wxString::Format(wxT("%lld"), c.stock_units));
            combo_list_->SetItem(idx, 4, wxString::FromUTF8(c.is_active ? "Activo" : "Dado de baja"));

            if (!c.is_active) {
                combo_list_->SetItemTextColour(idx, Theme::ColorError);
                combo_list_->SetItemBackgroundColour(idx, Theme::ColorErrorSoft);
            } else {
                combo_list_->SetItemTextColour(idx, Theme::ColorText);
                combo_list_->SetItemBackgroundColour(idx, Theme::ColorSurface);
            }
            if (!target_id.empty() && c.product_id == target_id) {
                row_to_select = idx;
            }
            filtered_combos_.push_back(c);
        }
    }

    if (row_to_select >= 0) {
        combo_list_->SetItemState(
            row_to_select, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED
        );
    }
    sync_selection_guard_ = false;

    if (combo_list_failed_) {
        combo_list_->Hide();
        lbl_combo_empty_->SetLabel(wxString::FromUTF8(combo_list_error_));
        lbl_combo_empty_->SetForegroundColour(Theme::ColorError);
        lbl_combo_empty_->Show();
    } else if (filtered_combos_.empty()) {
        combo_list_->Hide();
        lbl_combo_empty_->SetLabel(wxT("No hay combos registrados."));
        lbl_combo_empty_->SetForegroundColour(Theme::ColorTextSecondary);
        lbl_combo_empty_->Show();
    } else {
        lbl_combo_empty_->Hide();
        combo_list_->Show();
    }
    if (lbl_combo_empty_->IsShown()) {
        lbl_combo_empty_->Wrap(std::max(FromDIP(160), combo_list_card_->GetClientSize().GetWidth() - FromDIP(28)));
    }

    combo_list_card_->Layout();
    update_combo_list_columns();
}

void InventoryPromotionsPanel::update_combo_list_columns() {
    if (destroying_ || sizing_columns_ || !combo_list_ || !combo_list_->IsShownOnScreen() ||
        combo_list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(combo_list_);
    dc.SetFont(combo_list_->GetFont());

    int max_name = dc.GetTextExtent(wxT("Nombre")).GetWidth();
    int max_price = dc.GetTextExtent(wxT("Precio")).GetWidth();
    int max_parts = dc.GetTextExtent(wxT("Partes")).GetWidth();
    int max_disp = dc.GetTextExtent(wxT("Disponibles")).GetWidth();
    int max_st = dc.GetTextExtent(wxT("Estado")).GetWidth();

    for (const auto& c : filtered_combos_) {
        max_name = std::max(max_name, dc.GetTextExtent(wxString::FromUTF8(c.name)).GetWidth());
        max_price = std::max(max_price, dc.GetTextExtent(wxString::FromUTF8(c.price.to_string())).GetWidth());
        max_parts = std::max(max_parts, dc.GetTextExtent(wxString::FromUTF8(format_combo_parts(c))).GetWidth());
        max_disp = std::max(max_disp, dc.GetTextExtent(wxString::Format(wxT("%lld"), c.stock_units)).GetWidth());
        max_st = std::max(max_st, dc.GetTextExtent(c.is_active ? wxT("Activo") : wxT("Dado de baja")).GetWidth());
    }

    const int pad = 14;
    const int touch_col0_extra = Theme::is_touch() ? 7 : 0;
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Nombre"), max_name, 0, pad, touch_col0_extra});
    specs.push_back({wxT("Precio"), max_price, max_price, pad, 0});
    specs.push_back({wxT("Partes"), max_parts, 0, pad, 0});
    specs.push_back({wxT("Disponibles"), max_disp, max_disp, pad, 0});
    specs.push_back({wxT("Estado"), max_st, max_st, pad, 0});
    distribute_two_flex_columns(combo_list_, specs, 0, 2, 48, true);
    sizing_columns_ = false;
}

void InventoryPromotionsPanel::rebuild_combo_parts_ui(const std::string& keep_prod_id) {
    if (!combo_parts_list_) return;
    combo_parts_list_->DeleteAllItems();
    long sel = -1;
    for (std::size_t i = 0; i < combo_parts_.size(); ++i) {
        const auto& part = combo_parts_[i];
        const long idx = combo_parts_list_->InsertItem(static_cast<long>(i), wxString::FromUTF8(part.product_name));
        combo_parts_list_->SetItem(idx, 1, wxString::Format(wxT("%lld"), part.quantity));
        if (!keep_prod_id.empty() && part.product_id == keep_prod_id) {
            sel = idx;
        }
    }
    if (sel >= 0) {
        combo_parts_list_->SetItemState(
            sel, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED
        );
    }
    update_combo_parts_columns();
}

void InventoryPromotionsPanel::update_combo_parts_columns() {
    if (destroying_ || sizing_columns_ || !combo_parts_list_ || !combo_parts_list_->IsShownOnScreen() ||
        combo_parts_list_->GetClientSize().GetWidth() <= 0) {
        return;
    }
    sizing_columns_ = true;
    wxClientDC dc(combo_parts_list_);
    dc.SetFont(combo_parts_list_->GetFont());
    int max_prod = dc.GetTextExtent(wxT("Producto")).GetWidth();
    int max_qty = dc.GetTextExtent(wxT("Cantidad")).GetWidth();
    for (const auto& part : combo_parts_) {
        max_prod = std::max(max_prod, dc.GetTextExtent(wxString::FromUTF8(part.product_name)).GetWidth());
        max_qty = std::max(max_qty, dc.GetTextExtent(wxString::Format(wxT("%lld"), part.quantity)).GetWidth());
    }
    std::vector<ui::ColumnSpec> specs;
    specs.push_back({wxT("Producto"), max_prod, 0, 14, 0});
    specs.push_back({wxT("Cantidad"), max_qty, max_qty, 14, 0});
    ui::distribute_list_columns(combo_parts_list_, specs, 0);
    sizing_columns_ = false;
}

void InventoryPromotionsPanel::reload_combo_costing() {
    if (!pnl_combo_costing_) return;
    if (editing_combo_id_.empty() || !can_manage_products_ || !promotion_service_) {
        pnl_combo_costing_->Hide();
        update_combo_detail_scroll();
        return;
    }

    auto res = promotion_service_->get_combo_costing(editing_combo_id_);
    if (!res.is_success()) {
        last_combo_margin_text_.clear();
        if (res.error_code() == contracts::ErrorCode::Forbidden) {
            last_combo_cost_text_.clear();
            pnl_combo_costing_->Hide();
        } else {
            pnl_combo_costing_->Show();
            last_combo_cost_text_ = res.error_message();
            lbl_combo_cost_->SetLabel(wxString::FromUTF8(last_combo_cost_text_));
            lbl_combo_cost_->SetForegroundColour(Theme::ColorError);
            lbl_combo_margin_->Hide();
        }
        update_combo_detail_scroll();
        return;
    }

    const auto& c = res.value();
    pnl_combo_costing_->Show();
    if (!c.all_costs_known) {
        std::string names;
        for (std::size_t i = 0; i < c.unknown_component_names.size(); ++i) {
            if (i > 0) names += ", ";
            names += c.unknown_component_names[i];
        }
        last_combo_cost_text_ = names.empty()
            ? "Falta conocer el costo de los productos del combo."
            : "Falta conocer el costo de: " + names;
        last_combo_margin_text_.clear();
        lbl_combo_cost_->SetLabel(wxString::FromUTF8(last_combo_cost_text_));
        lbl_combo_cost_->SetForegroundColour(Theme::ColorError);
        lbl_combo_margin_->Hide();
    } else {
        last_combo_cost_text_ = "Costo estimado: " + c.cost_total.to_string();
        lbl_combo_cost_->SetLabel(wxString::FromUTF8(last_combo_cost_text_));
        lbl_combo_cost_->SetForegroundColour(Theme::ColorText);
        if (c.margin_known) {
            std::string margin_line = "Margen: " + c.margin.to_string();
            if (c.margin_percent_known) {
                margin_line += " (" + format_margin_percent(c.margin_basis_points) + ")";
            }
            last_combo_margin_text_ = margin_line;
            lbl_combo_margin_->SetLabel(wxString::FromUTF8(last_combo_margin_text_));
            lbl_combo_margin_->SetForegroundColour(c.margin.cents() < 0 ? Theme::ColorError : Theme::ColorSuccess);
            lbl_combo_margin_->Show();
        } else {
            last_combo_margin_text_.clear();
            lbl_combo_margin_->Hide();
        }
    }
    update_combo_detail_scroll();
}

void InventoryPromotionsPanel::update_combo_detail_scroll() {
    if (!combo_detail_scroll_ || !combo_detail_sizer_) return;

    auto run_pass = [&](int inner_w) {
        if (combo_sku_cat_sizer_ && combo_col_sku_ && combo_col_cat_ && lbl_combo_sku_) {
            const int sku_min = std::max(lbl_combo_sku_->GetBestSize().GetWidth() + FromDIP(16), FromDIP(120));
            const int sku_cat_min_w = sku_min + FromDIP(8) + FromDIP(170);
            const bool want_horiz = inner_w >= sku_cat_min_w;
            if (want_horiz != combo_sku_cat_horizontal_) {
                combo_sku_cat_horizontal_ = want_horiz;
                combo_sku_cat_sizer_->SetOrientation(want_horiz ? wxHORIZONTAL : wxVERTICAL);
                if (auto* it_s = combo_sku_cat_sizer_->GetItem(combo_col_sku_)) {
                    it_s->SetProportion(want_horiz ? 35 : 0);
                    it_s->SetFlag(want_horiz ? (wxEXPAND | wxRIGHT) : (wxEXPAND | wxBOTTOM));
                    it_s->SetBorder(FromDIP(want_horiz ? 8 : 6));
                }
                if (auto* it_c = combo_sku_cat_sizer_->GetItem(combo_col_cat_)) {
                    it_c->SetProportion(want_horiz ? 65 : 0);
                    it_c->SetFlag(wxEXPAND);
                    it_c->SetBorder(0);
                }
            }
        }

        const int wrap_w = std::max(FromDIP(140), inner_w - FromDIP(12));
        if (lbl_combo_feedback_) {
            const bool has_fb = !last_combo_feedback_.empty();
            lbl_combo_feedback_->Show(has_fb);
            if (has_fb) {
                wrap_static_text_deterministic(lbl_combo_feedback_, wxString::FromUTF8(last_combo_feedback_), wrap_w);
            }
        }
        if (pnl_combo_costing_ && pnl_combo_costing_->IsShown()) {
            const int cw = std::max(FromDIP(140), inner_w - FromDIP(22));
            if (lbl_combo_cost_ && !last_combo_cost_text_.empty()) {
                wrap_static_text_deterministic(lbl_combo_cost_, wxString::FromUTF8(last_combo_cost_text_), cw);
            }
            if (lbl_combo_margin_ && lbl_combo_margin_->IsShown() && !last_combo_margin_text_.empty()) {
                wrap_static_text_deterministic(lbl_combo_margin_, wxString::FromUTF8(last_combo_margin_text_), cw);
            }
            pnl_combo_costing_->Layout();
        }
        combo_detail_sizer_->Layout();
        combo_detail_scroll_->FitInside();
        combo_detail_scroll_->Layout();
        if (combo_detail_card_) combo_detail_card_->Layout();
    };

    const int w1 = std::max(FromDIP(180), combo_detail_scroll_->GetClientSize().GetWidth());
    run_pass(w1);
    const int w2 = std::max(FromDIP(180), combo_detail_scroll_->GetClientSize().GetWidth());
    if (w2 != w1) {
        run_pass(w2);
    }
    update_combo_parts_columns();
}

void InventoryPromotionsPanel::start_new_combo() {
    if (keypad_) keypad_->hide_keypad(false);
    editing_combo_id_.clear();
    editing_combo_active_ = true;
    combo_parts_.clear();

    if (combo_list_) {
        sync_selection_guard_ = true;
        const long sel = combo_list_->GetFirstSelected();
        if (sel >= 0) combo_list_->Select(sel, false);
        sync_selection_guard_ = false;
    }

    lbl_combo_detail_title_->SetLabel(wxT("Combo nuevo"));
    txt_combo_name_->ChangeValue(wxEmptyString);
    txt_combo_price_->ChangeValue(wxEmptyString);
    txt_combo_sku_->ChangeValue(wxEmptyString);
    ui::set_text_field_enabled(txt_combo_sku_, true);
    txt_combo_category_->ChangeValue(wxEmptyString);
    set_selector_index(ch_combo_cat_, cb_combo_cat_, 0);
    sync_combo_category_visibility(false);
    txt_combo_part_qty_->ChangeValue(wxT("1"));
    rebuild_combo_parts_ui();
    btn_combo_toggle_active_->Hide();
    if (pnl_combo_costing_) pnl_combo_costing_->Hide();
    set_combo_feedback("", false);
    update_combo_detail_scroll();
}

void InventoryPromotionsPanel::load_combo_into_form(const std::string& combo_id) {
    if (!promotion_service_) return;
    auto res = promotion_service_->get_combo(combo_id);
    if (!res.is_success()) {
        set_combo_feedback(res.error_message(), true);
        return;
    }
    const auto& c = res.value();
    editing_combo_id_ = c.product_id;
    editing_combo_active_ = c.is_active;

    lbl_combo_detail_title_->SetLabel(wxT("Detalle del combo"));
    txt_combo_name_->ChangeValue(wxString::FromUTF8(c.name));
    txt_combo_price_->ChangeValue(wxString::FromUTF8(format_pesos_plain(c.price)));
    txt_combo_sku_->ChangeValue(wxString::FromUTF8(c.sku));
    ui::set_text_field_enabled(txt_combo_sku_, false);
    int cat_idx = static_cast<int>(categories_.size());
    for (std::size_t i = 0; i < categories_.size(); ++i) {
        if (categories_[i] == c.category) {
            cat_idx = static_cast<int>(i);
            break;
        }
    }
    set_selector_index(ch_combo_cat_, cb_combo_cat_, cat_idx);
    if (cat_idx >= static_cast<int>(categories_.size())) {
        txt_combo_category_->ChangeValue(wxString::FromUTF8(c.category));
    } else {
        txt_combo_category_->ChangeValue(wxEmptyString);
    }
    sync_combo_category_visibility(false);

    combo_parts_.clear();
    for (const auto& comp : c.components) {
        combo_parts_.push_back({comp.product_id, comp.product_name, comp.quantity});
    }
    rebuild_combo_parts_ui();
    btn_combo_toggle_active_->SetLabel(c.is_active ? wxT("Dar de baja") : wxT("Reactivar"));
    btn_combo_toggle_active_->Show();
    ensure_btn_fits(btn_combo_toggle_active_, 120, Theme::is_touch() ? 48 : 34);
    set_combo_feedback("", false);
    reload_combo_costing();
}

void InventoryPromotionsPanel::set_combo_feedback(const std::string& msg, bool is_error) {
    last_combo_feedback_ = msg;
    last_combo_feedback_err_ = is_error;
    if (!lbl_combo_feedback_) return;
    lbl_combo_feedback_->SetForegroundColour(is_error ? Theme::ColorError : Theme::ColorSuccess);
    lbl_combo_feedback_->SetLabel(wxString::FromUTF8(msg));
    update_combo_detail_scroll();
}

void InventoryPromotionsPanel::on_tab_promos(wxCommandEvent&) {
    if (destroying_) return;
    if (keypad_) keypad_->hide_keypad(false);
    active_tab_ = ActiveTab::Promotions;
    update_tab_ui();
}

void InventoryPromotionsPanel::on_tab_combos(wxCommandEvent&) {
    if (destroying_) return;
    if (keypad_) keypad_->hide_keypad(false);
    active_tab_ = ActiveTab::Combos;
    update_tab_ui();
}

void InventoryPromotionsPanel::on_promo_new(wxCommandEvent&) {
    if (destroying_) return;
    start_new_promotion();
}

void InventoryPromotionsPanel::on_promo_refresh(wxCommandEvent&) {
    if (destroying_) return;
    reload_catalog_sources();
    reload_promotions(editing_promo_id_);
}

void InventoryPromotionsPanel::on_promo_search(wxCommandEvent&) {
    if (destroying_) return;
    apply_promo_list_filter(editing_promo_id_);
}

void InventoryPromotionsPanel::on_promo_only_active(wxCommandEvent&) {
    if (destroying_) return;
    promo_only_active_ = !promo_only_active_;
    const int btn_h = Theme::is_touch() ? 48 : 34;
    if (promo_only_active_) Theme::style_button_primary(btn_promo_only_active_, btn_h);
    else Theme::style_button_secondary(btn_promo_only_active_, btn_h);
    ensure_btn_fits(btn_promo_only_active_, 115, btn_h);
    promo_list_card_->Layout();
    reload_promotions(editing_promo_id_);
}

void InventoryPromotionsPanel::on_promo_row_selected(wxListEvent& event) {
    if (destroying_ || sync_selection_guard_) return;
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= filtered_promos_.size()) return;
    const auto& p = filtered_promos_[static_cast<std::size_t>(row)];
    last_promo_row_detail_ = p.name + " — " + p.summary_text + " · " + format_promo_applies(p) +
                             " · " + p.validity_text + " · " + format_promo_status(p);
    if (lbl_promo_row_detail_) {
        const int w_wrap = std::max(FromDIP(180), promo_list_card_->GetClientSize().GetWidth() - FromDIP(36));
        wrap_static_text_deterministic(lbl_promo_row_detail_, wxString::FromUTF8(last_promo_row_detail_), w_wrap);
        lbl_promo_row_detail_->Show();
        promo_list_card_->Layout();
    }
    load_promotion_into_form(p.id);
}

void InventoryPromotionsPanel::on_promo_kind_percent(wxCommandEvent&) {
    if (destroying_) return;
    promo_kind_ = PromoKind::Percent;
    update_promo_kind_ui();
}

void InventoryPromotionsPanel::on_promo_kind_fixed(wxCommandEvent&) {
    if (destroying_) return;
    promo_kind_ = PromoKind::Fixed;
    update_promo_kind_ui();
}

void InventoryPromotionsPanel::on_promo_kind_nxm(wxCommandEvent&) {
    if (destroying_) return;
    promo_kind_ = PromoKind::Nxm;
    update_promo_kind_ui();
}

void InventoryPromotionsPanel::on_promo_scope_product(wxCommandEvent&) {
    if (destroying_) return;
    promo_scope_ = PromoScope::Product;
    update_promo_scope_ui();
}

void InventoryPromotionsPanel::on_promo_scope_category(wxCommandEvent&) {
    if (destroying_) return;
    promo_scope_ = PromoScope::Category;
    update_promo_scope_ui();
}

void InventoryPromotionsPanel::on_promo_product_left_down(wxMouseEvent& event) {
    event.Skip();
    if (destroying_ || !promo_products_list_) return;
    int flags = 0;
    const long row = promo_products_list_->HitTest(event.GetPosition(), flags);
    if (row < 0 || static_cast<std::size_t>(row) >= non_combo_products_.size()) return;
    const std::string& pid = non_combo_products_[static_cast<std::size_t>(row)].id;
    const bool now_checked = selected_promo_product_ids_.count(pid) == 0;
    if (now_checked) selected_promo_product_ids_.insert(pid);
    else selected_promo_product_ids_.erase(pid);
    promo_products_list_->SetItem(row, 0, now_checked ? wxT("Sí") : wxT("No"));
    promo_products_list_->SetItemTextColour(row, now_checked ? Theme::ColorBrandDeep : Theme::ColorText);
}

void InventoryPromotionsPanel::on_promo_product_activated(wxListEvent& event) {
    if (destroying_ || !promo_products_list_) return;
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= non_combo_products_.size()) return;
    const std::string& pid = non_combo_products_[static_cast<std::size_t>(row)].id;
    const bool now_checked = selected_promo_product_ids_.count(pid) == 0;
    if (now_checked) selected_promo_product_ids_.insert(pid);
    else selected_promo_product_ids_.erase(pid);
    promo_products_list_->SetItem(row, 0, now_checked ? wxT("Sí") : wxT("No"));
    promo_products_list_->SetItemTextColour(row, now_checked ? Theme::ColorBrandDeep : Theme::ColorText);
}

void InventoryPromotionsPanel::on_promo_save(wxCommandEvent&) {
    if (destroying_ || !promotion_service_) return;
    if (keypad_) keypad_->hide_keypad(false);

    contracts::CreatePromotionDto data;
    data.name = trim_ascii(std::string(txt_promo_name_->GetValue().ToUTF8().data()));

    if (promo_kind_ == PromoKind::Percent) {
        data.kind = "PERCENT";
        std::string err;
        if (!parse_percent_to_bp(std::string(txt_promo_percent_->GetValue().ToUTF8().data()), data.percent_basis_points, err)) {
            set_promo_feedback(err, true);
            return;
        }
    } else if (promo_kind_ == PromoKind::Fixed) {
        data.kind = "FIXED";
        const std::string raw_fix = trim_ascii(std::string(txt_promo_fixed_->GetValue().ToUTF8().data()));
        if (raw_fix.empty()) {
            set_promo_feedback("Escribe el monto de descuento por pieza.", true);
            return;
        }
        auto money_res = contracts::Money::try_parse(raw_fix, false);
        if (!money_res.is_success()) {
            set_promo_feedback(money_res.error_message(), true);
            return;
        }
        data.fixed_off = money_res.value();
    } else {
        data.kind = "NXM";
        if (!parse_strict_int64(std::string(txt_promo_buy_->GetValue().ToUTF8().data()), false, data.buy_qty) ||
            !parse_strict_int64(std::string(txt_promo_pay_->GetValue().ToUTF8().data()), false, data.pay_qty)) {
            set_promo_feedback("N y M deben ser números enteros positivos.", true);
            return;
        }
    }

    std::int64_t prio = 0;
    const std::string raw_prio = trim_ascii(std::string(txt_promo_priority_->GetValue().ToUTF8().data()));
    if (!raw_prio.empty()) {
        if (!parse_strict_int64(raw_prio, true, prio) ||
            prio < std::numeric_limits<int>::min() || prio > std::numeric_limits<int>::max()) {
            set_promo_feedback("La prioridad debe ser un número entero válido.", true);
            return;
        }
    }
    data.priority = static_cast<int>(prio);
    data.is_active = editing_promo_id_.empty() ? true : editing_promo_active_;

    if (promo_scope_ == PromoScope::Category) {
        data.scope_kind = "CATEGORY";
        data.category = selected_promo_category();
    } else {
        data.scope_kind = "PRODUCT";
        for (const auto& p : non_combo_products_) {
            if (selected_promo_product_ids_.count(p.id) > 0) {
                data.product_ids.push_back(p.id);
            }
        }
    }

    data.valid_from = trim_ascii(std::string(txt_promo_valid_from_->GetValue().ToUTF8().data()));
    data.valid_to = trim_ascii(std::string(txt_promo_valid_to_->GetValue().ToUTF8().data()));
    data.time_from = trim_ascii(std::string(txt_promo_time_from_->GetValue().ToUTF8().data()));
    data.time_to = trim_ascii(std::string(txt_promo_time_to_->GetValue().ToUTF8().data()));
    for (int d = 0; d < 7; ++d) {
        if (chk_weekdays_[d] && chk_weekdays_[d]->GetValue()) {
            data.weekdays.push_back(d + 1);
        }
    }

    if (editing_promo_id_.empty()) {
        auto res = promotion_service_->create_promotion(data);
        if (!res.is_success()) {
            set_promo_feedback(res.error_message(), true);
            return;
        }
        const std::string new_id = res.value().id;
        reload_promotions(new_id);
        load_promotion_into_form(new_id);
        set_promo_feedback("Promoción guardada.", false);
    } else {
        contracts::UpdatePromotionDto upd;
        upd.promotion_id = editing_promo_id_;
        upd.data = std::move(data);
        auto res = promotion_service_->update_promotion(upd);
        if (!res.is_success()) {
            set_promo_feedback(res.error_message(), true);
            return;
        }
        const std::string keep = editing_promo_id_;
        reload_promotions(keep);
        load_promotion_into_form(keep);
        set_promo_feedback("Cambios guardados.", false);
    }
}

void InventoryPromotionsPanel::on_promo_toggle_active(wxCommandEvent&) {
    if (destroying_ || !promotion_service_ || editing_promo_id_.empty()) return;
    const std::string name = trim_ascii(std::string(txt_promo_name_->GetValue().ToUTF8().data()));
    const bool next_active = !editing_promo_active_;
    const wxString q = next_active
        ? wxString::Format(wxT("¿Seguro que deseas activar la promoción \"%s\"?"), wxString::FromUTF8(name))
        : wxString::Format(wxT("¿Seguro que deseas desactivar la promoción \"%s\"?"), wxString::FromUTF8(name));
    wxMessageDialog dlg(
        this, q, wxT("Confirmar estado de promoción"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION
    );
    if (dlg.ShowModal() != wxID_YES) return;

    auto res = promotion_service_->set_promotion_active(editing_promo_id_, next_active);
    if (!res.is_success()) {
        set_promo_feedback(res.error_message(), true);
        return;
    }
    const std::string keep = editing_promo_id_;
    reload_promotions(keep);
    if (!next_active && promo_only_active_) {
        start_new_promotion();
        set_promo_feedback("Promoción desactivada.", false);
    } else {
        load_promotion_into_form(keep);
        set_promo_feedback(next_active ? "Promoción activada." : "Promoción desactivada.", false);
    }
}

void InventoryPromotionsPanel::on_combo_new(wxCommandEvent&) {
    if (destroying_) return;
    start_new_combo();
}

void InventoryPromotionsPanel::on_combo_refresh(wxCommandEvent&) {
    if (destroying_) return;
    reload_catalog_sources();
    reload_combos(editing_combo_id_);
}

void InventoryPromotionsPanel::on_combo_search(wxCommandEvent&) {
    if (destroying_) return;
    apply_combo_list_filter(editing_combo_id_);
}

void InventoryPromotionsPanel::on_combo_toggle_inactive(wxCommandEvent&) {
    if (destroying_) return;
    combo_include_inactive_ = !combo_include_inactive_;
    const int btn_h = Theme::is_touch() ? 48 : 34;
    if (combo_include_inactive_) {
        btn_combo_inactive_->SetLabel(wxT("Viendo dados de baja"));
        Theme::style_button_primary(btn_combo_inactive_, btn_h);
    } else {
        btn_combo_inactive_->SetLabel(wxT("Ver dados de baja"));
        Theme::style_button_secondary(btn_combo_inactive_, btn_h);
    }
    combo_list_card_->Layout();
    reload_combos(editing_combo_id_);
}

void InventoryPromotionsPanel::on_combo_row_selected(wxListEvent& event) {
    if (destroying_ || sync_selection_guard_) return;
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= filtered_combos_.size()) return;
    load_combo_into_form(filtered_combos_[static_cast<std::size_t>(row)].product_id);
}

void InventoryPromotionsPanel::on_combo_cat_chosen(wxCommandEvent&) {
    if (destroying_) return;
    sync_combo_category_visibility(true);
    update_combo_detail_scroll();
}

void InventoryPromotionsPanel::on_combo_part_row_selected(wxListEvent& event) {
    if (destroying_) return;
    const long row = event.GetIndex();
    if (row < 0 || static_cast<std::size_t>(row) >= combo_parts_.size()) return;
    const auto& part = combo_parts_[static_cast<std::size_t>(row)];
    for (std::size_t i = 0; i < non_combo_products_.size(); ++i) {
        if (non_combo_products_[i].id == part.product_id) {
            set_selector_index(ch_combo_part_prod_, cb_combo_part_prod_, static_cast<int>(i));
            break;
        }
    }
    txt_combo_part_qty_->ChangeValue(wxString::Format(wxT("%lld"), part.quantity));
}

void InventoryPromotionsPanel::on_combo_part_add(wxCommandEvent&) {
    if (destroying_) return;
    if (keypad_) keypad_->hide_keypad(false);
    const std::string pid = selected_combo_part_product_id();
    if (pid.empty()) {
        set_combo_feedback("Elige un producto para agregar al combo.", true);
        return;
    }
    std::int64_t qty = 0;
    if (!parse_strict_int64(std::string(txt_combo_part_qty_->GetValue().ToUTF8().data()), false, qty) ||
        qty < 1 || qty > 99) {
        set_combo_feedback("La cantidad de cada producto del combo debe estar entre 1 y 99.", true);
        return;
    }
    std::string pname;
    for (const auto& p : non_combo_products_) {
        if (p.id == pid) {
            pname = p.name;
            break;
        }
    }
    bool updated = false;
    for (auto& part : combo_parts_) {
        if (part.product_id == pid) {
            part.quantity = qty;
            updated = true;
            break;
        }
    }
    if (!updated) {
        combo_parts_.push_back({pid, pname, qty});
    }
    txt_combo_part_qty_->ChangeValue(wxT("1"));
    rebuild_combo_parts_ui(pid);
    set_combo_feedback("Parte añadida. Pulsa \"Guardar combo\" para aplicar.", false);
}

void InventoryPromotionsPanel::on_combo_part_remove(wxCommandEvent&) {
    if (destroying_ || !combo_parts_list_) return;
    const long sel = combo_parts_list_->GetFirstSelected();
    if (sel < 0 || static_cast<std::size_t>(sel) >= combo_parts_.size()) {
        set_combo_feedback("Selecciona una parte de la tabla para quitarla.", true);
        return;
    }
    combo_parts_.erase(combo_parts_.begin() + sel);
    rebuild_combo_parts_ui();
    set_combo_feedback("Parte quitada. Pulsa \"Guardar combo\" para aplicar.", false);
}

void InventoryPromotionsPanel::on_combo_save(wxCommandEvent&) {
    if (destroying_ || !promotion_service_) return;
    if (keypad_) keypad_->hide_keypad(false);

    const std::string name = trim_ascii(std::string(txt_combo_name_->GetValue().ToUTF8().data()));
    const std::string sku = trim_ascii(std::string(txt_combo_sku_->GetValue().ToUTF8().data()));
    std::string cat_err;
    const std::string cat = resolve_combo_category(cat_err);
    const std::string price_raw = trim_ascii(std::string(txt_combo_price_->GetValue().ToUTF8().data()));

    if (!cat_err.empty()) {
        set_combo_feedback(cat_err, true);
        return;
    }

    if (price_raw.empty()) {
        set_combo_feedback("Escribe el precio del combo en pesos.", true);
        return;
    }
    auto price_res = contracts::Money::try_parse(price_raw, false);
    if (!price_res.is_success()) {
        set_combo_feedback(price_res.error_message(), true);
        return;
    }

    std::vector<contracts::ComboComponentDto> comps;
    comps.reserve(combo_parts_.size());
    for (const auto& part : combo_parts_) {
        contracts::ComboComponentDto c;
        c.product_id = part.product_id;
        c.product_name = part.product_name;
        c.quantity = part.quantity;
        comps.push_back(std::move(c));
    }

    if (editing_combo_id_.empty()) {
        contracts::CreateComboDto dto;
        dto.sku = sku;
        dto.name = name;
        dto.category = cat;
        dto.price = price_res.value();
        dto.components = std::move(comps);
        auto res = promotion_service_->create_combo(dto);
        if (!res.is_success()) {
            set_combo_feedback(res.error_message(), true);
            return;
        }
        const std::string new_id = res.value().product_id;
        reload_catalog_sources();
        reload_combos(new_id);
        load_combo_into_form(new_id);
        set_combo_feedback("Combo guardado.", false);
    } else {
        contracts::UpdateComboDto dto;
        dto.product_id = editing_combo_id_;
        dto.name = name;
        dto.category = cat;
        dto.price = price_res.value();
        dto.components = std::move(comps);
        auto res = promotion_service_->update_combo(dto);
        if (!res.is_success()) {
            set_combo_feedback(res.error_message(), true);
            return;
        }
        const std::string keep = editing_combo_id_;
        reload_catalog_sources();
        reload_combos(keep);
        load_combo_into_form(keep);
        set_combo_feedback("Cambios guardados.", false);
    }
}

void InventoryPromotionsPanel::on_combo_toggle_active(wxCommandEvent&) {
    if (destroying_ || !catalog_service_ || editing_combo_id_.empty()) return;
    const std::string name = trim_ascii(std::string(txt_combo_name_->GetValue().ToUTF8().data()));
    const wxString q = editing_combo_active_
        ? wxString::Format(wxT("¿Seguro que deseas dar de baja el combo \"%s\"?"), wxString::FromUTF8(name))
        : wxString::Format(wxT("¿Seguro que deseas reactivar el combo \"%s\"?"), wxString::FromUTF8(name));
    wxMessageDialog dlg(
        this, q, wxT("Confirmar cambio de estado"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION
    );
    if (dlg.ShowModal() != wxID_YES) return;

    auto res = editing_combo_active_
        ? catalog_service_->deactivate_product(editing_combo_id_)
        : catalog_service_->reactivate_product(editing_combo_id_);
    if (!res.is_success()) {
        set_combo_feedback(res.error_message(), true);
        return;
    }
    const bool deactivated_now = editing_combo_active_;
    const std::string keep = editing_combo_id_;
    reload_combos(keep);
    if (deactivated_now && !combo_include_inactive_) {
        start_new_combo();
        set_combo_feedback("Combo dado de baja.", false);
    } else {
        load_combo_into_form(keep);
        set_combo_feedback(deactivated_now ? "Combo dado de baja." : "Combo reactivado.", false);
    }
}

void InventoryPromotionsPanel::on_size(wxSizeEvent& event) {
    event.Skip();
    CallAfter([this]() {
        if (destroying_ || IsBeingDeleted()) return;
        apply_responsive_layout();
        Layout();
        if (active_tab_ == ActiveTab::Promotions) {
            if (pnl_promos_) pnl_promos_->Layout();
            update_promo_list_columns();
            update_promo_products_columns();
            update_promo_detail_scroll();
            if (lbl_promo_row_detail_ && lbl_promo_row_detail_->IsShown()) {
                const int w_wrap = std::max(FromDIP(180), promo_list_card_->GetClientSize().GetWidth() - FromDIP(36));
                wrap_static_text_deterministic(lbl_promo_row_detail_, wxString::FromUTF8(last_promo_row_detail_), w_wrap);
                promo_list_card_->Layout();
            }
        } else {
            if (pnl_combos_) pnl_combos_->Layout();
            update_combo_list_columns();
            update_combo_parts_columns();
            update_combo_detail_scroll();
        }
        Layout();
    });
}

std::string InventoryPromotionsPanel::selected_promo_category() const {
    const int idx = get_selector_index(ch_promo_cat_, cb_promo_cat_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < categories_.size()) {
        return categories_[static_cast<std::size_t>(idx)];
    }
    return "";
}

std::string InventoryPromotionsPanel::selected_combo_part_product_id() const {
    const int idx = get_selector_index(ch_combo_part_prod_, cb_combo_part_prod_);
    if (idx >= 0 && static_cast<std::size_t>(idx) < non_combo_products_.size()) {
        return non_combo_products_[static_cast<std::size_t>(idx)].id;
    }
    return "";
}

} // namespace app::frontend
