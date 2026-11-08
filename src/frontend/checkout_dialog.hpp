#ifndef APP_FRONTEND_CHECKOUT_DIALOG_HPP
#define APP_FRONTEND_CHECKOUT_DIALOG_HPP

#include <wx/wx.h>
#include <string>
#include "app/contracts/sale_service.hpp"
#include "app/contracts/money.hpp"

namespace app::frontend {

class CheckoutDialog : public wxDialog {
public:
    CheckoutDialog(
        wxWindow* parent,
        contracts::Money total
    );

    contracts::PaymentMethod get_payment_method() const { return selected_method_; }
    contracts::Money get_cash_received() const { return cash_received_; }
    contracts::Money get_change() const { return change_; }

private:
    void on_payment_method_changed(wxCommandEvent& evt);
    void on_cash_received_text_changed(wxCommandEvent& evt);
    void on_confirm(wxCommandEvent& evt);

    void update_cash_calculations();

    contracts::Money total_;
    contracts::PaymentMethod selected_method_{contracts::PaymentMethod::Efectivo};
    contracts::Money cash_received_{contracts::Money::zero()};
    contracts::Money change_{contracts::Money::zero()};
    bool cash_is_valid_{false};

    wxChoice* choice_method_{nullptr};
    wxTextCtrl* txt_cash_received_{nullptr};
    wxStaticText* lbl_change_val_{nullptr};
    wxStaticText* lbl_total_val_{nullptr};
    wxBoxSizer* cash_details_sizer_{nullptr};
    wxButton* btn_confirm_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_CHECKOUT_DIALOG_HPP
