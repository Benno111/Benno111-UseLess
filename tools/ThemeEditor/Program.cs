using System.Drawing;
using System.Globalization;
using System.Text;
using System.Windows.Forms;

ApplicationConfiguration.Initialize();
Application.Run(new ThemeEditorForm());

sealed class ThemeDocument
{
    public static readonly string[] Keys =
    {
        "app_bg", "app_fg", "accent", "accent_soft",
        "surface", "surface_alt", "card", "border",
        "settings_bg", "settings_panel", "settings_text", "settings_subtext",
    };

    public static readonly uint[] DefaultDark =
    {
        0x1A1A2E, 0xE4E4E7, 0x6366F1, 0xEC4899,
        0x27272A, 0x1F2937, 0x252535, 0x52525B,
        0x141824, 0x1F2937, 0xF2F2F2, 0xF8F8F8,
    };

    public static readonly uint[] DefaultLight =
    {
        0xF4F7FB, 0x172033, 0x2563EB, 0xDB2777,
        0xE9EEF5, 0xF6F9FC, 0xFFFFFF, 0xC9D4E5,
        0xECF3FA, 0xD7E2EF, 0x1B2430, 0x627084,
    };

    public string Path { get; set; } = string.Empty;
    public string Name { get; set; } = "Theme";
    public string Mode { get; set; } = "dark";
    public uint[] Colors { get; } = new uint[Keys.Length];

    public static ThemeDocument CreateDefault(string path, bool light)
    {
        var doc = new ThemeDocument
        {
            Path = path,
            Name = light ? "Light Theme" : "Dark Theme",
            Mode = light ? "light" : "dark",
        };
        Array.Copy(light ? DefaultLight : DefaultDark, doc.Colors, Keys.Length);
        return doc;
    }

    public static ThemeDocument Load(string path)
    {
        var doc = File.Exists(path)
            ? CreateDefault(path, path.Contains("light", StringComparison.OrdinalIgnoreCase))
            : CreateDefault(path, path.Contains("light", StringComparison.OrdinalIgnoreCase));

        if (!File.Exists(path))
            return doc;

        foreach (var rawLine in File.ReadAllLines(path))
        {
            var line = rawLine.Trim();
            if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal))
                continue;

            var idx = line.IndexOf('=');
            if (idx <= 0)
                continue;

            var key = line[..idx].Trim();
            var value = line[(idx + 1)..].Trim();

            if (key.Equals("name", StringComparison.OrdinalIgnoreCase))
            {
                doc.Name = value;
                continue;
            }

            if (key.Equals("mode", StringComparison.OrdinalIgnoreCase))
            {
                doc.Mode = value;
                continue;
            }

            for (var i = 0; i < Keys.Length; i++)
            {
                if (!key.Equals(Keys[i], StringComparison.OrdinalIgnoreCase))
                    continue;

                if (TryParseHexColor(value, out var color))
                    doc.Colors[i] = color;
                break;
            }
        }

        return doc;
    }

    public void Save()
    {
        var directory = System.IO.Path.GetDirectoryName(Path);
        if (!string.IsNullOrWhiteSpace(directory))
            Directory.CreateDirectory(directory);

        var sb = new StringBuilder();
        sb.AppendLine("# OS8 theme preset");
        sb.AppendLine($"name={Name}");
        sb.AppendLine($"mode={Mode}");
        for (var i = 0; i < Keys.Length; i++)
            sb.AppendLine($"{Keys[i]}={FormatHexColor(Colors[i])}");

        File.WriteAllText(Path, sb.ToString());
    }

    public static bool TryParseHexColor(string text, out uint color)
    {
        color = 0;
        if (string.IsNullOrWhiteSpace(text))
            return false;

        var value = text.Trim();
        if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            value = value[2..];
        if (value.StartsWith("#", StringComparison.Ordinal))
            value = value[1..];

        return uint.TryParse(value, NumberStyles.HexNumber, CultureInfo.InvariantCulture,
                             out color);
    }

    public static string FormatHexColor(uint color) => $"{color & 0x00FFFFFFu:X6}";
}

