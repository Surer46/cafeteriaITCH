#pragma once

#include "app/contracts/account_service.hpp"
#include "app/contracts/inventory_service.hpp"
#include "src/frontend/numeric_keypad.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/wx.h>

namespace app::frontend {

class CardPanel;
namespace ui {
class TouchComboBox;
}

class InventoryBarPanel final : public wxPanel {
public:
    InventoryBarPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IInventoryService> inventory_service,
        std::shared_ptr<contracts::IAccountService> account_service);
    ~InventoryBarPanel() override;

    void refresh_data(const std::string& keep_selection_id = "");

private:
    enum class ActionMode {
        Waste,
        Adjustment
    };

    void build_ui();
    void update_permissions_ui();
    void update_mode_ui();
    void apply_stock_filter(const std::string& keep_selection_id);
    void populate_ingredient_selector(const std::string& keep_id);
    void update_unit_choices_for_selected();
    void update_reason_counter();
    void update_empty_states();
    void update_movement_detail(long selected_row);
    void update_list_column_widths();
    void set_feedback(const std::string& message, bool is_error);

    void on_refresh(wxCommandEvent& event);
    void on_search_changed(wxCommandEvent& event);
    void on_stock_selected(wxListEvent& event);
    void on_movement_selected(wxListEvent& event);
    void on_movement_deselected(wxListEvent& event);
    void on_ingredient_changed(wxCommandEvent& event);
    void on_mode_waste(wxCommandEvent& event);
    void on_mode_adjustment(wxCommandEvent& event);
    void on_adjust_add(wxCommandEvent& event);
    void on_adjust_subtract(wxCommandEvent& event);
    void on_reason_changed(wxCommandEvent& event);
    void on_submit(wxCommandEvent& event);
    void on_size(wxSizeEvent& event);

    std::string selected_ingredient_id() const;
    std::string selected_ingredient_dimension() const;
    std::string selected_unit_code() const;

    std::shared_ptr<contracts::IInventoryService> inventory_service_;
    std::shared_ptr<contracts::IAccountService> account_service_;

    std::vector<contracts::IngredientDto> ingredients_;
    std::vector<contracts::IngredientDto> filtered_ingredients_;
    std::set<std::string> low_stock_ids_;
    std::vector<contracts::StockMovementDto> movements_;
    std::vector<std::string> current_units_;

    bool destroying_{false};
    bool can_view_{false};
    bool can_adjust_{false};
    bool stock_load_failed_{false};
    bool mov_load_failed_{false};
    bool sizing_columns_{false};
    bool sync_selection_guard_{false};
    ActionMode action_mode_{ActionMode::Waste};
    bool adjust_adds_{true};
    std::string last_stock_error_;
    std::string last_mov_error_;
    std::string last_feedback_;
    bool last_feedback_is_error_{false};

    wxPanel* pnl_no_permission_{nullptr};
    wxBoxSizer* main_body_{nullptr};

    CardPanel* stock_card_{nullptr};
    wxTextCtrl* txt_search_{nullptr};
    wxButton* btn_refresh_{nullptr};
    wxStaticText* lbl_stock_empty_{nullptr};
    wxListView* stock_list_{nullptr};

    CardPanel* mov_card_{nullptr};
    wxStaticText* lbl_mov_empty_{nullptr};
    wxListView* mov_list_{nullptr};
    wxStaticText* lbl_mov_detail_{nullptr};
    std::string last_mov_detail_;

    CardPanel* action_card_{nullptr};
    wxStaticText* lbl_no_adjust_{nullptr};
    wxScrolledWindow* form_scroll_{nullptr};
    wxBoxSizer* form_sizer_{nullptr};
    wxButton* btn_mode_waste_{nullptr};
    wxButton* btn_mode_adjust_{nullptr};
    wxPanel* pnl_adjust_dir_{nullptr};
    wxButton* btn_dir_add_{nullptr};
    wxButton* btn_dir_sub_{nullptr};
    wxChoice* ch_ingredient_{nullptr};
    ui::TouchComboBox* cb_ingredient_{nullptr};
    wxTextCtrl* txt_qty_{nullptr};
    wxChoice* ch_unit_{nullptr};
    ui::TouchComboBox* cb_unit_{nullptr};
    wxTextCtrl* txt_reason_{nullptr};
    wxStaticText* lbl_reason_count_{nullptr};
    wxButton* btn_submit_{nullptr};
    wxStaticText* lbl_feedback_{nullptr};

    ui::NumericKeypad* keypad_{nullptr};
};

} // namespace app::frontend
