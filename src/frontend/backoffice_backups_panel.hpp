#ifndef APP_FRONTEND_BACKOFFICE_BACKUPS_PANEL_HPP
#define APP_FRONTEND_BACKOFFICE_BACKUPS_PANEL_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/weakref.h>
#include <string>
#include <vector>
#include "app/services/mock_services.hpp"
#include "src/frontend/numeric_keypad.hpp"
#include "src/frontend/theme.hpp"
#include "src/frontend/toggle_switch.hpp"

namespace app::frontend {

/**
 * @brief Panel BackOffice > Respaldos para usuario final (persona mayor).
 *
 * Permite consultar el estado del último respaldo, activar o desactivar el respaldo
 * automático al cerrar turno, guardar un respaldo manual en carpeta/USB, verificar y
 * restaurar desde un respaldo previo con doble confirmación explícita, y configurar
 * la limpieza de copias antiguas dentro de la carpeta interna de respaldos.
 */
class BackofficeBackupsPanel final : public wxPanel {
public:
    BackofficeBackupsPanel(wxWindow* parent, services::ServiceRegistry services);

private:
    enum class FeedbackKind {
        Info,
        Success,
        Error
    };

    struct BusyScope {
        explicit BusyScope(BackofficeBackupsPanel& owner);
        ~BusyScope();
        BackofficeBackupsPanel& owner_;
        wxWindowRef prev_focus_;
        wxBusyCursor cursor_;
    };

    void setup_ui();
    void setup_access_denied_ui();
    void update_scroll_layout();
    void refresh_all();
    void refresh_status();
    void load_retention_policy();
    void reset_cleanup_preview();
    void auto_size_cleanup_columns();
    void sync_action_states();
    void show_feedback(FeedbackKind kind, const wxString& message);
    void hide_feedback();

    // Acciones de usuario
    void on_toggle_auto_backup(wxCommandEvent& evt);
    void on_backup_now(wxCommandEvent& evt);
    void on_restore_backup(wxCommandEvent& evt);
    void on_save_policy(wxCommandEvent& evt);
    void on_preview_cleanup(wxCommandEvent& evt);
    void on_execute_cleanup(wxCommandEvent& evt);
    void on_policy_field_edited(wxCommandEvent& evt);

    // Utilidades de formato local (dinero/bytes/fechas sin punto flotante innecesario)
    static wxString format_human_datetime(const std::string& raw);
    static wxString format_megabytes(std::uint64_t bytes);

    services::ServiceRegistry services_;
    bool can_manage_{false};
    bool can_restore_{false};
    bool op_busy_{false};
    bool loading_policy_{false};
    bool syncing_toggle_{false};
    bool preview_ready_{false};
    std::size_t preview_delete_count_{0};
    std::uint64_t preview_bytes_freed_{0};
    wxString last_backup_dir_;
    wxString last_error_text_;
    wxString cleanup_summary_text_;

    wxScrolledWindow* scroll_{nullptr};
    wxBoxSizer* scroll_sizer_{nullptr};

    // Banner de retroalimentación claro
    wxPanel* pnl_feedback_{nullptr};
    wxStaticText* lbl_feedback_{nullptr};

    // Tarjeta 1: Estado y automático
    CardPanel* card_status_{nullptr};
    wxStaticText* lbl_last_date_{nullptr};
    wxStaticText* lbl_last_result_{nullptr};
    wxPanel* pnl_last_error_{nullptr};
    wxStaticText* lbl_last_error_{nullptr};
    ToggleSwitch* toggle_auto_backup_{nullptr};
    wxStaticText* lbl_auto_state_{nullptr};
    wxStaticText* lbl_auto_desc_{nullptr};

    // Tarjeta 2: Respaldo manual
    CardPanel* card_manual_{nullptr};
    wxStaticText* lbl_manual_desc_{nullptr};
    wxButton* btn_backup_now_{nullptr};

    // Tarjeta 3: Restaurar
    CardPanel* card_restore_{nullptr};
    wxStaticText* lbl_restore_desc_{nullptr};
    wxStaticText* lbl_restore_restricted_{nullptr};
    wxButton* btn_restore_{nullptr};

    // Tarjeta 4: Limpieza de respaldos antiguos
    CardPanel* card_cleanup_{nullptr};
    wxStaticText* lbl_cleanup_desc_{nullptr};
    wxWindow* box_keep_last_{nullptr};
    wxTextCtrl* txt_keep_last_{nullptr};
    wxWindow* box_keep_days_{nullptr};
    wxTextCtrl* txt_keep_days_{nullptr};
    wxButton* btn_save_policy_{nullptr};
    wxButton* btn_preview_cleanup_{nullptr};
    wxButton* btn_execute_cleanup_{nullptr};
    wxStaticText* lbl_cleanup_summary_{nullptr};
    wxListCtrl* list_cleanup_{nullptr};

    ui::NumericKeypad* keypad_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_BACKUPS_PANEL_HPP
