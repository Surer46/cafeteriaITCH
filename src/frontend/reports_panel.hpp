#pragma once

#include "app/contracts/account_service.hpp"
#include "app/contracts/report_service.hpp"

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <memory>
#include <string>
#include <vector>

namespace app::frontend {

class CardPanel;
namespace ui {
class TouchComboBox;
}

class ReportsPanel final : public wxPanel {
public:
    ReportsPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IReportService> report_service,
        std::shared_ptr<contracts::IAccountService> account_service
    );
    ~ReportsPanel() override;

    void refresh_report();

private:
    void build_ui();
    void update_permissions_ui();
    void apply_preset(int preset_id);
    contracts::ReportKind current_kind() const;
    std::string suggested_csv_filename(const std::string& from, const std::string& to) const;

    void setup_columns(const std::vector<std::string>& headers, std::vector<int> weights);
    void append_row(const std::vector<std::string>& cells, bool is_total_row = false);
    void fit_columns();
    void clear_banners();
    void show_error(const std::string& msg);
    void show_mismatch(std::int64_t mismatch_cents);
    void show_truncated(std::size_t shown_count);
    void show_export_success(const contracts::ReportExportResultDto& res);

    void on_kind_changed(wxCommandEvent& evt);
    void on_preset_today(wxCommandEvent& evt);
    void on_preset_yesterday(wxCommandEvent& evt);
    void on_preset_week(wxCommandEvent& evt);
    void on_preset_month(wxCommandEvent& evt);
    void on_preset_last_month(wxCommandEvent& evt);
    void on_query_clicked(wxCommandEvent& evt);
    void on_export_clicked(wxCommandEvent& evt);
    void on_row_selected(wxListEvent& evt);
    void on_size(wxSizeEvent& evt);

    std::shared_ptr<contracts::IReportService> report_service_;
    std::shared_ptr<contracts::IAccountService> account_service_;

    bool destroying_{false};
    bool can_view_{false};
    bool sizing_columns_{false};
    std::vector<int> col_weights_;
    std::vector<std::string> row_summaries_;

    wxPanel* pnl_no_permission_{nullptr};
    CardPanel* main_card_{nullptr};

    wxChoice* ch_kind_{nullptr};
    ui::TouchComboBox* cb_kind_{nullptr};
    wxButton* btn_today_{nullptr};
    wxButton* btn_yesterday_{nullptr};
    wxButton* btn_week_{nullptr};
    wxButton* btn_month_{nullptr};
    wxButton* btn_last_month_{nullptr};
    wxTextCtrl* txt_from_{nullptr};
    wxTextCtrl* txt_to_{nullptr};
    wxButton* btn_query_{nullptr};
    wxButton* btn_export_{nullptr};

    wxPanel* pnl_error_{nullptr};
    wxStaticText* lbl_error_{nullptr};
    wxPanel* pnl_mismatch_{nullptr};
    wxStaticText* lbl_mismatch_{nullptr};
    wxPanel* pnl_truncated_{nullptr};
    wxStaticText* lbl_truncated_{nullptr};
    wxPanel* pnl_export_ok_{nullptr};
    wxStaticText* lbl_export_ok_{nullptr};

    wxStaticText* lbl_empty_{nullptr};
    wxListView* list_{nullptr};
    wxStaticText* lbl_detail_{nullptr};
};

} // namespace app::frontend
