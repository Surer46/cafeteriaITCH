#pragma once

#include "app/contracts/account_service.hpp"
#include "app/contracts/catalog_service.hpp"
#include "app/contracts/promotion_service.hpp"
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

class InventoryPromotionsPanel final : public wxPanel {
public:
    InventoryPromotionsPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IPromotionService> promotion_service,
        std::shared_ptr<contracts::ICatalogService> catalog_service,
        std::shared_ptr<contracts::IAccountService> account_service);
    ~InventoryPromotionsPanel() override;

    void refresh_all();

private:
    enum class ActiveTab {
        Promotions,
        Combos
    };

    enum class PromoKind {
        Percent,
        Fixed,
        Nxm
    };

    enum class PromoScope {
        Product,
        Category
    };

    struct ComboDraftPart {
        std::string product_id;
        std::string product_name;
        std::int64_t quantity{1};
    };

    void build_ui();
    void build_promotions_tab(wxWindow* parent);
    void build_combos_tab(wxWindow* parent);

    void update_permissions_ui();
    void update_tab_ui();
    void update_promo_kind_ui();
    void update_promo_scope_ui();
    void update_promo_detail_scroll();
    void update_combo_detail_scroll();
    void apply_responsive_layout();
    void sync_combo_category_visibility(bool focus_new_input = false);
    bool is_combo_new_category_selected() const;
    std::string resolve_combo_category(std::string& out_error) const;

    void reload_catalog_sources();
    void reload_promotions(const std::string& keep_id = "");
    void apply_promo_list_filter(const std::string& keep_id = "");
    void update_promo_list_columns();
    void update_promo_products_list_ui();
    void update_promo_products_columns();
    void start_new_promotion();
    void load_promotion_into_form(const std::string& promo_id);
    void set_promo_feedback(const std::string& msg, bool is_error);

    void reload_combos(const std::string& keep_id = "");
    void apply_combo_list_filter(const std::string& keep_id = "");
    void update_combo_list_columns();
    void rebuild_combo_parts_ui(const std::string& keep_prod_id = "");
    void update_combo_parts_columns();
    void reload_combo_costing();
    void start_new_combo();
    void load_combo_into_form(const std::string& combo_id);
    void set_combo_feedback(const std::string& msg, bool is_error);

    void on_tab_promos(wxCommandEvent& event);
    void on_tab_combos(wxCommandEvent& event);

    void on_promo_new(wxCommandEvent& event);
    void on_promo_refresh(wxCommandEvent& event);
    void on_promo_search(wxCommandEvent& event);
    void on_promo_only_active(wxCommandEvent& event);
    void on_promo_row_selected(wxListEvent& event);
    void on_promo_kind_percent(wxCommandEvent& event);
    void on_promo_kind_fixed(wxCommandEvent& event);
    void on_promo_kind_nxm(wxCommandEvent& event);
    void on_promo_scope_product(wxCommandEvent& event);
    void on_promo_scope_category(wxCommandEvent& event);
    void on_promo_product_left_down(wxMouseEvent& event);
    void on_promo_product_activated(wxListEvent& event);
    void on_promo_save(wxCommandEvent& event);
    void on_promo_toggle_active(wxCommandEvent& event);

    void on_combo_new(wxCommandEvent& event);
    void on_combo_refresh(wxCommandEvent& event);
    void on_combo_search(wxCommandEvent& event);
    void on_combo_toggle_inactive(wxCommandEvent& event);
    void on_combo_row_selected(wxListEvent& event);
    void on_combo_cat_chosen(wxCommandEvent& event);
    void on_combo_part_row_selected(wxListEvent& event);
    void on_combo_part_add(wxCommandEvent& event);
    void on_combo_part_remove(wxCommandEvent& event);
    void on_combo_save(wxCommandEvent& event);
    void on_combo_toggle_active(wxCommandEvent& event);

    void on_size(wxSizeEvent& event);

    std::string selected_promo_category() const;
    std::string selected_combo_part_product_id() const;

    std::shared_ptr<contracts::IPromotionService> promotion_service_;
    std::shared_ptr<contracts::ICatalogService> catalog_service_;
    std::shared_ptr<contracts::IAccountService> account_service_;

    std::vector<contracts::ProductDto> non_combo_products_;
    std::vector<std::string> categories_;

    std::vector<contracts::PromotionDto> all_promos_;
    std::vector<contracts::PromotionDto> filtered_promos_;
    std::set<std::string> selected_promo_product_ids_;

    std::vector<contracts::ComboDto> all_combos_;
    std::vector<contracts::ComboDto> filtered_combos_;
    std::vector<ComboDraftPart> combo_parts_;

    bool destroying_{false};
    bool can_manage_promos_{false};
    bool can_manage_products_{false};
    bool sizing_columns_{false};
    bool sync_selection_guard_{false};

    ActiveTab active_tab_{ActiveTab::Promotions};

    // Estado Promociones
    bool promo_only_active_{false};
    bool promo_list_failed_{false};
    std::string promo_list_error_;
    std::string editing_promo_id_;
    bool editing_promo_active_{true};
    PromoKind promo_kind_{PromoKind::Percent};
    PromoScope promo_scope_{PromoScope::Product};
    std::string last_promo_feedback_;
    bool last_promo_feedback_err_{false};
    std::string last_promo_row_detail_;

    // Estado Combos
    bool combo_include_inactive_{false};
    bool combo_list_failed_{false};
    std::string combo_list_error_;
    std::string editing_combo_id_;
    bool editing_combo_active_{true};
    std::string last_combo_feedback_;
    bool last_combo_feedback_err_{false};
    std::string last_combo_cost_text_;
    std::string last_combo_margin_text_;

    bool wide_split_active_{false};
    bool promo_name_inline_{false};
    bool promo_kind_inline_{false};
    bool promo_val_inline_{false};
    bool promo_scope_inline_{false};
    bool promo_prio_inline_{false};
    bool promo_vig_4col_{false};
    bool promo_days_inline_{false};
    bool combo_sku_cat_horizontal_{false};

    wxPanel* pnl_no_permission_{nullptr};
    wxBoxSizer* root_sizer_{nullptr};
    wxPanel* pnl_main_{nullptr};
    wxButton* btn_tab_promos_{nullptr};
    wxButton* btn_tab_combos_{nullptr};

    // Controles Promociones
    wxPanel* pnl_promos_{nullptr};
    wxBoxSizer* promo_split_{nullptr};
    CardPanel* promo_list_card_{nullptr};
    wxButton* btn_promo_new_{nullptr};
    wxButton* btn_promo_refresh_{nullptr};
    wxTextCtrl* txt_promo_search_{nullptr};
    wxButton* btn_promo_only_active_{nullptr};
    wxStaticText* lbl_promo_empty_{nullptr};
    wxListView* promo_list_{nullptr};
    wxStaticText* lbl_promo_row_detail_{nullptr};

    CardPanel* promo_detail_card_{nullptr};
    wxScrolledWindow* promo_detail_scroll_{nullptr};
    wxBoxSizer* promo_detail_sizer_{nullptr};
    wxStaticText* lbl_promo_detail_title_{nullptr};
    wxBoxSizer* promo_name_sizer_{nullptr};
    wxStaticText* lbl_promo_name_{nullptr};
    wxWindow* pname_box_{nullptr};
    wxTextCtrl* txt_promo_name_{nullptr};
    wxBoxSizer* promo_kind_outer_{nullptr};
    wxStaticText* lbl_kind_{nullptr};
    wxSizer* kind_wrap_{nullptr};
    wxButton* btn_kind_percent_{nullptr};
    wxButton* btn_kind_fixed_{nullptr};
    wxButton* btn_kind_nxm_{nullptr};

    wxPanel* pnl_kind_percent_{nullptr};
    wxBoxSizer* kp_sizer_{nullptr};
    wxStaticText* lbl_pct_{nullptr};
    wxWindow* pct_box_{nullptr};
    wxTextCtrl* txt_promo_percent_{nullptr};
    wxPanel* pnl_kind_fixed_{nullptr};
    wxBoxSizer* kf_sizer_{nullptr};
    wxStaticText* lbl_fix_{nullptr};
    wxWindow* fix_box_{nullptr};
    wxTextCtrl* txt_promo_fixed_{nullptr};
    wxPanel* pnl_kind_nxm_{nullptr};
    wxBoxSizer* col_buy_sizer_{nullptr};
    wxBoxSizer* col_pay_sizer_{nullptr};
    wxStaticText* lbl_buy_{nullptr};
    wxStaticText* lbl_pay_{nullptr};
    wxWindow* buy_box_{nullptr};
    wxWindow* pay_box_{nullptr};
    wxTextCtrl* txt_promo_buy_{nullptr};
    wxTextCtrl* txt_promo_pay_{nullptr};

    wxBoxSizer* promo_scope_outer_{nullptr};
    wxStaticText* lbl_scope_{nullptr};
    wxBoxSizer* scope_row_sizer_{nullptr};
    wxButton* btn_scope_product_{nullptr};
    wxButton* btn_scope_category_{nullptr};
    wxPanel* pnl_scope_product_{nullptr};
    wxStaticText* lbl_sp_hint_{nullptr};
    wxListView* promo_products_list_{nullptr};
    wxPanel* pnl_scope_category_{nullptr};
    wxChoice* ch_promo_cat_{nullptr};
    ui::TouchComboBox* cb_promo_cat_{nullptr};

    wxBoxSizer* promo_prio_sizer_{nullptr};
    wxStaticText* lbl_prio_{nullptr};
    wxWindow* prio_box_{nullptr};
    wxTextCtrl* txt_promo_priority_{nullptr};
    wxStaticText* lbl_priority_hint_{nullptr};
    wxFlexGridSizer* vig_grid_{nullptr};
    wxStaticText* lbl_df_{nullptr};
    wxStaticText* lbl_dt_{nullptr};
    wxStaticText* lbl_tf_{nullptr};
    wxStaticText* lbl_tt_{nullptr};
    wxTextCtrl* txt_promo_valid_from_{nullptr};
    wxTextCtrl* txt_promo_valid_to_{nullptr};
    wxBoxSizer* promo_days_outer_{nullptr};
    wxStaticText* lbl_days_{nullptr};
    wxSizer* days_wrap_{nullptr};
    wxCheckBox* chk_weekdays_[7]{nullptr};
    wxTextCtrl* txt_promo_time_from_{nullptr};
    wxTextCtrl* txt_promo_time_to_{nullptr};

    wxButton* btn_promo_save_{nullptr};
    wxButton* btn_promo_toggle_active_{nullptr};
    wxStaticText* lbl_promo_feedback_{nullptr};

    // Controles Combos
    wxPanel* pnl_combos_{nullptr};
    wxBoxSizer* combo_split_{nullptr};
    CardPanel* combo_list_card_{nullptr};
    wxButton* btn_combo_new_{nullptr};
    wxButton* btn_combo_refresh_{nullptr};
    wxTextCtrl* txt_combo_search_{nullptr};
    wxButton* btn_combo_inactive_{nullptr};
    wxStaticText* lbl_combo_empty_{nullptr};
    wxListView* combo_list_{nullptr};

    CardPanel* combo_detail_card_{nullptr};
    wxScrolledWindow* combo_detail_scroll_{nullptr};
    wxBoxSizer* combo_detail_sizer_{nullptr};
    wxStaticText* lbl_combo_detail_title_{nullptr};
    wxTextCtrl* txt_combo_name_{nullptr};
    wxTextCtrl* txt_combo_price_{nullptr};
    wxBoxSizer* combo_sku_cat_sizer_{nullptr};
    wxBoxSizer* combo_col_sku_{nullptr};
    wxBoxSizer* combo_col_cat_{nullptr};
    wxStaticText* lbl_combo_sku_{nullptr};
    wxTextCtrl* txt_combo_sku_{nullptr};
    wxChoice* ch_combo_cat_{nullptr};
    ui::TouchComboBox* cb_combo_cat_{nullptr};
    wxWindow* combo_cat_box_{nullptr};
    wxTextCtrl* txt_combo_category_{nullptr};

    wxListView* combo_parts_list_{nullptr};
    wxChoice* ch_combo_part_prod_{nullptr};
    ui::TouchComboBox* cb_combo_part_prod_{nullptr};
    wxTextCtrl* txt_combo_part_qty_{nullptr};
    wxButton* btn_combo_part_add_{nullptr};
    wxButton* btn_combo_part_remove_{nullptr};

    wxButton* btn_combo_save_{nullptr};
    wxButton* btn_combo_toggle_active_{nullptr};
    wxStaticText* lbl_combo_feedback_{nullptr};

    wxPanel* pnl_combo_costing_{nullptr};
    wxStaticText* lbl_combo_cost_{nullptr};
    wxStaticText* lbl_combo_margin_{nullptr};

    ui::NumericKeypad* keypad_{nullptr};
};

} // namespace app::frontend
