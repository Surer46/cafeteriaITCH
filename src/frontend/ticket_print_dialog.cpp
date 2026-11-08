#include "src/frontend/ticket_print_dialog.hpp"
#include "src/frontend/ticket_layout.hpp"
#include "src/frontend/theme.hpp"
#include <wx/statline.h>
#include <wx/print.h>
#include <wx/printdlg.h>
#include <wx/dc.h>
#include <sstream>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <winspool.h>
#endif

namespace app::frontend {

// ---------------------------------------------------------------------------
// Helpers: comprobar si una impresora está instalada en el sistema
// ---------------------------------------------------------------------------
namespace {

#ifdef _WIN32
/// Devuelve true si el nombre de impresora (UTF-8) está instalado en Windows.
/// En entornos sin impresoras, devuelve false sin lanzar excepciones.
bool is_printer_installed(const std::string& printer_name_utf8) {
    if (printer_name_utf8.empty()) return false;
    // Convertir a wstring para API Win32
    int len = MultiByteToWideChar(CP_UTF8, 0, printer_name_utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return false;
    std::wstring wname(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, printer_name_utf8.c_str(), -1, wname.data(), len);
    // Quitar nul terminal extra
    if (!wname.empty() && wname.back() == L'\0') wname.pop_back();

    DWORD needed = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                  nullptr, 0, &needed, &count);
    if (needed == 0) return false;

    std::vector<BYTE> buf(needed);
    if (!EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                       buf.data(), needed, &needed, &count)) {
        return false;
    }

    auto* info = reinterpret_cast<PRINTER_INFO_2W*>(buf.data());
    for (DWORD i = 0; i < count; ++i) {
        if (info[i].pPrinterName && wname == info[i].pPrinterName) {
            return true;
        }
    }
    return false;
}
#else
bool is_printer_installed(const std::string&) { return true; } // skip check on non-Windows
#endif

} // namespace

