#include "src/frontend/numeric_keypad.hpp"
#include "src/frontend/theme.hpp"
#include <wx/dcbuffer.h>
#include <algorithm>

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace app::frontend::ui {

namespace {
constexpr int kKeySizeLogical = 64;
constexpr int kKeyGapLogical = 8;
constexpr int kStdActionWLogical = 88;
constexpr int kStdPadLogical = 12;
constexpr int kCompactActionWLogical = 80;
constexpr int kCompactPadLogical = 6;
} // namespace

int NumericKeypad::OutsideClickFilter::FilterEvent(wxEvent& event) {
    if (!owner_ || !owner_->IsShown() || !owner_->IsShownOnScreen()) {
        return Event_Skip;
    }
    if (event.GetEventType() == wxEVT_LEFT_DOWN) {
        wxWindow* ev_win = wxDynamicCast(event.GetEventObject(), wxWindow);
        if (ev_win && wxGetTopLevelParent(ev_win) == wxGetTopLevelParent(owner_)) {
            const bool in_keypad = (ev_win == owner_) || owner_->IsDescendant(ev_win);
            const bool in_field = (ev_win == owner_->active_field_)
                || (owner_->active_hit_box_ && (ev_win == owner_->active_hit_box_ || owner_->active_hit_box_->IsDescendant(ev_win)));
            if (!in_keypad && !in_field) {
                owner_->hide_keypad(false);
            }
        }
    }
    return Event_Skip;
}

NumericKeypad::NumericKeypad(wxWindow* host)
    : wxPanel(host, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN | wxBORDER_NONE),
      embedded_(false),
      hide_on_done_(true),
      allow_decimal_(false),
      compact_(false),
      host_(host),
      filter_(std::make_unique<OutsideClickFilter>(this)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Theme::ColorSurfaceSubtle);
    Hide();

    Bind(wxEVT_PAINT, &NumericKeypad::on_paint, this);
    if (host_) {
        host_->Bind(wxEVT_SHOW, &NumericKeypad::on_host_show, this);
        host_->Bind(wxEVT_SIZE, &NumericKeypad::on_host_size, this);
        host_->Bind(wxEVT_DESTROY, &NumericKeypad::on_host_destroy, this);
    }
}

NumericKeypad::NumericKeypad(wxWindow* parent, bool allow_decimal, bool compact)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN | wxBORDER_NONE),
      embedded_(true),
      hide_on_done_(false),
      allow_decimal_(allow_decimal),
      compact_(compact),
      host_(parent) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Theme::ColorSurfaceSubtle);
    create_controls();
    apply_layout();
    const wxSize sz = size_for_mode(this, compact_);
    SetMinSize(sz);
    SetSize(sz);
    Bind(wxEVT_PAINT, &NumericKeypad::on_paint, this);
    Bind(wxEVT_SHOW, [this](wxShowEvent& evt) {
        evt.Skip();
        if (evt.IsShown() && embedded_ && active_field_) {
            wxTextCtrl* target = active_field_;
            CallAfter([this, target]() {
                if (embedded_ && active_field_ == target && active_field_->IsEnabled()
                    && active_field_->IsShownOnScreen()) {
                    if (!active_field_->HasFocus()) {
                        suppress_reopen_on_focus_ = true;
                        active_field_->SetFocus();
                        suppress_reopen_on_focus_ = false;
                    }
                    select_all_target();
                }
            });
        }
    });
}

NumericKeypad::~NumericKeypad() {
    if (host_) {
        host_->Unbind(wxEVT_SHOW, &NumericKeypad::on_host_show, this);
        host_->Unbind(wxEVT_SIZE, &NumericKeypad::on_host_size, this);
        host_->Unbind(wxEVT_DESTROY, &NumericKeypad::on_host_destroy, this);
        host_ = nullptr;
    }
    uninstall_filter();
}

void NumericKeypad::on_host_show(wxShowEvent& evt) {
    evt.Skip();
    if (!evt.IsShown()) {
        hide_keypad(false);
    }
}

void NumericKeypad::on_host_size(wxSizeEvent& evt) {
    evt.Skip();
    if (IsShown() && active_field_) {
        CallAfter([this]() {
            if (IsShown() && active_field_) {
                reposition();
            }
        });
    }
}

