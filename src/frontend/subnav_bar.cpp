#include "src/frontend/subnav_bar.hpp"
#include "src/frontend/theme.hpp"

namespace app::frontend {

SubnavBar::SubnavBar(wxWindow* parent, OnSubsectionChangedCallback callback)
    : wxPanel(parent, wxID_ANY), callback_(std::move(callback)) {
    SetBackgroundColour(Theme::ColorBackground);

    auto* root_sizer = new wxBoxSizer(wxHORIZONTAL);
    buttons_sizer_ = new wxBoxSizer(wxHORIZONTAL);
    root_sizer->Add(buttons_sizer_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, Theme::get_spacing_md());
    SetSizer(root_sizer);

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
            sub_names = {"Usuarios y cuentas", "Roles y permisos", "Configuración", "Respaldos"};
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

    int btn_h = FromDIP(Theme::is_touch() ? 48 : 36);

    for (const auto& name : sub_names) {
        auto* btn = new wxButton(this, wxID_ANY, wxString::FromUTF8(name.c_str()),
                                 wxDefaultPosition, wxSize(-1, btn_h));

        btn->Bind(wxEVT_BUTTON, [this, name](wxCommandEvent&) {
            on_button_clicked(name);
        });

        buttons_sizer_->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP | wxBOTTOM, 6);
        items_.push_back({btn, name});
    }

    buttons_sizer_->AddStretchSpacer(1);
    update_button_styles();
    Layout();
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
        item.button->Refresh();
    }
}

void SubnavBar::on_button_clicked(const std::string& name) {
    current_subsection_ = name;
    update_button_styles();

    if (callback_) {
        callback_(name);
    }
}

} // namespace app::frontend
