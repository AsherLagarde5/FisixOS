using System;
using System.ComponentModel;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class Program
{
    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetProcessDpiAwarenessContext(IntPtr context);

    [DllImport("user32.dll")]
    private static extern bool SetProcessDPIAware();

    [STAThread]
    private static void Main()
    {
        try
        {
            if (!SetProcessDpiAwarenessContext(new IntPtr(-4)))
                SetProcessDPIAware();
        }
        catch (EntryPointNotFoundException)
        {
            SetProcessDPIAware();
        }
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new SetupForm());
    }
}

internal sealed class CapsuleButton : Button
{
    private readonly Color accent;
    public bool DarkTheme { get; set; }

    public CapsuleButton(string text, Color accentColor)
    {
        Text = text;
        accent = accentColor;
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        FlatAppearance.MouseDownBackColor = Color.Transparent;
        FlatAppearance.MouseOverBackColor = Color.Transparent;
        BackColor = Color.Transparent;
        ForeColor = Color.White;
        Font = new Font("Segoe UI Semibold", 9.5F);
        Cursor = Cursors.Hand;
        UseVisualStyleBackColor = false;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
            ControlStyles.OptimizedDoubleBuffer | ControlStyles.Opaque, true);
    }

    protected override void OnPaintBackground(PaintEventArgs e) { }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        Rectangle bounds = new Rectangle(0, 0, Width - 1, Height - 1);
        Color fill = Capture ? ControlPaint.Dark(accent, 0.15F) :
            (MouseIsOver ? ControlPaint.Light(accent, 0.1F) : accent);
        Color border = ControlPaint.Dark(fill, 0.08F);

        using (GraphicsPath path = CreateRoundedRect(bounds, 6))
        using (SolidBrush brush = new SolidBrush(fill))
        using (Pen pen = new Pen(border, 1F))
        {
            e.Graphics.FillPath(brush, path);
            e.Graphics.DrawPath(pen, path);

            if (Focused)
            {
                Rectangle focusBounds = new Rectangle(2, 2, Width - 5, Height - 5);
                using (GraphicsPath focusPath = CreateRoundedRect(focusBounds, 4))
                using (Pen focusPen = new Pen(Color.FromArgb(190, 255, 255, 255), 1F))
                {
                    focusPen.DashStyle = DashStyle.Dot;
                    e.Graphics.DrawPath(focusPen, focusPath);
                }
            }

            TextRenderer.DrawText(e.Graphics, Text, Font, bounds, ForeColor,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter |
                TextFormatFlags.EndEllipsis);
        }
    }

    private bool mouseOver;
    private bool MouseIsOver { get { return mouseOver; } }

    public static GraphicsPath CreateRoundedRect(Rectangle bounds, int radius)
    {
        GraphicsPath path = new GraphicsPath();
        if (bounds.Width <= 0 || bounds.Height <= 0) return path;
        int d = radius * 2;
        if (d > bounds.Width) d = bounds.Width;
        if (d > bounds.Height) d = bounds.Height;

        path.AddArc(bounds.X, bounds.Y, d, d, 180, 90);
        path.AddArc(bounds.Right - d, bounds.Y, d, d, 270, 90);
        path.AddArc(bounds.Right - d, bounds.Bottom - d, d, d, 0, 90);
        path.AddArc(bounds.X, bounds.Bottom - d, d, d, 90, 90);
        path.CloseFigure();
        return path;
    }

    protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); mouseOver = true; Invalidate(); }
    protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); mouseOver = false; Invalidate(); }
    protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); Invalidate(); }
    protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); Invalidate(); }
}

