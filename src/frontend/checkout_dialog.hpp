#ifndef APP_FRONTEND_CHECKOUT_DIALOG_HPP
#define APP_FRONTEND_CHECKOUT_DIALOG_HPP

#include <wx/wx.h>
#include <wx/odcombo.h>
#include <memory>
#include <string>
#include "app/contracts/sale_service.hpp"
#include "app/contracts/money.hpp"
#include "src/frontend/numeric_keypad.hpp"

namespace app::frontend {

class CheckoutDialog : public wxDialog {
public:
    // T17: el total a cobrar con el cargo del método elegido lo calcula el servicio (ISaleService::preview_checkout).
    // `total` y `discount` son el neto del carrito y su descuento (los muestra mientras llega la vista previa).
    // Sin servicio no se puede cobrar: el diálogo lo avisa y el botón de confirmar queda deshabilitado.
    CheckoutDialog(
        wxWindow* parent,
        contracts::Money total,
        contracts::Money discount,
        std::shared_ptr<contracts::ISaleService> sale_service
    );

    contracts::PaymentMethod get_payment_method() const { return selected_method_; }
    contracts::Money get_cash_received() const { return cash_received_; }
    contracts::Money get_change() const { return change_; }

private:
    void on_payment_method_changed(wxCommandEvent& evt);
    void on_cash_received_text_changed(wxCommandEvent& evt);
    void on_confirm(wxCommandEvent& evt);

    void update_cash_calculations();
    void refresh_preview();
    void update_confirm_label();
    void relayout();

    contracts::Money total_;   // lo que se cobra con el método elegido (vista previa del servicio)
    contracts::Money discount_{contracts::Money::zero()};
    std::shared_ptr<contracts::ISaleService> sale_service_;
    contracts::CheckoutPreviewDto preview_;
    bool preview_ok_{false};
    int touch_wrap_width_{370};   // ancho lógico del texto dentro de la tarjeta del total
    contracts::PaymentMethod selected_method_{contracts::PaymentMethod::Efectivo};
    contracts::Money cash_received_{contracts::Money::zero()};
    contracts::Money change_{contracts::Money::zero()};
    bool cash_is_valid_{false};

    wxOwnerDrawnComboBox* choice_method_{nullptr};
    wxTextCtrl* txt_cash_received_{nullptr};
    wxStaticText* lbl_change_val_{nullptr};
    wxStaticText* lbl_total_val_{nullptr};
    wxStaticText* lbl_products_val_{nullptr};
    wxStaticText* lbl_discount_caption_{nullptr};
    wxStaticText* lbl_discount_val_{nullptr};
    wxStaticText* lbl_surcharge_caption_{nullptr};
    wxStaticText* lbl_surcharge_val_{nullptr};
    wxStaticText* lbl_preview_error_{nullptr};
    wxFlexGridSizer* breakdown_sizer_{nullptr};
    wxBoxSizer* cash_details_sizer_{nullptr};
    wxButton* btn_confirm_{nullptr};
    ui::NumericKeypad* keypad_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_CHECKOUT_DIALOG_HPP
