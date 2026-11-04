using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Threading;

namespace ClassicAA.Setup {
internal static class Bundle {
    internal static void Extract(Stream source, string folder) {
        string prefix = Path.GetFullPath(folder).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        Directory.CreateDirectory(prefix);
        using (var archive = new ZipArchive(source, ZipArchiveMode.Read)) {
            if (archive.Entries.Count > 256 || archive.Entries.Sum(e => e.Length) > 20 * 1024 * 1024)
                throw new InvalidDataException("The installer package is too large.");
            foreach (var entry in archive.Entries) {
                string path = Path.GetFullPath(Path.Combine(prefix, entry.FullName));
                if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("The installer package contains an invalid path.");
                if (entry.FullName.EndsWith("/")) { Directory.CreateDirectory(path); continue; }
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                using (var input = entry.Open()) using (var output = File.Create(path)) input.CopyTo(output);
            }
        }
    }
}

internal sealed class ReleaseInfo {
    internal Version Version;
    internal string Name, InstallerUrl, ChecksumUrl;
    internal const string ReleasesUrl = "https://github.com/kxrxh/SaintsReborn-ClassicAA/releases";
    internal static ReleaseInfo Parse(string json) {
        var data = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(json);
        string tag = data["tag_name"] as string;
        if (tag == null || !Regex.IsMatch(tag, @"^v\d+\.\d+\.\d+$")) throw new InvalidDataException("Unsupported release version.");
        var info = new ReleaseInfo { Version = new Version(tag.Substring(1)), Name = "SaintsReborn-ClassicAA-Setup-" + tag + ".exe" };
        string checksumName = "SaintsReborn-ClassicAA-" + tag + ".sha256";
        string prefix = ReleasesUrl + "/download/" + tag + "/";
        foreach (var value in (System.Collections.IEnumerable)data["assets"]) {
            var asset = (Dictionary<string, object>)value;
            string name = asset["name"] as string, url = asset["browser_download_url"] as string;
            if (name != info.Name && name != checksumName) continue;
            if (url != prefix + name) throw new InvalidDataException("The release download points outside the project.");
            if (name == info.Name) { if (info.InstallerUrl != null) throw new InvalidDataException("Duplicate installer."); info.InstallerUrl = url; }
            else { if (info.ChecksumUrl != null) throw new InvalidDataException("Duplicate checksum."); info.ChecksumUrl = url; }
        }
        if (info.InstallerUrl == null || info.ChecksumUrl == null) throw new InvalidDataException("This release has no Windows installer. Use the project Releases page.");
        return info;
    }
    internal static void Verify(byte[] bytes, string name, string checksums) {
        string pattern = @"^([0-9a-fA-F]{64})[ \t]+\*?" + Regex.Escape(name) + @"\r?$";
        var matches = Regex.Matches(checksums, pattern, RegexOptions.Multiline);
        if (matches.Count != 1) throw new InvalidDataException("The release checksum is missing or ambiguous.");
        string actual;
        using (var sha = SHA256.Create()) actual = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "");
        if (!actual.Equals(matches[0].Groups[1].Value, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("Download verification failed. The update was not opened.");
    }
}

internal static class ScriptRunner {
    internal static string FindPowerShell() {
        var paths = new List<string> { Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "PowerShell", "7", "pwsh.exe") };
        foreach (string folder in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator)) {
            if (!String.IsNullOrWhiteSpace(folder)) paths.Add(Path.Combine(folder.Trim('"'), "pwsh.exe"));
        }
        return paths.FirstOrDefault(File.Exists);
    }
    internal static int Run(string host, string script, string gameRoot, Action<string> log) {
        gameRoot = Path.GetFullPath(gameRoot).TrimEnd(Path.DirectorySeparatorChar);
        const string command = "$ErrorActionPreference='Stop'; [Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); & $env:CLASSIC_AA_SETUP_SCRIPT -GameRoot $env:CLASSIC_AA_GAME_ROOT; if ($LASTEXITCODE) { exit $LASTEXITCODE }";
        var start = new ProcessStartInfo(host, "-NoLogo -NoProfile -NonInteractive -OutputFormat Text -EncodedCommand " + Convert.ToBase64String(Encoding.Unicode.GetBytes(command))) {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8, WorkingDirectory = Path.GetDirectoryName(script)
        };
        start.EnvironmentVariables["CLASSIC_AA_SETUP_SCRIPT"] = Path.GetFullPath(script);
        start.EnvironmentVariables["CLASSIC_AA_GAME_ROOT"] = gameRoot;
        using (var process = new Process { StartInfo = start }) {
            process.OutputDataReceived += (sender, e) => { if (e.Data != null) log(e.Data); };
            process.ErrorDataReceived += (sender, e) => { if (e.Data != null) log(e.Data); };
            process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine(); process.WaitForExit();
            return process.ExitCode;
        }
    }
}

