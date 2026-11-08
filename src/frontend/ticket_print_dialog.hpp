#ifndef APP_FRONTEND_TICKET_PRINT_DIALOG_HPP
#define APP_FRONTEND_TICKET_PRINT_DIALOG_HPP

#include <wx/wx.h>
#include <string>
#include "app/contracts/sale_service.hpp"
#include "app/contracts/print_config_service.hpp"

namespace app::frontend {

class TicketPrintDialog : public wxDialog {
public:
    TicketPrintDialog(
        wxWindow* parent,
        const contracts::TicketDto& ticket,
        const contracts::PrinterStationConfig& print_config = contracts::PrinterStationConfig{}
    );

    bool was_printed() const { return printed_; }

private:
    void on_print(wxCommandEvent& evt);
    void on_not_now(wxCommandEvent& evt);

    contracts::TicketDto ticket_;
    contracts::PrinterStationConfig config_;
    bool printed_{false};
};

} // namespace app::frontend

#endif // APP_FRONTEND_TICKET_PRINT_DIALOG_HPP
