#ifndef APP_FRONTEND_LOGIN_DIALOG_HPP
#define APP_FRONTEND_LOGIN_DIALOG_HPP

#include <wx/wx.h>
#include <memory>
#include "app/contracts/account_service.hpp"

namespace app::frontend {

class LoginDialog : public wxDialog {
public:
    LoginDialog(wxWindow* parent, std::shared_ptr<contracts::IAccountService> account_service);

private:
    void on_submit(wxCommandEvent& event);

    std::shared_ptr<contracts::IAccountService> account_service_;

    wxTextCtrl* txt_username_{nullptr};
    wxTextCtrl* txt_password_{nullptr};
    wxStaticText* lbl_error_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_LOGIN_DIALOG_HPP
