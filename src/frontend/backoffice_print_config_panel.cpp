#include "src/frontend/backoffice_print_config_panel.hpp"
#include "src/frontend/theme.hpp"
#include "app/contracts/sale_service.hpp"

#ifdef _WIN32
#include <windows.h>
#include <winspool.h>
#endif

#include <vector>
#include <string>

namespace app::frontend {

namespace {

/// Obtiene la lista de impresoras instaladas (UTF-8). Vacía si no hay ninguna.
std::vector<std::string> enumerate_installed_printers() {
    std::vector<std::string> printers;
#ifdef _WIN32
    DWORD needed = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                  nullptr, 0, &needed, &count);
    if (needed == 0) return printers;

    std::vector<BYTE> buf(needed);
    if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                       buf.data(), needed, &needed, &count)) {
        return printers;
    }

    auto* info = reinterpret_cast<PRINTER_INFO_2W*>(buf.data());
    for (DWORD i = 0; i < count; ++i) {
        if (info[i].pPrinterName) {
            int len = WideCharToMultiByte(CP_UTF8, 0, info[i].pPrinterName, -1,
                                          nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                std::string name(len - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, info[i].pPrinterName, -1,
                                    name.data(), len, nullptr, nullptr);
                printers.push_back(std::move(name));
            }
        }
    }
#endif
    return printers;
}

} // namespace

BackofficePrintConfigPanel::BackofficePrintConfigPanel(
    wxWindow* parent,
    services::ServiceRegistry services
) : wxPanel(parent, wxID_ANY), services_(std::move(services)) {
    SetBackgroundColour(Theme::ColorBackground);
    setup_ui();
    load_current_config();
}

