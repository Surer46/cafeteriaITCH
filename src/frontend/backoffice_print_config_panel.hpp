#ifndef APP_FRONTEND_BACKOFFICE_PRINT_CONFIG_PANEL_HPP
#define APP_FRONTEND_BACKOFFICE_PRINT_CONFIG_PANEL_HPP

#include <wx/wx.h>
#include <memory>
#include <vector>
#include <string>
#include "app/services/mock_services.hpp"
#include "app/contracts/print_config_service.hpp"

namespace app::frontend {

class BackofficePrintConfigPanel : public wxPanel {
public:
    BackofficePrintConfigPanel(
        wxWindow* parent,
        services::ServiceRegistry services
    );

private:
    void setup_ui();
    void load_current_config();
    void update_preview();
    void on_profile_selected(wxCommandEvent& evt);
    void on_save_clicked(wxCommandEvent& evt);

    services::ServiceRegistry services_;
    contracts::PrinterStationConfig current_config_;

    wxChoice* choice_profile_{nullptr};
    wxChoice* choice_printer_{nullptr};
    std::vector<std::string> installed_printers_;
    wxTextCtrl* txt_preview_{nullptr};
    wxButton* btn_save_{nullptr};
    wxStaticText* lbl_status_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_PRINT_CONFIG_PANEL_HPP
