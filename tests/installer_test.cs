using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Controls;
using ClassicAA.Setup;

internal static class InstallerTests {
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    static void Reject(Action action, string message) {
        bool rejected = false; try { action(); } catch (InvalidDataException) { rejected = true; }
        Check(rejected, message);
    }
    [STAThread] static int Main(string[] args) {
        string root = Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location), "test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try {
            string package = Path.Combine(root, "package");
            using (var payload = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")) Bundle.Extract(payload, package);
            Check(File.Exists(Path.Combine(package, "Install.ps1")), "Embedded installer is missing.");
            Check(Program.IniRead(Path.Combine(package, "mod.ini"), "mod", "version") == Program.VersionText, "App and mod versions differ.");
            using (var stream = new MemoryStream()) {
                using (var zip = new ZipArchive(stream, ZipArchiveMode.Create, true)) {
                    using (var writer = new StreamWriter(zip.CreateEntry("../outside.txt").Open())) writer.Write("bad");
                }
                stream.Position = 0; Reject(() => Bundle.Extract(stream, Path.Combine(root, "unsafe")), "ZIP traversal was accepted.");
            }
            string tag = "v9.8.7", name = "SaintsReborn-ClassicAA-Setup-" + tag + ".exe", sum = "SaintsReborn-ClassicAA-" + tag + ".sha256";
            string prefix = ReleaseInfo.ReleasesUrl + "/download/" + tag + "/";
            var assets = new[] { new Dictionary<string, object> { { "name", name }, { "browser_download_url", prefix + name } }, new Dictionary<string, object> { { "name", sum }, { "browser_download_url", prefix + sum } } };
            var data = new Dictionary<string, object> { { "tag_name", tag }, { "assets", assets } };
            var json = new JavaScriptSerializer(); var release = ReleaseInfo.Parse(json.Serialize(data));
            Check(release.Version == new Version(9, 8, 7) && release.Name == name, "Release parsing failed.");
            assets[0]["browser_download_url"] = "https://example.com/untrusted.exe";
            Reject(() => ReleaseInfo.Parse(json.Serialize(data)), "An external update URL was accepted.");
            string download = Path.Combine(root, name); File.WriteAllText(download, "test download");
            string hash; using (var sha = SHA256.Create()) hash = BitConverter.ToString(sha.ComputeHash(File.ReadAllBytes(download))).Replace("-", "").ToLowerInvariant();
            ReleaseInfo.Verify(File.ReadAllBytes(download), name, hash + "  " + name + "\n");
            Reject(() => ReleaseInfo.Verify(File.ReadAllBytes(download), name, new string('0', 64) + "  " + name + "\n"), "Wrong checksum was accepted.");
            Reject(() => ReleaseInfo.Verify(File.ReadAllBytes(download), name, hash + "  another.exe\n"), "Wrong checksum filename was accepted.");
            string echo = Path.Combine(root, "echo script.ps1"); File.WriteAllText(echo, "param([string]$GameRoot)\nWrite-Output $GameRoot\nexit 17\n");
            var lines = new List<string>(); string game = Path.Combine(root, "game folder Кириллица"); Directory.CreateDirectory(game);
            int code = ScriptRunner.Run(ScriptRunner.FindPowerShell(), echo, game, line => { lock (lines) lines.Add(line); });
            Check(code == 17 && lines.Contains(game), "PowerShell invocation failed (exit " + code + "): " + String.Join(" | ", lines));
            string legacy = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), @"WindowsPowerShell\v1.0\powershell.exe");
            foreach (string script in new[] { "Install.ps1", "Uninstall.ps1", "Build.ps1" }) {
                var start = new ProcessStartInfo(legacy, "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(package, script) + "\" -GameRoot \"" + game + "\"") { UseShellExecute = false, CreateNoWindow = true, RedirectStandardError = true };
                using (var child = Process.Start(start)) {
                    string errors = child.StandardError.ReadToEnd(); child.WaitForExit();
                    Check(child.ExitCode != 0 && errors.Contains("7.0"), "Windows PowerShell 5.1 did not reject " + script + ": " + errors);
                }
            }
            if (args.Length == 1) {
                File.WriteAllText(echo, "param([string]$GameRoot)\n& '" + Path.Combine(package, "Install.ps1").Replace("'", "''") + "' -GameRoot $GameRoot -CheckOnly\n");
                lines.Clear(); code = ScriptRunner.Run(ScriptRunner.FindPowerShell(), echo, args[0], line => { lock (lines) lines.Add(line); });
                Check(code == 0 && lines.Exists(line => line.StartsWith("Compatible:")), "The real hidden installer preflight failed: " + String.Join(" | ", lines));
            }
            var window = new InstallerWindow(package);
            Check(window.Window.FindName("Install") is Button && window.Window.FindName("Uninstall") is Button, "The real UI did not load.");
            ((TextBox)window.Window.FindName("GameFolder")).Text = "invalid\"folder";
            Check(!((Button)window.Window.FindName("Install")).IsEnabled, "An invalid folder enabled installation.");
            window.Window.Close();
            Console.WriteLine("PASS: embedded payload, ZIP path validation, release URLs, SHA256 verification, script invocation, Unicode paths, exit codes and real WPF controls.");
            return 0;
        } catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        finally { Directory.Delete(root, true); }
    }
}
