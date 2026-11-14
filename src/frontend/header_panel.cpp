#include "src/frontend/header_panel.hpp"
#include <wx/dcbuffer.h>

namespace app::frontend {

HeaderPanel::HeaderPanel(wxWindow* parent, const std::string& app_title,
                         const std::string& shift_status, const std::string& user_name,
                         std::function<void(UiProfile)> on_profile_changed)
    : wxPanel(parent, wxID_ANY),
      app_title_(app_title),
      shift_status_(shift_status),
      user_name_(user_name),
      on_profile_changed_(std::move(on_profile_changed)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Theme::ColorSurface);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});

    auto* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    // 1. Marca / Título Cafetería ITCH II a la izquierda
    auto* brand_box = new wxBoxSizer(wxHORIZONTAL);
    title_text_ = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(app_title_.c_str()));
    title_text_->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title_text_->SetForegroundColour(Theme::ColorBrandDeep);
    brand_box->Add(title_text_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 16);

    main_sizer->Add(brand_box, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 10);
    main_sizer->AddStretchSpacer(1);

    // 2. Controles y estado a la derecha
    auto* right_box = new wxBoxSizer(wxHORIZONTAL);

    // Selector rápido de perfil (Estándar / Touch - FE-08 / FE-09)
    profile_toggle_btn_ = new wxButton(this, wxID_ANY, Theme::is_touch() ? wxT("Estándar") : wxT("Touch"));
    profile_toggle_btn_->SetToolTip(wxT("Alternar tamaño de interfaz entre Estándar y Touch"));
    Theme::setup_button_icon(profile_toggle_btn_, "touch_app", wxSize(18, 18), Theme::ColorTextSecondary);
    Theme::style_button_secondary(profile_toggle_btn_, Theme::is_touch() ? 48 : 34);
    profile_toggle_btn_->Bind(wxEVT_BUTTON, &HeaderPanel::on_toggle_profile, this);
    right_box->Add(profile_toggle_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

    // Badge de estado de caja
    bool is_open = (shift_status_.find("Abierta") != std::string::npos);
    shift_badge_ = new StatusBadge(this, wxString::FromUTF8(shift_status_.c_str()), is_open);
    right_box->Add(shift_badge_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 14);

    // Píldora de cuenta / usuario
    auto* user_box = new wxBoxSizer(wxHORIZONTAL);
    wxBitmapBundle person_icon = Theme::get_icon("person", wxSize(20, 20), Theme::ColorBrandDeep);
    if (person_icon.IsOk()) {
        user_avatar_ = new wxStaticBitmap(this, wxID_ANY, person_icon);
        user_box->Add(user_avatar_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    }

    user_text_ = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(user_name_.c_str()));
    user_text_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    user_text_->SetForegroundColour(Theme::ColorText);
    user_box->Add(user_text_, 0, wxALIGN_CENTER_VERTICAL);

    right_box->Add(user_box, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    main_sizer->Add(right_box, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 10);

    SetSizer(main_sizer);

    Bind(wxEVT_PAINT, &HeaderPanel::on_paint, this);
}

void HeaderPanel::update_status(const std::string& shift_status, const std::string& user_name) {
    shift_status_ = shift_status;
    user_name_ = user_name;

    if (shift_badge_) {
        bool is_open = (shift_status_.find("Abierta") != std::string::npos);
        shift_badge_->set_status(wxString::FromUTF8(shift_status_.c_str()), is_open);
    }
    if (user_text_) {
        user_text_->SetLabel(wxString::FromUTF8(user_name_.c_str()));
    }
    Layout();
}

void HeaderPanel::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Theme::ColorSurface));
    dc.Clear();

    wxSize sz = GetClientSize();
    // Borde inferior sutil de 1px
    dc.SetPen(wxPen(Theme::ColorBorderSubtle, 1));
    dc.DrawLine(0, sz.GetHeight() - 1, sz.GetWidth(), sz.GetHeight() - 1);
}

void HeaderPanel::sync_profile_button() {
    if (!profile_toggle_btn_) {
        return;
    }
    profile_toggle_btn_->SetLabel(Theme::is_touch() ? wxT("Estándar") : wxT("Touch"));
    Theme::setup_button_icon(profile_toggle_btn_, "touch_app", wxSize(18, 18), Theme::ColorTextSecondary);
    Theme::style_button_secondary(profile_toggle_btn_, Theme::is_touch() ? 48 : 34);
    Layout();
}

void HeaderPanel::on_toggle_profile(wxCommandEvent&) {
    UiProfile next_profile = Theme::is_touch() ? UiProfile::Standard : UiProfile::Touch;
    Theme::set_profile(next_profile);
    sync_profile_button();

    if (on_profile_changed_) {
        on_profile_changed_(next_profile);
    }
}

} // namespace app::frontend
