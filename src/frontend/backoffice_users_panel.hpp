#ifndef APP_FRONTEND_BACKOFFICE_USERS_PANEL_HPP
#define APP_FRONTEND_BACKOFFICE_USERS_PANEL_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <memory>
#include <vector>
#include "app/contracts/account_service.hpp"

namespace app::frontend {

class BackofficeUsersPanel : public wxPanel {
public:
    BackofficeUsersPanel(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service);

    void refresh_users();

private:
    void setup_ui();
    void on_new_user(wxCommandEvent& event);
    void on_change_password(wxCommandEvent& event);
    void on_toggle_active(wxCommandEvent& event);
    void on_view_avatar(wxCommandEvent& event);
    void on_item_selected(wxListEvent& event);
    void on_item_deselected(wxListEvent& event);

    std::shared_ptr<contracts::IAccountService> account_service_;
    wxListCtrl* list_ctrl_{nullptr};
    wxButton* btn_new_user_{nullptr};
    wxButton* btn_refresh_{nullptr};
    wxButton* btn_change_pwd_{nullptr};
    wxButton* btn_toggle_active_{nullptr};
    wxButton* btn_view_avatar_{nullptr};
    wxStaticText* lbl_status_{nullptr};

    std::vector<contracts::UserDto> users_;
    long selected_index_{-1};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_USERS_PANEL_HPP
