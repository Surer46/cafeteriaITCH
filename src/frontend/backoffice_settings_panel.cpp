#include "src/frontend/backoffice_settings_panel.hpp"
#include "src/frontend/backoffice_print_config_panel.hpp"
#include <algorithm>

namespace app::frontend {

BackofficeSettingsPanel::BackofficeSettingsPanel(
    wxWindow* parent,
    services::ServiceRegistry services,
    OnProfileSelectedCallback on_profile_changed,
    OnFullscreenChangedCallback on_fullscreen_changed
) : wxPanel(parent, wxID_ANY),
    services_(std::move(services)),
    on_profile_changed_(std::move(on_profile_changed)),
    on_fullscreen_changed_(std::move(on_fullscreen_changed)) {
    SetBackgroundColour(Theme::ColorBackground);
    setup_ui();
    sync_profile_selection();
}

void BackofficeSettingsPanel::setup_ui() {
    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL);
    scroll_->SetBackgroundColour(Theme::ColorBackground);
    scroll_->SetScrollRate(FromDIP(16), FromDIP(16));

    scroll_sizer_ = new wxBoxSizer(wxVERTICAL);

    // 1. Tarjeta "Pantalla de este equipo"
    auto* screen_card = new CardPanel(scroll_);
    screen_card->set_card_background(Theme::ColorSurface);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_title = new wxStaticText(screen_card, wxID_ANY, wxT("Pantalla de este equipo"));
    lbl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    card_sizer->Add(lbl_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    lbl_subtitle_ = new wxStaticText(
        screen_card,
        wxID_ANY,
        wxT("Elija el tamaño de botones y letras más cómodo para trabajar en esta computadora.")
    );
    lbl_subtitle_->SetFont(Theme::get_font_body());
    lbl_subtitle_->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_subtitle_, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(16));

    auto* options_sizer = new wxBoxSizer(wxHORIZONTAL);
    const int btn_h = FromDIP(Theme::is_touch() ? 56 : 48);

