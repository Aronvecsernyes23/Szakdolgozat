using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;

internal static class FunkcionalisTesztek
{
    private static readonly string[] CaseNames =
    {
        "Automatikus illesztés", "Jelöletlen nyitott vég", "Szándékosan nyitott végek",
        "Közvetlen átmérőeltérés", "Kapcsolat szűkítővel", "A* kerülőút fal mellett",
        "PVC-U nyomvonal PPR végpontok között", "Túl kis lejtés", "Lefolyási kapacitás",
        "Darcy–Weisbach-számítás", "Mentés és visszatöltés", "Darabjegyzék exportja",
        "Nyomáspróba", "Vízsebesség", "Megfelelő lefolyás", "Visszaemelkedés",
        "A* END átmenet 50 → 32 mm", "A* END átmenet 32 → 50 mm",
        "Eltérő méret az A* START-nál", "Akadály az A* END előtt", "Validációs CSV-export"
    };

    private static readonly Regex CaseHeader = new Regex(@"^CASE (?<number>\d{2}) .+$");
    private static readonly Regex TotalLine = new Regex(@"^TOTAL passed=(?<passed>\d+) failed=(?<failed>\d+)\r?$", RegexOptions.Multiline);
    private static readonly Regex ReopenedLine = new Regex(@"^Reopened (?<count>\d+) PCAD test projects\r?$", RegexOptions.Multiline);

    private sealed class CaseResult
    {
        public string Status = "Nem futott le";
        public string Css = "pending";
        public readonly List<string> Details = new List<string>();
    }

    private sealed class Execution
    {
        public int ExitCode = -1;
        public string Output = "";
        public string Errors = "";
        public string Problem = "";
    }

    private static int Main(string[] args)
    {
        try
        {
            string root = AppDomain.CurrentDomain.BaseDirectory;
            string runner = Path.Combine(root, "PlumbCADIntegrationTests.exe");
            DateTimeOffset started = DateTimeOffset.Now;
            string runId = started.ToString("yyyy-MM-dd_HH-mm-ss", CultureInfo.InvariantCulture) + "_" +
                Guid.NewGuid().ToString("N").Substring(0, 6);
            string runFolder = Path.Combine(root, "tesztfutasok", runId);
            Directory.CreateDirectory(runFolder);

            Console.WriteLine("A funkcionális tesztek futnak...");
            Execution execution = RunTests(runner, runFolder);
            CaseResult[] cases = ReadCases(execution);
            File.WriteAllText(Path.Combine(runFolder, "tesztfutas.txt"),
                execution.Output + Environment.NewLine + execution.Errors + Environment.NewLine + execution.Problem,
                Encoding.UTF8);

            string sampleFolder = Path.Combine(runFolder, "pcad");
            string[] samples = Directory.Exists(sampleFolder)
                ? Directory.GetFiles(sampleFolder, "*.pcad").Select(Path.GetFileName).OrderBy(name => name).ToArray()
                : new string[0];
            string report = Path.Combine(runFolder, "eredmenyek.html");
            bool successful = WriteReport(report, started, execution, cases, samples);
            int uiResult = Array.IndexOf(args, "--ui-result");
            if (uiResult >= 0 && uiResult + 1 < args.Length)
            {
                string codes = new string(cases.Select(result =>
                {
                    switch (result.Status)
                    {
                        case "Sikeres": return 'P';
                        case "Sikertelen": return 'F';
                        case "Megszakadt": return 'M';
                        default: return 'S';
                    }
                }).ToArray());
                File.WriteAllText(args[uiResult + 1], runId + "\n" + codes + "\n", Encoding.ASCII);
            }
            Console.WriteLine(report);

            if (!args.Contains("--no-open"))
            {
                try { Process.Start(report); }
                catch (System.ComponentModel.Win32Exception error) { Console.Error.WriteLine(error.Message); }
            }
            return successful ? 0 : 1;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine(error.Message);
            return 2;
        }
    }