internal sealed class InstallerWindow {
    internal readonly Window Window;
    internal string UpdatedInstallerPath;
    readonly string package, settingsPath;
    readonly Button install, uninstall, browse, updates;
    readonly TextBox folder, log;
    readonly TextBlock message, status, detail, sourceCheck, shellCheck, gameCheck;
    readonly ProgressBar progress;
    readonly DispatcherTimer timer;
    readonly HttpClient http;
    readonly object logLock = new object();
    string logPath;
    bool busy, recoveryNeeded;
    ReleaseInfo latest;
    internal InstallerWindow(string packageRoot) {
        package = packageRoot;
        settingsPath = Path.Combine(Program.DataRoot, "game-root.txt");
        using (var source = Assembly.GetExecutingAssembly().GetManifestResourceStream("MainWindow.xaml")) Window = (Window)XamlReader.Load(source);
        install = Find<Button>("Install"); uninstall = Find<Button>("Uninstall"); browse = Find<Button>("Browse"); updates = Find<Button>("CheckUpdates");
        folder = Find<TextBox>("GameFolder"); log = Find<TextBox>("Log"); message = Find<TextBlock>("Message");
        status = Find<TextBlock>("InstalledStatus"); detail = Find<TextBlock>("InstalledDetail");
        sourceCheck = Find<TextBlock>("SourceCheck"); shellCheck = Find<TextBlock>("PowerShellCheck"); gameCheck = Find<TextBlock>("GameCheck");
        progress = Find<ProgressBar>("Progress"); Find<TextBlock>("AppVersion").Text = "v" + Program.VersionText;
        http = new HttpClient { Timeout = TimeSpan.FromSeconds(45), MaxResponseContentBufferSize = 64 * 1024 * 1024 };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("SaintsReborn-ClassicAA-Setup/" + Program.VersionText);
        folder.Text = DetectFolder();
        folder.TextChanged += (s, e) => Refresh();
        browse.Click += (s, e) => Browse();
        install.Click += async (s, e) => await Operate(recoveryNeeded);
        uninstall.Click += async (s, e) => await Operate(true);
        updates.Click += async (s, e) => await UpdateInstaller();
        Find<Button>("OpenLog").Click += (s, e) => {
            if (logPath != null) Open(logPath); else message.Text = "A full log is saved when an operation starts.";
        };
        Find<Button>("Releases").Click += (s, e) => Open(ReleaseInfo.ReleasesUrl);
        Find<Button>("GetPowerShell").Click += (s, e) => Open("https://github.com/PowerShell/PowerShell/releases/latest");
        Window.SourceInitialized += (s, e) => {
            IntPtr handle = new WindowInteropHelper(Window).Handle;
            int dark = 1, caption = 0x221C19, text = 0xF4F2F1;
            DwmSetWindowAttribute(handle, 20, ref dark, sizeof(int));
            DwmSetWindowAttribute(handle, 35, ref caption, sizeof(int));
            DwmSetWindowAttribute(handle, 36, ref text, sizeof(int));
        };
        Window.Closing += (s, e) => { if (busy) { e.Cancel = true; message.Text = "Wait for the current operation to finish before closing."; } };
        timer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        timer.Tick += (s, e) => { if (!busy) Refresh(); }; timer.Start();
        Window.Closed += (s, e) => { timer.Stop(); http.Dispose(); };
        Refresh();
    }
    T Find<T>(string name) where T : class {
        var control = Window.FindName(name) as T;
        if (control == null) throw new InvalidDataException("Missing interface control: " + name);
        return control;
    }
    static bool IsGameFolder(string path) {
        try { return !String.IsNullOrWhiteSpace(path) && File.Exists(Path.Combine(path, "dist", "saintsrow.exe")); }
        catch (ArgumentException) { return false; }
        catch (IOException) { return false; }
        catch (NotSupportedException) { return false; }
    }
    void Open(string target) {
        try { Process.Start(new ProcessStartInfo(target) { UseShellExecute = true }); }
        catch (Exception error) { message.Text = error.Message; }
    }
    string DetectFolder() {
        if (File.Exists(settingsPath)) { string saved = File.ReadAllText(settingsPath).Trim(); if (IsGameFolder(saved)) return saved; }
        foreach (var drive in DriveInfo.GetDrives().Where(d => d.IsReady && d.DriveType == DriveType.Fixed)) {
            foreach (string relative in new[] { @"Games\SaintsReborn", "SaintsReborn" }) {
                string candidate = Path.Combine(drive.RootDirectory.FullName, relative); if (IsGameFolder(candidate)) return candidate;
            }
        }
        return "";
    }
    void Browse() {
        try { string selected = FolderPicker.Show(new WindowInteropHelper(Window).Handle); if (selected != null) folder.Text = selected; }
        catch (Exception error) { message.Text = error.Message; }
    }
    bool GameRunning() {
        foreach (var process in Process.GetProcessesByName("saintsrow")) using (process) {
            try { if (String.Equals(process.MainModule.FileName, Path.Combine(folder.Text, "dist", "saintsrow.exe"), StringComparison.OrdinalIgnoreCase)) return true; }
            catch { return true; }
        }
        return false;
    }
    void Refresh() {
        if (busy) return;
        string root = folder.Text.Trim();
        bool valid = IsGameFolder(root), source = valid && File.Exists(Path.Combine(root, "build", "game", "CMakeCache.txt"));
        bool shell = ScriptRunner.FindPowerShell() != null, running = valid && GameRunning();
        string ini = valid ? Path.Combine(root, "dist", "mods", "ClassicAA", "mod.ini") : "";
        bool installed = valid && File.Exists(Path.Combine(root, "dist", "mods", "ClassicAA", "ClassicAA.dll"));
        string version = installed ? Program.IniRead(ini, "mod", "version") : "";
        bool backup = valid && File.Exists(Path.Combine(root, "classic-aa-backups", "active.json"));
        recoveryNeeded = false;
        if (backup) {
            try { var state = new JavaScriptSerializer().Deserialize<Dictionary<string, object>>(File.ReadAllText(Path.Combine(root, "classic-aa-backups", "active.json"))); recoveryNeeded = !(bool)state["complete"]; }
            catch { recoveryNeeded = true; }
        }
        status.Text = installed ? "Installed" + (version.Length == 0 ? "" : " · v" + version) : valid ? "Ready to install" : "Choose your game folder";
        detail.Text = installed ? "Controlled from Options → Display → Anti-aliasing." : valid ? "The installer builds the renderer and menu changes locally." : "The folder containing dist, build and project.";
        if (recoveryNeeded) { status.Text = "Recovery needed"; detail.Text = "The previous operation did not finish. Restore the backup before installing again."; }
        Find<System.Windows.Shapes.Ellipse>("StatusDot").Fill = new SolidColorBrush(installed ? Color.FromRgb(153, 206, 168) : Color.FromRgb(162, 168, 180));
        sourceCheck.Text = source ? "✓  Source/build found" : "—  Source/build required";
        shellCheck.Text = shell ? "✓  PowerShell 7" : "—  PowerShell 7 required";
        Find<Button>("GetPowerShell").Visibility = shell ? Visibility.Collapsed : Visibility.Visible;
        gameCheck.Text = running ? "●  Close Saints Reborn" : "✓  Game closed";
        install.Content = recoveryNeeded ? "Recover installation" : installed ? (version == Program.VersionText ? "Up to date" : "Update to v" + Program.VersionText) : "Install Classic AA";
        install.IsEnabled = valid && shell && !running && (recoveryNeeded || (source && (!installed || version != Program.VersionText)));
        uninstall.IsEnabled = valid && shell && backup && !running;
        uninstall.Content = recoveryNeeded || !installed ? "Recover installation" : "Uninstall";
    }
    void SetBusy(bool value, string text) {
        busy = value; message.Text = text; progress.Visibility = value ? Visibility.Visible : Visibility.Collapsed;
        folder.IsEnabled = browse.IsEnabled = updates.IsEnabled = !value;
        if (value) install.IsEnabled = uninstall.IsEnabled = false; else Refresh();
    }
    void AddLog(string text) {
        lock (logLock) {
            try { if (logPath != null) File.AppendAllText(logPath, text + Environment.NewLine); }
            catch (Exception error) {
                if (!(error is IOException) && !(error is UnauthorizedAccessException)) throw;
                logPath = null; text += "\nFull log could not be written: " + error.Message;
            }
        }
        Window.Dispatcher.BeginInvoke(new Action(() => {
            if (log.Text.Length > 120000) log.Text = log.Text.Substring(log.Text.Length - 60000);
            log.AppendText(text + Environment.NewLine); log.ScrollToEnd();
        }));
    }
    async Task Operate(bool remove) {
        string root = folder.Text.Trim();
        SetBusy(true, remove ? "Restoring your original files…" : "Building and installing Classic AA…");
        try {
            root = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);
            Directory.CreateDirectory(Program.DataRoot); File.WriteAllText(settingsPath, root);
            string logs = Path.Combine(Program.DataRoot, "logs"); Directory.CreateDirectory(logs);
            logPath = Path.Combine(logs, DateTime.Now.ToString("yyyyMMdd-HHmmss-fff") + ".log");
            string shell = ScriptRunner.FindPowerShell(); if (shell == null) throw new InvalidOperationException("Install PowerShell 7, then reopen this app.");
            string ini = Path.Combine(root, "dist", "mods", "ClassicAA", "mod.ini");
            string enabled = Program.IniRead(ini, "settings", "enabled"), bypass = Program.IniRead(ini, "settings", "bypass_menus");
            await Task.Run(() => {
                if (remove || File.Exists(Path.Combine(root, "classic-aa-backups", "active.json"))) {
                    AddLog("Restoring the previous installation.");
                    if (ScriptRunner.Run(shell, Path.Combine(package, "Uninstall.ps1"), root, AddLog) != 0) throw new InvalidOperationException("Restoration stopped. Close the game and retry; details are in the activity log.");
                }
                if (!remove) {
                    AddLog("Installing Classic AA v" + Program.VersionText + ".");
                    if (ScriptRunner.Run(shell, Path.Combine(package, "Install.ps1"), root, AddLog) != 0) throw new InvalidOperationException("Installation stopped. Your backup is retained; details are in the activity log.");
                    foreach (var preference in new[] { new[] { "enabled", enabled }, new[] { "bypass_menus", bypass } })
                        if (preference[1] == "0" || preference[1] == "1") Program.IniWrite(ini, preference[0], preference[1]);
                }
            });
            SetBusy(false, remove ? "Original files restored. The backup has been kept." : "Installed. Enable Classic AA in the mod loader, then select SMAA High in Display settings.");
        } catch (Exception error) {
            Find<Expander>("Activity").IsExpanded = true;
            SetBusy(false, error.Message);
        }
    }
    async Task UpdateInstaller() {
        SetBusy(true, latest == null ? "Checking GitHub Releases…" : "Downloading and verifying the new installer…");
        try {
            if (latest == null) {
                latest = ReleaseInfo.Parse(await http.GetStringAsync("https://api.github.com/repos/kxrxh/SaintsReborn-ClassicAA/releases/latest"));
                if (latest.Version <= Program.Version) { latest = null; SetBusy(false, "You have the latest installer."); return; }
                updates.Content = "Get v" + latest.Version;
                SetBusy(false, "A newer installer is available. Download it to update Classic AA."); return;
            }
            string checksums = await http.GetStringAsync(latest.ChecksumUrl);
            string downloads = Path.Combine(Program.DataRoot, "downloads", latest.Version.ToString()); Directory.CreateDirectory(downloads);
            string destination = Path.Combine(downloads, latest.Name);
            byte[] bytes = await http.GetByteArrayAsync(latest.InstallerUrl);
            ReleaseInfo.Verify(bytes, latest.Name, checksums);
            File.WriteAllBytes(destination, bytes);
            UpdatedInstallerPath = destination; busy = false; Window.Close();
        } catch (Exception error) { SetBusy(false, "Update check or download failed: " + error.Message); }
    }
    [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);
}

