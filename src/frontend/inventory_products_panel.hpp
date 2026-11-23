#ifndef APP_FRONTEND_INVENTORY_PRODUCTS_PANEL_HPP
#define APP_FRONTEND_INVENTORY_PRODUCTS_PANEL_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "app/contracts/account_service.hpp"
#include "app/contracts/catalog_service.hpp"
#include "app/contracts/inventory_service.hpp"
#include "src/frontend/numeric_keypad.hpp"

namespace app::frontend {

namespace ui {
class TouchComboBox;
}

class InventoryProductsPanel final : public wxPanel {
public:
    InventoryProductsPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::ICatalogService> catalog_service,
        std::shared_ptr<contracts::IInventoryService> inventory_service,
        std::shared_ptr<contracts::IAccountService> account_service
    );
    ~InventoryProductsPanel() override;

private:
    struct RecipeDraftRow {
        std::string ingredient_id;
        std::string ingredient_name;
        std::string unit_dimension;
        std::string base_unit;
        std::int64_t quantity_base_units{0};
    };

    void build_ui();
    void refresh_all(const std::string& keep_product_id = "");
    void reload_product_list(const std::string& keep_product_id = "");
    void reload_categories();
    void reload_ingredients_catalog();

    void update_permissions_ui();
    void update_empty_state();
    void update_list_column_widths();
    void update_recipe_column_widths();
    void update_type_buttons_ui();
    void update_recipe_unit_choices();
    void update_detail_scroll_layout();
    void apply_responsive_layout();
    void sync_category_input_visibility(bool focus_new_input = false);
    bool is_new_category_selected() const;
    std::string resolve_selected_category(std::string& out_error) const;

    void start_new_product();
    void load_product_into_form(const std::string& product_id);
    void load_image_preview_from_bytes(const std::vector<std::uint8_t>& bytes, const wxString& status_text);
    void clear_image_preview(const wxString& status_text);
    void reload_recipe_and_costing();
    void rebuild_recipe_list_ui(const std::string& keep_ing_id = "");

    void set_feedback(const std::string& message, bool is_error);
    void set_recipe_feedback(const std::string& message, bool is_error);

    void on_new_product(wxCommandEvent& event);
    void on_refresh(wxCommandEvent& event);
    void on_search_changed(wxCommandEvent& event);
    void on_filter_category_changed(wxCommandEvent& event);
    void on_toggle_inactive(wxCommandEvent& event);
    void on_product_selected(wxListEvent& event);

    void on_category_combo_changed(wxCommandEvent& event);
    void on_type_prepared(wxCommandEvent& event);
    void on_type_direct(wxCommandEvent& event);
    void on_enable_direct_stock(wxCommandEvent& event);
    void on_pick_image(wxCommandEvent& event);
    void on_remove_image(wxCommandEvent& event);
    void on_save_product(wxCommandEvent& event);
    void on_toggle_active_product(wxCommandEvent& event);

    void on_recipe_ing_changed(wxCommandEvent& event);
    void on_recipe_row_selected(wxListEvent& event);
    void on_recipe_add_or_update(wxCommandEvent& event);
    void on_recipe_remove_item(wxCommandEvent& event);
    void on_recipe_save(wxCommandEvent& event);
    void on_size(wxSizeEvent& event);

    std::string selected_filter_category() const;
    std::string selected_recipe_ingredient_id() const;
    std::string selected_recipe_ingredient_dimension() const;
    std::string selected_recipe_unit_code() const;

    std::shared_ptr<contracts::ICatalogService> catalog_service_;
    std::shared_ptr<contracts::IInventoryService> inventory_service_;
    std::shared_ptr<contracts::IAccountService> account_service_;

    std::vector<contracts::ProductDto> products_;
    std::vector<std::string> categories_;
    std::vector<contracts::IngredientDto> ingredients_;
    std::vector<std::string> recipe_units_;
    std::vector<RecipeDraftRow> recipe_rows_;

    bool destroying_{false};
    bool can_manage_{false};
    bool include_inactive_{false};
    bool list_load_failed_{false};
    bool sizing_columns_{false};
    bool sync_selection_guard_{false};
    bool is_prepared_selected_{false};
    bool editing_is_active_{true};
    bool editing_has_image_{false};
    bool editing_stock_tracked_{false};
    std::int64_t editing_stock_units_{0};
    std::string editing_product_id_;
    std::string last_list_error_;
    std::string last_feedback_;
    bool last_feedback_is_error_{false};
    std::string last_recipe_feedback_;
    bool last_recipe_feedback_is_error_{false};
    std::string last_cost_text_;
    std::string last_margin_text_;
    std::string last_direct_stock_status_;
    std::string last_direct_stock_hint_;
    std::vector<std::uint8_t> pending_image_bytes_;
    std::string pending_image_mime_;
    bool wide_split_active_{false};
    bool np_inline_active_{false};
    bool sku_bc_horizontal_{true};
    bool cat_type_horizontal_{true};
    bool cat_type_labels_inline_{false};
    bool img_inline_active_{false};
    bool costing_horizontal_{false};