    auto build_option_tile = [this, screen_card, btn_h](
                                 CardPanel*& out_card,
                                 wxButton*& out_btn,
                                 wxStaticText*& out_status,
                                 wxStaticText*& out_desc,
                                 const wxString& initial_btn_label,
                                 const wxString& desc_text,
                                 UiProfile target_profile) {
        out_card = new CardPanel(screen_card);
        auto* tile_sizer = new wxBoxSizer(wxVERTICAL);

        out_btn = new wxButton(out_card, wxID_ANY, initial_btn_label);
        out_btn->SetMinSize(wxSize(-1, btn_h));
        out_btn->Bind(wxEVT_BUTTON, [this, target_profile](wxCommandEvent&) {
            on_select_profile(target_profile);
        });
        tile_sizer->Add(out_btn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

        out_status = new wxStaticText(out_card, wxID_ANY, wxEmptyString);
        out_status->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        tile_sizer->Add(out_status, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

        out_desc = new wxStaticText(out_card, wxID_ANY, desc_text);
        out_desc->SetFont(Theme::get_font_body());
        out_desc->SetForegroundColour(Theme::ColorTextSecondary);
        tile_sizer->Add(out_desc, 0, wxALL | wxEXPAND, FromDIP(14));

        out_card->SetSizer(tile_sizer);
        return out_card;
    };

    build_option_tile(
        card_standard_,
        btn_standard_,
        lbl_standard_status_,
        lbl_standard_desc_,
        wxT("Estándar"),
        wxT("Para usar con ratón y teclado."),
        UiProfile::Standard
    );
    options_sizer->Add(card_standard_, 1, wxEXPAND | wxRIGHT, FromDIP(12));

    build_option_tile(
        card_touch_,
        btn_touch_,
        lbl_touch_status_,
        lbl_touch_desc_,
        wxT("Táctil"),
        wxT("Para pantallas que se tocan con el dedo."),
        UiProfile::Touch
    );
    options_sizer->Add(card_touch_, 1, wxEXPAND);

    card_sizer->Add(options_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    // Opción "Pantalla completa" debajo de Estándar / Táctil
    card_fullscreen_ = new CardPanel(screen_card);
    auto* fs_tile_sizer = new wxBoxSizer(wxVERTICAL);

    auto* fs_top_row = new wxBoxSizer(wxHORIZONTAL);
    btn_fullscreen_ = new wxButton(card_fullscreen_, wxID_ANY, wxT("Usar pantalla completa"));
    btn_fullscreen_->SetMinSize(wxSize(-1, btn_h));
    btn_fullscreen_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        on_toggle_fullscreen();
    });
    fs_top_row->Add(btn_fullscreen_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(14));

    lbl_fullscreen_status_ = new wxStaticText(card_fullscreen_, wxID_ANY, wxEmptyString);
    lbl_fullscreen_status_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    fs_top_row->Add(lbl_fullscreen_status_, 1, wxALIGN_CENTER_VERTICAL);
    fs_tile_sizer->Add(fs_top_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    lbl_fullscreen_desc_ = new wxStaticText(
        card_fullscreen_,
        wxID_ANY,
        wxT("Ocupa toda la pantalla y oculta la barra de Windows. También con la tecla F11.")
    );
    lbl_fullscreen_desc_->SetFont(Theme::get_font_body());
    lbl_fullscreen_desc_->SetForegroundColour(Theme::ColorTextSecondary);
    fs_tile_sizer->Add(lbl_fullscreen_desc_, 0, wxALL | wxEXPAND, FromDIP(14));

    card_fullscreen_->SetSizer(fs_tile_sizer);
    card_sizer->Add(card_fullscreen_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    lbl_note_ = new wxStaticText(
        screen_card,
        wxID_ANY,
        wxT("Este cambio solo afecta a este equipo y se recuerda al reiniciar.")
    );
    lbl_note_->SetFont(Theme::get_font_body());
    lbl_note_->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_note_, 0, wxALL | wxEXPAND, FromDIP(16));

    screen_card->SetSizer(card_sizer);

    // Mismo margen lateral que BackofficePrintConfigPanel (16) para alinear las dos tarjetas.
    scroll_sizer_->Add(screen_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    // 2. Pantalla de configuración de impresora existente, incrustada tal cual
    print_panel_ = new BackofficePrintConfigPanel(scroll_, services_);
    scroll_sizer_->Add(print_panel_, 0, wxEXPAND);

    scroll_->SetSizer(scroll_sizer_);
    scroll_->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        update_scroll_layout();
    });

    root_sizer->Add(scroll_, 1, wxEXPAND);
    SetSizer(root_sizer);
    update_scroll_layout();
}

void BackofficeSettingsPanel::sync_profile_selection() {
    const bool touch = Theme::is_touch();
    const int btn_h_logical = touch ? 56 : 48;

    auto apply_tile = [this, btn_h_logical](
                          CardPanel* card,
                          wxButton* btn,
                          wxStaticText* status,
                          const wxString& base_label,
                          const std::string& inactive_icon,
                          bool active) {
        if (card) {
            card->set_card_background(active ? Theme::ColorBrandSoft : Theme::ColorSurfaceSubtle);
            card->set_card_border(active ? Theme::ColorBrand : Theme::ColorBorder, active ? 2 : 1);
            card->Refresh();
        }
        if (btn) {
            if (active) {
                btn->SetLabel(base_label + wxT(" (Activa)"));
                Theme::style_button_primary(btn, btn_h_logical);
                Theme::setup_button_icon(btn, "check_circle", wxSize(20, 20), Theme::ColorSurface);
            } else {
                btn->SetLabel(base_label);
                Theme::style_button_secondary(btn, btn_h_logical);
                Theme::setup_button_icon(btn, inactive_icon, wxSize(20, 20), Theme::ColorTextSecondary);
            }
        }
        if (status) {
            status->SetLabel(active ? wxT("Opción activa ahora") : wxT("Disponible"));
            status->SetForegroundColour(active ? Theme::ColorBrandDeep : Theme::ColorTextSecondary);
        }
    };

    apply_tile(card_standard_, btn_standard_, lbl_standard_status_, wxT("Estándar"), "manage_accounts", !touch);
    apply_tile(card_touch_, btn_touch_, lbl_touch_status_, wxT("Táctil"), "touch_app", touch);

    sync_fullscreen_selection();
}

void BackofficeSettingsPanel::sync_fullscreen_selection() {
    const bool full = Theme::get_fullscreen();
    const int btn_h_logical = Theme::is_touch() ? 56 : 48;

    if (card_fullscreen_) {
        card_fullscreen_->set_card_background(full ? Theme::ColorBrandSoft : Theme::ColorSurfaceSubtle);
        card_fullscreen_->set_card_border(full ? Theme::ColorBrand : Theme::ColorBorder, full ? 2 : 1);
        card_fullscreen_->Refresh();
    }
    if (btn_fullscreen_) {
        if (full) {
            btn_fullscreen_->SetLabel(wxT("Salir de pantalla completa"));
            Theme::style_button_primary(btn_fullscreen_, btn_h_logical);
            Theme::setup_button_icon(btn_fullscreen_, "check_circle", wxSize(20, 20), Theme::ColorSurface);
        } else {
            btn_fullscreen_->SetLabel(wxT("Usar pantalla completa"));
            Theme::style_button_secondary(btn_fullscreen_, btn_h_logical);
            btn_fullscreen_->SetBitmap(wxNullBitmap);
        }
        Theme::ensure_button_min_size(btn_fullscreen_, 240, btn_h_logical);
    }
    if (lbl_fullscreen_status_) {
        lbl_fullscreen_status_->SetLabel(full ? wxT("Opción activa ahora") : wxT("Desactivada (ventana normal)"));
        lbl_fullscreen_status_->SetForegroundColour(full ? Theme::ColorBrandDeep : Theme::ColorTextSecondary);
    }

    update_scroll_layout();
}

void BackofficeSettingsPanel::update_scroll_layout() {
    if (!scroll_ || !scroll_sizer_) {
        return;
    }
    const int client_w = scroll_->GetClientSize().GetWidth();
    if (client_w > FromDIP(240)) {
        const int card_wrap = std::max(FromDIP(180), client_w - FromDIP(64));
        if (lbl_subtitle_) {
            lbl_subtitle_->SetLabel(
                wxT("Elija el tamaño de botones y letras más cómodo para trabajar en esta computadora.")
            );
            lbl_subtitle_->Wrap(card_wrap);
        }
        if (lbl_note_) {
            lbl_note_->SetLabel(wxT("Este cambio solo afecta a este equipo y se recuerda al reiniciar."));
            lbl_note_->Wrap(card_wrap);
        }
        const int tile_wrap = std::max(FromDIP(140), (card_wrap - FromDIP(68)) / 2);
        if (lbl_standard_desc_) {
            lbl_standard_desc_->SetLabel(wxT("Para usar con ratón y teclado."));
            lbl_standard_desc_->Wrap(tile_wrap);
        }
        if (lbl_touch_desc_) {
            lbl_touch_desc_->SetLabel(wxT("Para pantallas que se tocan con el dedo."));
            lbl_touch_desc_->Wrap(tile_wrap);
        }
        if (lbl_fullscreen_desc_) {
            lbl_fullscreen_desc_->SetLabel(
                wxT("Ocupa toda la pantalla y oculta la barra de Windows. También con la tecla F11.")
            );
            lbl_fullscreen_desc_->Wrap(std::max(FromDIP(160), card_wrap - FromDIP(28)));
        }
    }

    const wxSize min_sz = scroll_sizer_->GetMinSize();
    const wxSize client_sz = scroll_->GetClientSize();
    const int virt_w = std::max(client_sz.GetWidth(), min_sz.GetWidth());
    const int virt_h = std::max(client_sz.GetHeight(), min_sz.GetHeight());
    scroll_->SetVirtualSize(min_sz.GetWidth(), min_sz.GetHeight());
    scroll_sizer_->SetDimension(scroll_->CalcScrolledPosition(wxPoint(0, 0)), wxSize(virt_w, virt_h));
}

void BackofficeSettingsPanel::on_select_profile(UiProfile profile) {
    if (Theme::get_profile() == profile) {
        return;
    }
    Theme::set_profile(profile);
    sync_profile_selection();
    if (on_profile_changed_) {
        on_profile_changed_(profile);
    }
}

void BackofficeSettingsPanel::on_toggle_fullscreen() {
    const bool next_state = !Theme::get_fullscreen();
    if (on_fullscreen_changed_) {
        on_fullscreen_changed_(next_state);
    } else {
        Theme::save_station_fullscreen(next_state);
        sync_fullscreen_selection();
    }
}

} // namespace app::frontend