internal static class FolderPicker {
    [ComImport, Guid("D57C7288-D4AD-4768-BE02-9D969532D960"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IFileOpenDialog {
        [PreserveSig] int Show(IntPtr owner);
        void SetFileTypes(uint count, IntPtr types); void SetFileTypeIndex(uint index); void GetFileTypeIndex(out uint index);
        void Advise(IntPtr events, out uint cookie); void Unadvise(uint cookie);
        void SetOptions(uint options); void GetOptions(out uint options);
        void SetDefaultFolder(IShellItem item); void SetFolder(IShellItem item); void GetFolder(out IShellItem item); void GetCurrentSelection(out IShellItem item);
        void SetFileName([MarshalAs(UnmanagedType.LPWStr)] string name); void GetFileName(out IntPtr name);
        void SetTitle([MarshalAs(UnmanagedType.LPWStr)] string title); void SetOkButtonLabel([MarshalAs(UnmanagedType.LPWStr)] string label);
        void SetFileNameLabel([MarshalAs(UnmanagedType.LPWStr)] string label); void GetResult(out IShellItem item);
    }
    [ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IShellItem {
        void BindToHandler(IntPtr context, ref Guid handler, ref Guid iid, out IntPtr result);
        void GetParent(out IShellItem parent); void GetDisplayName(uint format, out IntPtr name);
    }
    internal static string Show(IntPtr owner) {
        var dialog = (IFileOpenDialog)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7")));
        try {
            const uint pickFolders = 0x20u, forceFileSystem = 0x40u, pathMustExist = 0x800u;
            uint options; dialog.GetOptions(out options); dialog.SetOptions(options | pickFolders | forceFileSystem | pathMustExist);
            dialog.SetTitle("Select your Saints Reborn folder"); dialog.SetOkButtonLabel("Select folder");
            int result = dialog.Show(owner);
            if (result == unchecked((int)0x800704C7)) return null;
            Marshal.ThrowExceptionForHR(result);
            IShellItem item; dialog.GetResult(out item);
            try { IntPtr name; item.GetDisplayName(0x80058000u, out name); try { return Marshal.PtrToStringUni(name); } finally { Marshal.FreeCoTaskMem(name); } }
            finally { Marshal.ReleaseComObject(item); }
        } finally { Marshal.ReleaseComObject(dialog); }
    }
}

internal static class Program {
    internal static readonly Version Version = new Version(Assembly.GetExecutingAssembly().GetName().Version.ToString(3));
    internal static string VersionText { get { return Version.ToString(); } }
    internal static string DataRoot { get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SaintsRebornClassicAA"); } }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern uint GetPrivateProfileString(string section, string key, string fallback, StringBuilder result, uint size, string path);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool WritePrivateProfileString(string section, string key, string value, string path);
    internal static string IniRead(string path, string section, string key) { var value = new StringBuilder(256); GetPrivateProfileString(section, key, "", value, 256, path); return value.ToString(); }
    internal static void IniWrite(string path, string key, string value) {
        if (!WritePrivateProfileString("settings", key, value, path)) throw new IOException("Could not save the AA preference.");
    }
    [STAThread] static void Main() {
        ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
        string next = null;
        try {
            bool created;
            using (var mutex = new Mutex(true, @"Local\SaintsReborn-ClassicAA-Setup", out created)) {
                if (!created) { MessageBox.Show("Classic AA setup is already open.", "Classic AA"); return; }
                try {
                    string package = Path.Combine(DataRoot, "packages", VersionText);
                    using (var payload = Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip")) Bundle.Extract(payload, package);
                    var app = new Application(); var window = new InstallerWindow(package); app.Run(window.Window); next = window.UpdatedInstallerPath;
                } finally { mutex.ReleaseMutex(); }
            }
            if (next != null) Process.Start(new ProcessStartInfo(next) { UseShellExecute = true });
        } catch (Exception error) { MessageBox.Show(error.Message, "Classic AA setup", MessageBoxButton.OK, MessageBoxImage.Error); }
    }
}
}