void NumericKeypad::on_host_destroy(wxWindowDestroyEvent& evt) {
    evt.Skip();
    uninstall_filter();
    host_ = nullptr;
    active_field_ = nullptr;
    active_hit_box_ = nullptr;
}

void NumericKeypad::create_controls() {
    const wxFont digit_font = Theme::get_font_subtitle(wxFONTWEIGHT_BOLD);
    const wxFont action_font = Theme::get_font_body(wxFONTWEIGHT_BOLD);

    auto style_digit_btn = [&](wxButton* btn) {
        btn->SetFont(digit_font);
        btn->SetBackgroundColour(Theme::ColorSurface);
        btn->SetForegroundColour(Theme::ColorText);
        btn->SetWindowStyleFlag((btn->GetWindowStyleFlag() & ~wxBORDER_MASK) | wxBORDER_SIMPLE);
    };

    for (int d = 0; d <= 9; ++d) {
        const wxString label = wxString::Format(wxT("%d"), d);
        btn_digits_[d] = new wxButton(this, wxID_ANY, label);
        style_digit_btn(btn_digits_[d]);
        btn_digits_[d]->Bind(wxEVT_BUTTON, [this, label](wxCommandEvent&) { on_key_digit(label); });
    }

    btn_dot_ = new wxButton(this, wxID_ANY, wxT("."));
    style_digit_btn(btn_dot_);
    btn_dot_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_key_decimal(); });

    btn_backspace_ = new wxButton(this, wxID_ANY, wxT("Borrar"));
    btn_backspace_->SetFont(action_font);
    btn_backspace_->SetBackgroundColour(Theme::ColorSurface);
    btn_backspace_->SetForegroundColour(Theme::ColorText);
    btn_backspace_->SetWindowStyleFlag((btn_backspace_->GetWindowStyleFlag() & ~wxBORDER_MASK) | wxBORDER_SIMPLE);
    btn_backspace_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_key_backspace(); });

    btn_clear_ = new wxButton(this, wxID_ANY, wxT("Limpiar"));
    btn_clear_->SetFont(action_font);
    btn_clear_->SetBackgroundColour(Theme::ColorErrorSoft);
    btn_clear_->SetForegroundColour(Theme::ColorError);
    btn_clear_->SetWindowStyleFlag((btn_clear_->GetWindowStyleFlag() & ~wxBORDER_MASK) | wxBORDER_SIMPLE);
    btn_clear_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_key_clear(); });

    btn_done_ = new wxButton(this, wxID_ANY, wxT("Listo"));
    btn_done_->SetFont(action_font);
    btn_done_->SetBackgroundColour(Theme::ColorBrand);
    btn_done_->SetForegroundColour(Theme::ColorSurface);
    btn_done_->SetWindowStyleFlag((btn_done_->GetWindowStyleFlag() & ~wxBORDER_MASK) | wxBORDER_NONE);
    btn_done_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_key_done(); });
}

wxSize NumericKeypad::size_for_mode(const wxWindow* w, bool compact) {
    const int k = w->FromDIP(kKeySizeLogical);
    const int g = w->FromDIP(kKeyGapLogical);
    if (!compact) {
        const int pad = w->FromDIP(kStdPadLogical);
        const int aw = w->FromDIP(kStdActionWLogical);
        return wxSize(2 * pad + 3 * k + 3 * g + aw, 2 * pad + 4 * k + 3 * g);
    }
    const int pad = w->FromDIP(kCompactPadLogical);
    const int aw = w->FromDIP(kCompactActionWLogical);
    return wxSize(2 * pad + 4 * k + 4 * g + aw, 2 * pad + 3 * k + 2 * g);
}

wxSize NumericKeypad::DoGetBestSize() const {
    return size_for_mode(this, compact_);
}

