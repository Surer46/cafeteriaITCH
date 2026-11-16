#ifndef APP_FRONTEND_UPDATE_NOTES_DIALOG_HPP
#define APP_FRONTEND_UPDATE_NOTES_DIALOG_HPP

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include <functional>
#include "app/contracts/update_service.hpp"
#include "src/frontend/theme.hpp"

namespace app::frontend {

/**
 * @brief Ventana no modal "Novedades de la versión X.Y.Z".
 *
 * Muestra notas, tamaño en MB, progreso de descarga sin bloquear la ventana
 * principal y botones claros para descargar, cancelar o posponer.
 */
class UpdateNotesDialog final : public wxDialog {
public:
    UpdateNotesDialog(
        wxWindow* parent,
        std::shared_ptr<contracts::IUpdateService> update_service,
        std::function<void()> on_state_changed,
        std::function<void()> on_closed
    );
    ~UpdateNotesDialog() override;

    void refresh_status();
    void refresh_profile();

private:
    void setup_ui();
    void apply_status(const contracts::UpdateStatusDto& st);
    void rebuild_notes(const contracts::UpdateStatusDto& st);
    void on_poll_timer(wxTimerEvent& evt);
    void on_primary_action(wxCommandEvent& evt);
    void on_secondary_action(wxCommandEvent& evt);
    void on_close_window(wxCloseEvent& evt);

    static wxString format_size_mb(std::int64_t bytes);

    std::shared_ptr<contracts::IUpdateService> update_service_;
    std::function<void()> on_state_changed_;
    std::function<void()> on_closed_;

    bool destroying_{false};
    contracts::UpdateStatusDto last_status_{};
    wxTimer poll_timer_;

    wxStaticText* lbl_heading_{nullptr};
    wxStaticText* lbl_size_{nullptr};
    wxScrolledWindow* notes_scroll_{nullptr};
    wxBoxSizer* notes_sizer_{nullptr};
    wxStaticText* lbl_install_note_{nullptr};

    CardPanel* card_progress_{nullptr};
    wxGauge* gauge_progress_{nullptr};
    wxStaticText* lbl_progress_{nullptr};
    wxPanel* pnl_error_{nullptr};
    wxStaticText* lbl_error_{nullptr};

    wxButton* btn_primary_{nullptr};
    wxButton* btn_secondary_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_UPDATE_NOTES_DIALOG_HPP
