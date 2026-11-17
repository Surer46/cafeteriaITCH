#include "src/frontend/subnav_bar.hpp"
#include "src/frontend/theme.hpp"
#include <algorithm>

namespace app::frontend {

namespace {

wxButton* create_scroll_arrow_button(wxWindow* parent, const wxString& label, int side_px) {
    auto* btn = new wxButton(
        parent, wxID_ANY, label, wxDefaultPosition, wxSize(side_px, side_px), wxBU_EXACTFIT
    );
    btn->SetBackgroundColour(Theme::ColorBorderSubtle);
    btn->SetForegroundColour(Theme::ColorText);
    btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    btn->SetMinSize(wxSize(side_px, side_px));
    btn->Hide();
    return btn;
}

} // namespace

SubnavBar::SubnavBar(wxWindow* parent, OnSubsectionChangedCallback callback)
    : wxPanel(parent, wxID_ANY), callback_(std::move(callback)) {
    SetBackgroundColour(Theme::ColorBackground);

    const int btn_h = FromDIP(Theme::is_touch() ? 48 : 36);
    const int bar_h = btn_h + FromDIP(8);
    SetMinSize(wxSize(-1, bar_h));

    auto* root_sizer = new wxBoxSizer(wxHORIZONTAL);
    root_sizer->AddSpacer(Theme::get_spacing_sm(this));

    btn_prev_ = create_scroll_arrow_button(this, wxT("<"), btn_h);
    btn_prev_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { scroll_by_step(-1); });
    root_sizer->Add(btn_prev_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(4));

    scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL);
    scroll_->SetBackgroundColour(Theme::ColorBackground);
    scroll_->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_NEVER);
    scroll_->SetScrollRate(1, 0);
    scroll_->SetMinSize(wxSize(-1, bar_h));

    track_ = new wxPanel(scroll_, wxID_ANY, wxPoint(0, 0), wxSize(100, bar_h));
    track_->SetBackgroundColour(Theme::ColorBackground);
    buttons_sizer_ = new wxBoxSizer(wxHORIZONTAL);
    track_->SetSizer(buttons_sizer_);

    root_sizer->Add(scroll_, 1, wxEXPAND);

    btn_next_ = create_scroll_arrow_button(this, wxT(">"), btn_h);
    btn_next_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { scroll_by_step(+1); });
    root_sizer->Add(btn_next_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxTOP | wxBOTTOM, FromDIP(4));

    root_sizer->AddSpacer(Theme::get_spacing_sm(this));
    SetSizer(root_sizer);

    Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        if (!IsBeingDeleted()) {
            update_scroll_controls();
        }
    });

    set_section(SectionId::PointOfSale);
}

void SubnavBar::set_section(SectionId section, bool can_view_dev_server) {
    current_section_ = section;
    can_view_dev_server_ = can_view_dev_server;
    rebuild_buttons();
}

