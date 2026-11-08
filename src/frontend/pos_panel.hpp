#ifndef APP_FRONTEND_POS_PANEL_HPP
#define APP_FRONTEND_POS_PANEL_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/odcombo.h>
#include <vector>
#include <string>
#include "app/services/mock_services.hpp"

namespace app::frontend {

class PosPanel : public wxPanel {
public:
    PosPanel(
        wxWindow* parent,
        services::ServiceRegistry services
    );

    void refresh_cart();
    void update_shift_display();

private:
    void setup_ui();
    void load_catalog_products(const std::string& query = "");
    void on_search(wxCommandEvent& evt);
    void on_add_item(wxCommandEvent& evt);
    void on_remove_selected_item(wxCommandEvent& evt);
    void on_edit_quantity(wxCommandEvent& evt);
    void on_edit_notes(wxCommandEvent& evt);
    void on_open_ticket_history(wxCommandEvent& evt);
    void update_contextual_actions();
    void on_more_actions(wxCommandEvent& evt);
    void on_item_activated(wxListEvent& evt);
    void on_cancel_order(wxCommandEvent& evt);
    void on_checkout(wxCommandEvent& evt);

    services::ServiceRegistry services_;
    std::vector<contracts::ProductDto> loaded_products_;
    std::vector<contracts::CartItemDto> current_cart_items_;
    std::string active_idempotency_key_;

    wxTextCtrl* txt_search_{nullptr};
    wxButton* btn_search_{nullptr};
    wxOwnerDrawnComboBox* choice_products_{nullptr};
    wxButton* btn_qty_minus_{nullptr};
    wxTextCtrl* txt_qty_{nullptr};
    wxButton* btn_qty_plus_{nullptr};
    wxTextCtrl* txt_notes_{nullptr};
    wxButton* btn_add_{nullptr};

    wxListCtrl* list_items_{nullptr};
    wxButton* btn_remove_item_{nullptr};
    wxButton* btn_edit_qty_{nullptr};
    wxButton* btn_edit_notes_{nullptr};
    wxButton* btn_more_actions_{nullptr};

    wxStaticText* lbl_items_count_{nullptr};
    wxStaticText* lbl_subtotal_val_{nullptr};
    wxStaticText* lbl_discount_val_{nullptr};
    wxStaticText* lbl_total_val_{nullptr};
    wxButton* btn_checkout_{nullptr};
    wxButton* btn_cancel_order_{nullptr};
    wxStaticText* lbl_shift_status_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_POS_PANEL_HPP