void NumericKeypad::apply_layout() {
    if (!btn_done_) {
        return;
    }
    const int k = FromDIP(kKeySizeLogical);
    const int g = FromDIP(kKeyGapLogical);

    auto place = [](wxButton* btn, int x, int y, int w, int h) {
        btn->SetMinSize(wxSize(w, h));
        btn->SetSize(x, y, w, h);
    };

    if (!compact_) {
        const int pad = FromDIP(kStdPadLogical);
        const int aw = FromDIP(kStdActionWLogical);
        const int c0 = pad;
        const int c1 = c0 + k + g;
        const int c2 = c1 + k + g;
        const int c3 = c2 + k + g;
        const int r0 = pad;
        const int r1 = r0 + k + g;
        const int r2 = r1 + k + g;
        const int r3 = r2 + k + g;

        place(btn_digits_[7], c0, r0, k, k);
        place(btn_digits_[8], c1, r0, k, k);
        place(btn_digits_[9], c2, r0, k, k);
        place(btn_backspace_, c3, r0, aw, k);

        place(btn_digits_[4], c0, r1, k, k);
        place(btn_digits_[5], c1, r1, k, k);
        place(btn_digits_[6], c2, r1, k, k);
        place(btn_clear_, c3, r1, aw, k);

        place(btn_digits_[1], c0, r2, k, k);
        place(btn_digits_[2], c1, r2, k, k);
        place(btn_digits_[3], c2, r2, k, k);
        place(btn_done_, c3, r2, aw, 2 * k + g);

        if (allow_decimal_) {
            place(btn_digits_[0], c0, r3, 2 * k + g, k);
            place(btn_dot_, c2, r3, k, k);
            btn_dot_->Show();
        } else {
            place(btn_digits_[0], c0, r3, 3 * k + 2 * g, k);
            btn_dot_->Hide();
        }
    } else {
        const int pad = FromDIP(kCompactPadLogical);
        const int aw = FromDIP(kCompactActionWLogical);
        const int c0 = pad;
        const int c1 = c0 + k + g;
        const int c2 = c1 + k + g;
        const int c3 = c2 + k + g;
        const int c4 = c3 + k + g;
        const int r0 = pad;
        const int r1 = r0 + k + g;
        const int r2 = r1 + k + g;

        place(btn_digits_[7], c0, r0, k, k);
        place(btn_digits_[8], c1, r0, k, k);
        place(btn_digits_[9], c2, r0, k, k);
        place(btn_backspace_, c4, r0, aw, k);

        place(btn_digits_[4], c0, r1, k, k);
        place(btn_digits_[5], c1, r1, k, k);
        place(btn_digits_[6], c2, r1, k, k);
        place(btn_clear_, c4, r1, aw, k);

        if (allow_decimal_) {
            place(btn_digits_[0], c3, r0, k, k);
            place(btn_dot_, c3, r1, k, k);
            btn_dot_->Show();
        } else {
            place(btn_digits_[0], c3, r0, k, 2 * k + g);
            btn_dot_->Hide();
        }

        place(btn_digits_[1], c0, r2, k, k);
        place(btn_digits_[2], c1, r2, k, k);
        place(btn_digits_[3], c2, r2, k, k);
        place(btn_done_, c3, r2, k + g + aw, k);
    }

    const wxSize total_sz = size_for_mode(this, compact_);
    SetMinSize(total_sz);
    if (embedded_) {
        SetSize(total_sz);
    }
    Refresh();
}

void NumericKeypad::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    const wxSize sz = GetClientSize();
    dc.SetBrush(wxBrush(Theme::ColorSurfaceSubtle));
    dc.SetPen(wxPen(Theme::ColorBorder, 1));
    dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());
}

void NumericKeypad::set_allow_decimal(bool allow) {
    if (allow_decimal_ == allow) return;
    allow_decimal_ = allow;
    apply_layout();
}

void NumericKeypad::set_compact(bool compact) {
    if (compact_ == compact) return;
    compact_ = compact;
    apply_layout();
}

void NumericKeypad::select_all_target() {
    if (!active_field_ || !active_field_->IsEnabled()) return;
    const long len = static_cast<long>(active_field_->GetLastPosition());
    active_field_->SetSelection(0, len);
    last_caret_from_ = 0;
    last_caret_to_ = len;
}

void NumericKeypad::schedule_select_all(wxTextCtrl* field) {
    if (!field || !field->IsEnabled()) return;
    const wxString snapshot = field->GetValue();
    CallAfter([this, field, snapshot]() {
        if (active_field_ == field && active_field_->IsEnabled()
            && active_field_->IsShownOnScreen() && active_field_->GetValue() == snapshot) {
            select_all_target();
        }
    });
}

