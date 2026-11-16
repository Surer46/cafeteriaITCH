#ifndef APP_FRONTEND_UPDATE_BANNER_PANEL_HPP
#define APP_FRONTEND_UPDATE_BANNER_PANEL_HPP

#include <wx/wx.h>
#include <functional>
#include <string>
#include "app/contracts/update_service.hpp"

namespace app::frontend {

/**
 * @brief Franja discreta bajo el encabezado principal para avisos de actualización.
 *
 * Nunca interrumpe el punto de venta ni abre diálogos al iniciar; solo se muestra
 * a cuentas con permiso de actualizar.
 */
class UpdateBannerPanel final : public wxPanel {
public:
    UpdateBannerPanel(
        wxWindow* parent,
        std::function<void()> on_view_notes,
        std::function<void()> on_dismiss_available,
        std::function<void()> on_cancel_ready,
        std::function<void()> on_ack_result
    );

    void show_install_result(
        const contracts::UpdateInstallResultDto& result,
        const std::string& installed_version
    );
    void show_status(const contracts::UpdateStatusDto& status, bool dismissed_this_session);
    void clear_banner();
    void refresh_profile();

private:
    enum class Mode {
        Hidden,
        InstallResult,
        Available,
        Downloading,
        Ready
    };

    void setup_ui();
    void apply_layout_refresh();
    static int banner_button_height();

    std::function<void()> on_view_notes_;
    std::function<void()> on_dismiss_available_;
    std::function<void()> on_cancel_ready_;
    std::function<void()> on_ack_result_;

    Mode mode_{Mode::Hidden};
    wxStaticText* lbl_message_{nullptr};
    wxButton* btn_primary_{nullptr};
    wxButton* btn_secondary_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_UPDATE_BANNER_PANEL_HPP
