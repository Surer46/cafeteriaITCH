#ifndef APP_FRONTEND_BACKOFFICE_UPDATES_PANEL_HPP
#define APP_FRONTEND_BACKOFFICE_UPDATES_PANEL_HPP

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <functional>
#include "app/contracts/update_service.hpp"
#include "app/services/mock_services.hpp"
#include "src/frontend/theme.hpp"

namespace app::frontend {

/**
 * @brief Panel BackOffice > Actualizaciones para el Dueño.
 *
 * Permite consultar la versión instalada y la última comprobación, buscar
 * actualizaciones manualmente, revisar novedades o cancelar aceptación,
 * y cargar una actualización desde archivos locales (USB).
 */
class BackofficeUpdatesPanel final : public wxPanel {
public:
    BackofficeUpdatesPanel(
        wxWindow* parent,
        services::ServiceRegistry services,
        std::function<void()> on_open_notes,
        std::function<void()> on_state_changed
    );
    ~BackofficeUpdatesPanel() override;

    void refresh_status();

private:
    enum class FeedbackKind {
        Info,
        Success,
        Error
    };

    void setup_ui();
    void setup_access_denied_ui();
    void update_scroll_layout();
    void apply_status(const contracts::UpdateStatusDto& st);
    void show_feedback(FeedbackKind kind, const wxString& message);
    void hide_feedback();

    void on_poll_timer(wxTimerEvent& evt);
    void on_check_now(wxCommandEvent& evt);
    void on_open_notes(wxCommandEvent& evt);
    void on_accept_or_cancel(wxCommandEvent& evt);
    void on_usb_update(wxCommandEvent& evt);

    static wxString format_size_mb(std::int64_t bytes);

    services::ServiceRegistry services_;
    std::function<void()> on_open_notes_;
    std::function<void()> on_state_changed_;
    bool can_manage_{false};
    bool destroying_{false};
    contracts::UpdateStatusDto last_status_{};
    wxTimer poll_timer_;

    wxScrolledWindow* scroll_{nullptr};
    wxBoxSizer* scroll_sizer_{nullptr};

    wxPanel* pnl_feedback_{nullptr};
    wxStaticText* lbl_feedback_{nullptr};

    CardPanel* card_status_{nullptr};
    wxStaticText* lbl_installed_ver_{nullptr};
    wxStaticText* lbl_last_check_{nullptr};
    wxStaticText* lbl_check_result_{nullptr};
    wxPanel* pnl_info_{nullptr};
    wxStaticText* lbl_info_{nullptr};
    wxPanel* pnl_error_{nullptr};
    wxStaticText* lbl_error_{nullptr};
    wxButton* btn_check_now_{nullptr};

    CardPanel* card_available_{nullptr};
    wxStaticText* lbl_avail_title_{nullptr};
    wxStaticText* lbl_avail_summary_{nullptr};
    wxGauge* gauge_dl_{nullptr};
    wxPanel* pnl_dl_error_{nullptr};
    wxStaticText* lbl_dl_error_{nullptr};
    wxButton* btn_view_notes_{nullptr};
    wxButton* btn_avail_action_{nullptr};

    CardPanel* card_usb_{nullptr};
    wxStaticText* lbl_usb_desc_{nullptr};
    wxButton* btn_usb_update_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_UPDATES_PANEL_HPP