void NumericKeypad::set_target_field(wxTextCtrl* field, bool hide_on_done) {
    active_field_ = field;
    active_hit_box_ = field ? field->GetParent() : nullptr;
    hide_on_done_ = hide_on_done;
    if (!field) {
        last_caret_from_ = 0;
        last_caret_to_ = 0;
        return;
    }

    if (embedded_ && bound_embedded_field_ != field) {
        bound_embedded_field_ = field;
        field->Bind(wxEVT_SET_FOCUS, [this, field](wxFocusEvent& evt) {
            evt.Skip();
            if (suppress_reopen_on_focus_) {
                suppress_reopen_on_focus_ = false;
                return;
            }
            if (active_field_ == field && field->IsEnabled() && field->IsShownOnScreen()) {
                select_all_target();
                schedule_select_all(field);
            }
        });
        field->Bind(wxEVT_LEFT_DOWN, [this, field](wxMouseEvent& evt) {
            evt.Skip();
            if (active_field_ == field && field->IsEnabled() && field->IsShownOnScreen()) {
                select_all_target();
                schedule_select_all(field);
            }
        });
        if (active_hit_box_ && active_hit_box_ != field) {
            wxWindow* hb = active_hit_box_;
            hb->Bind(wxEVT_LEFT_DOWN, [this, field](wxMouseEvent& evt) {
                evt.Skip();
                if (active_field_ == field && field->IsEnabled() && field->IsShownOnScreen()) {
                    if (!field->HasFocus()) {
                        suppress_reopen_on_focus_ = true;
                        field->SetFocus();
                        suppress_reopen_on_focus_ = false;
                    }
                    select_all_target();
                    schedule_select_all(field);
                }
            });
        }
        field->Bind(wxEVT_KILL_FOCUS, [this, field](wxFocusEvent& evt) {
            evt.Skip();
            if (active_field_ == field) {
                field->GetSelection(&last_caret_from_, &last_caret_to_);
            }
        });
    }

    select_all_target();
    if (embedded_) {
        wxTextCtrl* target = field;
        CallAfter([this, target]() {
            if (embedded_ && active_field_ == target && active_field_->IsEnabled()
                && active_field_->IsShownOnScreen()) {
                if (!active_field_->HasFocus()) {
                    suppress_reopen_on_focus_ = true;
                    active_field_->SetFocus();
                    suppress_reopen_on_focus_ = false;
                }
                select_all_target();
            }
        });
    }
}

const NumericKeypad::BoundField* NumericKeypad::find_binding(const wxTextCtrl* field) const {
    for (const auto& b : bindings_) {
        if (b.field == field) return &b;
    }
    return nullptr;
}

void NumericKeypad::bind_field(
    wxTextCtrl* field,
    wxWindow* hit_box,
    bool allow_decimal,
    wxWindow* anchor_control,
    ProtectedRectsFn protected_fn
) {
    if (!field || embedded_) return;
    bindings_.push_back(BoundField{
        field,
        hit_box ? hit_box : static_cast<wxWindow*>(field),
        anchor_control ? anchor_control : (hit_box ? hit_box : static_cast<wxWindow*>(field)),
        allow_decimal,
        std::move(protected_fn)
    });

    field->Bind(wxEVT_SET_FOCUS, [this, field](wxFocusEvent& evt) {
        evt.Skip();
        if (suppress_reopen_on_focus_) {
            suppress_reopen_on_focus_ = false;
            return;
        }
        if (field->IsEnabled() && field->IsShownOnScreen()) {
            show_for_field(field);
        }
    });

    field->Bind(wxEVT_LEFT_DOWN, [this, field](wxMouseEvent& evt) {
        evt.Skip();
        if (field->IsEnabled() && field->IsShownOnScreen()) {
            show_for_field(field);
        }
    });

    if (hit_box && hit_box != field) {
        hit_box->Bind(wxEVT_LEFT_DOWN, [this, field](wxMouseEvent& evt) {
            evt.Skip();
            if (field->IsEnabled() && field->IsShownOnScreen()) {
                show_for_field(field);
            }
        });
    }

    field->Bind(wxEVT_KILL_FOCUS, [this, field](wxFocusEvent& evt) {
        evt.Skip();
        if (active_field_ != field) return;
        field->GetSelection(&last_caret_from_, &last_caret_to_);
        wxWindow* next = evt.GetWindow();
        if (next && (next == this || IsDescendant(next))) {
            return;
        }
        // Si el usuario tabuló o tocó otro campo numérico registrado, SET_FOCUS de ese campo tomará el relevo.
        if (next) {
            if (auto* next_edit = wxDynamicCast(next, wxTextCtrl)) {
                if (find_binding(next_edit) != nullptr) {
                    return;
                }
            }
        }
        hide_keypad(false);
    });
}