TicketPrintDialog::TicketPrintDialog(
    wxWindow* parent,
    const contracts::TicketDto& ticket,
    const contracts::PrinterStationConfig& print_config
) : wxDialog(parent, wxID_ANY, wxT("Venta Registrada — Cafetería ITCH II"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    ticket_(ticket), config_(print_config) {
    SetBackgroundColour(Theme::ColorBackground);

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    // Encabezado con Folio
    auto* banner = new wxPanel(this, wxID_ANY);
    banner->SetBackgroundColour(Theme::ColorSuccessSoft);
    auto* banner_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_folio = new wxStaticText(banner, wxID_ANY, wxString::Format(wxT("¡Venta confirmada! Folio: %s"), wxString::FromUTF8(ticket_.folio.c_str())));
    lbl_folio->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_folio->SetForegroundColour(Theme::ColorSuccess);
    banner_sizer->Add(lbl_folio, 0, wxALL, 14);

    banner->SetSizer(banner_sizer);
    main_sizer->Add(banner, 0, wxEXPAND);

    // Pregunta explícita según specs.md
    auto* lbl_question = new wxStaticText(this, wxID_ANY, wxT("¿Deseas imprimir el ticket?"));
    lbl_question->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_question->SetForegroundColour(Theme::ColorText);
    main_sizer->Add(lbl_question, 0, wxLEFT | wxRIGHT | wxTOP, 16);

    auto* lbl_hint = new wxStaticText(
        this,
        wxID_ANY,
        wxT("La respuesta no modifica el cobro. Si eliges «Ahora no», la venta permanece guardada ")
        wxT("y el ticket podrá reimprimirse posteriormente desde el historial.")
    );
    lbl_hint->SetFont(Theme::get_font_small());
    lbl_hint->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_hint->Wrap(460);
    main_sizer->Add(lbl_hint, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // Etiqueta de impresora configurada (informativa)
    wxString printer_info;
    if (config_.printer_name.empty()) {
        printer_info = wxT("Impresora: predeterminada del sistema");
    } else {
        printer_info = wxString::Format(wxT("Impresora configurada: %s"), wxString::FromUTF8(config_.printer_name.c_str()));
    }
    auto* lbl_printer = new wxStaticText(this, wxID_ANY, printer_info);
    lbl_printer->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_printer->SetForegroundColour(Theme::ColorTextSecondary);
    main_sizer->Add(lbl_printer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    // Vista previa nominal del ticket (geometría estimada; la real se calcula en OnPreparePrinting)
    auto* lbl_prev_title = new wxStaticText(this, wxID_ANY,
        wxT("Vista previa estimada (geometría exacta calculada al imprimir):"));
    lbl_prev_title->SetFont(Theme::get_font_small(wxFONTWEIGHT_MEDIUM));
    lbl_prev_title->SetForegroundColour(Theme::ColorTextSecondary);
    main_sizer->Add(lbl_prev_title, 0, wxLEFT | wxRIGHT, 14);

    std::string formatted_ticket = ticket_.to_formatted_text(config_.characters_per_line);
    auto* txt_preview = new wxTextCtrl(
        this,
        wxID_ANY,
        wxString::FromUTF8(formatted_ticket.c_str()),
        wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY
    );
    wxFont f_mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    txt_preview->SetFont(f_mono);
    txt_preview->SetBackgroundColour(Theme::ColorSurface);
    txt_preview->SetMinSize(wxSize(450, 260));
    main_sizer->Add(txt_preview, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

    // Botones obligatorios: Imprimir y Ahora no
    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    int btn_h = Theme::is_touch() ? 48 : 40;

    auto* btn_not_now = new wxButton(this, wxID_ANY, wxT("Ahora no"), wxDefaultPosition, wxSize(130, btn_h));
    Theme::style_button_secondary(btn_not_now, btn_h);
    btn_sizer->Add(btn_not_now, 1, wxRIGHT, 12);

    auto* btn_print = new wxButton(this, wxID_ANY, wxT("Imprimir"), wxDefaultPosition, wxSize(150, btn_h));
    Theme::style_button_primary(btn_print, btn_h);
    Theme::setup_button_icon(btn_print, "print", wxSize(18, 18), Theme::ColorSurface);
    btn_sizer->Add(btn_print, 1);

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);
    Fit();
    wxSize fitted = GetSize();
    fitted.SetWidth(std::max(fitted.GetWidth(), 500));
    fitted.SetHeight(std::max(fitted.GetHeight(), 580));
    SetSize(fitted);
    SetMinSize(fitted);
    Center();

    btn_print->Bind(wxEVT_BUTTON, &TicketPrintDialog::on_print, this);
    btn_not_now->Bind(wxEVT_BUTTON, &TicketPrintDialog::on_not_now, this);
}

void TicketPrintDialog::on_print(wxCommandEvent&) {
    // -----------------------------------------------------------------------
    // 1. Configurar datos de impresora: orientación y sugerencia de tamaño de papel
    // -----------------------------------------------------------------------
    wxPrintData print_data;
    print_data.SetOrientation(wxPORTRAIT);
    print_data.SetPaperId(wxPAPER_NONE);
    int width_mm = (config_.profile == contracts::ThermalPaperProfile::Profile55mm) ? 55 : 80;
    // Se sugiere el tamaño de papel térmico (55 mm u 80 mm de ancho, largo continuo de 200 mm).
    // Si el controlador del dispositivo lo ignora o no admite tamaños personalizados,
    // la impresión continúa con la configuración predeterminada del controlador (no verificado en hardware).
    print_data.SetPaperSize(wxSize(width_mm, 200));

    // -----------------------------------------------------------------------
    // 2. Comprobar disponibilidad de la impresora configurada
    // -----------------------------------------------------------------------
    bool use_custom_printer = false;
    if (!config_.printer_name.empty()) {
        if (is_printer_installed(config_.printer_name)) {
            use_custom_printer = true;
        } else {
            int ans = wxMessageBox(
                wxString::Format(
                    wxT("La impresora configurada «%s» no se encontró entre las impresoras instaladas.\n\n")
                    wxT("Opciones:\n")
                    wxT("• «Continuar»: usar la impresora predeterminada del sistema.\n")
                    wxT("• «Cancelar»: volver al diálogo para revisar la configuración.\n\n")
                    wxT("La venta no se revierte ni se duplica en ningún caso."),
                    wxString::FromUTF8(config_.printer_name.c_str())
                ),
                wxT("Impresora No Encontrada"),
                wxOK | wxCANCEL | wxICON_WARNING,
                this
            );
            if (ans == wxCANCEL) {
                printed_ = false;
                return;
            }
            // Si el usuario elige Continuar, NO llamar a SetPrinterName (usa la predeterminada del sistema)
            use_custom_printer = false;
        }
    }

    if (use_custom_printer) {
        print_data.SetPrinterName(wxString::FromUTF8(config_.printer_name.c_str()));
    }

    wxPrintDialogData printDialogData(print_data);
    printDialogData.SetMinPage(1);
    printDialogData.SetMaxPage(1);
    printDialogData.SetFromPage(1);
    printDialogData.SetToPage(1);
    printDialogData.SetAllPages(true);
    wxPrinter printer(&printDialogData);
    TicketPrintout printout(ticket_, wxString::Format(wxT("Ticket %s"), wxString::FromUTF8(ticket_.folio.c_str())), config_.profile);

    bool prompt = true;
#ifdef CAFETERIA_TEST_HOOKS
    const char* sim_err = std::getenv("CAFETERIA_SIMULATE_PRINT_ERROR");
    if (sim_err && (std::strcmp(sim_err, "1") == 0 || std::strcmp(sim_err, "true") == 0)) {
        prompt = false;
    }
#endif
    bool ok = printer.Print(this, &printout, prompt);
    if (!ok) {
        if (printer.GetLastError() == wxPRINTER_CANCELLED) {
            printed_ = false;
            wxMessageBox(
                wxT("Impresión cancelada por el usuario en el diálogo del sistema.\n\n")
                wxT("• La venta permanece debidamente guardada y confirmada en la base de datos local.\n")
                wxT("• El inventario y el turno de caja ya fueron actualizados de forma atómica.\n")
                wxT("• El ticket podrá reimprimirse posteriormente desde el historial de tickets."),
                wxT("Impresión Cancelada"),
                wxOK | wxICON_INFORMATION,
                this
            );
        } else {
            printed_ = false;
            wxMessageBox(
                wxT("No se pudo enviar el documento al subsistema de impresión (error de spooler o impresora no disponible).\n\n")
                wxT("• La venta no se revierte ni se duplica: permanece confirmada en SQLite.\n")
                wxT("• El ticket podrá reimprimirse desde el historial una vez resuelta la conexión con la impresora."),
                wxT("Aviso del Subsistema de Impresión"),
                wxOK | wxICON_WARNING,
                this
            );
        }
    } else {
        printed_ = true;
        wxMessageBox(
            wxT("Ticket enviado con éxito al subsistema nativo de impresión (spooler del sistema operativo).\n\n")
            wxT("Aviso operativo: Compruebe la salida física del papel en la impresora térmica del punto de venta."),
            wxT("Envío a Impresión Exitoso"),
            wxOK | wxICON_INFORMATION,
            this
        );
        EndModal(wxID_OK);
    }
}

void TicketPrintDialog::on_not_now(wxCommandEvent&) {
    printed_ = false;
    EndModal(wxID_CANCEL);
}

} // namespace app::frontend

