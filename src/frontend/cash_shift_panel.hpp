#ifndef APP_FRONTEND_CASH_SHIFT_PANEL_HPP
#define APP_FRONTEND_CASH_SHIFT_PANEL_HPP

#include <wx/wx.h>
#include <functional>
#include <memory>
#include "app/services/mock_services.hpp"

#include "src/frontend/theme.hpp"

namespace app::frontend {

class CashShiftPanel : public wxPanel {
public:
    CashShiftPanel(
        wxWindow* parent,
        services::ServiceRegistry services,
        std::function<void()> on_shift_changed = nullptr
    );

    void refresh_status();

private:
    void setup_ui();
    void on_open_shift_clicked(wxCommandEvent& evt);
    void on_close_shift_clicked(wxCommandEvent& evt);

    services::ServiceRegistry services_;
    std::function<void()> on_shift_changed_;

    wxBoxSizer* main_sizer_{nullptr};
    CardPanel* status_card_{nullptr};
    StatusBadge* shift_badge_{nullptr};
    wxStaticText* lbl_details_{nullptr};

    // Sección abrir turno
    wxBoxSizer* open_box_{nullptr};
    wxTextCtrl* txt_initial_cash_{nullptr};
    wxButton* btn_open_shift_{nullptr};

    // Sección cerrar turno
    wxBoxSizer* close_box_{nullptr};
    wxTextCtrl* txt_final_cash_{nullptr};
    wxButton* btn_close_shift_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_CASH_SHIFT_PANEL_HPP
