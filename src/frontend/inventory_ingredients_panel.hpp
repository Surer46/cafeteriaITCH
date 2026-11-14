#pragma once

#include "app/contracts/account_service.hpp"
#include "app/contracts/inventory_service.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <wx/listctrl.h>
#include <wx/wx.h>

namespace app::frontend {

class CardPanel;

/// Formatea una cantidad en unidad base usando aritmética entera exacta (sin flotantes).
std::string format_ingredient_quantity(std::int64_t amount_base, const std::string& unit_dimension);

/// Convierte un umbral entero capturado en gramos/mililitros/piezas a unidad base exacta.
bool parse_threshold_to_base(
    const std::string& raw_text,
    const std::string& unit_dimension,
    std::int64_t& out_base,
    std::string& out_error);

class InventoryIngredientsPanel final : public wxPanel {
public:
    InventoryIngredientsPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IInventoryService> inventory_service,
        std::shared_ptr<contracts::IAccountService> account_service);

    void refresh_data(const std::string& keep_selection_id = "");

private:
    void build_ui();
    void update_permissions();
    void load_ingredient_into_form(const contracts::IngredientDto& ing);
    void enter_create_mode();
    void update_unit_ui();
    void update_empty_state();
    void update_list_column_widths();
    void set_feedback(const std::string& message, bool is_error);

    void on_new_ingredient(wxCommandEvent& event);
    void on_refresh(wxCommandEvent& event);
    void on_list_selected(wxListEvent& event);
    void on_dimension_changed(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_size(wxSizeEvent& event);

    std::string selected_dimension_code() const;
    int dimension_choice_index(const std::string& unit_dimension) const;

    std::shared_ptr<contracts::IInventoryService> inventory_service_;
    std::shared_ptr<contracts::IAccountService> account_service_;

    std::vector<contracts::IngredientDto> ingredients_;
    std::set<std::string> low_stock_ids_;
    std::string selected_ingredient_id_;
    bool create_mode_{false};
    bool load_failed_{false};
    bool can_manage_products_{false};
    bool can_manage_thresholds_{false};
    bool sizing_columns_{false};

    wxButton* btn_new_{nullptr};
    wxButton* btn_refresh_{nullptr};
    wxPanel* pnl_warning_{nullptr};
    wxStaticText* lbl_warning_{nullptr};

    CardPanel* left_card_{nullptr};
    wxStaticText* lbl_empty_state_{nullptr};
    wxListView* list_{nullptr};

    CardPanel* right_card_{nullptr};
    wxStaticText* lbl_form_title_{nullptr};
    wxTextCtrl* txt_name_{nullptr};
    wxChoice* ch_dimension_{nullptr};
    wxPanel* pnl_dimension_ro_{nullptr};
    wxStaticText* lbl_dimension_ro_{nullptr};
    wxTextCtrl* txt_threshold_{nullptr};
    wxStaticText* lbl_threshold_unit_{nullptr};
    wxStaticText* lbl_threshold_hint_{nullptr};
    wxStaticText* lbl_stock_value_{nullptr};
    wxStaticText* lbl_stock_hint_{nullptr};
    wxButton* btn_save_{nullptr};
    wxStaticText* lbl_feedback_{nullptr};

    std::string last_list_error_;
    std::string last_feedback_;
    bool last_feedback_is_error_{false};
};

} // namespace app::frontend
