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

    BackofficeSettingsPanel(
        wxWindow* parent,
        services::ServiceRegistry services,
        OnProfileSelectedCallback on_profile_changed = nullptr
    );

    void sync_profile_selection();

private:
    void setup_ui();
    void update_scroll_layout();
    void on_select_profile(UiProfile profile);

    services::ServiceRegistry services_;
    OnProfileSelectedCallback on_profile_changed_;

    wxScrolledWindow* scroll_{nullptr};
    wxBoxSizer* scroll_sizer_{nullptr};
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

    BackofficePrintConfigPanel* print_panel_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_SETTINGS_PANEL_HPP