internal sealed class SetupForm : Form
{
    private static readonly Color Ink = Color.FromArgb(24, 26, 32);
    private static readonly Color AccentBlue = Color.FromArgb(0, 120, 212);
    private static readonly Color TextColor = Color.FromArgb(32, 35, 43);
    private static readonly Color Muted = Color.FromArgb(110, 116, 132);
    private readonly TextBox installPath;
    private readonly CheckBox addToPath;
    private readonly CheckBox desktopShortcut;
    private readonly Label status;
    private readonly ProgressBar progress;
    private readonly CapsuleButton installButton;
    private readonly CapsuleButton browseButton;
    private readonly CapsuleButton themeButton;
    private readonly Label logoBadge;
    private readonly BackgroundWorker worker;
    private readonly Dictionary<Control, Rectangle> designBounds =
        new Dictionary<Control, Rectangle>();
    private readonly Dictionary<Control, Font> designFonts =
        new Dictionary<Control, Font>();
    private readonly Dictionary<Control, Font> responsiveFonts =
        new Dictionary<Control, Font>();
    private float appliedScale = 1F;
    private bool darkTheme;

    public SetupForm()
    {
        Text = "Pathify Setup";
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = true;
        BackColor = Color.White;
        Font = new Font("Segoe UI", 10F);
        AutoScaleMode = AutoScaleMode.Dpi;
        AutoScaleDimensions = new SizeF(96F, 96F);
        ClientSize = new Size(1020, 650);
        MinimumSize = new Size(960, 580);

        Panel content = new Panel();
        content.Location = new Point(340, 0);
        content.Size = new Size(ClientSize.Width - 340, ClientSize.Height);
        content.Anchor = AnchorStyles.Top | AnchorStyles.Bottom |
            AnchorStyles.Left | AnchorStyles.Right;
        content.Padding = new Padding(48, 40, 48, 34);
        content.BackColor = Color.White;
        Controls.Add(content);

        Panel hero = new Panel();
        hero.Location = Point.Empty;
        hero.Size = new Size(340, ClientSize.Height);
        hero.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
        hero.Paint += PaintHero;
        Controls.Add(hero);

        Label logo = new Label();
        logoBadge = logo;
        logo.Text = "P";
        logo.Font = new Font("Segoe UI Semibold", 28F);
        logo.ForeColor = Color.White;
        logo.TextAlign = ContentAlignment.MiddleCenter;
        logo.BackColor = Color.FromArgb(20, 24, 32);
        logo.Location = new Point(36, 36);
        logo.Size = new Size(64, 64);
        hero.Controls.Add(logo);

        Label brand = new Label();
        brand.Text = "PATHIFY";
        brand.Font = new Font("Segoe UI Semibold", 21F);
        brand.ForeColor = Color.White;
        brand.BackColor = Color.Transparent;
        brand.Location = new Point(36, 122);
        brand.Size = new Size(260, 38);
        hero.Controls.Add(brand);

        Label tagline = new Label();
        tagline.Text = "Modern PATH manager & diagnostics for Windows.";
        tagline.Font = new Font("Segoe UI", 11F);
        tagline.ForeColor = Color.FromArgb(200, 210, 228);
        tagline.BackColor = Color.Transparent;
        tagline.Location = new Point(38, 168);
        tagline.Size = new Size(260, 58);
        hero.Controls.Add(tagline);

        Label features = new Label();
        features.Text = "●  Visual PATH inspector\r\n\r\n●  Scope-aware backups\r\n\r\n●  Safe registry restoration\r\n\r\n●  No background services";
        features.Font = new Font("Segoe UI", 10F);
        features.ForeColor = Color.FromArgb(220, 228, 242);
        features.BackColor = Color.Transparent;
        features.Location = new Point(40, 260);
        features.Size = new Size(264, 210);
        hero.Controls.Add(features);

        Label footer = new Label();
        footer.Text = "SAFE & LOCAL  ·  WINDOWS 11 SLATE EDITION";
        footer.Font = new Font("Segoe UI Semibold", 8F);
        footer.ForeColor = Color.FromArgb(160, 175, 200);
        footer.BackColor = Color.Transparent;
        footer.Location = new Point(36, 590);
        footer.Size = new Size(284, 24);
        footer.Anchor = AnchorStyles.Left | AnchorStyles.Bottom;
        hero.Controls.Add(footer);

        Label eyebrow = MakeLabel("WELCOME TO PATHIFY", 1, 22, 30, 270, 24);
        eyebrow.Tag = "accent";
        eyebrow.Font = new Font("Segoe UI Semibold", 9F);
        eyebrow.ForeColor = AccentBlue;
        content.Controls.Add(eyebrow);

        themeButton = new CapsuleButton("Dark mode", Color.FromArgb(50, 56, 68));
        themeButton.Location = new Point(530, 22);
        themeButton.Size = new Size(126, 36);
        themeButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        themeButton.Click += ToggleTheme;
        content.Controls.Add(themeButton);

        Label title = MakeLabel("Install Pathify", 1, 22, 62, 500, 48);
        title.Font = new Font("Segoe UI Semibold", 25F);
        title.ForeColor = Ink;
        content.Controls.Add(title);

        Label description = MakeLabel(
            "Choose where Pathify lives and which conveniences to enable. The installer only changes settings you select.",
            1, 24, 119, 565, 52);
        description.ForeColor = Muted;
        description.Tag = "muted";
        content.Controls.Add(description);

        Label locationLabel = MakeLabel("INSTALL LOCATION", 1, 24, 208, 300, 24);
        locationLabel.Font = new Font("Segoe UI Semibold", 9F);
        locationLabel.ForeColor = TextColor;
        locationLabel.Tag = "primary";
        content.Controls.Add(locationLabel);

        installPath = new TextBox();
        installPath.Text = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            "Webtek", "Pathify");
        installPath.Location = new Point(24, 238);
        installPath.Size = new Size(420, 34);
        installPath.Anchor = AnchorStyles.Top | AnchorStyles.Left;
        installPath.BorderStyle = BorderStyle.FixedSingle;
        installPath.Font = new Font("Segoe UI", 10F);
        content.Controls.Add(installPath);
        installPath.Tag = "input";

