#include "UI/ValidationPanel.hpp"
#include "Validation/ValidationReport.hpp"
#include "UI/EditorUi.hpp"

#include <imgui.h>

#include <string>

namespace plumbcad
{
    namespace
    {
        void drawEndpointPair(const char *tableId, const char *startLabel, ObjectId startId, int startEndpoint,
                              const char *endLabel, ObjectId endId, int endEndpoint)
        {
            if (!ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
            {
                return;
            }

            auto drawRow = [](const char *label, ObjectId id, int endpoint) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", label);
                ImGui::TableSetColumnIndex(1);

                if (id == InvalidObjectId)
                {
                    ImGui::TextDisabled("nincs kiválasztva");
                }
                else
                {
                    ImGui::Text("#%u / %d. vég", static_cast<unsigned int>(id), endpoint);
                }
            };

            drawRow(startLabel, startId, startEndpoint);
            drawRow(endLabel, endId, endEndpoint);
            ImGui::EndTable();
        }

        void drawLabeledFloat(const char *label, const char *inputId, float &value, float step, float fastStep,
                              const char *format)
        {
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputFloat(inputId, &value, step, fastStep, format);
        }

        bool fullWidthButton(const char *label)
        {
            return ImGui::Button(label, ImVec2(-1.0f, 0.0f));
        }

        void drawCommonValidationPath(const ValidationPanelModel &model, UiActions &actions)
        {
            if (!ImGui::CollapsingHeader("Közös START / END"))
            {
                return;
            }

            drawEndpointPair("CommonValidationEndpointStatus", "START", model.validationStartId,
                             model.validationStartEndpoint, "END", model.validationEndId,
                             model.validationEndEndpoint);

            if (fullWidthButton("Közös START/END törlése")) actions.validationClear = true;
        }

        void drawQuickChecks(UiActions &actions)
        {
            if (!ImGui::CollapsingHeader("Alap hálózati ellenőrzések"))
            {
                return;
            }

            if (fullWidthButton("Nyitott csővégek ellenőrzése")) actions.runValidationOpenEnds = true;
            if (fullWidthButton("Izolált elemek ellenőrzése")) actions.runValidationIsolated = true;
            if (fullWidthButton("Hálózat összefüggőségének ellenőrzése")) actions.runValidationConnected = true;
            if (fullWidthButton("Vízhálózati alapadatok ellenőrzése")) actions.runValidationMszBasic = true;
            ImGui::TextDisabled("Átmérő, falvastagság, belső átmérő, anyag, nyomás, hossz és katalógusadatok.");
        }

        void drawPressureTest(const ValidationPanelModel &model, UiActions &actions)
        {
            if (!ImGui::CollapsingHeader("Nyomáspróba"))
            {
                return;
            }

            if (model.validationStartId == InvalidObjectId)
            {
                ImGui::TextDisabled("START nincs kiválasztva.");
            }

            drawLabeledFloat("Tervezési nyomás [bar]", "##pressure_network_design",
                             model.pressureDesignPressureBar, 0.5f, 1.0f, "%.2f");

            drawLabeledFloat("Próbanyomás [bar]", "##pressure_network_test", model.pressureTestPressureBar, 0.5f,
                             1.0f, "%.2f");

            drawLabeledFloat("Tartási idő [perc]", "##pressure_network_hold", model.pressureHoldTimeMin, 5.0f,
                             10.0f, "%.1f");

            drawLabeledFloat("Kezdeti mért nyomás [bar]", "##pressure_network_initial",
                             model.pressureInitialMeasuredPressureBar, 0.05f, 0.10f, "%.3f");

            drawLabeledFloat("Végső mért nyomás [bar]", "##pressure_network_final",
                             model.pressureFinalMeasuredPressureBar, 0.05f, 0.10f, "%.3f");

            drawLabeledFloat("Megengedett nyomásesés [bar]", "##pressure_network_allowed_drop",
                             model.pressureAllowedDropBar, 0.05f, 0.10f, "%.3f");

            const float calculatedPressureDropBar =
                model.pressureInitialMeasuredPressureBar - model.pressureFinalMeasuredPressureBar;

            ImGui::Text("Számított nyomásesés: %.3f bar", calculatedPressureDropBar);

            if (fullWidthButton("Nyomáspróba ellenőrzése"))
            {
                actions.runValidationPressure = true;
            }
        }