void BackofficePrintConfigPanel::setup_ui() {
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    auto* card = new CardPanel(this);
    card->set_card_background(Theme::ColorSurface);
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);

    // Título de sección
    auto* lbl_title = new wxStaticText(card, wxID_ANY, wxT("Configuración de Impresora Térmica"));
    lbl_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_title->SetForegroundColour(Theme::ColorBrandDeep);
    card_sizer->Add(lbl_title, 0, wxALL, 16);

    auto* lbl_subtitle = new wxStaticText(card, wxID_ANY,
        wxT("Seleccione la impresora instalada y el ancho nominal del papel para esta estación.\n")
        wxT("El sistema adaptará el formateo y la paginación al perfil elegido.\n")
        wxT("Verifique que el tamaño de papel (55 mm o 80 mm) esté configurado en el controlador de la impresora.")
    );
    lbl_subtitle->SetFont(Theme::get_font_small());
    lbl_subtitle->SetForegroundColour(Theme::ColorTextSecondary);
    card_sizer->Add(lbl_subtitle, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);

    // Contenido dividido en 2 columnas: Opciones a la izquierda, Vista Previa a la derecha
    auto* cols_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Columna izquierda: Opciones de configuración
    auto* opt_sizer = new wxBoxSizer(wxVERTICAL);

    // --- Selector de impresora instalada ---
    auto* lbl_printer_sel = new wxStaticText(card, wxID_ANY, wxT("Impresora instalada:"));
    lbl_printer_sel->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_printer_sel->SetForegroundColour(Theme::ColorText);
    opt_sizer->Add(lbl_printer_sel, 0, wxBOTTOM, 6);

    // Enumerar impresoras instaladas
    installed_printers_ = enumerate_installed_printers();
    wxArrayString printer_choices;
    printer_choices.Add(wxT("(Predeterminada del sistema)"));
    for (const auto& p : installed_printers_) {
        printer_choices.Add(wxString::FromUTF8(p.c_str()));
    }
    if (installed_printers_.empty()) {
        printer_choices.Add(wxT("(No se detectaron impresoras instaladas)"));
    }
    choice_printer_ = new wxChoice(card, wxID_ANY, wxDefaultPosition,
                                   wxSize(380, Theme::is_touch() ? 44 : 36), printer_choices);
    choice_printer_->SetSelection(0);
    opt_sizer->Add(choice_printer_, 0, wxBOTTOM, 8);

    auto* lbl_printer_hint = new wxStaticText(card, wxID_ANY,
        wxT("Seleccione la impresora térmica de este puesto. Si no aparece en la lista,\n")
        wxT("instale su controlador desde la configuración de Windows."));
    lbl_printer_hint->SetFont(Theme::get_font_small());
    lbl_printer_hint->SetForegroundColour(Theme::ColorTextSecondary);
    opt_sizer->Add(lbl_printer_hint, 0, wxBOTTOM, 16);

    // --- Perfil de papel ---
    auto* lbl_profile = new wxStaticText(card, wxID_ANY, wxT("Perfil de Papel Térmico:"));
    lbl_profile->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_profile->SetForegroundColour(Theme::ColorText);
    opt_sizer->Add(lbl_profile, 0, wxBOTTOM, 6);

    wxArrayString profiles;
    profiles.Add(wxT("Perfil 80 mm (Estándar de mostrador — ~42 columnas nominales)"));
    profiles.Add(wxT("Perfil 55 mm (Térmica compacta — ~28 columnas nominales)"));
    choice_profile_ = new wxChoice(card, wxID_ANY, wxDefaultPosition,
                                   wxSize(380, Theme::is_touch() ? 44 : 36), profiles);
    choice_profile_->SetSelection(0);
    opt_sizer->Add(choice_profile_, 0, wxBOTTOM, 6);

    // Nota: sin etiqueta "Columnas efectivas" ya que es estimación nominal,
    // no una medición real del DC del controlador.
    auto* lbl_paper_hint = new wxStaticText(card, wxID_ANY,
        wxT("Las columnas nominales son una estimación. La geometría real depende\n")
        wxT("del controlador y el DPI de la impresora seleccionada."));
    lbl_paper_hint->SetFont(Theme::get_font_small());
    lbl_paper_hint->SetForegroundColour(Theme::ColorTextSecondary);
    opt_sizer->Add(lbl_paper_hint, 0, wxBOTTOM, 20);

    // Botón Guardar
    int btn_h = Theme::is_touch() ? 48 : 38;
    btn_save_ = new wxButton(card, wxID_ANY, wxT("Guardar Configuración"), wxDefaultPosition, wxSize(220, btn_h));
    Theme::style_button_primary(btn_save_, btn_h);
    Theme::setup_button_icon(btn_save_, "check_circle", wxSize(18, 18), Theme::ColorSurface);
    opt_sizer->Add(btn_save_, 0, wxBOTTOM, 12);

    lbl_status_ = new wxStaticText(card, wxID_ANY, wxEmptyString);
    lbl_status_->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_status_->SetForegroundColour(Theme::ColorTextMuted);
    opt_sizer->Add(lbl_status_, 0, wxEXPAND);

    cols_sizer->Add(opt_sizer, 0, wxRIGHT, 24);

    // Columna derecha: Vista previa interactiva en vivo
    auto* prev_box = new wxBoxSizer(wxVERTICAL);
    auto* lbl_prev_title = new wxStaticText(card, wxID_ANY,
        wxT("Vista Previa Estimada del Formato de Impresión:"));
    lbl_prev_title->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_prev_title->SetForegroundColour(Theme::ColorText);
    prev_box->Add(lbl_prev_title, 0, wxBOTTOM, 2);

    auto* lbl_prev_note = new wxStaticText(card, wxID_ANY,
        wxT("Estimación basada en columnas nominales del perfil."));
    lbl_prev_note->SetFont(Theme::get_font_small());
    lbl_prev_note->SetForegroundColour(Theme::ColorTextSecondary);
    prev_box->Add(lbl_prev_note, 0, wxBOTTOM, 6);

    txt_preview_ = new wxTextCtrl(card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(360, 340), wxTE_MULTILINE | wxTE_READONLY);
    wxFont f_mono(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    txt_preview_->SetFont(f_mono);
    txt_preview_->SetBackgroundColour(Theme::ColorSurface);
    prev_box->Add(txt_preview_, 1, wxEXPAND);

    cols_sizer->Add(prev_box, 1, wxEXPAND);

    card_sizer->Add(cols_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);
    card->SetSizer(card_sizer);

    main_sizer->Add(card, 1, wxEXPAND | wxALL, 16);
    SetSizer(main_sizer);

    choice_profile_->Bind(wxEVT_CHOICE, &BackofficePrintConfigPanel::on_profile_selected, this);
    btn_save_->Bind(wxEVT_BUTTON, &BackofficePrintConfigPanel::on_save_clicked, this);
}

void BackofficePrintConfigPanel::load_current_config() {
    if (services_.print_config_service) {
        auto res = services_.print_config_service->get_station_config();
        if (res.is_success()) {
            current_config_ = res.value();
            if (current_config_.profile == contracts::ThermalPaperProfile::Profile55mm) {
                choice_profile_->SetSelection(1);
            } else {
                choice_profile_->SetSelection(0);
            }

            // Seleccionar la impresora configurada en el choice
            if (!current_config_.printer_name.empty()) {
                wxString target = wxString::FromUTF8(current_config_.printer_name.c_str());
                int found = choice_printer_->FindString(target);
                if (found != wxNOT_FOUND) {
                    choice_printer_->SetSelection(found);
                } else {
                    // La impresora guardada no está instalada: mostrar en status
                    lbl_status_->SetLabel(wxString::Format(
                        wxT("Aviso: La impresora «%s» no está instalada en este sistema."),
                        target));
                    lbl_status_->SetForegroundColour(Theme::ColorError);
                    lbl_status_->GetParent()->Layout();
                }
            }
        }
    }

    // Verificar permiso config.manage
    bool can_manage = services_.account_service && services_.account_service->has_permission("config.manage");
    if (!can_manage) {
        btn_save_->Enable(false);
        lbl_status_->SetLabel(wxT("Aviso: Se requiere el permiso 'config.manage' para modificar la configuración de impresión."));
        lbl_status_->SetForegroundColour(Theme::ColorError);
    } else if (lbl_status_->GetLabel().IsEmpty()) {
        lbl_status_->SetLabel(wxT("Configuración lista para editar."));
        lbl_status_->SetForegroundColour(Theme::ColorTextMuted);
    }

    update_preview();
}