        browseButton = new CapsuleButton("Browse", Color.FromArgb(93, 82, 115));
        browseButton.Location = new Point(456, 234);
        browseButton.Size = new Size(98, 40);
        browseButton.Anchor = AnchorStyles.Top | AnchorStyles.Left;
        browseButton.Click += Browse;
        content.Controls.Add(browseButton);

        addToPath = MakeCheckBox(
            "Add Pathify to my user PATH", "Open a new terminal to use the pathify command.", 24, 310);
        addToPath.Checked = true;
        content.Controls.Add(addToPath);

        desktopShortcut = MakeCheckBox(
            "Create a desktop shortcut", "The Start menu shortcut is always created.", 24, 390);
        desktopShortcut.Checked = true;
        content.Controls.Add(desktopShortcut);

        Panel note = new Panel();
        note.Tag = "noteSurface";
        note.Location = new Point(24, 478);
        note.Size = new Size(530, 56);
        note.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
        note.BackColor = Color.FromArgb(247, 244, 252);
        content.Controls.Add(note);

        Label noteText = MakeLabel(
            "Program Files installs require administrator permission. Uninstall any time.",
            1, 14, 10, 502, 40);
        noteText.ForeColor = Muted;
        noteText.Tag = "muted";
        note.Controls.Add(noteText);

        status = MakeLabel("Ready when you are.", 1, 24, 548, 370, 26);
        status.ForeColor = Muted;
        status.Tag = "muted";
        status.Anchor = AnchorStyles.Left | AnchorStyles.Bottom;
        content.Controls.Add(status);

        progress = new ProgressBar();
        progress.Location = new Point(24, 580);
        progress.Size = new Size(530, 7);
        progress.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
        progress.Style = ProgressBarStyle.Continuous;
        content.Controls.Add(progress);

