#ifndef APP_FRONTEND_REFUND_DIALOG_HPP
#define APP_FRONTEND_REFUND_DIALOG_HPP

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <string>
#include <vector>
#include "app/services/mock_services.hpp"
#include "src/frontend/numeric_keypad.hpp"

namespace app::frontend {

// T17: reembolso de una venta. La interfaz no calcula dinero: el importe a devolver, el método y los mensajes salen de
// IRefundService::preview_refund (en vivo) y de refund_sale (al confirmar).
class RefundDialog : public wxDialog {
public:
    RefundDialog(wxWindow* parent, services::ServiceRegistry services, const std::string& sale_folio);

    // true si el reembolso se registró (la lista de tickets debe recargarse)
    bool was_refunded() const { return refunded_; }
    const std::string& refund_folio() const { return refund_folio_; }

private:
    struct LineRow {
        std::string sale_item_id;
        std::int64_t refundable{0};
        bool has_prepared_parts{false};
        wxTextCtrl* qty{nullptr};
        wxButton* minus{nullptr};
        wxButton* plus{nullptr};
        wxCheckBox* prepared{nullptr};
    };

    void build_ui();
    void build_blocked_ui(const std::string& message);
    void set_qty(std::size_t index, std::int64_t value);
    std::int64_t qty_of(std::size_t index) const;
    contracts::RefundRequestDto build_request() const;
    void update_preview();
    void on_confirm(wxCommandEvent& evt);
    void show_error(const wxString& text);

    services::ServiceRegistry services_;
    std::string folio_;
    contracts::SaleDetailDto detail_;
    std::vector<LineRow> rows_;
    std::string idempotency_key_;   // se genera UNA vez al abrir el diálogo: un doble clic o un reintento no duplica
    bool refunding_{false};
    bool refunded_{false};
    bool preview_ok_{false};
    bool reason_attempted_{false};
    std::string refund_folio_;
    std::string preview_reason_error_;

    wxScrolledWindow* scroll_{nullptr};
    wxTextCtrl* txt_reason_{nullptr};
    wxStaticText* lbl_amount_{nullptr};
    wxStaticText* lbl_method_{nullptr};
    wxStaticText* lbl_surcharge_note_{nullptr};
    wxStaticText* lbl_error_{nullptr};
    wxButton* btn_confirm_{nullptr};
    ui::NumericKeypad* keypad_{nullptr};
    int wrap_width_{520};
};

} // namespace app::frontend

#endif // APP_FRONTEND_REFUND_DIALOG_HPP
