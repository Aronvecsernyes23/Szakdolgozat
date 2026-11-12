#include "UI/FunctionalTestsPanel.hpp"

#ifdef PLUMBCAD_HAS_TEST_RUNNER
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif
#include <imgui.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace plumbcad
{
    namespace
    {
        struct TestCase
        {
            const char* name;
            const char* description;
        };

        constexpr std::array Cases{
            TestCase{"Automatikus illesztés", "60 mm résnél nyitva marad, 30 mm résnél a két cső illeszkedik és kölcsönösen kapcsolódik."},
            TestCase{"Jelöletlen nyitott vég", "A program a jelöletlenül nyitott végponthoz rendel hibát."},
            TestCase{"Szándékosan nyitott végek", "A szándékosan nyitottnak jelölt végpontok nem kapnak hibát."},
            TestCase{"Közvetlen átmérőeltérés", "A közvetlenül összekötött Ø110 és Ø75 cső eltérését mindkét mérettel jelzi."},
            TestCase{"Csatlakozás szűkítővel", "Az Ø110 és Ø75 cső között az Ø110/75 szűkítő megfelelő átmenetet ad."},
            TestCase{"A* kerülőút", "A két végpont között összefüggő út épül a fal érintése nélkül."},
            TestCase{"A* anyagválasztás", "A PPR végpontok közötti új elemek a kiválasztott PVC-U kategória méretét és színét kapják."},
            TestCase{"Túl kis lejtés", "Az 1:100 lejtésű Ø75 csőnél megjelenik a lejtéshiba és az 1:40 követelmény."},
            TestCase{"Lefolyási kapacitás", "A 3,00 l/s csúcstérfogatáram meghaladja a 2,80 l/s kapacitást."},
            TestCase{"Darcy-Weisbach", "A cső és a szelep együttes vesztesége 0,0308 bar. A 0,02 bar-os határ figyelmeztetést ad."},
            TestCase{"Mentés és visszatöltés", "Visszaállnak a csövek, a fal, a kapcsolatok, a végpontok és a validációs beállítások."},
            TestCase{"Darabjegyzék exportja", "Négy elem három csoportba kerül, az egyenes csövek összesített hossza 5,50 m."},
            TestCase{"Nyomáspróba", "0,20 bar-os határ mellett a 0,10 bar nyomásesés elfogadható, a 0,40 bar hibás."},
            TestCase{"Vízsebesség", "Az 1, 2 és 3 l/s térfogatáram a három különböző sebességminősítést adja."},
            TestCase{"Megfelelő lefolyás", "Az 1:30 lejtésű Ø75 cső 0,50 l/s mellett megfelel."},
            TestCase{"Visszaemelkedés", "A program felismeri, ha a cső az END irányába emelkedik."},
            TestCase{"A* END 50-ről 32 mm-re", "Egyetlen szűkítő kerül közvetlenül az END elé. A kapcsolat visszatöltés után is megmarad."},
            TestCase{"A* END 32-ről 50 mm-re", "A fordított méretsorrendben az END-hez megfelelő irányú átmenet épül."},
            TestCase{"Eltérés az A* START-nál", "Az útvonal a kiválasztott réz kategóriát használja. A START-hoz nem kerül automatikus szűkítő."},
            TestCase{"Akadály az A* END-nél", "Ha fal van az END-szűkítő helyén, a keresés hibát jelez és megőrzi a hálózatot."},
            TestCase{"Validációs CSV-export", "A CSV megőrzi a státuszsorrendet és helyesen kezeli a pontosvesszőt, idézőjelet és sortörést."}
        };

        using SamplesByCase = std::array<std::vector<std::filesystem::path>, Cases.size()>;

        SamplesByCase collectSamples(const std::filesystem::path& directory)
        {
            SamplesByCase samples;
            std::error_code error;
            std::filesystem::directory_iterator entry(directory, error), end;
            for (; !error && entry != end; entry.increment(error))
            {
                const auto& path = entry->path();
                std::error_code fileError;
                if (path.extension() != ".pcad" || !entry->is_regular_file(fileError)) continue;

                const std::string name = path.filename().string();
                if (name.size() < 3 || name[0] < '0' || name[0] > '9' || name[1] < '0' || name[1] > '9') continue;
                const auto number = static_cast<std::size_t>((name[0] - '0') * 10 + name[1] - '0');
                if (number >= 1 && number <= samples.size()) samples[number - 1].push_back(path);
            }
            for (auto& files : samples) std::sort(files.begin(), files.end());
            return samples;
        }

        void drawSamples(const std::vector<std::filesystem::path>& samples, std::string& sampleToLoad)
        {
            if (samples.empty())
            {
                ImGui::TextDisabled("-");
                return;
            }

            if (samples.size() == 1)
            {
                if (ImGui::SmallButton("Betöltés")) sampleToLoad = samples.front().string();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", samples.front().filename().string().c_str());
                return;
            }

            if (ImGui::SmallButton("Minták...")) ImGui::OpenPopup("PCADMinta");
            if (ImGui::BeginPopup("PCADMinta"))
            {
                for (const auto& sample : samples)
                {
                    if (ImGui::Selectable(sample.filename().string().c_str())) sampleToLoad = sample.string();
                }
                ImGui::EndPopup();
            }
        }

#ifdef PLUMBCAD_HAS_TEST_RUNNER
        std::filesystem::path applicationFolder()
        {
            std::wstring path(32768, L'\0');
            const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0 || length >= path.size()) return {};
            path.resize(length);
            return std::filesystem::path(path).parent_path();
        }

        class TestRun
        {
        public:
            ~TestRun()
            {
                if (process) CloseHandle(process);
            }

            void start()
            {
                statuses = {};
                report.clear();
                message.clear();
                if (runtimeFolder.empty())
                {
                    message = "Az alkalmazás futtatási mappája nem található.";
                    return;
                }
                const auto launcher = runtimeFolder / "FunkcionalisTesztek.exe";
                resultPath = runtimeFolder / "tesztfutasok" /
                    ("ui-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + ".txt");
                std::wstring command = L"\"" + launcher.wstring() + L"\" --no-open --ui-result \"" +
                    resultPath.wstring() + L"\"";
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION info{};
                if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE,
                                    CREATE_NO_WINDOW, nullptr, runtimeFolder.c_str(), &startup, &info))
                {
                    message = "A funkcionális tesztfuttató nem indult el. Ellenőrizd a futtatófájlokat a program mappájában.";
                    return;
                }
                CloseHandle(info.hThread);
                process = info.hProcess;
                message = "A funkcionális tesztek futnak...";
            }

            void poll()
            {
                if (!process || WaitForSingleObject(process, 0) == WAIT_TIMEOUT) return;
                DWORD exitCode = 2;
                GetExitCodeProcess(process, &exitCode);
                CloseHandle(process);
                process = nullptr;

                std::ifstream result(resultPath);
                std::string runId;
                std::string codes;
                std::getline(result, runId);
                std::getline(result, codes);
                result.close();
                std::error_code ignored;
                std::filesystem::remove(resultPath, ignored);
                if (runId.empty() || runId.find_first_not_of("0123456789-_:abcdef") != std::string::npos ||
                    codes.size() != Cases.size() || codes.find_first_not_of("PFMS") != std::string::npos)
                {
                    message = "A futtató nem adott olvasható eredményt.";
                    return;
                }

                std::size_t passed = 0;
                for (std::size_t index = 0; index < codes.size(); ++index)
                {
                    statuses[index] = codes[index];
                    if (codes[index] == 'P') ++passed;
                }
                report = runtimeFolder / "tesztfutasok" / runId / "eredmenyek.html";
                samples = collectSamples(report.parent_path() / "pcad");
                message = std::to_string(passed) + " / " + std::to_string(Cases.size()) + " eset sikeres.";
                if (exitCode != 0) message += " A futtató hibát jelzett.";
            }

            void openReport()
            {
                const auto result = ShellExecuteW(nullptr, L"open", report.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (reinterpret_cast<INT_PTR>(result) <= 32) message = "A jelentés megnyitása nem sikerült.";
            }

            const std::filesystem::path runtimeFolder = applicationFolder();
            HANDLE process = nullptr;
            std::filesystem::path resultPath;
            std::filesystem::path report;
            std::array<char, Cases.size()> statuses{};
            SamplesByCase samples;
            std::string message;
        };
#endif

        void drawStatus(char status)
        {
            switch (status)
            {
            case 'P': ImGui::TextColored(ImVec4(0.37f, 0.86f, 0.49f, 1.0f), "Sikeres"); break;
            case 'F': ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f), "Sikertelen"); break;
            case 'M': ImGui::TextDisabled("Megszakadt"); break;
            case 'S': ImGui::TextDisabled("Kimaradt"); break;
            default: ImGui::TextDisabled("-"); break;
            }
        }
    }

    void drawFunctionalTestsPanel(std::string& sampleToLoad)
    {
        static const auto savedSamples = collectSamples(std::filesystem::path(ASSET_DIR).parent_path() / "tests" / "pcad");
#ifdef PLUMBCAD_HAS_TEST_RUNNER
        static TestRun run;
        run.poll();

        ImGui::BeginDisabled(run.process != nullptr);
        if (ImGui::Button("Funkcionális tesztek futtatása")) run.start();
        ImGui::EndDisabled();
        if (!run.report.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Részletes jelentés")) run.openReport();
        }
        if (!run.message.empty()) ImGui::TextWrapped("%s", run.message.c_str());
#else
        ImGui::TextWrapped("A felületi futtatáshoz az integrációs tesztekkel fordított Windows-változat szükséges.");
#endif

        const auto flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("FunkcionalisTesztek", 4, flags))
        {
            ImGui::TableSetupColumn("Teszteset", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Mit ellenőriz?", ImGuiTableColumnFlags_WidthStretch, 2.1f);
            ImGui::TableSetupColumn("Eredmény", ImGuiTableColumnFlags_WidthFixed, 87.0f);
            ImGui::TableSetupColumn("Projekt", ImGuiTableColumnFlags_WidthFixed, 88.0f);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();
            for (std::size_t index = 0; index < Cases.size(); ++index)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%02d %s", static_cast<int>(index + 1), Cases[index].name);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", Cases[index].description);
                ImGui::TableSetColumnIndex(2);
#ifdef PLUMBCAD_HAS_TEST_RUNNER
                drawStatus(run.statuses[index]);
                const auto& files = run.samples[index].empty() ? savedSamples[index] : run.samples[index];
#else
                drawStatus(0);
                const auto& files = savedSamples[index];
#endif
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(static_cast<int>(index));
                drawSamples(files, sampleToLoad);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}