        CapsuleButton close = new CapsuleButton("Close", Color.FromArgb(50, 56, 68));
        close.Location = new Point(340, 604);
        close.Size = new Size(98, 40);
        close.Anchor = AnchorStyles.Right | AnchorStyles.Bottom;
        close.Click += delegate { Close(); };
        content.Controls.Add(close);

        installButton = new CapsuleButton("Install Pathify", AccentBlue);
        installButton.Location = new Point(450, 604);
        installButton.Size = new Size(140, 40);
        installButton.Anchor = AnchorStyles.Right | AnchorStyles.Bottom;
        installButton.Click += Install;
        content.Controls.Add(installButton);

        worker = new BackgroundWorker();
        worker.WorkerReportsProgress = true;
        worker.DoWork += InstallWorker;
        worker.ProgressChanged += delegate(object sender, ProgressChangedEventArgs e)
        {
            progress.Value = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            status.Text = (string)e.UserState;
        };
        worker.RunWorkerCompleted += InstallCompleted;

        CaptureDesignLayout(content);
        content.Resize += delegate { ApplyResponsiveLayout(content); };
        Shown += delegate { ApplyResponsiveLayout(content); };
        ApplyTheme(content);
        FormClosed += delegate
        {
            foreach (Font font in responsiveFonts.Values)
                font.Dispose();
        };
    }

    private void CaptureDesignLayout(Control parent)
    {
        foreach (Control control in parent.Controls)
        {
            designBounds.Add(control, control.Bounds);
            designFonts.Add(control, control.Font);
            CaptureDesignLayout(control);
        }
    }

    private void ApplyResponsiveLayout(Control content)
    {
        float scale = Math.Min(1F, Math.Min(
            content.ClientSize.Width / 680F,
            content.ClientSize.Height / 650F));
        if (scale <= 0F || Math.Abs(scale - appliedScale) < 0.01F)
            return;
        appliedScale = scale;

        foreach (KeyValuePair<Control, Rectangle> item in designBounds)
        {
            Control control = item.Key;
            if (control.IsDisposed)
                continue;

            Rectangle bounds = item.Value;
            control.Bounds = new Rectangle(
                (int)Math.Round(bounds.X * scale),
                (int)Math.Round(bounds.Y * scale),
                Math.Max(1, (int)Math.Round(bounds.Width * scale)),
                Math.Max(1, (int)Math.Round(bounds.Height * scale)));

            Font designFont = designFonts[control];
            Font replacement = new Font(
                designFont.FontFamily,
                Math.Max(7F, designFont.Size * scale),
                designFont.Style,
                designFont.Unit,
                designFont.GdiCharSet,
                designFont.GdiVerticalFont);
            Font previous;
            if (responsiveFonts.TryGetValue(control, out previous))
                previous.Dispose();
            control.Font = replacement;
            responsiveFonts[control] = replacement;
        }

        int left = (int)Math.Round(24 * scale);
        int gap = (int)Math.Round(12 * scale);
        int right = (int)Math.Round(24 * scale);
        int buttonWidth = (int)Math.Round(98 * scale);
        int buttonX = content.ClientSize.Width - right - buttonWidth;
        browseButton.SetBounds(buttonX, (int)Math.Round(234 * scale),
            buttonWidth, (int)Math.Round(40 * scale));
        installPath.SetBounds(left, (int)Math.Round(238 * scale),
            Math.Max(1, buttonX - gap - left), (int)Math.Round(34 * scale));
        int themeWidth = (int)Math.Round(126 * scale);
        themeButton.SetBounds(content.ClientSize.Width - right - themeWidth,
            (int)Math.Round(22 * scale), themeWidth, (int)Math.Round(36 * scale));
    }

    private void ToggleTheme(object sender, EventArgs e)
    {
        darkTheme = !darkTheme;
        themeButton.Text = darkTheme ? "Light mode" : "Dark mode";
        ApplyTheme(themeButton.Parent);
    }

    private void ApplyTheme(Control parent)
    {
        Color contentColor = darkTheme ? Color.FromArgb(24, 26, 32) : Color.White;
        Color panelColor = darkTheme ? Color.FromArgb(32, 35, 43) : Color.FromArgb(244, 246, 250);
        Color primary = darkTheme ? Color.FromArgb(240, 242, 247) : TextColor;
        Color muted = darkTheme ? Color.FromArgb(148, 155, 172) : Muted;

        BackColor = darkTheme ? Color.FromArgb(20, 22, 27) : Color.White;
        themeButton.Parent.BackColor = contentColor;
        logoBadge.BackColor = darkTheme ? Color.FromArgb(18, 20, 26) : Color.FromArgb(32, 36, 46);
        foreach (Control control in parent.Controls)
        {
            string role = control.Tag as string;
            if (role == "noteSurface")
                control.BackColor = panelColor;
            else if (control is Panel)
                control.BackColor = contentColor;
            else if (control is TextBox)
            {
                control.BackColor = darkTheme ? Color.FromArgb(32, 35, 43) : Color.White;
                control.ForeColor = primary;
            }
            else if (control is CheckBox)
            {
                control.BackColor = Color.Transparent;
                control.ForeColor = primary;
            }
            else if (control is Label && role != null)
            {
                control.ForeColor = role == "accent"
                    ? (darkTheme ? Color.FromArgb(90, 170, 255) : AccentBlue)
                    : role == "muted" ? muted : primary;
            }

            CapsuleButton button = control as CapsuleButton;
            if (button != null)
            {
                button.DarkTheme = darkTheme;
                button.Invalidate();
            }
            if (control.HasChildren)
                ApplyTheme(control);
        }
        Invalidate(true);
    }

    private static Label MakeLabel(string text, int tabIndex, int x, int y, int width, int height)
    {
        return new Label
        {
            Text = text,
            TabIndex = tabIndex,
            Location = new Point(x, y),
            Size = new Size(width, height),
            BackColor = Color.Transparent,
            Tag = "primary",
            AutoEllipsis = true
        };
    }

    private static CheckBox MakeCheckBox(string title, string description, int x, int y)
    {
        CheckBox box = new CheckBox();
        box.Text = title + Environment.NewLine + description;
        box.Location = new Point(x, y);
        box.Size = new Size(530, 58);
        box.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        box.Font = new Font("Segoe UI", 10F);
        box.ForeColor = TextColor;
        box.BackColor = Color.Transparent;
        box.CheckAlign = ContentAlignment.TopLeft;
        box.TextAlign = ContentAlignment.TopLeft;
        return box;
    }

    private void PaintHero(object sender, PaintEventArgs e)
    {
        Panel panel = (Panel)sender;
        using (LinearGradientBrush brush = new LinearGradientBrush(
            panel.ClientRectangle,
            darkTheme ? Color.FromArgb(16, 18, 23) : Color.FromArgb(28, 32, 42),
            darkTheme ? Color.FromArgb(30, 36, 48) : Color.FromArgb(44, 52, 70), 35F))
        {
            e.Graphics.FillRectangle(brush, panel.ClientRectangle);
        }
        using (SolidBrush orb = new SolidBrush(Color.FromArgb(18, 255, 255, 255)))
        {
            e.Graphics.FillEllipse(orb, panel.Width - 134, panel.Height - 210, 240, 240);
        }
    }

    private void Browse(object sender, EventArgs e)
    {
        using (FolderBrowserDialog dialog = new FolderBrowserDialog())
        {
            dialog.Description = "Choose the folder where Pathify will be installed.";
            dialog.ShowNewFolderButton = true;
            if (Directory.Exists(installPath.Text.Trim()))
                dialog.SelectedPath = installPath.Text.Trim();
            if (dialog.ShowDialog(this) == DialogResult.OK)
                installPath.Text = dialog.SelectedPath;
        }
    }

    private void Install(object sender, EventArgs e)
    {
        string destination = installPath.Text.Trim();
        if (destination.Length == 0 || destination.IndexOfAny(Path.GetInvalidPathChars()) >= 0)
        {
            MessageBox.Show(this, "Enter a valid installation folder.", "Pathify Setup",
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        if (worker.IsBusy)
            return;
        worker.RunWorkerAsync(new InstallOptions
        {
            Directory = Path.GetFullPath(destination),
            AddToPath = addToPath.Checked,
            DesktopShortcut = desktopShortcut.Checked
        });
        installButton.Enabled = false;
        status.Text = "Preparing installation...";
        progress.Value = 0;
    }

    private static void InstallWorker(object sender, DoWorkEventArgs e)
    {
        BackgroundWorker currentWorker = (BackgroundWorker)sender;
        InstallOptions options = (InstallOptions)e.Argument;
        Directory.CreateDirectory(options.Directory);
        currentWorker.ReportProgress(12, "Installing Pathify...");
        ExtractResource("Pathify.exe", Path.Combine(options.Directory, "Pathify.exe"));
        ExtractResource("PathifyGui.exe", Path.Combine(options.Directory, "PathifyGui.exe"));
        ExtractResource("Module.hello-go.module.json",
            Path.Combine(options.Directory, "modules", "hello-go", "module.json"));
        ExtractResource("Module.hello-go.module.dll",
            Path.Combine(options.Directory, "modules", "hello-go", "module.dll"));
        ExtractResource("Module.hello-rust.module.json",
            Path.Combine(options.Directory, "modules", "hello-rust", "module.json"));
        ExtractResource("Module.hello-rust.module.dll",
            Path.Combine(options.Directory, "modules", "hello-rust", "module.dll"));
        currentWorker.ReportProgress(42, "Adding the uninstaller...");
        ExtractResource("Uninstall-Pathify.ps1",
            Path.Combine(options.Directory, "Uninstall-Pathify.ps1"));

        string startMenu = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "Microsoft", "Windows", "Start Menu", "Programs", "Pathify");
        Directory.CreateDirectory(startMenu);
        CreateShortcut(Path.Combine(startMenu, "Pathify GUI.lnk"),
            Path.Combine(options.Directory, "PathifyGui.exe"), "", options.Directory);
        if (options.DesktopShortcut)
        {
            currentWorker.ReportProgress(60, "Creating shortcuts...");
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            CreateShortcut(Path.Combine(desktop, "Pathify GUI.lnk"),
                Path.Combine(options.Directory, "PathifyGui.exe"), "", options.Directory);
        }

        if (options.AddToPath)
        {
            currentWorker.ReportProgress(76, "Updating your user PATH...");
            AddUserPath(options.Directory);
        }

        currentWorker.ReportProgress(90, "Registering the uninstaller...");
        RegisterUninstaller(options.Directory);
        currentWorker.ReportProgress(100, "Installation complete.");
    }

    private static void ExtractResource(string resourceName, string destination)
    {
        string parentDirectory = Path.GetDirectoryName(destination);
        if (!String.IsNullOrEmpty(parentDirectory))
            Directory.CreateDirectory(parentDirectory);
        using (Stream input = Assembly.GetExecutingAssembly().GetManifestResourceStream(resourceName))
        {
            if (input == null)
                throw new InvalidOperationException("Installer payload is missing: " + resourceName);
            using (FileStream output = new FileStream(destination, FileMode.Create, FileAccess.Write))
                input.CopyTo(output);
        }
    }

    private static void CreateShortcut(string shortcutPath, string target, string arguments, string workingDirectory)
    {
        Type shellType = Type.GetTypeFromProgID("WScript.Shell");
        if (shellType == null)
            throw new InvalidOperationException("Windows Script Host is unavailable; shortcuts could not be created.");
        object shell = Activator.CreateInstance(shellType);
        object shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod,
            null, shell, new object[] { shortcutPath });
        Type shortcutType = shortcut.GetType();
        shortcutType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, new object[] { target });
        shortcutType.InvokeMember("Arguments", BindingFlags.SetProperty, null, shortcut, new object[] { arguments });
        shortcutType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut,
            new object[] { workingDirectory });
        shortcutType.InvokeMember("Description", BindingFlags.SetProperty, null, shortcut,
            new object[] { "Open Pathify PATH manager" });
        shortcutType.InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
        Marshal.FinalReleaseComObject(shortcut);
        Marshal.FinalReleaseComObject(shell);
    }

    private static void AddUserPath(string installDirectory)
    {
        using (RegistryKey key = Registry.CurrentUser.CreateSubKey("Environment"))
        {
            if (key == null)
                throw new InvalidOperationException("Could not open the current user's environment settings.");
            string existing = Convert.ToString(key.GetValue("Path", "",
                RegistryValueOptions.DoNotExpandEnvironmentNames));
            foreach (string entry in existing.Split(';'))
            {
                if (String.Equals(NormalizePath(entry), NormalizePath(installDirectory),
                    StringComparison.OrdinalIgnoreCase))
                    return;
            }
            string updated = String.IsNullOrEmpty(existing) ? installDirectory : existing + ";" + installDirectory;
            RegistryValueKind kind;
            try { kind = key.GetValueKind("Path"); }
            catch (ArgumentException) { kind = RegistryValueKind.ExpandString; }
            key.SetValue("Path", updated, kind);
        }
        IntPtr result;
        SendMessageTimeout(new IntPtr(0xffff), 0x001A, IntPtr.Zero, "Environment", 0x0002, 5000, out result);
    }

    private static string NormalizePath(string value)
    {
        return (value ?? "").Trim().Trim('"').TrimEnd('\\');
    }

    private static void RegisterUninstaller(string installDirectory)
    {
        string uninstallScript = Path.Combine(installDirectory, "Uninstall-Pathify.ps1");
        string command = "\"" + Path.Combine(Environment.SystemDirectory, "WindowsPowerShell",
            "v1.0", "powershell.exe") + "\" -NoProfile -ExecutionPolicy Bypass -File \"" +
            uninstallScript + "\"";
        using (RegistryKey key = Registry.CurrentUser.CreateSubKey(
            "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Pathify"))
        {
            if (key == null)
                throw new InvalidOperationException("Could not register the Pathify uninstaller.");
            key.SetValue("DisplayName", "Pathify");
            key.SetValue("DisplayVersion", "0.1.0");
            key.SetValue("Publisher", "Pathify");
            key.SetValue("InstallLocation", installDirectory);
            key.SetValue("UninstallString", command);
            key.SetValue("NoModify", 1, RegistryValueKind.DWord);
            key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    private static extern IntPtr SendMessageTimeout(
        IntPtr hWnd, uint msg, IntPtr wParam, string lParam,
        uint flags, uint timeout, out IntPtr result);

    private void InstallCompleted(object sender, RunWorkerCompletedEventArgs e)
    {
        installButton.Enabled = true;
        if (e.Error != null)
        {
            status.Text = "Installation could not be completed.";
            MessageBox.Show(this, e.Error.Message, "Pathify Setup",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        installButton.Text = "Open Pathify";
        progress.Value = 100;
        status.Text = "Installed successfully. Open a new terminal to use the command.";
        installButton.Click -= Install;
        installButton.Click += OpenPathify;
    }

    private void OpenPathify(object sender, EventArgs e)
    {
        string executable = Path.Combine(Path.GetFullPath(installPath.Text.Trim()), "PathifyGui.exe");
        if (!File.Exists(executable))
        {
            MessageBox.Show(this, "PathifyGui.exe was not found at the installation location.",
                "Pathify Setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return;
        }
        System.Diagnostics.Process.Start(executable);
        Close();
    }

    private sealed class InstallOptions
    {
        public string Directory;
        public bool AddToPath;
        public bool DesktopShortcut;
    }
}