void NumericKeypad::show_for_field(wxTextCtrl* field) {
    if (embedded_ || !host_ || !field) return;
    if (!field->IsEnabled() || !field->IsShownOnScreen()) return;

    const BoundField* b = find_binding(field);
    if (!b) return;

    active_field_ = b->field;
    active_hit_box_ = b->hit_box;
    if (!active_field_->HasFocus()) {
        suppress_reopen_on_focus_ = true;
        active_field_->SetFocus();
        suppress_reopen_on_focus_ = false;
    }
    select_all_target();
    schedule_select_all(active_field_);
    if (!btn_done_) {
        create_controls();
        allow_decimal_ = b->allow_decimal;
        apply_layout();
    } else {
        set_allow_decimal(b->allow_decimal);
    }

    reposition();
    if (!IsShown()) {
        Show();
    }
    raise_over_siblings();
    install_filter();
}

void NumericKeypad::hide_keypad(bool restore_focus) {
    if (embedded_) return;
    wxTextCtrl* prev_field = active_field_;
    uninstall_filter();
    if (IsShown()) {
        Hide();
    }
    if (restore_focus && prev_field && prev_field->IsEnabled() && prev_field->IsShownOnScreen()) {
        suppress_reopen_on_focus_ = true;
        prev_field->SetFocus();
        prev_field->SetSelection(last_caret_from_, last_caret_to_);
    } else {
        active_field_ = nullptr;
        active_hit_box_ = nullptr;
    }
}

bool NumericKeypad::intersects_any(const wxRect& candidate, const std::vector<wxRect>& rects) {
    for (const auto& r : rects) {
        if (!r.IsEmpty() && candidate.Intersects(r)) {
            return true;
        }
    }
    return false;
}

