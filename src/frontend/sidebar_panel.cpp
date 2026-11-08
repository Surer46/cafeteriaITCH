#include "src/frontend/sidebar_panel.hpp"
#include "src/frontend/theme.hpp"

namespace app::frontend {

SidebarPanel::SidebarPanel(wxWindow* parent, OnSectionChangedCallback callback)
    : wxPanel(parent, wxID_ANY), callback_(std::move(callback)) {
    SetBackgroundColour(Theme::ColorBrandDeep);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddSpacer(14);

    int btn_h = Theme::is_touch() ? 56 : 46;
    int btn_w = Theme::is_touch() ? 220 : 190;

    auto make_btn = [this, btn_h, btn_w](const wxString& label) {
        auto* btn = new wxButton(this, wxID_ANY, label, wxDefaultPosition, wxSize(btn_w, btn_h));
        btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        return btn;
    };

    btn_pos_ = make_btn(wxT("Punto de Venta"));
    btn_inventory_ = make_btn(wxT("Inventario"));
    btn_cash_ = make_btn(wxT("Caja y Reportes"));
    btn_backoffice_ = make_btn(wxT("BackOffice"));

    btn_pos_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_button_click(SectionId::PointOfSale); });
    btn_inventory_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_button_click(SectionId::Inventory); });
    btn_cash_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_button_click(SectionId::CashAndReports); });
    btn_backoffice_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { on_button_click(SectionId::BackOffice); });

    int pad = Theme::get_spacing_sm();
    sizer->Add(btn_pos_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    sizer->Add(btn_inventory_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    sizer->Add(btn_cash_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    sizer->Add(btn_backoffice_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);

    sizer->AddStretchSpacer(1);

    SetSizer(sizer);
    update_button_styles();
}

void SidebarPanel::on_button_click(SectionId section) {
    current_section_ = section;
    update_button_styles();
    if (callback_) {
        callback_(section);
    }
}

void SidebarPanel::set_active_section(SectionId section) {
    current_section_ = section;
    update_button_styles();
}

void SidebarPanel::refresh_profile() {
    int btn_h = Theme::is_touch() ? 56 : 46;
    int btn_w = Theme::is_touch() ? 220 : 190;
    wxSize sz(btn_w, btn_h);
    for (auto* btn : {btn_pos_, btn_inventory_, btn_cash_, btn_backoffice_}) {
        if (btn) {
            btn->SetMinSize(sz);
            btn->SetSize(sz);
        }
    }
    update_button_styles();
    Layout();
}

void SidebarPanel::update_button_styles() {
    auto apply_style = [](wxButton* btn, const std::string& icon_name, bool active) {
        if (!btn) return;
        wxSize icon_sz(20, 20);
        if (active) {
            btn->SetBackgroundColour(Theme::ColorSurface);
            btn->SetForegroundColour(Theme::ColorBrandDeep);
            btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
            Theme::setup_button_icon(btn, icon_name, icon_sz, Theme::ColorBrandDeep);
        } else {
            // Fondo ligeramente más oscuro que el fondo del sidebar
            btn->SetBackgroundColour(wxColour(0x7F, 0x1D, 0x1D));
            btn->SetForegroundColour(wxColour(0xFE, 0xE2, 0xE2));
            btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_NORMAL));
            Theme::setup_button_icon(btn, icon_name, icon_sz, wxColour(0xFE, 0xE2, 0xE2));
        }
        btn->Refresh();
    };

    apply_style(btn_pos_, "point_of_sale", current_section_ == SectionId::PointOfSale);
    apply_style(btn_inventory_, "inventory_2", current_section_ == SectionId::Inventory);
    apply_style(btn_cash_, "account_balance_wallet", current_section_ == SectionId::CashAndReports);
    apply_style(btn_backoffice_, "manage_accounts", current_section_ == SectionId::BackOffice);
}

} // namespace app::frontend