    wxPanel* pnl_no_permission_{nullptr};
    wxBoxSizer* main_body_{nullptr};

    wxPanel* list_card_{nullptr};
    wxButton* btn_new_{nullptr};
    wxButton* btn_refresh_{nullptr};
    wxTextCtrl* txt_search_{nullptr};
    wxChoice* ch_filter_cat_{nullptr};
    ui::TouchComboBox* cb_filter_cat_{nullptr};
    wxButton* btn_toggle_inactive_{nullptr};
    wxStaticText* lbl_list_empty_{nullptr};
    wxListView* list_{nullptr};

    wxPanel* detail_card_{nullptr};
    wxScrolledWindow* detail_scroll_{nullptr};
    wxBoxSizer* detail_sizer_{nullptr};
    wxBoxSizer* col_name_sizer_{nullptr};
    wxBoxSizer* col_price_sizer_{nullptr};
    wxStaticText* lbl_name_{nullptr};
    wxStaticText* lbl_price_{nullptr};
    wxWindow* name_box_{nullptr};
    wxWindow* price_box_{nullptr};
    wxBoxSizer* row_sb_sizer_{nullptr};
    wxBoxSizer* col_sku_sizer_{nullptr};
    wxBoxSizer* col_bc_sizer_{nullptr};
    wxStaticText* lbl_sku_{nullptr};
    wxStaticText* lbl_bc_{nullptr};
    wxBoxSizer* cat_type_sizer_{nullptr};
    wxBoxSizer* col_cat_sizer_{nullptr};
    wxBoxSizer* col_type_sizer_{nullptr};
    wxStaticText* lbl_cat_{nullptr};
    wxBoxSizer* type_btns_sizer_{nullptr};
    wxWindow* cat_box_{nullptr};
    wxBoxSizer* img_row_sizer_{nullptr};
    wxBoxSizer* img_right_sizer_{nullptr};
    wxStaticText* lbl_type_{nullptr};
    wxStaticText* lbl_detail_title_{nullptr};
    wxTextCtrl* txt_name_{nullptr};
    wxTextCtrl* txt_price_{nullptr};
    wxTextCtrl* txt_sku_{nullptr};
    wxTextCtrl* txt_barcode_{nullptr};
    wxChoice* ch_category_{nullptr};
    ui::TouchComboBox* cb_category_{nullptr};
    wxTextCtrl* txt_category_{nullptr};
    wxButton* btn_type_prepared_{nullptr};
    wxButton* btn_type_direct_{nullptr};

    wxPanel* pnl_direct_stock_{nullptr};
    wxCheckBox* chk_track_stock_{nullptr};
    wxStaticText* lbl_direct_stock_status_{nullptr};
    wxStaticText* lbl_direct_stock_hint_{nullptr};
    wxButton* btn_enable_stock_{nullptr};

    wxStaticBitmap* bmp_thumb_{nullptr};
    wxStaticText* lbl_image_status_{nullptr};
    wxButton* btn_pick_image_{nullptr};
    wxButton* btn_remove_image_{nullptr};

    wxButton* btn_save_{nullptr};
    wxButton* btn_toggle_active_{nullptr};
    wxStaticText* lbl_feedback_{nullptr};

    wxPanel* pnl_recipe_{nullptr};
    wxBoxSizer* recipe_sizer_{nullptr};
    wxStaticText* lbl_recipe_new_hint_{nullptr};
    wxPanel* pnl_recipe_editor_{nullptr};
    wxListView* recipe_list_{nullptr};
    wxChoice* ch_recipe_ing_{nullptr};
    ui::TouchComboBox* cb_recipe_ing_{nullptr};
    wxTextCtrl* txt_recipe_qty_{nullptr};
    wxChoice* ch_recipe_unit_{nullptr};
    ui::TouchComboBox* cb_recipe_unit_{nullptr};
    wxButton* btn_recipe_add_{nullptr};
    wxButton* btn_recipe_remove_{nullptr};
    wxButton* btn_recipe_save_{nullptr};
    wxStaticText* lbl_recipe_feedback_{nullptr};
    wxPanel* pnl_costing_{nullptr};
    wxBoxSizer* cost_sizer_{nullptr};
    wxStaticText* lbl_cost_{nullptr};
    wxStaticText* lbl_margin_{nullptr};

    ui::NumericKeypad* keypad_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_INVENTORY_PRODUCTS_PANEL_HPP
