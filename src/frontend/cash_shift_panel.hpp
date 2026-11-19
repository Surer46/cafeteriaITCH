#ifndef APP_FRONTEND_CASH_SHIFT_PANEL_HPP
#define APP_FRONTEND_CASH_SHIFT_PANEL_HPP

#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/wx.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "app/contracts/shift_service.hpp"
#include "app/services/mock_services.hpp"

#include "src/frontend/numeric_keypad.hpp"
#include "src/frontend/theme.hpp"

namespace app::frontend {

class CashShiftPanel : public wxPanel {
public:
    CashShiftPanel(
        wxWindow* parent,
        services::ServiceRegistry services,
        std::function<void()> on_shift_changed = nullptr
    );
    ~CashShiftPanel() override;

    void refresh_status();

private:
    enum class ActiveTab { Current, History };

    void setup_ui();
    void build_current_view(wxWindow* parent);
    void build_history_view(wxWindow* parent);

    void switch_tab(ActiveTab tab);
    void update_tab_styles();
    void update_movement_kind_styles();
    void update_live_difference();
    void refresh_open_preview();
    void populate_movements_list(
        wxListView* list,
        wxStaticText* empty_lbl,
        const std::vector<contracts::CashMovementDto>& movements,
        const wxString& empty_msg
    );
    void fit_list_columns(wxListView* list, const std::vector<int>& weights);
    void render_last_closed_summary();

    void apply_history_preset(int preset_id);
    void load_history();
    void load_history_detail(const std::string& shift_id);

    void on_tab_current(wxCommandEvent& evt);
    void on_tab_history(wxCommandEvent& evt);
    void on_open_shift_clicked(wxCommandEvent& evt);
    void on_move_kind_inflow(wxCommandEvent& evt);
    void on_move_kind_outflow(wxCommandEvent& evt);
    void on_register_movement(wxCommandEvent& evt);
    void on_refresh_preview(wxCommandEvent& evt);
    void on_final_cash_changed(wxCommandEvent& evt);
    void on_close_shift_clicked(wxCommandEvent& evt);

    void on_hist_preset_today(wxCommandEvent& evt);
    void on_hist_preset_week(wxCommandEvent& evt);
    void on_hist_preset_month(wxCommandEvent& evt);
    void on_hist_search(wxCommandEvent& evt);
    void on_hist_row_selected(wxListEvent& evt);
    void on_size(wxSizeEvent& evt);

    services::ServiceRegistry services_;
    std::function<void()> on_shift_changed_;

    bool destroying_{false};
    bool sizing_{false};
    ActiveTab active_tab_{ActiveTab::Current};
    contracts::CashMovementKind move_kind_{contracts::CashMovementKind::Inflow};
    bool has_open_preview_{false};
    contracts::ShiftCloseoutDto open_preview_{};
    std::optional<contracts::ShiftCloseoutDto> last_closed_summary_;
    std::vector<contracts::ShiftCloseoutDto> history_items_;

    wxButton* btn_tab_current_{nullptr};
    wxButton* btn_tab_history_{nullptr};

    // --- Vista Turno actual ---
    wxPanel* pnl_current_{nullptr};

    // Estado cerrado
    wxPanel* pnl_closed_wrap_{nullptr};
    CardPanel* status_card_{nullptr};
    StatusBadge* shift_badge_{nullptr};
    wxStaticText* lbl_details_{nullptr};
    wxTextCtrl* txt_initial_cash_{nullptr};
    wxButton* btn_open_shift_{nullptr};
    CardPanel* last_closed_card_{nullptr};
    wxStaticText* lbl_last_closed_title_{nullptr};
    wxStaticText* lbl_last_closed_body_{nullptr};
    wxPanel* pnl_last_mismatch_{nullptr};
    wxStaticText* lbl_last_mismatch_{nullptr};

    // Estado abierto
    wxPanel* pnl_open_wrap_{nullptr};
    CardPanel* open_header_card_{nullptr};
    StatusBadge* open_badge_{nullptr};
    wxStaticText* lbl_open_details_{nullptr};
    wxButton* btn_preview_refresh_{nullptr};
    wxPanel* pnl_open_mismatch_{nullptr};
    wxStaticText* lbl_open_mismatch_{nullptr};

    // Movimientos de caja (izquierda)
    CardPanel* movements_card_{nullptr};
    wxButton* btn_move_inflow_{nullptr};
    wxButton* btn_move_outflow_{nullptr};
    wxTextCtrl* txt_move_amount_{nullptr};
    wxTextCtrl* txt_move_desc_{nullptr};
    wxButton* btn_move_save_{nullptr};
    wxStaticText* lbl_move_feedback_{nullptr};
    wxStaticText* lbl_move_empty_{nullptr};
    wxListView* movements_list_{nullptr};

    // Arqueo y cierre (derecha)
    CardPanel* closeout_card_{nullptr};
    wxScrolledWindow* closeout_scroll_{nullptr};
    wxStaticText* lbl_co_initial_{nullptr};
    wxStaticText* lbl_co_sales_{nullptr};
    wxStaticText* lbl_co_methods_{nullptr};
    wxStaticText* lbl_co_flows_{nullptr};
    wxStaticText* lbl_co_expected_{nullptr};
    wxTextCtrl* txt_final_cash_{nullptr};
    wxStaticText* lbl_diff_preview_{nullptr};
    wxTextCtrl* txt_close_reason_{nullptr};
    wxStaticText* lbl_close_feedback_{nullptr};
    wxButton* btn_close_shift_{nullptr};

    // --- Vista Cortes anteriores ---
    wxPanel* pnl_history_{nullptr};
    CardPanel* hist_filter_card_{nullptr};
    wxButton* btn_hist_today_{nullptr};
    wxButton* btn_hist_week_{nullptr};
    wxButton* btn_hist_month_{nullptr};
    wxTextCtrl* txt_hist_from_{nullptr};
    wxTextCtrl* txt_hist_to_{nullptr};
    wxButton* btn_hist_search_{nullptr};
    wxPanel* pnl_hist_banner_{nullptr};
    wxStaticText* lbl_hist_banner_{nullptr};

    CardPanel* hist_list_card_{nullptr};
    wxStaticText* lbl_hist_empty_{nullptr};
    wxListView* history_list_{nullptr};

    CardPanel* hist_detail_card_{nullptr};
    wxScrolledWindow* hist_detail_scroll_{nullptr};
    wxPanel* pnl_hist_snapshot_{nullptr};
    wxStaticText* lbl_hist_snapshot_{nullptr};
    wxPanel* pnl_hist_mismatch_{nullptr};
    wxStaticText* lbl_hist_mismatch_{nullptr};
    wxStaticText* lbl_hist_detail_{nullptr};
    wxStaticText* lbl_hist_moves_empty_{nullptr};
    wxListView* hist_movements_list_{nullptr};

    ui::NumericKeypad* keypad_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_CASH_SHIFT_PANEL_HPP