void NumericKeypad::reposition() {
    if (embedded_ || !host_ || !active_field_) return;
    const BoundField* b = find_binding(active_field_);
    if (!b) return;

    const wxSize client = host_->GetClientSize();
    const int cw = client.GetWidth();
    const int ch = client.GetHeight();
    if (cw <= 50 || ch <= 50) return;

    wxRect anchor_rect;
    if (b->anchor) {
        const wxPoint tl = host_->ScreenToClient(b->anchor->GetScreenPosition());
        anchor_rect = wxRect(tl, b->anchor->GetSize());
    } else {
        const wxPoint tl = host_->ScreenToClient(b->hit_box->GetScreenPosition());
        anchor_rect = wxRect(tl, b->hit_box->GetSize());
    }

    std::vector<wxRect> prot;
    const wxPoint f_tl = host_->ScreenToClient(b->hit_box->GetScreenPosition());
    prot.emplace_back(f_tl, b->hit_box->GetSize());
    if (b->protected_fn) {
        auto extra = b->protected_fn();
        prot.insert(prot.end(), extra.begin(), extra.end());
    }

    const int margin = FromDIP(2);
    const int gap = FromDIP(8);

    auto pick_rect_for_mode = [&](bool try_compact, wxRect& out_rect, wxRect* best_fallback = nullptr) -> bool {
        const wxSize ksz = size_for_mode(host_, try_compact);
        const int kw = ksz.GetWidth();
        const int kh = ksz.GetHeight();
        if (kw > cw - 2 * margin || kh > ch - 2 * margin) {
            return false;
        }

        const int clamp_x_min = margin;
        const int clamp_x_max = std::max(margin, cw - margin - kw);
        const int clamp_y_min = margin;
        const int clamp_y_max = std::max(margin, ch - margin - kh);

        auto clamp_pt = [&](int x, int y) {
            return wxRect(
                std::clamp(x, clamp_x_min, clamp_x_max),
                std::clamp(y, clamp_y_min, clamp_y_max),
                kw,
                kh
            );
        };

        std::vector<wxRect> candidates;
        // 1. Debajo del control de ancla (sin recortar abajo)
        if (anchor_rect.GetBottom() + gap + kh <= ch - margin) {
            candidates.push_back(clamp_pt(anchor_rect.GetLeft(), anchor_rect.GetBottom() + gap));
            candidates.push_back(clamp_pt(margin, anchor_rect.GetBottom() + gap));
            candidates.push_back(clamp_pt(f_tl.x, anchor_rect.GetBottom() + gap));
        }
        // 2. A la derecha del ancla
        if (anchor_rect.GetRight() + gap + kw <= cw - margin) {
            candidates.push_back(clamp_pt(anchor_rect.GetRight() + gap, anchor_rect.GetTop()));
            candidates.push_back(clamp_pt(anchor_rect.GetRight() + gap, margin));
        }
        // 3. A la izquierda del ancla / campo (p. ej. Ingredientes sobre lista izquierda)
        if (anchor_rect.GetLeft() - gap - kw >= margin) {
            candidates.push_back(clamp_pt(anchor_rect.GetLeft() - gap - kw, anchor_rect.GetTop()));
            candidates.push_back(clamp_pt(anchor_rect.GetLeft() - gap - kw, (ch - kh) / 2));
        }
        // 4. Encima del ancla (p. ej. Caja y turnos en ventana 960x600)
        if (anchor_rect.GetTop() - gap - kh >= margin) {
            candidates.push_back(clamp_pt(cw - margin - kw, anchor_rect.GetTop() - gap - kh));
            candidates.push_back(clamp_pt(cw - margin - kw, margin));
        }
        // 5. Esquinas libres dentro del cliente
        candidates.push_back(clamp_pt(margin, ch - margin - kh));
        candidates.push_back(clamp_pt(cw - margin - kw, ch - margin - kh));
        candidates.push_back(clamp_pt(cw - margin - kw, margin));
        candidates.push_back(clamp_pt(margin, margin));

        for (const auto& c : candidates) {
            if (!intersects_any(c, prot)) {
                out_rect = c;
                return true;
            }
        }
        if (best_fallback) {
            long long best_area = -1;
            for (const auto& c : candidates) {
                long long area = 0;
                for (const auto& r : prot) {
                    const wxRect inter = c.Intersect(r);
                    if (!inter.IsEmpty()) {
                        area += static_cast<long long>(inter.width) * static_cast<long long>(inter.height);
                    }
                }
                if (best_area < 0 || area < best_area) {
                    best_area = area;
                    *best_fallback = c;
                }
            }
        }
        return false;
    };

    wxRect chosen;
    bool use_compact = false;
    if (!pick_rect_for_mode(false, chosen)) {
        wxRect min_overlap_rect;
        if (pick_rect_for_mode(true, chosen, &min_overlap_rect)) {
            use_compact = true;
        } else {
            use_compact = true;
            if (!min_overlap_rect.IsEmpty()) {
                chosen = min_overlap_rect;
            } else {
                const wxSize ksz = size_for_mode(host_, true);
                chosen = wxRect(
                    margin,
                    std::max(margin, ch - margin - ksz.GetHeight()),
                    ksz.GetWidth(),
                    ksz.GetHeight()
                );
            }
        }
    }

    set_compact(use_compact);
    SetSize(chosen);
    if (IsShown()) {
        raise_over_siblings();
    }
}