void SubnavBar::rebuild_buttons() {
    buttons_sizer_->Clear(true);
    items_.clear();
    content_width_px_ = 0;

    std::vector<std::string> sub_names;
    switch (current_section_) {
        case SectionId::PointOfSale:
            sub_names = {"Venta", "Catálogo visual"};
            break;
        case SectionId::Inventory:
            sub_names = {"Existencias", "Reabastecimiento", "Productos y recetas",
                         "Ingredientes", "Promociones y combos", "Barra"};
            break;
        case SectionId::CashAndReports:
            sub_names = {"Caja y turnos", "Bitácora", "Reportes"};
            break;
        case SectionId::BackOffice:
            sub_names = {"Usuarios y cuentas", "Roles y permisos", "Configuración", "Respaldos", "Actualizaciones"};
            if (can_view_dev_server_) {
                sub_names.push_back("Servidor de respaldos");
            }
            break;
    }

    bool keep_current = false;
    for (const auto& name : sub_names) {
        if (name == current_subsection_) {
            keep_current = true;
            break;
        }
    }
    if (!keep_current) {
        current_subsection_ = !sub_names.empty() ? sub_names.front() : "";
    }

    const int btn_h = FromDIP(Theme::is_touch() ? 48 : 36);
    const int bar_h = btn_h + FromDIP(8);
    const int btn_pad_x = FromDIP(Theme::is_touch() ? 14 : 12);
    const int gap_x = FromDIP(4);
    const wxFont bold_font = Theme::get_font_body(wxFONTWEIGHT_BOLD);

    SetMinSize(wxSize(-1, bar_h));
    if (scroll_) {
        scroll_->SetMinSize(wxSize(-1, bar_h));
    }
    for (wxButton* arrow : {btn_prev_, btn_next_}) {
        if (arrow) {
            arrow->SetFont(bold_font);
            arrow->SetMinSize(wxSize(btn_h, btn_h));
            arrow->SetSize(wxSize(btn_h, btn_h));
        }
    }

    for (std::size_t i = 0; i < sub_names.size(); ++i) {
        const std::string& name = sub_names[i];
        const wxString label_wx = wxString::FromUTF8(name.c_str());
        auto* btn = new wxButton(track_, wxID_ANY, label_wx,
                                 wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        btn->SetFont(bold_font);
        const int text_w = btn->GetTextExtent(label_wx).GetWidth();
        const int btn_w = std::max(FromDIP(Theme::is_touch() ? 64 : 52), text_w + btn_pad_x);
        btn->SetMinSize(wxSize(btn_w, btn_h));

        btn->Bind(wxEVT_BUTTON, [this, name](wxCommandEvent&) {
            on_button_clicked(name);
        });

        const bool is_last = (i + 1 == sub_names.size());
        const int flags = wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM | (is_last ? 0 : wxRIGHT);
        buttons_sizer_->Add(btn, 0, flags, gap_x);
        items_.push_back({btn, name});

        content_width_px_ += btn_w + (is_last ? 0 : gap_x);
    }

    update_button_styles();
    if (track_) {
        track_->SetSize(0, 0, std::max(1, content_width_px_), bar_h);
        track_->Layout();
    }
    if (scroll_) {
        scroll_->SetVirtualSize(content_width_px_, bar_h);
        scroll_->Scroll(0, 0);
    }
    update_scroll_controls();
    ensure_active_visible();
}

void SubnavBar::update_button_styles() {
    for (const auto& item : items_) {
        if (!item.button) continue;
        bool is_active = (item.name == current_subsection_);
        if (is_active) {
            item.button->SetBackgroundColour(Theme::ColorSurface);
            item.button->SetForegroundColour(Theme::ColorBrand);
            item.button->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        } else {
            item.button->SetBackgroundColour(Theme::ColorBorderSubtle);
            item.button->SetForegroundColour(Theme::ColorTextSecondary);
            item.button->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        }
        item.button->InvalidateBestSize();
        item.button->Refresh();
    }
}

void SubnavBar::update_scroll_controls() {
    if (updating_scroll_ || IsBeingDeleted() || !scroll_ || !track_ || !btn_prev_ || !btn_next_) {
        return;
    }
    updating_scroll_ = true;

    const int total_w = GetSize().GetWidth();
    const int side_margins = 2 * Theme::get_spacing_sm(this);
    const int avail_no_arrows = std::max(0, total_w - side_margins);
    const bool need_arrows = (total_w > 0) && (content_width_px_ > avail_no_arrows);

    if (btn_prev_->IsShown() != need_arrows || btn_next_->IsShown() != need_arrows) {
        btn_prev_->Show(need_arrows);
        btn_next_->Show(need_arrows);
    }
    Layout();

    const int bar_h = FromDIP(Theme::is_touch() ? 48 : 36) + FromDIP(8);
    const int view_w = std::max(0, scroll_->GetClientSize().GetWidth());
    const int max_scroll = std::max(0, content_width_px_ - view_w);
    int cur_x = scroll_->GetViewStart().x;
    if (!need_arrows) {
        if (cur_x != 0) {
            scroll_->Scroll(0, 0);
            cur_x = 0;
        }
    } else if (cur_x > max_scroll) {
        scroll_->Scroll(max_scroll, 0);
        cur_x = max_scroll;
    }

    scroll_->SetVirtualSize(content_width_px_, bar_h);
    track_->SetSize(-cur_x, 0, std::max(content_width_px_, view_w), bar_h);
    track_->Layout();

    if (need_arrows) {
        btn_prev_->Enable(cur_x > 0);
        btn_next_->Enable(cur_x < max_scroll);
        btn_prev_->Refresh();
        btn_next_->Refresh();
    }

    updating_scroll_ = false;
}

void SubnavBar::scroll_by_step(int direction) {
    if (!scroll_ || !track_ || items_.empty()) {
        return;
    }
    const int view_w = std::max(1, scroll_->GetClientSize().GetWidth());
    const int max_scroll = std::max(0, content_width_px_ - view_w);
    if (max_scroll <= 0) {
        return;
    }

    const int cur_x = scroll_->GetViewStart().x;
    const int gap_x = FromDIP(4);
    int target_x = cur_x;

    if (direction > 0) {
        int x_acc = 0;
        bool found = false;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            wxButton* b = items_[i].button;
            if (!b) continue;
            const int bw = b->GetMinWidth() > 0 ? b->GetMinWidth() : b->GetSize().GetWidth();
            const int b_right = x_acc + bw;
            if (b_right > cur_x + view_w) {
                target_x = std::min(max_scroll, b_right - view_w + gap_x);
                found = true;
                break;
            }
            x_acc = b_right + gap_x;
        }
        if (!found || target_x <= cur_x) {
            target_x = std::min(max_scroll, cur_x + std::max(FromDIP(120), view_w / 2));
        }
    } else {
        std::vector<std::pair<int, int>> spans;
        spans.reserve(items_.size());
        int x_acc = 0;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            wxButton* b = items_[i].button;
            if (!b) continue;
            const int bw = b->GetMinWidth() > 0 ? b->GetMinWidth() : b->GetSize().GetWidth();
            spans.emplace_back(x_acc, x_acc + bw);
            x_acc += bw + gap_x;
        }
        bool found = false;
        for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
            if (it->first < cur_x) {
                target_x = std::max(0, it->first - gap_x);
                found = true;
                break;
            }
        }
        if (!found || target_x >= cur_x) {
            target_x = std::max(0, cur_x - std::max(FromDIP(120), view_w / 2));
        }
    }

    scroll_->Scroll(target_x, 0);
    if (btn_prev_ && btn_next_) {
        btn_prev_->Enable(target_x > 0);
        btn_next_->Enable(target_x < max_scroll);
    }
}

