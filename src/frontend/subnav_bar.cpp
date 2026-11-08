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
    buttons_.clear();

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

    int btn_h = Theme::is_touch() ? 44 : 36;

    for (const auto& name : sub_names) {
        auto* btn = new wxButton(this, wxID_ANY, wxString::FromUTF8(name.c_str()),
                                 wxDefaultPosition, wxSize(-1, btn_h));
        
        bool is_active = (name == current_subsection_);
        if (is_active) {
            btn->SetBackgroundColour(Theme::ColorSurface);
            btn->SetForegroundColour(Theme::ColorBrand);
            btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        } else {
            btn->SetBackgroundColour(Theme::ColorBorderSubtle);
            btn->SetForegroundColour(Theme::ColorTextSecondary);
            btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        }

        btn->Bind(wxEVT_BUTTON, [this, name](wxCommandEvent&) {
            on_button_clicked(name);
        });

        buttons_sizer_->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxTOP | wxBOTTOM, 6);
        buttons_.push_back(btn);
    }

    buttons_sizer_->AddStretchSpacer(1);
    Layout();
}

void SubnavBar::on_button_clicked(const std::string& name) {
    current_subsection_ = name;
    for (auto* btn : buttons_) {
        bool is_active = (btn->GetLabel().ToStdString() == name);
        if (is_active) {
            btn->SetBackgroundColour(Theme::ColorSurface);
            btn->SetForegroundColour(Theme::ColorBrand);
            btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        } else {
            btn->SetBackgroundColour(Theme::ColorBorderSubtle);
            btn->SetForegroundColour(Theme::ColorTextSecondary);
            btn->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        }
        btn->Refresh();
    }

    if (callback_) {
        callback_(name);
    }
}

} // namespace app::frontend