        void drawFlowVelocity(const ValidationPanelModel &model, UiActions &actions)
        {
            if (!ImGui::CollapsingHeader("Vízellátás | sebesség"))
            {
                return;
            }

            drawLabeledFloat("Tervezési térfogatáram Q [l/s]", "##open_water_flow_rate", model.flowRateLps, 0.05f,
                             0.10f, "%.3f");

            if (ImGui::BeginTable("OpenWaterVelocityReferenceTable", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Sebesség");
                ImGui::TableSetupColumn("Minősítés");
                ImGui::TableSetupColumn("Referencia");
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("v <= 1.5 m/s");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted("AJÁNLOTT");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted("ABCB");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("1.5 < v <= 3.0 m/s");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted("FIGYELMEZTETÉS");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted("NCC");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("v > 3.0 m/s");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted("HIBA");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted("NCC");

                ImGui::EndTable();
            }

            if (fullWidthButton("Sebesség ellenőrzése"))
            {
                actions.runValidationFlow = true;
            }
        }

        void drawDrainage(const ValidationPanelModel &model, UiActions &actions)
        {
            if (!ImGui::CollapsingHeader("Lefolyás | Table 6"))
            {
                return;
            }

            drawLabeledFloat("Csúcstérfogatáram Qpeak [l/s]", "##open_drainage_peak_flow", model.flowRateLps,
                             0.05f, 0.10f, "%.3f");

            if (ImGui::BeginTable("OpenDrainageTable6", 5,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Qpeak");
                ImGui::TableSetupColumn("Table 6");
                ImGui::TableSetupColumn("Katalógus");
                ImGui::TableSetupColumn("Min. lejtés");
                ImGui::TableSetupColumn("Kapacitás");
                ImGui::TableHeadersRow();

                auto row = [](const char *q, const char *dn, const char *catalog, const char *gradient,
                              const char *capacity) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(q);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(dn);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(catalog);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(gradient);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(capacity);
                };

                row("< 1 l/s", "DN75", "PVC-U Ø75", "1:40", "4.1 l/s");
                row("< 1 l/s", "DN100", "PVC-U Ø110", "1:40", "9.2 l/s");
                row(">= 1 l/s*", "DN75", "PVC-U Ø75", "1:80", "2.8 l/s");
                row(">= 1 l/s*", "DN100", "PVC-U Ø110", "1:80", "6.3 l/s");
                row(">= 1 l/s*", "DN150", "PVC-U Ø160", "1:150", "15.0 l/s");

                ImGui::EndTable();
            }

            if (fullWidthButton("Lefolyás ellenőrzése"))
            {
                actions.runValidationMszEn1610 = true;
            }
        }

        void drawDarcy(const ValidationPanelModel &model, UiActions &actions)
        {
            if (!ImGui::CollapsingHeader("Nyomásveszteség | Darcy-Weisbach"))
            {
                return;
            }

            drawLabeledFloat("Térfogatáram Q [l/s]", "##darcy_flow_rate", model.hydraulicFlowRateLps, 0.05f, 0.10f,
                             "%.3f");

            drawLabeledFloat("Folyadéksűrűség [kg/m3]", "##darcy_density", model.hydraulicDensityKgM3, 1.0f, 10.0f,
                             "%.1f");

            drawLabeledFloat("Kinematikai viszkozitás [m2/s]", "##darcy_viscosity",
                             model.hydraulicKinematicViscosityM2s, 0.0000001f, 0.000001f, "%.8f");

            drawLabeledFloat("Max. teljes nyomásveszteség [bar]", "##darcy_max_loss",
                             model.hydraulicMaxTotalPressureLossBar, 0.05f, 0.10f, "%.3f");

            if (fullWidthButton("Nyomásveszteség számítása"))
            {
                actions.runValidationDarcyWeisbach = true;
            }
        }

        const char *validationRunDisplayName(const std::vector<ValidationMessage> &messages)
        {
            bool openEnds = false;
            bool isolated = false;
            bool connected = false;
            bool basicData = false;
            bool pressure = false;
            bool waterVelocity = false;
            bool drainage = false;
            bool darcy = false;

            for (const ValidationMessage &message : messages)
            {
                const std::string &name = message.ruleName;

                if (name.find("Nyitott csővég") != std::string::npos ||
                    name.find("Nyitott csővégek") != std::string::npos ||
                    name.find("Szándékos nyitott vég") != std::string::npos)
                {
                    openEnds = true;
                }

                if (name.find("Izolált") != std::string::npos)
                {
                    isolated = true;
                }

                if (name.find("Hálózat összefüggősége") != std::string::npos ||
                    name.find("Hálózati kapcsolat") != std::string::npos ||
                    name.find("Közvetlen átmérő-eltérés") != std::string::npos ||
                    name.find("Fő hálózatrész") != std::string::npos ||
                    name.find("Különálló hálózatrész") != std::string::npos)
                {
                    connected = true;
                }

                if (name.find("Vízhálózati alapadat") != std::string::npos)
                {
                    basicData = true;
                }

                if (name.find("Projekt nyomáspróba") != std::string::npos)
                {
                    pressure = true;
                }

                if (name.find("Nyílt vízellátási") != std::string::npos)
                {
                    waterVelocity = true;
                }

                if (name.find("Nyílt lefolyási") != std::string::npos)
                {
                    drainage = true;
                }

                if (name.find("Darcy-Weisbach") != std::string::npos)
                {
                    darcy = true;
                }
            }

            const int categoryCount = static_cast<int>(openEnds) + static_cast<int>(isolated) +
                                      static_cast<int>(connected) + static_cast<int>(basicData) +
                                      static_cast<int>(pressure) + static_cast<int>(waterVelocity) +
                                      static_cast<int>(drainage) + static_cast<int>(darcy);

            if (categoryCount > 1) return "Összes ellenőrzés";

            if (openEnds) return "Nyitott csővégek ellenőrzése";

            if (isolated) return "Izolált elemek ellenőrzése";

            if (connected) return "Hálózat összefüggőségének ellenőrzése";

            if (basicData) return "Vízhálózati alapadatok ellenőrzése";

            if (pressure) return "Nyomáspróba";

            if (waterVelocity) return "Vízellátási sebesség";

            if (drainage) return "Lefolyás | Table 6";

            if (darcy) return "Nyomásveszteség | Darcy-Weisbach";

            return "Validáció";
        }

        ImVec4 resultColor(const ValidationMessage &message)
        {
            if (message.isReference) return {0.65f, 0.70f, 0.75f, 1.0f};
            if (message.severity == ValidationSeverity::Error) return {1.00f, 0.30f, 0.22f, 1.0f};
            if (message.severity == ValidationSeverity::Warning) return {1.00f, 0.78f, 0.18f, 1.0f};
            return {0.30f, 0.88f, 0.38f, 1.0f};
        }

        void drawResults(const ValidationPanelModel &model, UiActions &actions)
        {
            if (!ImGui::CollapsingHeader("Eredmények")) return;
            if (!model.validationHasRun || model.messages.empty())
            {
                ImGui::TextDisabled("Nincs eredmény.");
                return;
            }

            ImGui::Text("Vizsgálat: %s", validationRunDisplayName(model.messages));
            ImGui::Separator();

            const auto rows = validationResultOrder(model.messages);
            int errors = 0;
            int warnings = 0;
            for (const auto i : rows)
            {
                const auto &message = model.messages[i];
                if (message.isRedundantSummary) continue;
                errors += message.severity == ValidationSeverity::Error;
                warnings += message.severity == ValidationSeverity::Warning;
            }

            ValidationMessage summary;
            if (errors > 0)
            {
                summary.severity = ValidationSeverity::Error;
                ImGui::TextColored(resultColor(summary), "EREDMÉNY: NEM FELELT MEG | Hiba: %d", errors);
            }
            else if (warnings > 0)
            {
                summary.severity = ValidationSeverity::Warning;
                ImGui::TextColored(resultColor(summary), "MEGFELELT FIGYELMEZTETÉSSEL (%d)", warnings);
            }
            else
            {
                ImGui::TextColored(resultColor(summary), "EREDMÉNY: MEGFELELT");
            }

            if (ImGui::BeginTable("ValidationResultTable", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Vizsgálat", ImGuiTableColumnFlags_WidthFixed, 145.0f);
                ImGui::TableSetupColumn("Részletek##validation_details_column",
                                        ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto i : rows)
                {
                    const auto &message = model.messages[i];
                    if (message.isRedundantSummary) continue;
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (message.objectId == InvalidObjectId)
                    {
                        ImGui::TextDisabled("-");
                    }
                    else
                    {
                        std::string id = "#" + std::to_string(message.objectId);
                        if (message.endpointIndex >= 0) id += " / " + std::to_string(message.endpointIndex);
                        if (ImGui::Selectable(id.c_str(), model.selectedId == message.objectId))
                            model.selectedId = message.objectId;
                    }

                    ImGui::TableSetColumnIndex(1);
                    const auto statusSeparator = message.ruleName.find(" / ");
                    const auto checkName = message.ruleName.substr(0, statusSeparator);
                    ImGui::PushStyleColor(ImGuiCol_Text, resultColor(message));
                    ImGui::TextWrapped("%s", checkName.c_str());
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(2);
                    if ((message.severity != ValidationSeverity::Info || message.isReference) &&
                        ImGui::TreeNodeEx("validation_result_details", ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
                                          "Részletek"))
                    {
                        ImGui::TextWrapped("%s", message.text.c_str());
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            ImGui::Spacing();
            if (fullWidthButton("Mentés Excelhez (CSV)")) actions.exportValidationCsv = true;
            if (model.statusMessage.starts_with("Validációs CSV"))
                ImGui::TextWrapped("%s", model.statusMessage.c_str());
        }

    }

    void drawValidationPanel(const ValidationPanelModel &model, UiActions &actions)
    {
        ImGui::SeparatorText("Validáció");

        if (fullWidthButton("Összes ellenőrzés futtatása"))
        {
            actions.runValidation = true;
        }

        ImGui::Spacing();
        drawCommonValidationPath(model, actions);

        drawQuickChecks(actions);

        drawPressureTest(model, actions);

        drawFlowVelocity(model, actions);

        drawDrainage(model, actions);

        drawDarcy(model, actions);

        drawResults(model, actions);
    }
}