void SubnavBar::ensure_active_visible() {
    if (!scroll_ || !track_ || items_.empty()) {
        return;
    }
    const int view_w = scroll_->GetClientSize().GetWidth();
    if (view_w <= 0) {
        return;
    }
    const int max_scroll = std::max(0, content_width_px_ - view_w);
    if (max_scroll <= 0) {
        return;
    }

    const int cur_x = scroll_->GetViewStart().x;
    const int gap_x = FromDIP(4);
    int x_acc = 0;
    for (const auto& item : items_) {
        if (!item.button) continue;
        const int bw = item.button->GetMinWidth() > 0 ? item.button->GetMinWidth() : item.button->GetSize().GetWidth();
        const int b_right = x_acc + bw;
        if (item.name == current_subsection_) {
            int next_x = cur_x;
            if (x_acc < cur_x) {
                next_x = std::max(0, x_acc);
            } else if (b_right > cur_x + view_w) {
                next_x = std::min(max_scroll, b_right - view_w);
            }
            if (next_x != cur_x) {
                scroll_->Scroll(next_x, 0);
                if (btn_prev_ && btn_next_) {
                    btn_prev_->Enable(next_x > 0);
                    btn_next_->Enable(next_x < max_scroll);
                }
            }
            break;
        }
        x_acc = b_right + gap_x;
    }
}

void SubnavBar::on_button_clicked(const std::string& name) {
    current_subsection_ = name;
    update_button_styles();
    ensure_active_visible();

    if (callback_) {
        callback_(name);
    }
}

} // namespace app::frontend
