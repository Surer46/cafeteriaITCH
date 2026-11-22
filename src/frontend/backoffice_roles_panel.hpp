#ifndef APP_FRONTEND_BACKOFFICE_ROLES_PANEL_HPP
#define APP_FRONTEND_BACKOFFICE_ROLES_PANEL_HPP

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include "app/contracts/account_service.hpp"

namespace app::frontend {

class CardPanel;
class ToggleSwitch;

class BackofficeRolesPanel : public wxPanel {
public:
    BackofficeRolesPanel(
        wxWindow* parent,
        std::shared_ptr<contracts::IAccountService> account_service
    );
    ~BackofficeRolesPanel() override;

    void refresh_roles(const std::string& select_role_id = "");

private:
    struct PermissionRow {
        contracts::PermissionDto perm;
        wxPanel* row_panel{nullptr};
        wxStaticText* lbl_title{nullptr};
        wxStaticText* lbl_desc{nullptr};
        ToggleSwitch* toggle{nullptr};
    };

    struct PermissionGroup {
        wxStaticText* header{nullptr};
        std::vector<size_t> row_indices;
    };

    void setup_ui();
    void load_permissions();
    void build_permission_matrix();
    void rebuild_permission_sizers(bool two_cols);
    void update_responsive_layout();
    void update_permission_matrix_layout();
    void update_permission_count();
    void set_form_feedback(const wxString& message, const wxColour& colour);
    void set_permissions_editable(bool editable);
    void auto_size_columns();
    void on_size(wxSizeEvent& event);
    void on_role_selected(wxListEvent& event);
    void on_role_deselected(wxListEvent& event);
    void on_new_role(wxCommandEvent& event);
    void on_save_role(wxCommandEvent& event);
    void on_delete_role(wxCommandEvent& event);
    void populate_role_details(long index);
    void reset_form_to_empty();

    std::shared_ptr<contracts::IAccountService> account_service_;

    // Componentes principales
    wxBoxSizer* main_sizer_{nullptr};
    wxButton* btn_new_role_{nullptr};
    wxButton* btn_refresh_{nullptr};
    wxStaticText* lbl_top_status_{nullptr};
    wxPanel* pnl_warning_{nullptr};
    wxStaticText* lbl_warning_{nullptr};
    wxString warning_raw_;

    // Columna izquierda: lista de roles
    CardPanel* left_card_{nullptr};
    wxListCtrl* list_roles_{nullptr};
    wxStaticText* lbl_roles_count_{nullptr};

    // Columna derecha: formulario y permisos
    CardPanel* right_card_{nullptr};
    wxBoxSizer* rhead_container_{nullptr};
    wxBoxSizer* rhead_active_{nullptr};
    wxStaticText* lbl_editor_title_{nullptr};
    wxStaticText* lbl_system_notice_{nullptr};
    wxString editor_title_raw_;
    wxString system_notice_raw_;

    wxBoxSizer* form_container_{nullptr};
    wxSizer* form_active_{nullptr};
    wxStaticText* lbl_name_{nullptr};
    wxStaticText* lbl_desc_{nullptr};
    wxWindow* field_name_box_{nullptr};
    wxWindow* field_desc_box_{nullptr};
    wxTextCtrl* txt_name_{nullptr};
    wxTextCtrl* txt_desc_{nullptr};

    wxBoxSizer* perm_header_container_{nullptr};
    wxBoxSizer* perm_header_active_{nullptr};
    wxStaticText* lbl_p_header_{nullptr};
    wxStaticText* lbl_form_feedback_{nullptr};
    wxString form_feedback_raw_;

    wxScrolledWindow* scroll_perms_{nullptr};
    wxBoxSizer* perms_sizer_{nullptr};
    std::vector<PermissionGroup> perm_groups_;
    std::vector<PermissionRow> perm_rows_;
    std::map<std::string, ToggleSwitch*> perm_toggles_;
    std::vector<wxPanel*> perm_separators_;

    wxButton* btn_save_{nullptr};
    wxButton* btn_delete_{nullptr};
    wxStaticText* lbl_perm_count_{nullptr};

    // Datos en memoria
    std::vector<contracts::RoleDto> roles_;
    std::vector<contracts::PermissionDto> all_permissions_;
    long selected_index_{-1};
    bool is_creating_new_{false};
    bool can_manage_{false};
    bool in_size_event_{false};
    bool in_matrix_layout_{false};
    bool wide_split_{false};
    bool split_initialized_{false};
    bool rhead_stacked_{false};
    bool rhead_initialized_{false};
    bool form_single_row_{false};
    bool form_initialized_{false};
    bool phead_stacked_{false};
    bool phead_initialized_{false};
    bool matrix_two_cols_{false};
    bool matrix_sizers_built_{false};
    int last_matrix_avail_w_{-1};
    int last_rhead_wrap_w_{-1};
    int last_phead_wrap_w_{-1};
    int last_warning_wrap_w_{-1};
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

} // namespace app::frontend

#endif // APP_FRONTEND_BACKOFFICE_ROLES_PANEL_HPP
