#pragma once

#include "app/contracts/account_service.hpp"
#include "app/contracts/inventory_service.hpp"
#include "src/frontend/numeric_keypad.hpp"

#include <cstdint>
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

/// Opciones de unidad de pantalla compatibles con la dimensión del ingrediente ("g"/"kg", "ml"/"l", "piezas").
std::vector<std::string> compatible_units_for_dimension(const std::string& unit_dimension);

/// Traduce una unidad devuelta por el contrato a etiqueta legible en pantalla.
std::string friendly_unit_label(const std::string& unit_code);

/// Deduce la dimensión ("MASS", "VOLUME", "COUNT") a partir de la unidad base del contrato.
std::string dimension_from_base_unit(const std::string& base_unit);

/// Parsea un entero positivo estricto desde texto de entrada (sin punto decimal ni signo negativo).
bool parse_positive_int64_input(const std::string& raw, std::int64_t& out_value, std::string& out_error);

class InventoryRestockPanel final : public wxPanel {
public:
    InventoryRestockPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IInventoryService> inventory_service,
        std::shared_ptr<contracts::IAccountService> account_service);
    ~InventoryRestockPanel() override;

    void refresh_data();

private:
    void build_ui();
    void update_permissions_ui();
    void populate_ingredient_selector(const std::string& keep_id);
    void update_unit_choices_for_selected();
    void update_empty_state();
    void update_list_column_widths();
    void set_feedback(const std::string& message, bool is_error);

    void on_refresh(wxCommandEvent& event);
    void on_ingredient_changed(wxCommandEvent& event);
    void on_register_purchase(wxCommandEvent& event);
    void on_size(wxSizeEvent& event);

    std::string selected_ingredient_id() const;
    std::string selected_ingredient_dimension() const;
    std::string selected_unit_code() const;

    std::shared_ptr<contracts::IInventoryService> inventory_service_;
    std::shared_ptr<contracts::IAccountService> account_service_;

    std::vector<contracts::IngredientDto> ingredients_;
    std::vector<contracts::PurchaseEntryRecordDto> entries_;
    std::vector<std::string> current_units_;

    bool destroying_{false};
    bool can_restock_{false};
    bool can_view_purchases_{false};
    bool list_load_failed_{false};
    bool sizing_columns_{false};
    std::string last_list_error_;
    std::string last_feedback_;
    bool last_feedback_is_error_{false};

    wxPanel* pnl_no_permission_{nullptr};
    wxBoxSizer* main_body_{nullptr};

    CardPanel* form_card_{nullptr};
    wxChoice* ch_ingredient_{nullptr};
    ui::TouchComboBox* cb_ingredient_{nullptr};
    wxTextCtrl* txt_qty_{nullptr};
    wxChoice* ch_unit_{nullptr};
    ui::TouchComboBox* cb_unit_{nullptr};
    wxTextCtrl* txt_cost_{nullptr};
    wxButton* btn_register_{nullptr};
    wxStaticText* lbl_feedback_{nullptr};

    CardPanel* list_card_{nullptr};
    wxPanel* pnl_readonly_notice_{nullptr};
    wxButton* btn_refresh_{nullptr};
    wxStaticText* lbl_empty_state_{nullptr};
    wxListView* list_{nullptr};

    ui::NumericKeypad* keypad_{nullptr};
};

} // namespace app::frontend