void NumericKeypad::raise_over_siblings() {
    Raise();
#ifdef __WXMSW__
    HWND hwnd = static_cast<HWND>(GetHWND());
    if (hwnd && host_) {
        HWND host_hwnd = static_cast<HWND>(host_->GetHWND());
        if (host_hwnd) {
            LONG_PTR hstyle = ::GetWindowLongPtr(host_hwnd, GWL_STYLE);
            if ((hstyle & WS_CLIPCHILDREN) == 0) {
                ::SetWindowLongPtr(host_hwnd, GWL_STYLE, hstyle | WS_CLIPCHILDREN);
            }
        }
        for (wxWindow* sib : host_->GetChildren()) {
            if (!sib) continue;
            HWND sib_hwnd = static_cast<HWND>(sib->GetHWND());
            if (!sib_hwnd) continue;
            LONG_PTR sstyle = ::GetWindowLongPtr(sib_hwnd, GWL_STYLE);
            if ((sstyle & WS_CLIPSIBLINGS) == 0) {
                ::SetWindowLongPtr(sib_hwnd, GWL_STYLE, sstyle | WS_CLIPSIBLINGS);
            }
        }
        ::SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#endif
    Refresh(true);
    Update();
}

void NumericKeypad::install_filter() {
    if (embedded_ || filter_installed_ || !filter_) return;
    wxEvtHandler::AddFilter(filter_.get());
    filter_installed_ = true;
}

void NumericKeypad::uninstall_filter() {
    if (!filter_installed_ || !filter_) return;
    wxEvtHandler::RemoveFilter(filter_.get());
    filter_installed_ = false;
}

void NumericKeypad::restore_target_focus(long new_pos) {
    if (!active_field_ || !active_field_->IsEnabled() || !active_field_->IsShownOnScreen()) {
        return;
    }
    suppress_reopen_on_focus_ = true;
    active_field_->SetFocus();
    if (new_pos >= 0) {
        active_field_->SetInsertionPoint(new_pos);
        last_caret_from_ = new_pos;
        last_caret_to_ = new_pos;
    } else {
        active_field_->GetSelection(&last_caret_from_, &last_caret_to_);
    }
    suppress_reopen_on_focus_ = false;
}

void NumericKeypad::on_key_digit(const wxString& ch) {
    if (!active_field_ || !active_field_->IsEnabled()) return;
    long from = last_caret_from_;
    long to = last_caret_to_;
    if (active_field_->HasFocus()) {
        active_field_->GetSelection(&from, &to);
    }
    const long len = static_cast<long>(active_field_->GetLastPosition());
    from = std::clamp(from, 0L, len);
    to = std::clamp(to, from, len);
    active_field_->SetSelection(from, to);
    active_field_->WriteText(ch);
    restore_target_focus(from + static_cast<long>(ch.length()));
}

void NumericKeypad::on_key_decimal() {
    if (!allow_decimal_ || !active_field_ || !active_field_->IsEnabled()) return;
    long from = last_caret_from_;
    long to = last_caret_to_;
    if (active_field_->HasFocus()) {
        active_field_->GetSelection(&from, &to);
    }
    const wxString val = active_field_->GetValue();
    const long len = static_cast<long>(val.length());
    from = std::clamp(from, 0L, len);
    to = std::clamp(to, from, len);

    const wxString prefix = val.Left(static_cast<size_t>(from));
    const wxString suffix = val.Mid(static_cast<size_t>(to));
    if (prefix.Contains(wxT(".")) || suffix.Contains(wxT("."))) {
        restore_target_focus(to);
        return;
    }
    active_field_->SetSelection(from, to);
    active_field_->WriteText(wxT("."));
    restore_target_focus(from + 1);
}

void NumericKeypad::on_key_backspace() {
    if (!active_field_ || !active_field_->IsEnabled()) return;
    long from = last_caret_from_;
    long to = last_caret_to_;
    if (active_field_->HasFocus()) {
        active_field_->GetSelection(&from, &to);
    }
    const long len = static_cast<long>(active_field_->GetLastPosition());
    from = std::clamp(from, 0L, len);
    to = std::clamp(to, from, len);

    long next_pos = from;
    if (to > from) {
        active_field_->Remove(from, to);
        next_pos = from;
    } else if (from > 0) {
        active_field_->Remove(from - 1, from);
        next_pos = from - 1;
    }
    restore_target_focus(next_pos);
}

void NumericKeypad::on_key_clear() {
    if (!active_field_ || !active_field_->IsEnabled()) return;
    active_field_->Clear();
    restore_target_focus(0);
}

void NumericKeypad::on_key_done() {
    if (embedded_ && !hide_on_done_) {
        restore_target_focus(-1);
        return;
    }
    hide_keypad(true);
}

} // namespace app::frontend::ui