    private static Execution RunTests(string runner, string runFolder)
    {
        var result = new Execution();
        if (!File.Exists(runner))
        {
            result.Problem = "Hiányzik a PlumbCADIntegrationTests.exe fájl. Fordítsd le a PlumbCADIntegrationTests célt.";
            return result;
        }

        var start = new ProcessStartInfo(runner, "\"" + runFolder + "\"")
        {
            WorkingDirectory = runFolder,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8
        };
        try
        {
            using (Process process = Process.Start(start))
            {
                var output = process.StandardOutput.ReadToEndAsync();
                var errors = process.StandardError.ReadToEndAsync();
                if (!process.WaitForExit(60000))
                {
                    if (!process.HasExited) process.Kill();
                    process.WaitForExit();
                    result.Problem = "A tesztfuttatás túllépte a 60 másodperces időkorlátot.";
                }
                result.Output = output.GetAwaiter().GetResult();
                result.Errors = errors.GetAwaiter().GetResult();
                result.ExitCode = process.ExitCode;
            }
        }
        catch (System.ComponentModel.Win32Exception error)
        {
            result.Problem = "A tesztfuttató indítási hibája. " + error.Message;
        }
        return result;
    }

    private static CaseResult[] ReadCases(Execution execution)
    {
        var cases = CaseNames.Select(name => new CaseResult()).ToArray();
        CaseResult current = null;
        var seen = new HashSet<int>();
        using (var reader = new StringReader(execution.Output))
        {
            string line;
            while ((line = reader.ReadLine()) != null)
            {
                Match header = CaseHeader.Match(line);
                if (header.Success)
                {
                    int index = int.Parse(header.Groups["number"].Value, CultureInfo.InvariantCulture) - 1;
                    current = null;
                    if (index < 0 || index >= cases.Length || !seen.Add(index))
                    {
                        if (execution.Problem.Length == 0)
                            execution.Problem = "A tesztfuttató esetszámai eltérnek a jelentésben szereplő tesztlistától.";
                        continue;
                    }
                    current = cases[index];
                    current.Status = "Megszakadt";
                }
                else if (current != null && (line == "PASS" || line.StartsWith("FAIL:", StringComparison.Ordinal)))
                {
                    bool passed = line == "PASS";
                    current.Status = passed ? "Sikeres" : "Sikertelen";
                    current.Css = passed ? "pass" : "fail";
                    if (!passed) current.Details.Add(line.Substring(5).Trim());
                    current = null;
                }
                else if (current != null)
                {
                    current.Details.Add(line);
                }
            }
        }
        return cases;
    }