sealed class ThemeEditorForm : Form
{
    private readonly ComboBox themePicker = new();
    private readonly TextBox nameBox = new();
    private readonly ComboBox modeBox = new();
    private readonly TableLayoutPanel slotsTable = new();
    private readonly Label statusLabel = new();
    private readonly Panel preview = new();
    private readonly Button saveButton = new();
    private readonly Button reloadButton = new();
    private readonly Button openButton = new();
    private readonly Button openFolderButton = new();
    private readonly List<(Label label, TextBox box, Button swatch)> rows = new();
    private readonly ColorDialog colorDialog = new();

    private string repoRoot = string.Empty;
    private ThemeDocument current = ThemeDocument.CreateDefault("",
        light: false);
    private bool suppressEvents;

    public ThemeEditorForm()
    {
        Text = "OS8 Theme Editor";
        MinimumSize = new Size(1100, 760);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 10f, FontStyle.Regular, GraphicsUnit.Point);
        BackColor = Color.FromArgb(0x18, 0x1C, 0x24);
        ForeColor = Color.White;

        BuildLayout();
        Load += (_, _) => InitializeData();
    }

    private void BuildLayout()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 1,
            Padding = new Padding(16),
            BackColor = BackColor,
        };
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 280));
        root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        Controls.Add(root);

        var left = new Panel { Dock = DockStyle.Fill, BackColor = BackColor };
        root.Controls.Add(left, 0, 0);

        var right = new Panel { Dock = DockStyle.Fill, BackColor = BackColor };
        root.Controls.Add(right, 1, 0);

        var leftTitle = new Label
        {
            Dock = DockStyle.Top,
            Height = 54,
            Text = "Theme presets",
            Font = new Font(Font.FontFamily, 18f, FontStyle.Bold),
        };
        left.Controls.Add(leftTitle);

        themePicker.Dock = DockStyle.Top;
        themePicker.DropDownStyle = ComboBoxStyle.DropDownList;
        themePicker.Height = 32;
        themePicker.SelectedIndexChanged += (_, _) => LoadSelectedTheme();
        left.Controls.Add(themePicker);

        var leftButtons = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 44,
            WrapContents = false,
            Padding = new Padding(0, 8, 0, 0),
        };
        left.Controls.Add(leftButtons);

        openButton.Text = "Open";
        openButton.Width = 62;
        openButton.Click += (_, _) => OpenThemeFile();
        leftButtons.Controls.Add(openButton);

        reloadButton.Text = "Reload";
        reloadButton.Width = 72;
        reloadButton.Click += (_, _) => LoadSelectedTheme();
        leftButtons.Controls.Add(reloadButton);

        openFolderButton.Text = "Folder";
        openFolderButton.Width = 72;
        openFolderButton.Click += (_, _) => OpenThemeFolder();
        leftButtons.Controls.Add(openFolderButton);

        var leftInfo = new Label
        {
            Dock = DockStyle.Top,
            Height = 120,
            Padding = new Padding(0, 10, 0, 0),
            AutoSize = false,
            Text = "Edit the shipped theme files under assets/themes.\n" +
                   "Save writes the preset file directly, so the OS build can pick it up.",
        };
        left.Controls.Add(leftInfo);

        preview.Dock = DockStyle.Top;
        preview.Height = 220;
        preview.Margin = new Padding(0, 18, 0, 0);
        preview.Paint += Preview_Paint;
        left.Controls.Add(preview);

        var rightHeader = new Panel { Dock = DockStyle.Top, Height = 92 };
        right.Controls.Add(rightHeader);

        var title = new Label
        {
            Dock = DockStyle.Top,
            Height = 44,
            Text = "Preset editor",
            Font = new Font(Font.FontFamily, 20f, FontStyle.Bold),
        };
        rightHeader.Controls.Add(title);

        var headerRow = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 4,
            RowCount = 1,
        };
        headerRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 90));
        headerRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 42));
        headerRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 70));
        headerRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 58));
        rightHeader.Controls.Add(headerRow);

        headerRow.Controls.Add(new Label
        {
            Text = "Name",
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleLeft,
        }, 0, 0);
        nameBox.Dock = DockStyle.Fill;
        nameBox.TextChanged += (_, _) => { if (!suppressEvents) current.Name = nameBox.Text; };
        headerRow.Controls.Add(nameBox, 1, 0);
        headerRow.Controls.Add(new Label
        {
            Text = "Mode",
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleLeft,
        }, 2, 0);
        modeBox.Dock = DockStyle.Fill;
        modeBox.DropDownStyle = ComboBoxStyle.DropDownList;
        modeBox.Items.AddRange(new object[] { "dark", "light" });
        modeBox.SelectedIndexChanged += (_, _) =>
        {
            if (suppressEvents || modeBox.SelectedItem is null)
                return;
            current.Mode = modeBox.SelectedItem.ToString() ?? "dark";
        };
        headerRow.Controls.Add(modeBox, 3, 0);

        var buttons = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 48,
            WrapContents = false,
            Padding = new Padding(0, 8, 0, 0),
        };
        right.Controls.Add(buttons);

        saveButton.Text = "Save";
        saveButton.Width = 82;
        saveButton.Click += (_, _) => SaveCurrentTheme();
        buttons.Controls.Add(saveButton);

        var saveAsButton = new Button { Text = "Save As", Width = 82 };
        saveAsButton.Click += (_, _) => SaveCurrentThemeAs();
        buttons.Controls.Add(saveAsButton);

        var cloneButton = new Button { Text = "New Copy", Width = 90 };
        cloneButton.Click += (_, _) => CloneCurrentTheme();
        buttons.Controls.Add(cloneButton);

        var editHint = new Label
        {
            Dock = DockStyle.Top,
            Height = 30,
            Text = "Each row maps to a color slot in the OS theme preset.",
            ForeColor = Color.FromArgb(0xC8, 0xD3, 0xE0),
        };
        right.Controls.Add(editHint);

        slotsTable.Dock = DockStyle.Fill;
        slotsTable.ColumnCount = 3;
        slotsTable.AutoScroll = true;
        slotsTable.Padding = new Padding(0, 8, 0, 0);
        slotsTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 42));
        slotsTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 38));
        slotsTable.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 20));
        right.Controls.Add(slotsTable);

        for (var i = 0; i < ThemeDocument.Keys.Length; i++)
        {
            slotsTable.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
            var slotLabel = new Label
            {
                Text = ThemeDocument.Keys[i],
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleLeft,
                Padding = new Padding(0, 4, 0, 0),
            };
            var slotBox = new TextBox
            {
                Dock = DockStyle.Fill,
                MaxLength = 6,
                Font = new Font(Font.FontFamily, 10f, FontStyle.Regular),
            };
            var slotButton = new Button
            {
                Dock = DockStyle.Fill,
                Text = "Pick",
                Tag = i,
            };
            slotBox.TextChanged += SlotBox_TextChanged;
            slotButton.Click += SlotButton_Click;

            rows.Add((slotLabel, slotBox, slotButton));
            slotsTable.Controls.Add(slotLabel, 0, i);
            slotsTable.Controls.Add(slotBox, 1, i);
            slotsTable.Controls.Add(slotButton, 2, i);
        }

        statusLabel.Dock = DockStyle.Bottom;
        statusLabel.Height = 24;
        statusLabel.Padding = new Padding(0, 6, 0, 0);
        statusLabel.ForeColor = Color.FromArgb(0xA5, 0xB4, 0xC3);
        Controls.Add(statusLabel);
    }

    private void InitializeData()
    {
        repoRoot = FindRepoRoot();
        var themeDir = Path.Combine(repoRoot, "assets", "themes");
        Directory.CreateDirectory(themeDir);
        EnsureDefaultThemes(themeDir);

        themePicker.Items.Clear();
        foreach (var file in Directory.GetFiles(themeDir, "*.theme")
                                      .OrderBy(Path.GetFileName))
        {
            themePicker.Items.Add(file);
        }

        if (themePicker.Items.Count > 0)
            themePicker.SelectedIndex = 0;

        statusLabel.Text = $"Loaded themes from {themeDir}";
    }

    private static string FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir is not null)
        {
            if (Directory.Exists(Path.Combine(dir.FullName, "assets", "themes")))
                return dir.FullName;
            dir = dir.Parent;
        }

        return AppContext.BaseDirectory;
    }

    private static void EnsureDefaultThemes(string themeDir)
    {
        var dark = Path.Combine(themeDir, "dark.theme");
        var light = Path.Combine(themeDir, "light.theme");
        var defaultTheme = Path.Combine(themeDir, "default.theme");

        if (!File.Exists(dark))
            ThemeDocument.CreateDefault(dark, light: false).Save();
        if (!File.Exists(light))
            ThemeDocument.CreateDefault(light, light: true).Save();
        if (!File.Exists(defaultTheme))
        {
            var doc = ThemeDocument.CreateDefault(defaultTheme, light: false);
            doc.Name = "Default Theme";
            doc.Save();
        }
    }

    private void LoadSelectedTheme()
    {
        if (themePicker.SelectedItem is not string path || string.IsNullOrWhiteSpace(path))
            return;

        current = ThemeDocument.Load(path);
        suppressEvents = true;
        nameBox.Text = current.Name;
        modeBox.SelectedItem = string.Equals(current.Mode, "light", StringComparison.OrdinalIgnoreCase)
            ? "light"
            : "dark";
        for (var i = 0; i < rows.Count; i++)
            rows[i].box.Text = ThemeDocument.FormatHexColor(current.Colors[i]);
        suppressEvents = false;
        preview.Invalidate();
        UpdateStatus($"Loaded {Path.GetFileName(path)}");
    }

    private void SaveCurrentTheme()
    {
        if (string.IsNullOrWhiteSpace(current.Path))
            return;

        SyncUiToDocument();
        current.Save();
        preview.Invalidate();
        UpdateStatus($"Saved {Path.GetFileName(current.Path)}");
    }

    private void SaveCurrentThemeAs()
    {
        using var dialog = new SaveFileDialog
        {
            Filter = "OS8 theme preset (*.theme)|*.theme|All files (*.*)|*.*",
            InitialDirectory = Path.GetDirectoryName(current.Path),
            FileName = Path.GetFileName(current.Path),
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;

        SyncUiToDocument();
        current.Path = dialog.FileName;
        current.Save();
        RefreshThemeList(dialog.FileName);
        UpdateStatus($"Saved as {Path.GetFileName(dialog.FileName)}");
    }

    private void CloneCurrentTheme()
    {
        SyncUiToDocument();
        var baseName = Path.GetFileNameWithoutExtension(current.Path);
        var clonePath = Path.Combine(Path.GetDirectoryName(current.Path) ?? repoRoot,
                                     $"{baseName}-copy.theme");
        current.Path = clonePath;
        current.Name = $"{current.Name} Copy";
        current.Save();
        RefreshThemeList(clonePath);
        UpdateStatus($"Created {Path.GetFileName(clonePath)}");
    }

    private void RefreshThemeList(string selectedPath)
    {
        var themeDir = Path.Combine(repoRoot, "assets", "themes");
        themePicker.Items.Clear();
        foreach (var file in Directory.GetFiles(themeDir, "*.theme").OrderBy(Path.GetFileName))
            themePicker.Items.Add(file);

        if (!themePicker.Items.Contains(selectedPath))
            themePicker.Items.Add(selectedPath);

        themePicker.SelectedItem = selectedPath;
    }

    private void SyncUiToDocument()
    {
        current.Name = nameBox.Text.Trim();
        current.Mode = modeBox.SelectedItem?.ToString() ?? "dark";
        for (var i = 0; i < rows.Count; i++)
        {
            if (ThemeDocument.TryParseHexColor(rows[i].box.Text, out var color))
                current.Colors[i] = color;
        }
    }

    private void SlotBox_TextChanged(object? sender, EventArgs e)
    {
        if (suppressEvents || sender is not TextBox box)
            return;

        var index = rows.FindIndex(row => ReferenceEquals(row.box, box));
        if (index < 0)
            return;

        if (ThemeDocument.TryParseHexColor(box.Text, out var color))
        {
            current.Colors[index] = color;
            preview.Invalidate();
            UpdateStatus($"Updated {ThemeDocument.Keys[index]}");
        }
        else
        {
            box.ForeColor = Color.FromArgb(0xF3, 0x8B, 0xA8);
        }
    }

    private void SlotButton_Click(object? sender, EventArgs e)
    {
        if (sender is not Button button || button.Tag is not int index)
            return;

        using var dialog = new ColorDialog
        {
            Color = ColorTranslator.FromHtml("#" + ThemeDocument.FormatHexColor(current.Colors[index])),
            FullOpen = true,
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;

        current.Colors[index] = (uint)dialog.Color.R << 16 |
                                (uint)dialog.Color.G << 8 |
                                (uint)dialog.Color.B;
        suppressEvents = true;
        rows[index].box.Text = ThemeDocument.FormatHexColor(current.Colors[index]);
        rows[index].box.ForeColor = Color.White;
        suppressEvents = false;
        preview.Invalidate();
        UpdateStatus($"Picked {ThemeDocument.Keys[index]}");
    }

    private void OpenThemeFile()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "OS8 theme preset (*.theme)|*.theme|All files (*.*)|*.*",
            InitialDirectory = Path.Combine(repoRoot, "assets", "themes"),
        };

        if (dialog.ShowDialog(this) != DialogResult.OK)
            return;

        if (!themePicker.Items.Contains(dialog.FileName))
            themePicker.Items.Add(dialog.FileName);
        themePicker.SelectedItem = dialog.FileName;
    }

    private void OpenThemeFolder()
    {
        var folder = Path.Combine(repoRoot, "assets", "themes");
        if (Directory.Exists(folder))
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = folder,
                UseShellExecute = true,
            });
    }

    private void UpdateStatus(string message) => statusLabel.Text = message;

    private void Preview_Paint(object? sender, PaintEventArgs e)
    {
        e.Graphics.Clear(Color.FromArgb(0x10, 0x14, 0x1C));
        var bg = Color.FromArgb((int)current.Colors[0]);
        var fg = Color.FromArgb((int)current.Colors[1]);
        var accent = Color.FromArgb((int)current.Colors[2]);
        var card = Color.FromArgb((int)current.Colors[6]);
        using var titleFont = new Font(Font.FontFamily, 11f, FontStyle.Bold);

        using (var brush = new SolidBrush(bg))
            e.Graphics.FillRectangle(brush, 12, 12, preview.Width - 24, preview.Height - 24);
        using (var cardBrush = new SolidBrush(card))
            e.Graphics.FillRectangle(cardBrush, 24, 24, preview.Width - 48, 150);
        using (var accentBrush = new SolidBrush(accent))
            e.Graphics.FillRectangle(accentBrush, 24, 24, 8, 150);
        using (var fgBrush = new SolidBrush(fg))
        {
            e.Graphics.DrawString(current.Name, titleFont, fgBrush, 44, 40);
            e.Graphics.DrawString($"Mode: {current.Mode}", Font, fgBrush, 44, 70);
            e.Graphics.DrawString("Preview card", Font, fgBrush, 44, 100);
        }

        for (var i = 0; i < ThemeDocument.Keys.Length; i++)
        {
            var row = i / 4;
            var col = i % 4;
            var x = 44 + col * 48;
            var y = 130 + row * 18;
            using var swatch = new SolidBrush(Color.FromArgb((int)current.Colors[i]));
            e.Graphics.FillRectangle(swatch, x, y, 18, 12);
            e.Graphics.DrawRectangle(Pens.White, x, y, 18, 12);
        }
    }
}
