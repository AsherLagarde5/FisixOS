using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class PathifyGuiProgram
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
        Application.Run(new PathifyWindow());
    }
}

internal sealed class CapsuleButton : Button
{
    private static readonly Color AccentBlue = Color.FromArgb(0, 103, 192);
    private static readonly Color AccentHover = Color.FromArgb(24, 122, 214);
    private static readonly Color AccentPressed = Color.FromArgb(0, 90, 168);
    private static readonly Color ButtonNormal = Color.FromArgb(43, 45, 53);
    private static readonly Color ButtonHover = Color.FromArgb(54, 57, 67);
    private static readonly Color ButtonPressed = Color.FromArgb(34, 36, 43);
    private static readonly Color BorderNormal = Color.FromArgb(64, 68, 79);
    private static readonly Color BorderHover = Color.FromArgb(88, 93, 108);

    private bool primary;
    private bool hovered;
    private bool pressed;

    public CapsuleButton(string text, bool isPrimary)
    {
        Text = text;
        primary = isPrimary;
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
        AccessibleRole = AccessibleRole.PushButton;
    }

    protected override void OnPaintBackground(PaintEventArgs e) { }

    protected override void OnMouseEnter(EventArgs e)
    {
        base.OnMouseEnter(e);
        hovered = true;
        Invalidate();
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        base.OnMouseLeave(e);
        hovered = false;
        pressed = false;
        Invalidate();
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button == MouseButtons.Left)
        {
            pressed = true;
            Invalidate();
        }
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        pressed = false;
        Invalidate();
    }

    protected override void OnGotFocus(EventArgs e)
    {
        base.OnGotFocus(e);
        Invalidate();
    }

    protected override void OnLostFocus(EventArgs e)
    {
        base.OnLostFocus(e);
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        Rectangle bounds = new Rectangle(0, 0, Width - 1, Height - 1);
        Color fill = primary
            ? (pressed ? AccentPressed : (hovered ? AccentHover : AccentBlue))
            : (pressed ? ButtonPressed : (hovered ? ButtonHover : ButtonNormal));
        Color border = primary
            ? (pressed ? AccentPressed : (hovered ? Color.FromArgb(80, 160, 240) : AccentBlue))
            : (hovered ? BorderHover : BorderNormal);

        using (GraphicsPath path = RoundedRectangle(bounds, 6))
        using (SolidBrush brush = new SolidBrush(fill))
        using (Pen pen = new Pen(border, 1F))
        {
            e.Graphics.FillPath(brush, path);
            e.Graphics.DrawPath(pen, path);

            if (Focused)
            {
                Rectangle focusBounds = new Rectangle(2, 2, Width - 5, Height - 5);
                using (GraphicsPath focusPath = RoundedRectangle(focusBounds, 4))
                using (Pen focusPen = new Pen(Color.FromArgb(200, 255, 255, 255), 1F))
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

    public static GraphicsPath RoundedRectangle(Rectangle bounds, int radius)
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
}


internal sealed class PathifyWindow : Form
{
    private static readonly Color Background = Color.FromArgb(24, 26, 32);
    private static readonly Color Surface = Color.FromArgb(32, 35, 43);
    private static readonly Color SurfaceRaised = Color.FromArgb(43, 47, 57);
    private static readonly Color CardBorder = Color.FromArgb(52, 57, 69);
    private static readonly Color SlateBorder = Color.FromArgb(64, 70, 84);
    private static readonly Color AccentBlue = Color.FromArgb(0, 120, 212);
    private static readonly Color SuccessGreen = Color.FromArgb(54, 184, 123);
    private static readonly Color WarningYellow = Color.FromArgb(245, 171, 53);
    private static readonly Color DangerRed = Color.FromArgb(235, 87, 87);
    private static readonly Color Foreground = Color.FromArgb(240, 242, 247);
    private static readonly Color Muted = Color.FromArgb(148, 155, 172);
    private readonly string executable;
    private readonly Panel sidebar;
    private readonly Panel content;
    private readonly Label pageTitle;
    private readonly Label pageSubtitle;
    private readonly Label brand;
    private readonly Label tagline;
    private readonly Label footer;
    private readonly Label featureHeading;
    private readonly ComboBox scope;
    private readonly CapsuleButton menuToggle;
    private readonly ToolTip navigationTips;
    private readonly List<Button> categoryButtons = new List<Button>();
    private readonly List<Button> navButtons = new List<Button>();
    private readonly List<Control> pageCards = new List<Control>();
    private readonly Dictionary<Control, Point> pageMotion = new Dictionary<Control, Point>();
    private readonly Timer pageTimer;
    private readonly Timer menuTimer;
    private int pageMotionStep;
    private int targetSidebarWidth = 230;
    private string activePage = "Overview";
    private bool focusPresetName;

    public PathifyWindow()
    {
        executable = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Pathify.exe");
        Text = "Pathify";
        StartPosition = FormStartPosition.CenterScreen;
        Size = new Size(1200, 840);
        MinimumSize = new Size(1060, 700);
        BackColor = Background;
        ForeColor = Foreground;
        Font = new Font("Segoe UI", 10F);
        AutoScaleMode = AutoScaleMode.Dpi;

        sidebar = new Panel();
        sidebar.Dock = DockStyle.Left;
        sidebar.Width = 230;
        sidebar.BackColor = Surface;
        Controls.Add(sidebar);
        brand = Label("PATHIFY", 22, 22, 180, 36, 20, AccentBlue);
        brand.Font = new Font("Segoe UI", 20F, FontStyle.Bold);
        sidebar.Controls.Add(brand);
        tagline = Label("PATH management", 24, 58, 180, 24, 9.5F, Muted);
        sidebar.Controls.Add(tagline);
        navigationTips = new ToolTip();
        navigationTips.ShowAlways = true;

        string[] categories = { "PATH", "Files", "System", "Advanced" };
        int y = 104;
        foreach (string category in categories)
        {
            Button button = new Button();
            button.Text = category;
            button.Tag = category;
            button.Location = new Point(12, y);
            button.Size = new Size(206, 40);
            button.TextAlign = ContentAlignment.MiddleLeft;
            button.Padding = new Padding(14, 0, 0, 0);
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 0;
            button.BackColor = Surface;
            button.ForeColor = Muted;
            button.Font = new Font("Segoe UI Semibold", 10F);
            button.Cursor = Cursors.Hand;
            button.AccessibleRole = AccessibleRole.PushButton;
            button.AccessibleName = category + " category";
            navigationTips.SetToolTip(button, category + " category");
            button.Click += delegate(object sender, EventArgs e) { ShowCategory((string)((Button)sender).Tag); };
            categoryButtons.Add(button);
            sidebar.Controls.Add(button);
            y += 44;
        }
        featureHeading = Label("FEATURES", 22, 286, 185, 22, 8.5F, Muted);
        sidebar.Controls.Add(featureHeading);
        footer = Label("SAFE PATH MANAGEMENT\r\nLocal-only  ·  No services",
            22, 0, 195, 52, 8.5F, Muted);
        footer.Anchor = AnchorStyles.Left | AnchorStyles.Bottom;
        footer.Location = new Point(22, ClientSize.Height - 72);
        sidebar.Controls.Add(footer);
        sidebar.Resize += delegate { footer.Top = sidebar.ClientSize.Height - footer.Height - 18; };

        Panel main = new Panel();
        main.Dock = DockStyle.Fill;
        main.BackColor = Background;
        Controls.Add(main);
        Panel header = new Panel();
        header.Dock = DockStyle.Top;
        header.Height = 102;
        header.BackColor = Background;
        main.Controls.Add(header);
        pageTitle = Label("Overview", 28, 18, 430, 38, 22, Foreground);
        pageTitle.Font = new Font("Segoe UI Semibold", 22F);
        header.Controls.Add(pageTitle);
        pageSubtitle = Label("A cleaner PATH starts with a clear view.", 30, 58, 500, 24, 10, Muted);
        header.Controls.Add(pageSubtitle);

        scope = new ComboBox();
        scope.DropDownStyle = ComboBoxStyle.DropDownList;
        scope.Items.AddRange(new object[] { "User PATH", "System PATH" });
        scope.SelectedIndex = 0;
        scope.Location = new Point(640, 24);
        scope.Size = new Size(180, 34);
        scope.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        scope.BackColor = SurfaceRaised;
        scope.ForeColor = Foreground;
        scope.FlatStyle = FlatStyle.Flat;
        header.Controls.Add(scope);
        scope.SelectedIndexChanged += delegate { ShowPage(activePage); };

        content = new Panel();
        content.Dock = DockStyle.Fill;
        content.AutoScroll = true;
        content.BackColor = Background;
        main.Controls.Add(content);
        main.Controls.SetChildIndex(header, 1);

        menuToggle = new CapsuleButton("Hide tabs", false);
        menuToggle.Size = new Size(126, 42);
        menuToggle.Anchor = AnchorStyles.Right | AnchorStyles.Bottom;
        menuToggle.Location = new Point(main.ClientSize.Width - menuToggle.Width - 22,
            main.ClientSize.Height - menuToggle.Height - 18);
        menuToggle.AccessibleName = "Open or close the tabs menu";
        menuToggle.Click += delegate
        {
            targetSidebarWidth = targetSidebarWidth == 230 ? 76 : 230;
            if (!menuTimer.Enabled) menuTimer.Start();
        };
        main.Controls.Add(menuToggle);
        menuToggle.BringToFront();

        sidebar.SendToBack();
        pageTimer = new Timer();
        pageTimer.Interval = 15;
        pageTimer.Tick += AnimatePage;
        menuTimer = new Timer();
        menuTimer.Interval = 15;
        menuTimer.Tick += AnimateSidebar;
        FormClosed += delegate
        {
            pageTimer.Stop();
            menuTimer.Stop();
            pageTimer.Dispose();
            menuTimer.Dispose();
            navigationTips.Dispose();
        };
        main.SizeChanged += delegate
        {
            menuToggle.Left = main.ClientSize.Width - menuToggle.Width - 22;
            menuToggle.Top = main.ClientSize.Height - menuToggle.Height - 18;
        };
        sidebar.SizeChanged += delegate { UpdateNavigationLayout(); };
        ShowCategory("PATH");
    }

    private bool SystemScope { get { return scope.SelectedIndex == 1; } }

    private void ShowCategory(string category)
    {
        string[] pages = category == "PATH"
            ? new string[] { "Overview", "PATH Actions", "Cleaner", "Presets", "Backups" }
            : category == "Files"
                ? new string[] { "Archives", "Nuke" }
                : category == "System"
                    ? new string[] { "ReReg", "Tools" }
                    : new string[] { "Modules", "Import / Export", "Command Center" };
        foreach (Button previous in navButtons)
        {
            sidebar.Controls.Remove(previous);
            previous.Dispose();
        }
        navButtons.Clear();
        foreach (Button button in categoryButtons)
        {
            bool selected = (string)button.Tag == category;
            button.BackColor = selected ? SurfaceRaised : Surface;
            button.ForeColor = selected ? Foreground : Muted;
        }

        int y = 312;
        foreach (string page in pages)
        {
            Button button = new Button();
            button.Text = page;
            button.Tag = page;
            button.Location = new Point(12, y);
            button.Size = new Size(206, 42);
            button.TextAlign = ContentAlignment.MiddleLeft;
            button.Padding = new Padding(24, 0, 0, 0);
            button.FlatStyle = FlatStyle.Flat;
            button.FlatAppearance.BorderSize = 0;
            button.BackColor = Surface;
            button.ForeColor = Muted;
            button.Font = new Font("Segoe UI", 9.5F);
            button.Cursor = Cursors.Hand;
            button.AccessibleRole = AccessibleRole.PushButton;
            button.AccessibleName = page + " feature";
            button.Paint += delegate(object sender, PaintEventArgs e)
            {
                Button b = (Button)sender;
                bool isSel = (string)b.Tag == activePage;
                if (isSel)
                {
                    // Draw accent indicator on the left
                    using (SolidBrush barBrush = new SolidBrush(AccentBlue))
                    {
                        e.Graphics.FillRectangle(barBrush, 0, 8, 3, b.Height - 16);
                    }
                }
            };
            navigationTips.SetToolTip(button, category + " / " + page);
            button.Click += delegate(object sender, EventArgs e) { ShowPage((string)((Button)sender).Tag); };
            navButtons.Add(button);
            sidebar.Controls.Add(button);
            y += 46;
        }
        ShowPage(pages[0]);
    }

    private void ShowPage(string page)
    {
        activePage = page;
        pageTitle.Text = page;
        pageSubtitle.Text = page == "Overview" ? "Live audit, length meters, and health status for current environment." :
            page == "PATH Actions" ? "Preview and apply PATH presets with explicit safety options." :
            page == "Cleaner" ? "Find duplicate entries and safely clean the selected PATH." :
            page == "Nuke" ? "Overwrite and delete one file with explicit recovery limits." :
            page == "ReReg" ? "Create a sensitive backup of the Windows registry hives." :
            page == "Presets" ? "Build, preview, and apply PATH collections." :
            page == "Backups" ? "Restore with confidence. Every restore creates an undo point." :
            page == "Archives" ? "Compress, inspect, restore, and remove folder archives." :
            page == "Modules" ? "Run trusted native Go and Rust extensions." :
            page == "Command Center" ? "Run any Pathify CLI command with its full interactive options." :
            page == "Import / Export" ? "Move your preset library with portable JSON." :
            "Validation, diagnostics, shells, and folder archives.";
        foreach (Button button in navButtons)
        {
            bool selected = (string)button.Tag == page;
            button.BackColor = selected ? SurfaceRaised : Surface;
            button.ForeColor = selected ? Color.White : Muted;
            button.Invalidate();
        }
        while (content.Controls.Count > 0)
        {
            Control previous = content.Controls[0];
            content.Controls.RemoveAt(0);
            previous.Dispose();
        }
        pageCards.Clear();
        try
        {
            if (page == "Overview") BuildOverview();
            else if (page == "PATH Actions") BuildPathActions();
            else if (page == "Cleaner") BuildCleaner();
            else if (page == "Nuke") BuildNuke();
            else if (page == "ReReg") BuildReReg();
            else if (page == "Presets") BuildPresets();
            else if (page == "Backups") BuildBackups();
            else if (page == "Archives") BuildArchives();
            else if (page == "Modules") BuildModules();
            else if (page == "Import / Export") BuildImportExport();
            else if (page == "Command Center") BuildCommandCenter();
            else BuildTools();
            foreach (Control control in content.Controls)
                if (!pageCards.Contains(control)) control.BringToFront();
            foreach (Control card in pageCards)
                card.SendToBack();
            AnimatePageIn();
        }
        catch (Exception error) { ShowError(error); }
    }

    private void BuildOverview()
    {
        string path = ReadPath(SystemScope);
        string[] rawEntries = path.Split(new char[] { ';' }, StringSplitOptions.RemoveEmptyEntries);
        int backupCount = 0;
        try
        {
            string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Pathify", "backups");
            if (Directory.Exists(folder)) backupCount = Directory.GetFiles(folder, "*.json").Length;
        }
        catch (IOException) { }

        // Analyze PATH health
        List<AuditEntry> auditList = new List<AuditEntry>();
        HashSet<string> seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int validCount = 0;
        int brokenCount = 0;
        int duplicateCount = 0;

        for (int i = 0; i < rawEntries.Length; i++)
        {
            string item = rawEntries[i].Trim();
            if (item.Length == 0) continue;
            bool isDup = seen.Contains(item);
            if (!isDup) seen.Add(item);
            else duplicateCount++;

            bool exists = false;
            try { exists = Directory.Exists(item); } catch { }
            if (exists) validCount++;
            else brokenCount++;

            auditList.Add(new AuditEntry
            {
                Index = i + 1,
                Path = item,
                Exists = exists,
                IsDuplicate = isDup
            });
        }

        int pathLen = path.Length;
        int warnLimit = 2047;
        int maxLimit = 8191;

        // Hero Metric Card
        Panel hero = Card(18, 16, content.ClientSize.Width - 54, 138);
        Label heroTitle = Label("Visual PATH Inspector & Health Auditor", 28, 22, 560, 36, 18, Foreground);
        heroTitle.Font = new Font("Segoe UI Semibold", 18F);
        hero.Controls.Add(heroTitle);
        hero.Controls.Add(Label(SystemScope ? "SYSTEM SCOPE" : "USER SCOPE",
            30, 60, 140, 20, 8.5F, AccentBlue));

        Label healthStats = Label(
            rawEntries.Length + " ENTRIES  ·  " + validCount + " VALID  ·  " +
            brokenCount + " MISSING  ·  " + duplicateCount + " DUPLICATES  ·  " + backupCount + " BACKUPS",
            30, 84, Math.Max(300, hero.Width - 60), 22, 9F,
            brokenCount > 0 ? WarningYellow : (duplicateCount > 0 ? Muted : SuccessGreen));
        hero.Controls.Add(healthStats);

        // Path Length Gauge Meter
        string meterStatus = pathLen >= warnLimit ? " (WARNING: Approaching limit!)" : " (Safe)";
        Color meterColor = pathLen >= warnLimit ? DangerRed : (pathLen >= 1500 ? WarningYellow : SuccessGreen);
        Label meterLabel = Label(
            "Length: " + pathLen + " / " + maxLimit + " chars" + meterStatus,
            Math.Max(240, hero.Width - 360), 26, 330, 24, 9.5F, meterColor);
        meterLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        hero.Controls.Add(meterLabel);

        ProgressBar lenMeter = new ProgressBar();
        lenMeter.Location = new Point(Math.Max(240, hero.Width - 360), 54);
        lenMeter.Size = new Size(330, 16);
        lenMeter.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        lenMeter.Maximum = maxLimit;
        lenMeter.Value = Math.Min(maxLimit, Math.Max(0, pathLen));
        hero.Controls.Add(lenMeter);

        // Visual Inspector DataGridView Card
        Panel inspectCard = Card(18, 166, content.ClientSize.Width - 54, 460);
        Label tableTitle = Label("PATH Entries & Diagnostics", 34, 180, 320, 28, 14, Foreground);
        content.Controls.Add(tableTitle);

        TextBox searchBox = TextBox("", 34, 214, Math.Min(340, content.ClientSize.Width - 110), 30, false);
        searchBox.Font = new Font("Segoe UI", 9.5F);
        content.Controls.Add(searchBox);
        Label searchTip = Label("Search / Filter entries", 380, 220, 220, 20, 9F, Muted);
        content.Controls.Add(searchTip);

        DataGridView grid = new DataGridView();
        grid.Location = new Point(34, 252);
        grid.Size = new Size(content.ClientSize.Width - 88, 300);
        grid.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        grid.BackgroundColor = Color.FromArgb(20, 22, 27);
        grid.ForeColor = Foreground;
        grid.GridColor = CardBorder;
        grid.BorderStyle = BorderStyle.None;
        grid.CellBorderStyle = DataGridViewCellBorderStyle.SingleHorizontal;
        grid.ColumnHeadersBorderStyle = DataGridViewHeaderBorderStyle.None;
        grid.EnableHeadersVisualStyles = false;
        grid.RowHeadersVisible = false;
        grid.AllowUserToAddRows = false;
        grid.AllowUserToDeleteRows = false;
        grid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        grid.MultiSelect = false;
        grid.ReadOnly = true;
        grid.Font = new Font("Segoe UI", 9.5F);

        grid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(28, 31, 38);
        grid.ColumnHeadersDefaultCellStyle.ForeColor = Foreground;
        grid.ColumnHeadersDefaultCellStyle.Font = new Font("Segoe UI Semibold", 9.5F);
        grid.ColumnHeadersHeight = 32;

        grid.DefaultCellStyle.BackColor = Color.FromArgb(24, 26, 32);
        grid.DefaultCellStyle.ForeColor = Foreground;
        grid.DefaultCellStyle.SelectionBackColor = Color.FromArgb(43, 47, 57);
        grid.DefaultCellStyle.SelectionForeColor = Color.White;
        grid.RowTemplate.Height = 28;

        grid.Columns.Add("num", "#");
        grid.Columns["num"].Width = 46;
        grid.Columns.Add("status", "Status");
        grid.Columns["status"].Width = 100;
        grid.Columns.Add("dup", "Duplicate");
        grid.Columns["dup"].Width = 90;
        grid.Columns.Add("dir", "Directory Path");
        grid.Columns["dir"].AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;

        Action<string> populateGrid = delegate(string filter)
        {
            grid.Rows.Clear();
            foreach (AuditEntry item in auditList)
            {
                if (!String.IsNullOrEmpty(filter) &&
                    item.Path.IndexOf(filter, StringComparison.OrdinalIgnoreCase) < 0)
                    continue;

                int rowIdx = grid.Rows.Add(
                    item.Index.ToString(),
                    item.Exists ? "VALID" : "MISSING",
                    item.IsDuplicate ? "YES" : "NO",
                    item.Path
                );
                DataGridViewRow row = grid.Rows[rowIdx];
                if (!item.Exists)
                {
                    row.Cells["status"].Style.ForeColor = DangerRed;
                    row.Cells["status"].Style.Font = new Font("Segoe UI Semibold", 9.5F);
                }
                else
                {
                    row.Cells["status"].Style.ForeColor = SuccessGreen;
                }
                if (item.IsDuplicate)
                {
                    row.Cells["dup"].Style.ForeColor = WarningYellow;
                    row.Cells["dup"].Style.Font = new Font("Segoe UI Semibold", 9.5F);
                }
                else
                {
                    row.Cells["dup"].Style.ForeColor = Muted;
                }
            }
        };

        populateGrid("");
        searchBox.TextChanged += delegate
        {
            populateGrid(searchBox.Text.Trim());
        };

        content.Controls.Add(grid);

        // Action Toolbar Buttons
        FlowLayoutPanel actions = new FlowLayoutPanel();
        actions.Location = new Point(34, 564);
        actions.Size = new Size(content.ClientSize.Width - 88, 54);
        actions.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
        actions.WrapContents = true;
        actions.Padding = new Padding(0, 4, 0, 0);
        content.Controls.Add(actions);

        actions.Controls.Add(Button("Copy Selected Path", 0, 0, 160, false, delegate
        {
            if (grid.SelectedRows.Count > 0)
            {
                string sel = Convert.ToString(grid.SelectedRows[0].Cells["dir"].Value);
                Clipboard.SetText(sel);
                ShowOutput("Clipboard", "Copied path to clipboard:\r\n" + sel);
            }
            else
            {
                throw new InvalidOperationException("Select an entry in the table first.");
            }
        }));

        actions.Controls.Add(Button("Clean Missing / Duplicates", 0, 0, 200, false, delegate
        {
            ShowPage("Cleaner");
        }));

        actions.Controls.Add(Button("Backup PATH", 0, 0, 140, true, delegate
        {
            List<string> args = new List<string> { "Backup" };
            if (SystemScope) args.Add("-s");
            ShowOutput("PATH backup", Run(args.ToArray()));
            ShowPage("Overview");
        }));

        actions.Controls.Add(Button("Restore PATH", 0, 0, 140, false, delegate
        {
            string backupScope = SystemScope ? "system" : "user";
            string[] backups = Run("Backups").Split(new string[] { "\r\n", "\n" },
                StringSplitOptions.RemoveEmptyEntries);
            bool hasBackup = Array.Exists(backups, line =>
                line.StartsWith(backupScope + "\t", StringComparison.OrdinalIgnoreCase));
            if (!hasBackup)
                throw new InvalidOperationException("No " + backupScope +
                    " PATH backups found. Create one with Backup PATH first.");
            if (!Confirm("Restore the latest " + backupScope +
                " PATH backup? Pathify will first save the current PATH as an undo backup.")) return;

            List<string> args = new List<string> { "Restore", "--latest", "-f" };
            if (SystemScope) args.Add("-s");
            RunConfirmedCommand(args.ToArray(), SystemScope, "Restore PATH");
        }));

        actions.Controls.Add(Button("Make preset", 0, 0, 130, false, delegate
        {
            focusPresetName = true;
            ShowPage("Presets");
        }));
    }


    private void BuildPathActions()
    {
        Card(18, 16, content.ClientSize.Width - 54, 520);
        content.Controls.Add(Label("Apply a preset", 34, 28, 400, 30, 15, Foreground));
        content.Controls.Add(Label("Preset", 34, 72, 120, 24, 9.5F, Muted));
        ComboBox presets = new ComboBox();
        presets.DropDownStyle = ComboBoxStyle.DropDownList;
        foreach (string name in ReadPresets().Keys) presets.Items.Add(name);
        if (presets.Items.Count > 0) presets.SelectedIndex = 0;
        else
            content.Controls.Add(Label("No presets yet. Create one on the Presets page.", 34, 132,
                content.ClientSize.Width - 100, 20, 9.5F, Muted));
        presets.Location = new Point(34, 98);
        presets.Size = new Size(Math.Min(420, content.ClientSize.Width - 100), 34);
        presets.BackColor = SurfaceRaised;
        presets.ForeColor = Foreground;
        content.Controls.Add(presets);

        CheckBox backup = Check("Create a backup before changing PATH", 34, 154, true);
        CheckBox restart = Check("Restart managed shells", 34, 190, false);
        CheckBox forceRestart = Check("Force-restart managed shells (may lose unsaved work)", 34, 226, false);
        CheckBox restartWindows = Check("Restart Windows after applying", 34, 262, false);
        CheckBox forceRestartWindows = Check("Force-restart Windows", 34, 298, false);
        CheckBox restartExplorer = Check("Restart Explorer", 34, 334, false);
        CheckBox restartDisplay = Check("Restart display drivers", 34, 370, false);
        CheckBox reconnect = Check("Reconnect internet adapters", 34, 406, false);
        restart.CheckedChanged += delegate { if (restart.Checked) forceRestart.Checked = false; };
        forceRestart.CheckedChanged += delegate { if (forceRestart.Checked) restart.Checked = false; };
        restartWindows.CheckedChanged += delegate { if (restartWindows.Checked) forceRestartWindows.Checked = false; };
        forceRestartWindows.CheckedChanged += delegate { if (forceRestartWindows.Checked) restartWindows.Checked = false; };

        FlowLayoutPanel actions = new FlowLayoutPanel();
        actions.Location = new Point(34, 454);
        actions.Size = new Size(content.ClientSize.Width - 90, 52);
        actions.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        actions.WrapContents = true;
        actions.Padding = new Padding(0, 4, 0, 0);
        content.Controls.Add(actions);
        actions.Controls.Add(Button("Preview", 0, 0, 130, false, delegate
        {
            if (presets.SelectedItem == null) throw new InvalidOperationException("Create or select a preset first.");
            List<string> args = new List<string> { "Preview", (string)presets.SelectedItem };
            if (SystemScope) args.Add("-s");
            ShowOutput("PATH preview", Run(args.ToArray()));
        }));
        actions.Controls.Add(Button("Apply preset", 0, 0, 150, true, delegate
        {
            if (presets.SelectedItem == null) throw new InvalidOperationException("Create or select a preset first.");
            string scopeName = SystemScope ? "system-wide" : "user";
            if (!Confirm("Apply preset '" + presets.SelectedItem + "' to the " + scopeName +
                " PATH? Pathify will save a backup first if selected.")) return;

            List<string> args = new List<string> { "Set", (string)presets.SelectedItem, "-f" };
            if (SystemScope) args.Add("-s");
            if (backup.Checked) args.Add("-b");
            if (restart.Checked) args.Add("-r");
            if (forceRestart.Checked) args.Add("--force-restart");
            if (restartWindows.Checked) args.Add("--restart-system");
            if (forceRestartWindows.Checked) args.Add("--force-restart-system");
            if (restartExplorer.Checked) args.Add("--restart-explorer");
            if (restartDisplay.Checked) args.Add("--restart-display-drivers");
            if (reconnect.Checked) args.Add("--reconnect-internet");

            if (restart.Checked || forceRestart.Checked || restartWindows.Checked ||
                forceRestartWindows.Checked || restartExplorer.Checked || restartDisplay.Checked || reconnect.Checked)
            {
                string warning = "Selected Windows restart/reconnection actions can close applications or interrupt active work. Continue?";
                if (!Confirm(warning)) return;
            }
            RunConfirmedCommand(args.ToArray(), SystemScope, "Apply PATH preset");
        }));
        Label safetyNote = Label("System PATH changes may require administrator permission. Restart actions can interrupt active work.",
            34, 512, content.ClientSize.Width - 120, 24, 9, Muted);
        safetyNote.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        content.Controls.Add(safetyNote);
    }

    private void BuildCleaner()
    {
        Panel card = Card(18, 16, content.ClientSize.Width - 54, 540);
        TableLayoutPanel layout = new TableLayoutPanel();
        layout.Dock = DockStyle.Fill;
        layout.Padding = new Padding(16);
        layout.ColumnCount = 1;
        layout.RowCount = 5;
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 56F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40F));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 58F));
        card.Controls.Add(layout);

        Label title = Label("PATH health and cleanup", 0, 0, 380, 30, 15, Foreground);
        layout.Controls.Add(title, 0, 0);

        FlowLayoutPanel targetRow = new FlowLayoutPanel();
        targetRow.Dock = DockStyle.Fill;
        targetRow.WrapContents = false;
        targetRow.Padding = new Padding(0, 7, 0, 0);
        layout.Controls.Add(targetRow, 0, 1);
        targetRow.Controls.Add(Label("Target", 0, 0, 52, 28, 9.5F, Muted));
        ComboBox target = new ComboBox();
        target.DropDownStyle = ComboBoxStyle.DropDownList;
        target.Width = 128;
        target.Items.AddRange(new object[] { "User PATH", "System PATH", "Saved preset" });
        target.SelectedIndex = 0;
        targetRow.Controls.Add(target);
        ComboBox preset = new ComboBox();
        preset.DropDownStyle = ComboBoxStyle.DropDownList;
        preset.Width = 174;
        foreach (string name in ReadPresets().Keys) preset.Items.Add(name);
        if (preset.Items.Count > 0) preset.SelectedIndex = 0;
        preset.Enabled = false;
        targetRow.Controls.Add(preset);

        FlowLayoutPanel options = new FlowLayoutPanel();
        options.Dock = DockStyle.Fill;
        options.WrapContents = true;
        options.Padding = new Padding(0, 4, 0, 0);
        layout.Controls.Add(options, 0, 2);
        CheckBox protectPresets = CleanerOption("Protect preset entries", true,
            "Keep PATH entries referenced by any saved preset.");
        CheckBox removeBroken = CleanerOption("Remove broken entries", false,
            "Opt in to removing missing or unresolved directories.");
        CheckBox removeShadowed = CleanerOption("Remove shadowed entries", false,
            "Opt in to removing later aliases of the same resolved directory.");
        options.Controls.Add(protectPresets);
        options.Controls.Add(removeBroken);
        options.Controls.Add(removeShadowed);

        TextBox results = TextBox("", 0, 0, 300, 200, true);
        results.Dock = DockStyle.Fill;
        results.ReadOnly = true;
        results.Font = new Font("Consolas", 9F);
        layout.Controls.Add(results, 0, 3);
        target.SelectedIndexChanged += delegate
        {
            preset.Enabled = target.SelectedIndex == 2;
            results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
        };
        preset.SelectedIndexChanged += delegate
        {
            if (target.SelectedIndex == 2)
                results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
        };
        protectPresets.CheckedChanged += delegate
        {
            results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
        };
        removeBroken.CheckedChanged += delegate
        {
            results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
        };
        removeShadowed.CheckedChanged += delegate
        {
            results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
        };

        FlowLayoutPanel actions = new FlowLayoutPanel();
        actions.Dock = DockStyle.Fill;
        actions.WrapContents = true;
        actions.Padding = new Padding(0, 6, 0, 0);
        layout.Controls.Add(actions, 0, 4);
        actions.Controls.Add(Button("Scan target", 0, 0, 140, false, delegate
        {
            results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
        }));
        actions.Controls.Add(Button("Apply cleanup", 0, 0, 140, true, delegate
        {
            if (target.SelectedIndex == 2 && preset.SelectedItem == null)
                throw new InvalidOperationException("Create or select a preset first.");
            string targetName = target.SelectedIndex == 2 ? "preset '" + preset.SelectedItem + "'" :
                target.SelectedIndex == 1 ? "system PATH" : "user PATH";
            if (!Confirm("Apply the selected cleanup to the " + targetName +
                "? The preview will be shown and Pathify will create a backup before writing.")) return;
            List<string> arguments = new List<string> { "Clean", "--apply" };
            AddCleanerOptions(arguments, target, preset, protectPresets, removeBroken, removeShadowed);
            bool elevate = target.SelectedIndex == 1;
            RunConfirmedCommand(arguments.ToArray(), elevate, "Clean PATH");
        }));
        actions.Controls.Add(Button("Refresh", 0, 0, 112, false, delegate
        {
            results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
        }));
        results.Text = RunCleanerPreview(target, preset, protectPresets, removeBroken, removeShadowed);
    }

    private static CheckBox CleanerOption(string text, bool selected, string tip)
    {
        CheckBox option = new CheckBox();
        option.Text = text;
        option.Checked = selected;
        option.AutoSize = true;
        option.ForeColor = Foreground;
        option.Font = new Font("Segoe UI", 9F);
        option.Margin = new Padding(0, 4, 18, 0);
        ToolTip tips = new ToolTip();
        tips.SetToolTip(option, tip);
        return option;
    }

    private static void AddCleanerOptions(List<string> arguments, ComboBox target, ComboBox preset,
        CheckBox protectPresets, CheckBox removeBroken, CheckBox removeShadowed)
    {
        if (target.SelectedIndex == 1) arguments.Add("-s");
        if (target.SelectedIndex == 2)
        {
            if (preset.SelectedItem == null)
                throw new InvalidOperationException("Create or select a preset first.");
            arguments.Add("--preset");
            arguments.Add((string)preset.SelectedItem);
        }
        else if (!protectPresets.Checked) arguments.Add("--include-presets");
        if (removeBroken.Checked) arguments.Add("--remove-broken");
        if (removeShadowed.Checked) arguments.Add("--remove-shadowed");
    }

    private string RunCleanerPreview(ComboBox target, ComboBox preset,
        CheckBox protectPresets, CheckBox removeBroken, CheckBox removeShadowed)
    {
        if (target.SelectedIndex == 2 && preset.SelectedItem == null)
            return "No saved presets are available. Create a preset before scanning this target.";
        List<string> arguments = new List<string> { "Clean" };
        AddCleanerOptions(arguments, target, preset, protectPresets, removeBroken, removeShadowed);
        return Run(arguments.ToArray());
    }

    private void BuildNuke()
    {
        Panel card = Card(18, 16, content.ClientSize.Width - 54, 420);
        content.Controls.Add(Label("Overwrite and delete a file", 34, 28, 440, 32, 15, Foreground));
        Label warning = Label(
            "Nuke refuses protected system folders, directories, symbolic links, and files with multiple hard links. It cannot guarantee recovery is impossible; storage hardware, snapshots, and backups may retain data.",
            34, 66, content.ClientSize.Width - 110, 58, 9.5F, Muted);
        warning.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        content.Controls.Add(warning);
        content.Controls.Add(Label("File", 34, 142, 72, 24, 9.5F, Muted));
        TextBox filePath = TextBox("", 34, 168, content.ClientSize.Width - 265, 36, false);
        content.Controls.Add(filePath);
        CapsuleButton browse = Button("Browse", content.ClientSize.Width - 205, 166, 120, false, delegate
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.CheckFileExists = true;
                dialog.Multiselect = false;
                if (dialog.ShowDialog(this) == DialogResult.OK) filePath.Text = dialog.FileName;
            }
        });
        browse.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        content.Controls.Add(browse);
        content.Controls.Add(Label("Overwrite mode", 34, 226, 150, 24, 9.5F, Muted));
        ComboBox mode = new ComboBox();
        mode.DropDownStyle = ComboBoxStyle.DropDownList;
        mode.Location = new Point(34, 252);
        mode.Size = new Size(360, 34);
        mode.Items.AddRange(new object[] { "Shuffle (one random permutation)", "Overkill (7 random overwrite passes)" });
        mode.SelectedIndex = 0;
        mode.BackColor = SurfaceRaised;
        mode.ForeColor = Foreground;
        content.Controls.Add(mode);
        Label modeWarning = Label(
            "Overkill takes longer and writes the file seven times. Neither mode guarantees secure erasure on SSDs, journaled filesystems, snapshots, backups, or cloud-synced folders.",
            34, 304, content.ClientSize.Width - 110, 48, 9F, Muted);
        modeWarning.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        content.Controls.Add(modeWarning);
        content.Controls.Add(Button("Nuke selected file", 34, 368, 190, true, delegate
        {
            if (!File.Exists(filePath.Text))
                throw new InvalidOperationException("Choose an existing file first.");
            string selectedMode = mode.SelectedIndex == 1 ? "overkill" : "shuffle";
            string confirmation = "Overwrite and delete this file using " +
                (selectedMode == "overkill" ? "seven random passes" : "shuffle mode") + "?\r\n\r\n" +
                filePath.Text + "\r\n\r\nRecovery cannot be guaranteed impossible.";
            if (!Confirm(confirmation)) return;
            RunConfirmedCommand(new string[] { "Nuke", filePath.Text, "--mode", selectedMode },
                false, "Nuke");
        }));
        Label confirmationHint = Label("A Windows confirmation dialog appears before any bytes are written.",
            244, 376, content.ClientSize.Width - 330, 30, 9F, Muted);
        confirmationHint.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        content.Controls.Add(confirmationHint);
        card.SendToBack();
    }

    private void BuildReReg()
    {
        Panel card = Card(18, 16, content.ClientSize.Width - 54, 390);
        content.Controls.Add(Label("ReReg registry backup", 34, 28, 440, 32, 15, Foreground));
        Label registrySummary = Label(
            "Creates binary copies of SYSTEM, SOFTWARE, SAM, SECURITY, the default user hive, and loaded user registry hives under %APPDATA%\\Pathify\\registry-backups.",
            34, 72, content.ClientSize.Width - 110, 54, 9.5F, Muted);
        registrySummary.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        content.Controls.Add(registrySummary);
        Label registryWarning = Label(
            "Administrator permission is required. These files may contain credentials and other sensitive data; protect the backup folder. The operation is sequential, not an atomic snapshot, and excludes volatile HKLM\\HARDWARE.",
            34, 144, content.ClientSize.Width - 110, 76, 9.5F, Muted);
        registryWarning.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        content.Controls.Add(registryWarning);
        content.Controls.Add(Button("Back up registry", 34, 250, 180, true, delegate
        {
            if (!Confirm("Create a sensitive registry backup? The process needs administrator permission and will save binary hive files in your Pathify AppData folder.")) return;
            RunConfirmedCommand(new string[] { "ReReg" }, true, "ReReg registry backup");
        }));
        content.Controls.Add(Label("A Windows confirmation dialog appears before the elevated backup starts.",
            34, 306, content.ClientSize.Width - 110, 30, 9F, Muted));
        card.SendToBack();
    }

    private void BuildPresets()
    {
        Panel card = Card(18, 16, content.ClientSize.Width - 54, 570);
        TableLayoutPanel layout = new TableLayoutPanel();
        layout.Dock = DockStyle.Fill;
        layout.Padding = new Padding(16);
        layout.ColumnCount = 1;
        layout.RowCount = 3;
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34F));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 96F));
        card.Controls.Add(layout);

        Dictionary<string, List<string>> presets = ReadPresets();
        Label title = Label("Saved presets", 0, 0, 300, 30, 15, Foreground);
        layout.Controls.Add(title, 0, 0);

        SplitContainer editor = new SplitContainer();
        editor.Dock = DockStyle.Fill;
        editor.Size = new Size(Math.Max(538, card.ClientSize.Width - 32), 400);
        editor.Orientation = Orientation.Vertical;
        editor.Panel1MinSize = 180;
        editor.Panel2MinSize = 350;
        editor.SplitterWidth = 8;
        editor.SplitterDistance = Math.Min(250,
            editor.Width - editor.Panel2MinSize - editor.SplitterWidth);
        layout.Controls.Add(editor, 0, 1);

        TableLayoutPanel presetListLayout = new TableLayoutPanel();
        presetListLayout.Dock = DockStyle.Fill;
        presetListLayout.ColumnCount = 1;
        presetListLayout.RowCount = 2;
        presetListLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        presetListLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 26F));
        presetListLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        editor.Panel1.Controls.Add(presetListLayout);
        Label listTitle = Label("Presets", 0, 0, 200, 24, 9.5F, Muted);
        presetListLayout.Controls.Add(listTitle, 0, 0);
        ListBox list = ListBox(0, 0, 200, 100);
        list.Dock = DockStyle.Fill;
        presetListLayout.Controls.Add(list, 0, 1);
        foreach (string name in presets.Keys) list.Items.Add(name);

        TableLayoutPanel fields = new TableLayoutPanel();
        fields.Dock = DockStyle.Fill;
        fields.Padding = new Padding(10, 0, 0, 0);
        fields.ColumnCount = 1;
        fields.RowCount = 4;
        fields.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 26F));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 38F));
        fields.RowStyles.Add(new RowStyle(SizeType.Absolute, 30F));
        fields.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        editor.Panel2.Controls.Add(fields);
        Label nameLabel = Label("Preset name", 0, 0, 300, 24, 9.5F, Muted);
        fields.Controls.Add(nameLabel, 0, 0);
        TextBox nameBox = TextBox("", 0, 0, 300, 34, false);
        nameBox.Dock = DockStyle.Fill;
        fields.Controls.Add(nameBox, 0, 1);
        Label entriesLabel = Label("PATH entries (one per line)", 0, 0, 360, 24, 9.5F, Muted);
        fields.Controls.Add(entriesLabel, 0, 2);
        TextBox entriesBox = TextBox("", 0, 0, 300, 200, true);
        entriesBox.Dock = DockStyle.Fill;
        fields.Controls.Add(entriesBox, 0, 3);

        if (focusPresetName)
        {
            focusPresetName = false;
            BeginInvoke((MethodInvoker)delegate
            {
                if (!nameBox.IsDisposed) nameBox.Focus();
            });
        }
        list.SelectedIndexChanged += delegate
        {
            string selected = list.SelectedItem as string;
            if (selected == null || !presets.ContainsKey(selected)) return;
            nameBox.Text = selected;
            entriesBox.Lines = presets[selected].ToArray();
        };

        FlowLayoutPanel actions = new FlowLayoutPanel();
        actions.Dock = DockStyle.Fill;
        actions.FlowDirection = FlowDirection.LeftToRight;
        actions.WrapContents = true;
        actions.AutoScroll = false;
        actions.Padding = new Padding(0, 4, 0, 0);
        layout.Controls.Add(actions, 0, 2);
        actions.Controls.Add(Button("New", 0, 0, 92, false, delegate
        {
            list.ClearSelected(); nameBox.Clear(); entriesBox.Clear(); nameBox.Focus();
        }));
        actions.Controls.Add(Button("Save preset", 0, 0, 140, true, delegate
        {
            string name = nameBox.Text.Trim();
            string[] values = EntryLines(entriesBox);
            if (name.Length == 0 || values.Length == 0) throw new InvalidOperationException("Enter a name and at least one PATH entry.");
            if (!Confirm((presets.ContainsKey(name) ? "Replace" : "Create") + " preset '" + name + "'?"))
                return;
            List<string> args = new List<string> { "Preset", presets.ContainsKey(name) ? "Edit" : "Create", name };
            args.AddRange(values);
            if (presets.ContainsKey(name)) args.Add("-f");
            Run(args.ToArray()); ShowPage("Presets");
        }));
        actions.Controls.Add(Button("Remove", 0, 0, 92, false, delegate
        {
            string selected = list.SelectedItem as string;
            if (selected == null) throw new InvalidOperationException("Select a preset first.");
            if (Confirm("Remove preset '" + selected + "'?"))
            {
                Run("Preset", "Remove", selected, "-f"); ShowPage("Presets");
            }
        }));
        actions.Controls.Add(Button("Rename", 0, 0, 110, false, delegate
        {
            string selected = list.SelectedItem as string;
            string newName = nameBox.Text.Trim();
            if (selected == null) throw new InvalidOperationException("Select a preset first.");
            if (newName.Length == 0 || String.Equals(selected, newName, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Enter a different preset name.");
            if (!Confirm("Rename preset '" + selected + "' to '" + newName + "'?")) return;
            Run("Preset", "Rename", selected, newName);
            ShowPage("Presets");
        }));
        actions.Controls.Add(Button("Preview", 0, 0, 110, false, delegate
        {
            string selected = nameBox.Text.Trim();
            if (selected.Length == 0) throw new InvalidOperationException("Select a preset first.");
            List<string> args = new List<string> { "Preview", selected };
            if (SystemScope) args.Add("-s");
            ShowOutput("PATH preview", Run(args.ToArray()));
        }));
        actions.Controls.Add(Button("Apply", 0, 0, 110, true, delegate
        {
            string selected = nameBox.Text.Trim();
            if (selected.Length == 0) throw new InvalidOperationException("Select a preset first.");
            string warning = SystemScope
                ? "This replaces the complete SYSTEM PATH. Missing Windows folders can break applications. Continue?"
                : "Replace the current user PATH with this preset?";
            if (!Confirm(warning)) return;
            List<string> args = new List<string> { "Set", selected, "-f", "-b" };
            if (SystemScope) args.Add("-s");
            if (SystemScope)
                RunConfirmedCommand(args.ToArray(), true, "Apply PATH preset");
            else
            {
                ShowOutput("Pathify", Run(args.ToArray()));
                ShowPage("Overview");
            }
        }));
    }

    private void BuildBackups()
    {
        Card(18, 16, content.ClientSize.Width - 54, 540);
        content.Controls.Add(Label("PATH backups", 34, 28, 500, 30, 15, Foreground));
        ListBox list = ListBox(34, 68, content.ClientSize.Width - 100, 360);
        string[] lines = Run("Backups").Split(new string[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries);
        foreach (string line in lines) if (line.Contains("\t")) list.Items.Add(line);
        content.Controls.Add(list);
        FlowLayoutPanel actions = new FlowLayoutPanel();
        actions.Location = new Point(34, 444);
        actions.Size = new Size(content.ClientSize.Width - 100, 92);
        actions.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        actions.WrapContents = true;
        actions.Padding = new Padding(0, 4, 0, 0);
        content.Controls.Add(actions);
        actions.Controls.Add(Button("Back up current PATH", 0, 0, 210, true, delegate
        {
            List<string> args = new List<string> { "Backup" };
            if (SystemScope) args.Add("-s");
            ShowOutput("Backup", Run(args.ToArray())); ShowPage("Backups");
        }));
        actions.Controls.Add(Button("Restore latest", 0, 0, 160, false, delegate
        {
            if (!Confirm("Restore the latest backup? The current PATH will be saved as an undo backup.")) return;
            List<string> args = new List<string> { "Restore", "--latest", "-f" };
            if (SystemScope) args.Add("-s");
            RunConfirmedCommand(args.ToArray(), SystemScope, "Restore PATH");
        }));
        CapsuleButton selectedRestore = Button("Restore selected", 0, 0, 180, false, delegate
        {
            if (list.SelectedItem == null) throw new InvalidOperationException("Select a backup first.");
            string[] parts = ((string)list.SelectedItem).Split('\t');
            if (!Confirm("Restore " + parts[parts.Length - 1] + "? The current PATH will be saved as an undo backup.")) return;
            List<string> args = new List<string> { "Restore", "--backup", parts[parts.Length - 1], "-f" };
            RunConfirmedCommand(args.ToArray(),
                parts[0].StartsWith("system", StringComparison.OrdinalIgnoreCase), "Restore PATH");
        });
        actions.Controls.Add(selectedRestore);
        actions.Controls.Add(Button("Restore from file", 0, 0, 170, false, delegate
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Title = "Select a Pathify backup";
                dialog.Filter = "Path backups (*.json;*.txt)|*.json;*.txt|All files (*.*)|*.*";
                string backupDirectory = Path.Combine(Environment.GetFolderPath(
                    Environment.SpecialFolder.ApplicationData), "Pathify", "backups");
                if (Directory.Exists(backupDirectory)) dialog.InitialDirectory = backupDirectory;
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                if (!Confirm("Restore PATH from " + dialog.FileName + "? The current PATH will be backed up first.")) return;
                string filename = Path.GetFileName(dialog.FileName);
                string selectedDirectory = Path.GetDirectoryName(Path.GetFullPath(dialog.FileName));
                if (!String.Equals(selectedDirectory, Path.GetFullPath(backupDirectory),
                    StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Select a backup from %APPDATA%\\Pathify\\backups; Pathify restores managed backup files by name.");
                List<string> args = new List<string> { "Restore", "--backup", filename, "-f" };
                RunConfirmedCommand(args.ToArray(),
                    filename.StartsWith("system-path-", StringComparison.OrdinalIgnoreCase), "Restore PATH");
            }
        }));
    }

    private void BuildArchives()
    {
        Panel createCard = Card(18, 16, content.ClientSize.Width - 54, 188);
        TableLayoutPanel createLayout = new TableLayoutPanel();
        createLayout.Dock = DockStyle.Fill;
        createLayout.Padding = new Padding(16);
        createLayout.ColumnCount = 1;
        createLayout.RowCount = 3;
        createLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        createLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38F));
        createLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48F));
        createLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        createCard.Controls.Add(createLayout);
        createLayout.Controls.Add(Label("Create archive", 0, 0, 320, 30, 15, Foreground), 0, 0);

        TableLayoutPanel folderRow = new TableLayoutPanel();
        folderRow.Dock = DockStyle.Fill;
        folderRow.ColumnCount = 2;
        folderRow.RowCount = 1;
        folderRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        folderRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 112F));
        folderRow.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        createLayout.Controls.Add(folderRow, 0, 1);
        TextBox folder = TextBox("", 0, 0, 300, 34, false);
        folder.Dock = DockStyle.Fill;
        folder.Margin = new Padding(0, 4, 10, 4);
        folderRow.Controls.Add(folder, 0, 0);
        CapsuleButton browseFolder = Button("Browse", 0, 0, 100, false, delegate
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Select a folder to compress and replace with a restore shortcut.";
                dialog.ShowNewFolderButton = false;
                if (dialog.ShowDialog(this) == DialogResult.OK) folder.Text = dialog.SelectedPath;
            }
        });
        browseFolder.Dock = DockStyle.Fill;
        browseFolder.Margin = new Padding(0, 2, 0, 2);
        folderRow.Controls.Add(browseFolder, 1, 0);

        FlowLayoutPanel createActions = new FlowLayoutPanel();
        createActions.Dock = DockStyle.Fill;
        createActions.FlowDirection = FlowDirection.LeftToRight;
        createActions.WrapContents = true;
        createActions.Padding = new Padding(0, 4, 0, 0);
        createLayout.Controls.Add(createActions, 0, 2);
        ComboBox mode = new ComboBox();
        mode.DropDownStyle = ComboBoxStyle.DropDownList;
        mode.Items.AddRange(new object[] { "Max", "Balanced", "Fast" });
        mode.SelectedIndex = 0;
        mode.Width = 150;
        mode.Height = 32;
        mode.Margin = new Padding(0, 5, 12, 0);
        mode.BackColor = SurfaceRaised;
        mode.ForeColor = Foreground;
        createActions.Controls.Add(mode);
        createActions.Controls.Add(Button("Archive folder", 0, 0, 160, true, delegate
        {
            if (!Directory.Exists(folder.Text)) throw new InvalidOperationException("Select an existing folder.");
            if (!Confirm("Pathify will create and verify the ZIP, then remove the original folder and replace it with a restore shortcut. Continue?"))
                return;
            RunConfirmedCommand(new string[] { "Archive", "Create", folder.Text, "--mode",
                ((string)mode.SelectedItem).ToLowerInvariant() }, true, "Create archive");
        }));

        Panel manageCard = Card(18, 222, content.ClientSize.Width - 54, 330);
        TableLayoutPanel manageLayout = new TableLayoutPanel();
        manageLayout.Dock = DockStyle.Fill;
        manageLayout.Padding = new Padding(16);
        manageLayout.ColumnCount = 1;
        manageLayout.RowCount = 3;
        manageLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        manageLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 38F));
        manageLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48F));
        manageLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        manageCard.Controls.Add(manageLayout);
        manageLayout.Controls.Add(Label("Manage ZIP archive", 0, 0, 360, 30, 15, Foreground), 0, 0);

        TableLayoutPanel archiveRow = new TableLayoutPanel();
        archiveRow.Dock = DockStyle.Fill;
        archiveRow.ColumnCount = 2;
        archiveRow.RowCount = 1;
        archiveRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        archiveRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 112F));
        archiveRow.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        manageLayout.Controls.Add(archiveRow, 0, 1);
        TextBox archive = TextBox("", 0, 0, 300, 34, false);
        archive.Dock = DockStyle.Fill;
        archive.Margin = new Padding(0, 4, 10, 4);
        archiveRow.Controls.Add(archive, 0, 0);
        CapsuleButton browseArchive = Button("Browse", 0, 0, 100, false, delegate
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Title = "Select a Pathify folder archive";
                dialog.Filter = "ZIP archives (*.zip)|*.zip|All files (*.*)|*.*";
                string archiveDirectory = Path.Combine(Path.GetDirectoryName(executable), "archive");
                if (Directory.Exists(archiveDirectory)) dialog.InitialDirectory = archiveDirectory;
                if (dialog.ShowDialog(this) == DialogResult.OK) archive.Text = dialog.FileName;
            }
        });
        browseArchive.Dock = DockStyle.Fill;
        browseArchive.Margin = new Padding(0, 2, 0, 2);
        archiveRow.Controls.Add(browseArchive, 1, 0);

        FlowLayoutPanel archiveActions = new FlowLayoutPanel();
        archiveActions.Dock = DockStyle.Fill;
        archiveActions.FlowDirection = FlowDirection.LeftToRight;
        archiveActions.WrapContents = true;
        archiveActions.Padding = new Padding(0, 4, 0, 0);
        manageLayout.Controls.Add(archiveActions, 0, 2);
        archiveActions.Controls.Add(Button("View info", 0, 0, 120, false, delegate
        {
            if (!File.Exists(archive.Text)) throw new InvalidOperationException("Select an existing ZIP archive.");
            ShowOutput("Archive info", Run("Archive", "Info", archive.Text));
        }));
        archiveActions.Controls.Add(Button("Restore to folder", 0, 0, 170, true, delegate
        {
            if (!File.Exists(archive.Text)) throw new InvalidOperationException("Select an existing ZIP archive.");
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Choose where to restore the archived folder.";
                dialog.ShowNewFolderButton = true;
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                string destination = Path.Combine(dialog.SelectedPath, Path.GetFileNameWithoutExtension(archive.Text));
                if (!Confirm("Restore the archive to " + destination + "?")) return;
                ShowOutput("Archive restore", Run("Archive", "Restore", archive.Text, destination));
            }
        }));
        archiveActions.Controls.Add(Button("Delete archive", 0, 0, 150, false, delegate
        {
            if (!File.Exists(archive.Text)) throw new InvalidOperationException("Select an existing ZIP archive.");
            if (!Confirm("Permanently delete this ZIP archive? This cannot be undone.")) return;
            RunConfirmedCommand(new string[] { "Archive", "Delete", archive.Text }, true, "Delete archive");
        }));
    }

    private void BuildCommandCenter()
    {
        Card(18, 16, content.ClientSize.Width - 54, 520);
        content.Controls.Add(Label("Run any Pathify CLI command", 34, 28, 520, 30, 15, Foreground));
        content.Controls.Add(Label("Use the same arguments as Pathify in a terminal. Quotes are supported for paths with spaces.",
            34, 64, content.ClientSize.Width - 100, 24, 9.5F, Muted));
        content.Controls.Add(Label("Command arguments", 34, 98, 260, 24, 9.5F, Muted));
        TextBox command = TextBox("Doctor", 34, 126, content.ClientSize.Width - 100, 38, false);
        command.Font = new Font("Consolas", 10F);
        content.Controls.Add(command);
        CheckBox elevate = Check("Run as administrator", 34, 178, false);
        content.Controls.Add(elevate);
        content.Controls.Add(Button("Run in terminal", 34, 220, 180, true, delegate
        {
            string[] args = SplitCommand(command.Text);
            if (args.Length == 0) throw new InvalidOperationException("Enter a Pathify command.");
            RunInteractive(args, elevate.Checked);
        }));
        content.Controls.Add(Button("Run and capture output", 232, 220, 220, false, delegate
        {
            string[] args = SplitCommand(command.Text);
            if (args.Length == 0) throw new InvalidOperationException("Enter a Pathify command.");
            if (IsInteractiveCommand(args))
                throw new InvalidOperationException("This command may prompt for confirmation or user input. Use Run in terminal instead.");
            ShowOutput("Pathify command", Run(args));
        }));
        TextBox output = TextBox(
            "Examples:\r\nList\r\nInit\r\nSet \"Preset name\" -b\r\nRestore --latest\r\nArchive Create \"C:\\\\Folder\" --mode max\r\nPreset Rename old-name new-name\r\n\r\nUse `--help` for any command's complete CLI options.",
            34, 276, content.ClientSize.Width - 100, 228, true);
        output.ReadOnly = true;
        output.Font = new Font("Consolas", 9F);
        content.Controls.Add(output);
        command.KeyDown += delegate(object sender, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Enter && e.Control)
            {
                e.SuppressKeyPress = true;
                try
                {
                    string[] args = SplitCommand(command.Text);
                    if (args.Length == 0) throw new InvalidOperationException("Enter a Pathify command.");
                    RunInteractive(args, elevate.Checked);
                }
                catch (Exception error) { ShowError(error); }
            }
        };
    }

    private bool IsInteractiveCommand(string[] arguments)
    {
        string command = arguments[0].ToLowerInvariant();
        if (command == "list" || command == "backups" || command == "preview" ||
            command == "validate" || command == "doctor" || command == "--help" ||
            command == "-h" || command == "help")
            return false;
        if (command == "archive" && arguments.Length > 1 &&
            arguments[1].Equals("info", StringComparison.OrdinalIgnoreCase))
            return false;
        return true;
    }

    private void RunInteractive(string[] arguments, bool elevated)
    {
        if (!File.Exists(executable))
            throw new FileNotFoundException("Pathify.exe was not found beside the GUI.", executable);
        ProcessStartInfo info = new ProcessStartInfo();
        info.FileName = executable;
        info.Arguments = JoinArguments(arguments);
        info.WorkingDirectory = Path.GetDirectoryName(executable);
        info.UseShellExecute = true;
        if (elevated) info.Verb = "runas";
        try
        {
            Process.Start(info);
        }
        catch (Win32Exception error)
        {
            if (error.NativeErrorCode != 1223) throw;
        }
    }

    private void RunConfirmedCommand(string[] arguments, bool elevated, string title,
        bool skipCliConfirmation = true)
    {
        if (!File.Exists(executable))
            throw new FileNotFoundException("Pathify.exe was not found beside the GUI.", executable);

        List<string> confirmedArguments = new List<string>(arguments);
        if (skipCliConfirmation) confirmedArguments.Add("--gui-confirmed");
        if (!elevated)
        {
            ShowOutput(title, Run(confirmedArguments.ToArray()));
            return;
        }

        ProcessStartInfo info = new ProcessStartInfo();
        info.FileName = executable;
        info.Arguments = JoinArguments(confirmedArguments);
        info.WorkingDirectory = Path.GetDirectoryName(executable);
        info.UseShellExecute = true;
        info.Verb = "runas";
        info.WindowStyle = ProcessWindowStyle.Hidden;
        try
        {
            Process process = Process.Start(info);
            if (process == null)
                throw new InvalidOperationException("Windows did not start the elevated operation.");
            process.EnableRaisingEvents = true;
            process.Exited += delegate
            {
                if (IsDisposed || !IsHandleCreated) return;
                BeginInvoke((MethodInvoker)delegate
                {
                    if (process.ExitCode == 0)
                        ShowOutput(title, "The elevated operation completed successfully.");
                    else
                        ShowError(new InvalidOperationException(
                            "The elevated operation failed with exit code " + process.ExitCode +
                            ". Its console output was hidden."));
                    process.Dispose();
                });
            };
        }
        catch (Win32Exception error)
        {
            if (error.NativeErrorCode != 1223) throw;
        }
    }

    private void BuildModules()
    {
        Panel card = Card(18, 16, content.ClientSize.Width - 54, 470);
        TableLayoutPanel layout = new TableLayoutPanel();
        layout.Dock = DockStyle.Fill;
        layout.Padding = new Padding(16);
        layout.ColumnCount = 1;
        layout.RowCount = 6;
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 34F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 44F));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 22F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 36F));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48F));
        card.Controls.Add(layout);

        Label title = Label("Native modules", 0, 0, 360, 30, 15, Foreground);
        title.Dock = DockStyle.Fill;
        layout.Controls.Add(title, 0, 0);
        Label warning = Label(
            "Modules are native DLLs and can run arbitrary code with your account permissions. Only run modules you trust.",
            0, 0, 500, 40, 9.5F, Muted);
        warning.Dock = DockStyle.Fill;
        layout.Controls.Add(warning, 0, 1);

        TableLayoutPanel moduleRow = new TableLayoutPanel();
        moduleRow.Dock = DockStyle.Fill;
        moduleRow.ColumnCount = 2;
        moduleRow.RowCount = 1;
        moduleRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 32F));
        moduleRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 68F));
        layout.Controls.Add(moduleRow, 0, 2);
        ListBox modules = ListBox(0, 0, 200, 200);
        modules.Dock = DockStyle.Fill;
        modules.Margin = new Padding(0, 0, 10, 0);
        moduleRow.Controls.Add(modules, 0, 0);
        TextBox details = TextBox("Select a module to view its details.", 0, 0, 300, 200, true);
        details.ReadOnly = true;
        details.Dock = DockStyle.Fill;
        details.Margin = new Padding(0);
        moduleRow.Controls.Add(details, 1, 0);

        Label argumentsLabel = Label("Arguments (one quoted command-line value at a time)",
            0, 0, 500, 24, 9.5F, Muted);
        argumentsLabel.Dock = DockStyle.Fill;
        layout.Controls.Add(argumentsLabel, 0, 3);
        TextBox arguments = TextBox("", 0, 0, 300, 36, false);
        arguments.Dock = DockStyle.Fill;
        arguments.Margin = new Padding(0);
        arguments.Font = new Font("Consolas", 10F);
        layout.Controls.Add(arguments, 0, 4);

        FlowLayoutPanel actions = new FlowLayoutPanel();
        actions.Dock = DockStyle.Fill;
        actions.FlowDirection = FlowDirection.LeftToRight;
        actions.WrapContents = true;
        actions.Margin = new Padding(0);
        actions.Padding = new Padding(0, 3, 0, 0);
        layout.Controls.Add(actions, 0, 5);
        CheckBox elevate = Check("Run module as administrator", 0, 0, false);
        elevate.Size = new Size(280, 36);
        elevate.Margin = new Padding(0, 3, 12, 0);
        actions.Controls.Add(elevate);
        actions.Controls.Add(Button("Run selected module", 0, 0, 190, true, delegate
        {
            if (modules.SelectedItem == null) throw new InvalidOperationException("Select a module first.");
            string name = (string)modules.SelectedItem;
            string question = "Run native module '" + name + "'? DLL modules are not sandboxed and can access your files.";
            if (!Confirm(question)) return;
            List<string> command = new List<string> { name };
            command.AddRange(SplitCommand(arguments.Text));
            if (elevate.Checked)
                RunConfirmedCommand(command.ToArray(), true, "Run native module", false);
            else
                ShowOutput("Native module", Run(command.ToArray()));
        }));
        actions.Controls.Add(Button("Refresh list", 0, 0, 130, false, delegate
        {
            ShowPage("Modules");
        }));
        Dictionary<string, string[]> moduleInfo = new Dictionary<string, string[]>(
            StringComparer.OrdinalIgnoreCase);
        modules.SelectedIndexChanged += delegate
        {
            if (modules.SelectedItem == null)
            {
                details.Text = "Select a module to view its details.";
                return;
            }
            string[] fields;
            if (!moduleInfo.TryGetValue((string)modules.SelectedItem, out fields)) return;
            details.Text = "Command: " + fields[0] + Environment.NewLine +
                "Version: " + (fields.Length > 1 ? fields[1] : "unknown") + Environment.NewLine +
                "Description: " + (fields.Length > 2 ? fields[2] : "") + Environment.NewLine +
                "DLL: " + (fields.Length > 3 ? fields[3] : "");
        };

        string output = Run("Module", "List");
        foreach (string line in output.Split(new string[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries))
        {
            if (line.StartsWith("No native modules", StringComparison.OrdinalIgnoreCase)) continue;
            if (!line.Contains("\t")) continue;
            string[] fields = line.Split('\t');
            if (fields.Length == 0 || String.IsNullOrWhiteSpace(fields[0])) continue;
            moduleInfo[fields[0]] = fields;
            modules.Items.Add(fields[0]);
        }
        if (modules.Items.Count == 0)
            details.Text = "No modules are installed." + Environment.NewLine +
                "Install a module folder under Pathify\\modules or %APPDATA%\\Pathify\\modules.";
    }

    private void AnimatePageIn()
    {
        pageMotion.Clear();
        pageMotionStep = 0;
        foreach (Control control in content.Controls)
        {
            Point target = control.Location;
            pageMotion.Add(control, target);
            control.Location = new Point(target.X, target.Y + 14);
        }
        if (pageMotion.Count > 0 && !pageTimer.Enabled) pageTimer.Start();
    }

    private void AnimatePage(object sender, EventArgs e)
    {
        pageMotionStep++;
        double progress = Math.Min(1.0, pageMotionStep / 9.0);
        double eased = 1.0 - Math.Pow(1.0 - progress, 3.0);
        List<Control> finished = new List<Control>();
        foreach (KeyValuePair<Control, Point> motion in pageMotion)
        {
            if (motion.Key.IsDisposed)
            {
                finished.Add(motion.Key);
                continue;
            }
            int y = motion.Value.Y + (int)Math.Round(14 * (1.0 - eased));
            motion.Key.Location = new Point(motion.Value.X, y);
            if (progress >= 1.0) finished.Add(motion.Key);
        }
        foreach (Control control in finished) pageMotion.Remove(control);
        if (pageMotion.Count == 0) pageTimer.Stop();
    }

    private void AnimateSidebar(object sender, EventArgs e)
    {
        int delta = targetSidebarWidth - sidebar.Width;
        if (Math.Abs(delta) <= 3)
        {
            sidebar.Width = targetSidebarWidth;
            UpdateNavigationLayout();
            menuTimer.Stop();
            menuToggle.Text = targetSidebarWidth == 230 ? "Hide tabs" : "Show tabs";
            menuToggle.AccessibleDescription = targetSidebarWidth == 230
                ? "Close the tabs menu" : "Open the tabs menu";
            return;
        }
        sidebar.Width += Math.Sign(delta) * Math.Max(3, Math.Abs(delta) / 4);
    }

    private void UpdateNavigationLayout()
    {
        bool expanded = sidebar.Width > 150;
        brand.Text = expanded ? "PATHIFY" : "P";
        tagline.Visible = expanded;
        footer.Visible = expanded;
        featureHeading.Visible = expanded;
        foreach (Button button in categoryButtons)
        {
            button.Left = 10;
            button.Width = Math.Max(48, sidebar.ClientSize.Width - 20);
            button.TextAlign = expanded ? ContentAlignment.MiddleLeft : ContentAlignment.MiddleCenter;
            button.Padding = expanded ? new Padding(14, 0, 0, 0) : Padding.Empty;
            button.Text = expanded ? (string)button.Tag : CategoryGlyph((string)button.Tag);
        }
        foreach (Button button in navButtons)
        {
            button.Left = 10;
            button.Width = Math.Max(48, sidebar.ClientSize.Width - 20);
            button.TextAlign = expanded ? ContentAlignment.MiddleLeft : ContentAlignment.MiddleCenter;
            button.Padding = expanded ? new Padding(24, 0, 0, 0) : Padding.Empty;
            button.Text = expanded ? (string)button.Tag : NavigationGlyph((string)button.Tag);
        }
    }

    private static string CategoryGlyph(string category)
    {
        if (category == "PATH") return "P";
        if (category == "Files") return "F";
        if (category == "System") return "S";
        return "A";
    }

    private static string NavigationGlyph(string page)
    {
        if (page == "Overview") return "O";
        if (page == "PATH Actions") return "P";
        if (page == "Cleaner") return "C";
        if (page == "Nuke") return "N";
        if (page == "ReReg") return "R";
        if (page == "Presets") return "S";
        if (page == "Backups") return "B";
        if (page == "Archives") return "A";
        if (page == "Modules") return "M";
        if (page == "Import / Export") return "I";
        if (page == "Tools") return "T";
        if (page == "Command Center") return "C";
        return "⋯";
    }

    private static CheckBox Check(string text, int x, int y, bool isChecked)
    {
        return new CheckBox
        {
            Text = text,
            Location = new Point(x, y),
            Size = new Size(700, 28),
            Checked = isChecked,
            ForeColor = Foreground,
            BackColor = Color.Transparent,
            Font = new Font("Segoe UI", 9.5F),
            AutoEllipsis = true
        };
    }

    private void BuildImportExport()
    {
        Card(18, 16, content.ClientSize.Width - 54, 204);
        content.Controls.Add(Label("Import presets", 34, 28, 300, 30, 15, Foreground));
        TextBox importPath = TextBox("", 34, 76, content.ClientSize.Width - 220, 34, false);
        content.Controls.Add(importPath);
        CapsuleButton browseImport = Button("Browse", content.ClientSize.Width - 164, 74, 100, false, delegate
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Filter = "JSON files (*.json)|*.json|All files (*.*)|*.*";
                if (dialog.ShowDialog(this) == DialogResult.OK) importPath.Text = dialog.FileName;
            }
        });
        browseImport.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        content.Controls.Add(browseImport);
        content.Controls.Add(Button("Import presets", 34, 132, 160, true, delegate
        {
            if (!File.Exists(importPath.Text)) throw new InvalidOperationException("Choose an existing JSON file.");
            if (Confirm("Import presets? Matching names will be replaced."))
                ShowOutput("Import", Run("Import", importPath.Text, "-f"));
        }));

        Card(18, 242, content.ClientSize.Width - 54, 204);
        content.Controls.Add(Label("Export presets", 34, 254, 300, 30, 15, Foreground));
        TextBox exportPath = TextBox(Path.Combine(Environment.GetFolderPath(
            Environment.SpecialFolder.MyDocuments), "pathify-presets.json"),
            34, 302, content.ClientSize.Width - 220, 34, false);
        content.Controls.Add(exportPath);
        CapsuleButton browseExport = Button("Browse", content.ClientSize.Width - 164, 300, 100, false, delegate
        {
            using (SaveFileDialog dialog = new SaveFileDialog())
            {
                dialog.Filter = "JSON files (*.json)|*.json";
                dialog.FileName = Path.GetFileName(exportPath.Text);
                if (dialog.ShowDialog(this) == DialogResult.OK) exportPath.Text = dialog.FileName;
            }
        });
        browseExport.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        content.Controls.Add(browseExport);
        content.Controls.Add(Button("Export presets", 34, 358, 160, true, delegate
        {
            if (File.Exists(exportPath.Text) && !Confirm("Overwrite this file?")) return;
            ShowOutput("Export", Run("Export", exportPath.Text, "-f"));
        }));
    }

    private void BuildTools()
    {
        Card(18, 16, content.ClientSize.Width - 54, 238);
        content.Controls.Add(Label("Diagnostics and utilities", 34, 28, 400, 30, 15, Foreground));
        FlowLayoutPanel utilityActions = new FlowLayoutPanel();
        utilityActions.Location = new Point(34, 68);
        utilityActions.Size = new Size(content.ClientSize.Width - 100, 52);
        utilityActions.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        utilityActions.WrapContents = true;
        utilityActions.Padding = new Padding(0, 4, 0, 0);
        content.Controls.Add(utilityActions);
        utilityActions.Controls.Add(Button("Validate presets", 0, 0, 180, false,
            delegate { ShowOutput("Validation", Run("Validate")); }));
        utilityActions.Controls.Add(Button("Run Doctor", 0, 0, 150, false,
            delegate { ShowOutput("Pathify Doctor", Run("Doctor")); }));
        utilityActions.Controls.Add(Button("Archive a folder", 0, 0, 180, true,
            delegate { ShowCategory("Files"); ShowPage("Archives"); }));
        content.Controls.Add(Label("Managed shell", 34, 140, 200, 24, 9.5F, Muted));
        ComboBox shell = new ComboBox();
        shell.DropDownStyle = ComboBoxStyle.DropDownList;
        shell.Items.AddRange(new object[] { "pwsh", "powershell", "cmd" });
        shell.SelectedIndex = 0;
        shell.Location = new Point(34, 168);
        shell.Size = new Size(190, 34);
        shell.BackColor = SurfaceRaised;
        shell.ForeColor = Foreground;
        content.Controls.Add(shell);
        content.Controls.Add(Button("Launch shell", 244, 164, 150, false,
            delegate { ShowOutput("Launch shell", Run("Launch", (string)shell.SelectedItem)); }));

        Card(18, 272, content.ClientSize.Width - 54, 164);
        content.Controls.Add(Label("Advanced command", 34, 284, 300, 30, 15, Foreground));
        TextBox command = TextBox("Set <Preset> -rse", 34, 330, content.ClientSize.Width - 220, 36, false);
        content.Controls.Add(command);
        CapsuleButton openCommand = Button("Open command", content.ClientSize.Width - 164, 328, 130, false, delegate
        {
            if (String.IsNullOrWhiteSpace(command.Text)) throw new InvalidOperationException("Enter a Pathify command.");
            string[] args = SplitCommand(command.Text);
            RunInteractive(args, false);
        });
        openCommand.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        content.Controls.Add(openCommand);
    }

    private Panel Card(int x, int y, int width, int height)
    {
        Panel panel = new Panel();
        panel.Location = new Point(x, y);
        panel.Size = new Size(width, height);
        panel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        panel.BackColor = Surface;
        panel.Paint += delegate(object sender, PaintEventArgs e)
        {
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            Rectangle rect = new Rectangle(0, 0, panel.Width - 1, panel.Height - 1);
            using (GraphicsPath path = CapsuleButton.RoundedRectangle(rect, 8))
            using (Pen pen = new Pen(CardBorder, 1F))
            {
                e.Graphics.DrawPath(pen, path);
            }
        };
        content.Controls.Add(panel);
        pageCards.Add(panel);
        panel.SendToBack();
        return panel;
    }

    private static Label Label(string text, int x, int y, int width, int height, float size, Color color)
    {
        return new Label
        {
            Text = text, Location = new Point(x, y), Size = new Size(width, height),
            ForeColor = color, BackColor = Color.Transparent,
            Font = new Font("Segoe UI", size), AutoEllipsis = true
        };
    }

    private static TextBox TextBox(string text, int x, int y, int width, int height, bool multiline)
    {
        return new TextBox
        {
            Text = text, Location = new Point(x, y), Size = new Size(width, height),
            Multiline = multiline, ScrollBars = multiline ? ScrollBars.Vertical : ScrollBars.None,
            AcceptsReturn = multiline, BackColor = Color.FromArgb(20, 22, 27),
            ForeColor = Foreground, BorderStyle = BorderStyle.FixedSingle,
            Font = new Font("Segoe UI", 10F), Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right
        };
    }

    private static ListBox ListBox(int x, int y, int width, int height)
    {
        return new ListBox
        {
            Location = new Point(x, y), Size = new Size(width, height),
            BackColor = Color.FromArgb(20, 22, 27), ForeColor = Foreground,
            BorderStyle = BorderStyle.FixedSingle, Font = new Font("Segoe UI", 10F),
            Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right
        };
    }

    private static CapsuleButton Button(string text, int x, int y, int width, bool primary, EventHandler click)
    {
        CapsuleButton button = new CapsuleButton(text, primary);
        button.Location = new Point(x, y);
        button.Size = new Size(width, 42);
        button.Click += delegate(object sender, EventArgs e)
        {
            try { click(sender, e); }
            catch (Exception error) { ShowError(error); }
        };
        return button;
    }

    private string ReadPath(bool system)
    {
        RegistryKey key = system
            ? Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Session Manager\Environment")
            : Registry.CurrentUser.OpenSubKey("Environment");
        if (key == null) throw new InvalidOperationException("Could not read the selected PATH registry scope.");
        using (key)
            return Convert.ToString(key.GetValue("Path", "",
                RegistryValueOptions.DoNotExpandEnvironmentNames));
    }

    private string Run(params string[] arguments)
    {
        if (!File.Exists(executable))
            throw new FileNotFoundException("Pathify.exe was not found beside the GUI.", executable);
        ProcessStartInfo info = new ProcessStartInfo();
        info.FileName = executable;
        // Ensure the CLI does not prompt for confirmations when invoked from the GUI.
        List<string> argsList = new List<string>(arguments);
        if (!argsList.Contains("--gui-confirmed")) argsList.Add("--gui-confirmed");
        info.Arguments = JoinArguments(argsList);
        info.WorkingDirectory = Path.GetDirectoryName(executable);
        info.UseShellExecute = false;
        info.CreateNoWindow = true;
        info.RedirectStandardOutput = true;
        info.RedirectStandardError = true;
        using (Process process = new Process())
        {
            StringBuilder output = new StringBuilder();
            StringBuilder errors = new StringBuilder();
            object sync = new object();
            process.StartInfo = info;
            process.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data != null) lock (sync) output.AppendLine(e.Data);
            };
            process.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                if (e.Data != null) lock (sync) errors.AppendLine(e.Data);
            };
            if (!process.Start()) throw new InvalidOperationException("Could not start Pathify.");
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            process.WaitForExit();
            process.WaitForExit();
            string result;
            string errorText;
            lock (sync) { result = output.ToString().Trim(); errorText = errors.ToString().Trim(); }
            if (process.ExitCode != 0)
                throw new InvalidOperationException(String.IsNullOrEmpty(errorText) ? result : errorText + Environment.NewLine + result);
            return String.IsNullOrEmpty(errorText) ? result : result + Environment.NewLine + errorText;
        }
    }

    private static string JoinArguments(IEnumerable<string> arguments)
    {
        List<string> quoted = new List<string>();
        foreach (string argument in arguments) quoted.Add(QuoteArgument(argument));
        return String.Join(" ", quoted.ToArray());
    }

    private static string QuoteArgument(string value)
    {
        StringBuilder result = new StringBuilder("\"");
        int slashes = 0;
        foreach (char character in value)
        {
            if (character == '\\') { slashes++; continue; }
            if (character == '"')
            {
                result.Append('\\', slashes * 2 + 1);
                result.Append('"');
                slashes = 0;
                continue;
            }
            result.Append('\\', slashes);
            slashes = 0;
            result.Append(character);
        }
        result.Append('\\', slashes * 2);
        result.Append('"');
        return result.ToString();
    }

    private Dictionary<string, List<string>> ReadPresets()
    {
        string output = Run("List");
        Dictionary<string, List<string>> presets = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        string current = null;
        foreach (string line in output.Split(new string[] { "\r\n", "\n" }, StringSplitOptions.None))
        {
            if (line.StartsWith("Presets in ", StringComparison.OrdinalIgnoreCase) || String.IsNullOrWhiteSpace(line))
                continue;
            if (Char.IsWhiteSpace(line[0]))
            {
                if (current != null) presets[current].Add(line.Trim());
            }
            else
            {
                current = line.Trim();
                if (!presets.ContainsKey(current)) presets.Add(current, new List<string>());
            }
        }
        return presets;
    }

    private static string[] EntryLines(TextBox box)
    {
        List<string> result = new List<string>();
        foreach (string line in box.Lines)
            if (!String.IsNullOrWhiteSpace(line)) result.Add(line.Trim());
        return result.ToArray();
    }

    private static string[] SplitCommand(string command)
    {
        List<string> result = new List<string>();
        StringBuilder current = new StringBuilder();
        bool quoted = false;
        for (int i = 0; i < command.Length; i++)
        {
            char c = command[i];
            if (c == '"') { quoted = !quoted; continue; }
            if (Char.IsWhiteSpace(c) && !quoted)
            {
                if (current.Length > 0) { result.Add(current.ToString()); current.Clear(); }
            }
            else current.Append(c);
        }
        if (quoted) throw new InvalidOperationException("Command has an unmatched quote.");
        if (current.Length > 0) result.Add(current.ToString());
        return result.ToArray();
    }

    private bool Confirm(string message)
    {
        return MessageBox.Show(this, message, "Pathify", MessageBoxButtons.YesNo,
            MessageBoxIcon.Warning) == DialogResult.Yes;
    }

    private void ShowOutput(string title, string output)
    {
        MessageBox.Show(this, String.IsNullOrWhiteSpace(output) ? "Completed successfully." : output,
            title, MessageBoxButtons.OK, MessageBoxIcon.Information);
    }

    private static void ShowError(Exception error)
    {
        MessageBox.Show(error.Message, "Pathify", MessageBoxButtons.OK, MessageBoxIcon.Error);
    }
}

internal sealed class AuditEntry
{
    public int Index { get; set; }
    public string Path { get; set; }
    public bool Exists { get; set; }
    public bool IsDuplicate { get; set; }
}