    private static bool WriteReport(string report, DateTimeOffset started, Execution execution,
        CaseResult[] cases, string[] samples)
    {
        int passed = cases.Count(result => result.Css == "pass");
        int failed = cases.Count(result => result.Css == "fail");
        int unfinished = cases.Length - passed - failed;
        Match summary = TotalLine.Match(execution.Output);
        Match reopened = ReopenedLine.Match(execution.Output);
        int reopenedCount = reopened.Success ? int.Parse(reopened.Groups["count"].Value, CultureInfo.InvariantCulture) : 0;
        bool summaryMatches = summary.Success &&
            summary.Groups["passed"].Value == passed.ToString(CultureInfo.InvariantCulture) &&
            summary.Groups["failed"].Value == failed.ToString(CultureInfo.InvariantCulture);
        bool filesChecked = reopened.Success && reopenedCount == samples.Length &&
            Enumerable.Range(1, cases.Length).All(number => samples.Any(name => BelongsTo(name, number)));
        bool successful = execution.ExitCode == 0 && execution.Problem.Length == 0 &&
            passed == cases.Length && summaryMatches && filesChecked;
        var html = new StringBuilder();
        html.AppendLine("<!doctype html><html lang='hu'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
        html.AppendLine("<title>PlumbCAD funkcionális teszteredmények</title><style>");
        html.AppendLine("body{margin:0;background:#f7f8fa;color:#202831;font:16px/1.55 Inter,'Segoe UI',sans-serif}main{max-width:1060px;margin:auto;padding:36px 20px}h1{line-height:1.2;margin:0 0 14px}p{margin:8px 0 16px}a{color:#20588b}table{border-collapse:collapse;width:100%;background:white}th,td{border-bottom:1px solid #d9e0e7;padding:10px;text-align:left;vertical-align:top}th{background:#edf1f5}caption{text-align:left;font-weight:700;padding-bottom:8px}.pass{color:#176138}.fail{color:#aa211e}.pending{color:#855800}.status{font-weight:700}summary{cursor:pointer}pre{font:13px/1.5 'Cascadia Mono',Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere;margin:8px 0}.samples{overflow-wrap:anywhere;min-width:12ch}.table-wrap{overflow-x:auto}</style></head><body><main>");
        html.AppendLine("<h1>PlumbCAD funkcionális teszteredmények</h1>");
        html.AppendFormat("<p>Futtatás ideje {0}</p>\n", started.ToString("yyyy. MM. dd. HH:mm:ss zzz", CultureInfo.InvariantCulture));
        html.AppendFormat("<p class='status {0}'>{1}</p>\n", successful ? "pass" : "fail",
            successful ? "A funkcionális tesztcsomag sikeresen lefutott." : "A funkcionális tesztcsomag hibát jelzett.");
        html.AppendFormat("<p>{0} sikeres, {1} sikertelen, {2} befejezetlen eset. {3} mentett .pcad fájl, {4} ellenőrzött visszatöltés.</p>\n",
            passed, failed, unfinished, samples.Length, reopenedCount);
        if (execution.Problem.Length != 0) html.AppendFormat("<p class='fail'>{0}</p>\n", Escape(execution.Problem));
        if (execution.Errors.Trim().Length != 0) html.AppendFormat("<details open><summary>Futtatói hibaüzenetek</summary><pre>{0}</pre></details>\n", Escape(execution.Errors.Trim()));
        if (execution.ExitCode >= 0 && (!summaryMatches || !filesChecked))
            html.AppendLine("<p class='fail'>Ellenőrizd a naplót. A futtató összesítése vagy a .pcad fájlok visszatöltésének ellenőrzése hiányos.</p>");
        html.AppendLine("<p><a href='tesztfutas.txt'>Teljes futási napló</a></p>");
        if (samples.Length != 0)
        {
            html.AppendLine("<p>A .pcad fájlokat a jelentés melletti <strong>pcad</strong> mappában találod. Húzd a kiválasztott fájlt a PlumbCAD.exe fájljára a megnyitáshoz.</p>");
        }
        html.AppendLine("<div class='table-wrap'><table><caption>Eredmények esetenként</caption><thead><tr><th scope='col'>Eset</th><th scope='col'>Eredmény</th><th scope='col'>Mentett minták</th><th scope='col'>Napló</th></tr></thead><tbody>");
        for (int index = 0; index < cases.Length; index++)
        {
            int number = index + 1;
            CaseResult result = cases[index];
            html.AppendFormat("<tr><th scope='row'>{0:00} {1}</th><td class='status {2}'>{3}</td><td class='samples'>",
                number, Escape(CaseNames[index]), result.Css, result.Status);
            string[] caseSamples = samples.Where(name => BelongsTo(name, number)).ToArray();
            html.Append(caseSamples.Length == 0 ? "—" : string.Join("<br>", caseSamples.Select(name =>
                "<a href='pcad/" + Uri.EscapeDataString(name) + "'>" + Escape(name) + "</a>")));
            html.Append("</td><td>");
            if (result.Details.Count != 0)
                html.AppendFormat("<details{0}><summary>Részletek</summary><pre>{1}</pre></details>",
                    result.Css == "fail" ? " open" : "", Escape(string.Join("\n", result.Details)));
            html.AppendLine("</td></tr>");
        }
        html.AppendLine("</tbody></table></div></main></body></html>");
        File.WriteAllText(report, html.ToString(), Encoding.UTF8);
        return successful;
    }

    private static bool BelongsTo(string name, int number)
    {
        return name.Length >= 3 && name.StartsWith(number.ToString("00", CultureInfo.InvariantCulture), StringComparison.Ordinal);
    }

    private static string Escape(string text)
    {
        return WebUtility.HtmlEncode(text);
    }
}
