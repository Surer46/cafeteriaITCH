#ifndef APP_FRONTEND_BACKOFFICE_SETTINGS_PANEL_HPP
#define APP_FRONTEND_BACKOFFICE_SETTINGS_PANEL_HPP

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <functional>
#include "app/services/mock_services.hpp"
#include "src/frontend/theme.hpp"

namespace app::frontend {

class BackofficePrintConfigPanel;

class BackofficeSettingsPanel : public wxPanel {
public:
    using OnProfileSelectedCallback = std::function<void(UiProfile)>;
    using OnFullscreenChangedCallback = std::function<void(bool)>;

    BackofficeSettingsPanel(
        wxWindow* parent,
        services::ServiceRegistry services,
        OnProfileSelectedCallback on_profile_changed = nullptr,
        OnFullscreenChangedCallback on_fullscreen_changed = nullptr
    );

    void sync_profile_selection();
    void sync_fullscreen_selection();

private:
    void setup_ui();
    void update_scroll_layout();
    // T17: tarjeta «Cobro con tarjeta (Mercado Pago)» (solo con config.manage)
    void build_surcharge_card();
    void load_surcharge_config();
    bool parse_surcharge_inputs(std::int64_t& percent_hundredths, std::int64_t& fixed_cents, wxString& error) const;
    void update_surcharge_example();
    void on_save_surcharge(wxCommandEvent& evt);
    void set_surcharge_message(const wxString& text, const wxColour& color);
    void wrap_surcharge_texts();
    void on_select_profile(UiProfile profile);
    void on_toggle_fullscreen();

    services::ServiceRegistry services_;
    OnProfileSelectedCallback on_profile_changed_;
    OnFullscreenChangedCallback on_fullscreen_changed_;

    wxScrolledWindow* scroll_{nullptr};
    wxBoxSizer* scroll_sizer_{nullptr};
    wxBoxSizer* content_row_{nullptr};
    wxBoxSizer* options_sizer_{nullptr};
    CardPanel* screen_card_{nullptr};
    bool wide_layout_{false};
    bool layout_initialized_{false};
    wxStaticText* lbl_subtitle_{nullptr};
    wxStaticText* lbl_note_{nullptr};

    CardPanel* card_standard_{nullptr};
    wxButton* btn_standard_{nullptr};
    wxStaticText* lbl_standard_status_{nullptr};
    wxStaticText* lbl_standard_desc_{nullptr};

    CardPanel* card_touch_{nullptr};
    wxButton* btn_touch_{nullptr};
    wxStaticText* lbl_touch_status_{nullptr};
    wxStaticText* lbl_touch_desc_{nullptr};

    CardPanel* card_fullscreen_{nullptr};
    wxButton* btn_fullscreen_{nullptr};
    wxStaticText* lbl_fullscreen_status_{nullptr};
    wxStaticText* lbl_fullscreen_desc_{nullptr};

    BackofficePrintConfigPanel* print_panel_{nullptr};

    // T17: cargo por pago con tarjeta
    wxBoxSizer* left_col_{nullptr};   // tarjeta de pantalla + (con config.manage) tarjeta del cargo por tarjeta
    CardPanel* surcharge_card_{nullptr};
    wxStaticText* lbl_surcharge_sub_{nullptr};
    wxTextCtrl* txt_surcharge_pct_{nullptr};
    wxTextCtrl* txt_surcharge_fixed_{nullptr};
    wxButton* btn_surcharge_save_{nullptr};
    wxStaticText* lbl_surcharge_example_{nullptr};
    wxStaticText* lbl_surcharge_msg_{nullptr};
    wxString surcharge_example_raw_;
    wxString surcharge_msg_raw_;
    int surcharge_wrap_w_{300};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_SETTINGS_PANEL_HPP
