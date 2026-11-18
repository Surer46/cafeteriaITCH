#pragma once

#include "app/contracts/account_service.hpp"
#include "app/contracts/catalog_service.hpp"
#include "app/contracts/inventory_service.hpp"

#include <memory>
#include <string>
#include <vector>

#include <wx/listctrl.h>
#include <wx/wx.h>

namespace app::frontend {

class CardPanel;
namespace ui {
class TouchComboBox;
}

class InventoryStockPanel final : public wxPanel {
public:
    InventoryStockPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IInventoryService> inventory_service,
        std::shared_ptr<contracts::ICatalogService> catalog_service,
        std::shared_ptr<contracts::IAccountService> account_service);
    ~InventoryStockPanel() override;

    void refresh_data();

private:
    struct StockEntryRow {
        std::string name;
        std::string type_label;       // "Ingrediente", "Directo", "Preparado", "Combo"
        std::string stock_label;      // g/kg, ml/l, piezas, N porciones o "Sin control de existencia"
        std::string status_label;     // "Suficiente", "Poco", "Agotado", "Sin control"
        std::string last_movement;    // last_movement_at o "—"
        bool is_low_or_out{false};
        bool is_out{false};
        bool is_low{false};
    };

    void build_ui();
    void update_permissions_ui();
    void apply_filters();
    void update_empty_state();
    void update_list_column_widths();
    void update_detail_label();

    void on_refresh(wxCommandEvent& event);
    void on_search_changed(wxCommandEvent& event);
    void on_type_filter_changed(wxCommandEvent& event);
    void on_toggle_low_only(wxCommandEvent& event);
    void on_row_selected(wxListEvent& event);
    void on_row_deselected(wxListEvent& event);
    void on_size(wxSizeEvent& event);

    std::string selected_type_filter() const;

    std::shared_ptr<contracts::IInventoryService> inventory_service_;
    std::shared_ptr<contracts::ICatalogService> catalog_service_;
    std::shared_ptr<contracts::IAccountService> account_service_;

    std::vector<StockEntryRow> all_rows_;
    std::vector<StockEntryRow> filtered_rows_;

    bool destroying_{false};
    bool can_view_{false};
    bool load_failed_{false};
    bool only_low_or_out_{false};
    bool sizing_columns_{false};
    std::string last_error_;
    std::string last_detail_;

    wxPanel* pnl_no_permission_{nullptr};
    CardPanel* main_card_{nullptr};

    wxButton* btn_refresh_{nullptr};
    wxTextCtrl* txt_search_{nullptr};
    wxChoice* ch_type_{nullptr};
    ui::TouchComboBox* cb_type_{nullptr};
    wxButton* btn_low_only_{nullptr};
    wxStaticText* lbl_count_{nullptr};
    wxStaticText* lbl_empty_{nullptr};
    wxListView* list_{nullptr};
    wxStaticText* lbl_detail_{nullptr};
};

} // namespace app::frontend
