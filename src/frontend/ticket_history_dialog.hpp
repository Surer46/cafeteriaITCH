#ifndef APP_FRONTEND_TICKET_HISTORY_DIALOG_HPP
#define APP_FRONTEND_TICKET_HISTORY_DIALOG_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <vector>
#include <string>
#include "app/services/mock_services.hpp"

namespace app::frontend {

class TicketHistoryDialog : public wxDialog {
public:
    TicketHistoryDialog(
        wxWindow* parent,
        services::ServiceRegistry services
    );

private:
    void setup_ui();
    void load_tickets();
    void on_search(wxCommandEvent& evt);
    void on_view_reprint(wxCommandEvent& evt);
    void on_item_activated(wxListEvent& evt);

    services::ServiceRegistry services_;
    std::vector<contracts::TicketSummaryDto> tickets_;

    wxTextCtrl* txt_search_folio_{nullptr};
    wxButton* btn_search_{nullptr};
    wxListCtrl* list_tickets_{nullptr};
    wxButton* btn_reprint_{nullptr};
    wxButton* btn_close_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_TICKET_HISTORY_DIALOG_HPP