void BackofficePrintConfigPanel::update_preview() {
    int sel = choice_profile_->GetSelection();
    int cols = (sel == 1) ? 28 : 42;
    int mm = (sel == 1) ? 55 : 80;
    (void)mm; // usado solo para referencia visual

    // Generar ticket de demostración con notas y nombres representativos
    contracts::TicketDto demo;
    demo.business_name = "Cafetería ITCH II";
    demo.folio = "V-DEMO-001";
    demo.created_at = "2026-10-14 10:30:00";
    demo.cashier_name = "Cajero Operativo";

    contracts::TicketLineItemDto item1;
    item1.product_name = "Torta Especial Gourmet de Pierna";
    item1.quantity = 2;
    item1.unit_price = contracts::Money::from_major_minor(45, 0);
    item1.subtotal = contracts::Money::from_major_minor(90, 0);
    item1.notes = "sin cebolla, con extra chipotle";
    demo.items.push_back(item1);

    contracts::TicketLineItemDto item2;
    item2.product_name = "Café de Olla Tradicional";
    item2.quantity = 1;
    item2.unit_price = contracts::Money::from_major_minor(20, 0);
    item2.subtotal = contracts::Money::from_major_minor(20, 0);
    item2.notes = "poco dulce";
    demo.items.push_back(item2);

    demo.subtotal = contracts::Money::from_major_minor(110, 0);
    demo.discount = contracts::Money::zero();
    demo.total = contracts::Money::from_major_minor(110, 0);
    demo.payment_method = contracts::PaymentMethod::Efectivo;
    demo.cash_received = contracts::Money::from_major_minor(200, 0);
    demo.change = contracts::Money::from_major_minor(90, 0);

    std::string preview_text = demo.to_formatted_text(cols);
    txt_preview_->SetValue(wxString::FromUTF8(preview_text.c_str()));
}

void BackofficePrintConfigPanel::on_profile_selected(wxCommandEvent&) {
    update_preview();
}

void BackofficePrintConfigPanel::on_save_clicked(wxCommandEvent&) {
    if (!services_.print_config_service) {
        wxMessageBox(wxT("Servicio de configuración de impresión no disponible."), wxT("Error"), wxOK | wxICON_ERROR, this);
        return;
    }

    contracts::PrinterStationConfig new_cfg;
    int sel = choice_profile_->GetSelection();
    if (sel == 1) {
        new_cfg.profile = contracts::ThermalPaperProfile::Profile55mm;
        new_cfg.nominal_width_mm = 55;
        new_cfg.characters_per_line = 28;
    } else {
        new_cfg.profile = contracts::ThermalPaperProfile::Profile80mm;
        new_cfg.nominal_width_mm = 80;
        new_cfg.characters_per_line = 42;
    }

    // Obtener nombre de impresora seleccionada
    int printer_sel = choice_printer_->GetSelection();
    if (printer_sel <= 0 || printer_sel > static_cast<int>(installed_printers_.size())) {
        // Selección predeterminada o fuera de rango: dejar vacío para usar predeterminada
        new_cfg.printer_name = "";
    } else {
        new_cfg.printer_name = installed_printers_[printer_sel - 1];
    }

    auto res = services_.print_config_service->set_station_config(new_cfg);
    if (res.is_success()) {
        current_config_ = new_cfg;
        wxString printer_label = new_cfg.printer_name.empty()
            ? wxT("predeterminada del sistema")
            : wxString::FromUTF8(new_cfg.printer_name.c_str());
        lbl_status_->SetLabel(wxT("¡Configuración guardada exitosamente en esta estación!"));
        lbl_status_->SetForegroundColour(Theme::ColorSuccess);
        lbl_status_->GetParent()->Layout();
        wxMessageBox(
            wxString::Format(wxT("Configuración de impresión actualizada:\n")
                             wxT("• Perfil: %d mm (%d columnas nominales)\n")
                             wxT("• Impresora: %s\n\n")
                             wxT("Los nuevos tickets y reimpresiones se adaptarán a este formato.\n")
                             wxT("Verifique que el papel configurado en el controlador coincida con el perfil elegido."),
                             new_cfg.nominal_width_mm, new_cfg.characters_per_line,
                             printer_label),
            wxT("Configuración Guardada"),
            wxOK | wxICON_INFORMATION,
            this
        );
    } else {
        lbl_status_->SetLabel(wxString::FromUTF8(res.error_message().c_str()));
        lbl_status_->SetForegroundColour(Theme::ColorError);
        lbl_status_->GetParent()->Layout();
        wxMessageBox(
            wxString::FromUTF8(res.error_message().c_str()),
            wxT("Error al Guardar"),
            wxOK | wxICON_ERROR,
            this
        );
    }
}

} // namespace app::frontend
